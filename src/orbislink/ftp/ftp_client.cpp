// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/ftp/ftp_client.h"

#include "orbislink/common/log.h"
#include "orbislink/common/tr.h"
#include "orbislink/common/util.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <curl/curl.h>
#include <fstream>
#include <sstream>
#include <thread>

namespace orbislink {

namespace {

std::condition_variable g_slotAvailable;

std::string upperCase(std::string text)
{
	for(char &c : text)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return text;
}

// Days since 1970-01-01 → year, month, day (Howard Hinnant's civil_from_days).
void civilFromDays(int64_t days, int *year, int *month, int *day)
{
	days += 719468;
	const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
	const int64_t doe = days - era * 146097;
	const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const int64_t mp = (5 * doy + 2) / 153;
	*day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
	*month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
	*year = static_cast<int>(yoe + era * 400 + (*month <= 2 ? 1 : 0));
}

int64_t daysFromCivil(int year, int month, int day)
{
	year -= month <= 2 ? 1 : 0;
	const int64_t era = (year >= 0 ? year : year - 399) / 400;
	const int64_t yoe = year - era * 400;
	const int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

// "20261117221300" (MLSD's modify, UTC) → seconds since 1970; 0 if unreadable.
int64_t machineTime(const std::string &text)
{
	if(text.size() < 14 || text.find_first_not_of("0123456789", 0) < 14)
		return 0;
	auto number = [&](size_t at, size_t length) { return std::atoi(text.substr(at, length).c_str()); };
	const int year = number(0, 4), month = number(4, 2), day = number(6, 2);
	if(year < 1970 || month < 1 || month > 12 || day < 1 || day > 31)
		return 0;
	return daysFromCivil(year, month, day) * 86400 + number(8, 2) * 3600 + number(10, 2) * 60 + number(12, 2);
}

// The same moment as LIST writes it: "Nov 17 22:13" within the last half
// year, "Nov 17 2024" before.
std::string listStyleTime(int64_t when, int64_t nowUnix)
{
	static const char *const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
		"Nov", "Dec" };
	int year = 0, month = 0, day = 0;
	civilFromDays(when / 86400, &year, &month, &day);
	char text[32];
	const bool recent = when <= nowUnix + 86400 && nowUnix - when < 180LL * 86400;
	if(recent)
	{
		const int64_t seconds = ((when % 86400) + 86400) % 86400;
		std::snprintf(text, sizeof(text), "%s %02d %02d:%02d", months[month - 1], day,
			static_cast<int>(seconds / 3600), static_cast<int>(seconds % 3600 / 60));
	}
	else
		std::snprintf(text, sizeof(text), "%s %02d %d", months[month - 1], day, year);
	return text;
}

// "0755" → "rwxr-xr-x", behind the kind ("d" or "-").
std::string permissionText(const std::string &mode, bool directory)
{
	std::string out(1, directory ? 'd' : '-');
	const std::string digits = mode.size() > 3 ? mode.substr(mode.size() - 3) : mode;
	if(digits.size() != 3 || digits.find_first_not_of("01234567") != std::string::npos)
		return std::string();
	for(const char digit : digits)
	{
		const int bits = digit - '0';
		out += (bits & 4) ? 'r' : '-';
		out += (bits & 2) ? 'w' : '-';
		out += (bits & 1) ? 'x' : '-';
	}
	return out;
}

void sortEntries(std::vector<FtpEntry> &entries)
{
	std::sort(entries.begin(), entries.end(), [](const FtpEntry &a, const FtpEntry &b) {
		if(a.isDirectory != b.isDirectory)
			return a.isDirectory;
		return toLower(a.name) < toLower(b.name);
	});
}

struct CurlGlobalFtp
{
	CurlGlobalFtp() { curl_global_init(CURL_GLOBAL_DEFAULT); }
	~CurlGlobalFtp() { curl_global_cleanup(); }
};

void ensureCurl()
{
	static CurlGlobalFtp global;
	(void)global;
}

size_t appendToString(char *data, size_t size, size_t count, void *userdata)
{
	static_cast<std::string *>(userdata)->append(data, size * count);
	return size * count;
}

size_t writeToStream(char *data, size_t size, size_t count, void *userdata)
{
	auto *stream = static_cast<std::ofstream *>(userdata);
	stream->write(data, static_cast<std::streamsize>(size * count));
	return stream->good() ? size * count : 0;
}

size_t readFromStream(char *buffer, size_t size, size_t count, void *userdata)
{
	auto *stream = static_cast<std::ifstream *>(userdata);
	stream->read(buffer, static_cast<std::streamsize>(size * count));
	return static_cast<size_t>(stream->gcount());
}

struct ProgressState
{
	FtpProgressCallback callback;
	const std::atomic<uint64_t> *generation = nullptr;
	uint64_t startedAt = 0;
	int64_t knownTotal = 0;
	int64_t offset = 0; // already on the other side before this transfer
	bool cancelled = false;
};

int progressCallback(void *userdata, curl_off_t dlTotal, curl_off_t dlNow, curl_off_t ulTotal,
	curl_off_t ulNow)
{
	auto *state = static_cast<ProgressState *>(userdata);
	if(state->generation && state->generation->load() != state->startedAt)
	{
		state->cancelled = true;
		return 1; // aborts the transfer
	}
	const int64_t done = state->offset + (ulNow > 0 ? static_cast<int64_t>(ulNow) : static_cast<int64_t>(dlNow));
	int64_t total = ulTotal > 0 ? static_cast<int64_t>(ulTotal) : static_cast<int64_t>(dlTotal);
	if(total <= 0 || state->offset > 0)
		total = state->knownTotal;
	if(state->callback && !state->callback(done, total))
	{
		state->cancelled = true;
		return 1;
	}
	return 0;
}

// Field order of a Unix-style LIST:
// drwxr-xr-x  2 user group  4096 Jan 01 00:00 name with spaces
bool parseUnixLine(const std::string &line, FtpEntry *entry)
{
	if(line.size() < 10)
		return false;
	const char first = line[0];
	if(first != 'd' && first != '-' && first != 'l' && first != 'b' && first != 'c' && first != 'p'
		&& first != 's')
		return false;

	std::istringstream stream(line);
	std::string permissions, links, owner, group, sizeText, month, day, timeOrYear;
	if(!(stream >> permissions >> links >> owner >> group >> sizeText >> month >> day >> timeOrYear))
		return false;

	std::string name;
	std::getline(stream, name);
	name = trim(name);
	if(name.empty())
		return false;

	entry->permissions = permissions;
	entry->isDirectory = first == 'd';
	entry->isSymlink = first == 'l';
	entry->modified = month + " " + day + " " + timeOrYear;
	try { entry->size = static_cast<int64_t>(std::stoll(sizeText)); }
	catch(...) { entry->size = 0; }

	if(entry->isSymlink)
	{
		const size_t arrow = name.find(" -> ");
		if(arrow != std::string::npos)
			name = name.substr(0, arrow);
	}
	entry->name = name;
	return true;
}

// MS-DOS format, in case some server uses it:
// 01-01-70  00:00AM       <DIR>          name
bool parseDosLine(const std::string &line, FtpEntry *entry)
{
	std::istringstream stream(line);
	std::string date, time, sizeOrDir;
	if(!(stream >> date >> time >> sizeOrDir))
		return false;
	if(date.size() < 6 || date.find('-') == std::string::npos)
		return false;

	std::string name;
	std::getline(stream, name);
	name = trim(name);
	if(name.empty())
		return false;

	entry->modified = date + " " + time;
	if(sizeOrDir == "<DIR>")
	{
		entry->isDirectory = true;
		entry->size = 0;
	}
	else
	{
		try { entry->size = static_cast<int64_t>(std::stoll(sizeOrDir)); }
		catch(...) { return false; }
	}
	entry->name = name;
	return true;
}

std::string curlMessage(CURLcode code, const char *errorBuffer)
{
	const std::string detail = errorBuffer && errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
	return detail;
}

// A failure to read: the server refusing, or the file not being there, is
// its answer and not a dropped connection.
FtpResult readFailure(CURLcode code, const char *errorBuffer)
{
	FtpResult result = FtpResult::failure(curlMessage(code, errorBuffer));
	result.final = code == CURLE_REMOTE_ACCESS_DENIED || code == CURLE_REMOTE_FILE_NOT_FOUND;
	return result;
}

} // namespace

// Room for one more transfer (upload or download). Listing, renaming and
// the other short commands do not wait for it: with one connection allowed,
// browsing would otherwise freeze for as long as an upload lasts.
struct FtpClient::Slot
{
	FtpClient *owner;
	explicit Slot(FtpClient *client) : owner(client)
	{
		std::unique_lock<std::mutex> lock(owner->slotMutex_);
		const int limit = std::max(1, std::min(2, owner->config_.maxConnections));
		g_slotAvailable.wait(lock, [&]() { return owner->slotsInUse_ < limit; });
		++owner->slotsInUse_;
	}
	~Slot()
	{
		std::lock_guard<std::mutex> lock(owner->slotMutex_);
		--owner->slotsInUse_;
		g_slotAvailable.notify_one();
	}
};

FtpClient::FtpClient(Config config) : config_(std::move(config))
{
	ensureCurl();
	if(config_.port == 0)
		config_.port = 2121;
	if(config_.maxConnections < 1)
		config_.maxConnections = 1;
	if(config_.maxConnections > 2)
		config_.maxConnections = 2;
}

FtpClient::~FtpClient() { dropIdleHandles(); }

void FtpClient::setConfig(const Config &config)
{
	bool otherServer = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		otherServer = config.host != config_.host || config.port != config_.port || config.user != config_.user;
		config_ = config;
		if(config_.port == 0)
			config_.port = 2121;
		config_.maxConnections = std::max(1, std::min(2, config_.maxConnections));
	}
	// Another console (or port): what was kept open, and what was learned,
	// belonged to the one before.
	if(otherServer)
	{
		dropIdleHandles();
		std::lock_guard<std::mutex> lock(featureMutex_);
		featuresKnown_ = false;
		features_.clear();
		mlsdBroken_.store(false);
	}
}

namespace {

// How long a connection at rest is kept for the next operation. Short: the
// consoles' servers close idle ones themselves, and holding them would
// keep sessions other programs (FileZilla) may need.
constexpr int64_t kIdleKeepMs = 20000;
constexpr size_t kIdleHandles = 2;

} // namespace

void *FtpClient::takeHandle()
{
	ensureCurl();
	CURL *handle = nullptr;
	std::vector<CURL *> stale;
	{
		std::lock_guard<std::mutex> lock(poolMutex_);
		const int64_t now = monotonicMillis();
		while(!idle_.empty())
		{
			auto entry = idle_.back();
			idle_.pop_back();
			if(now - entry.second > kIdleKeepMs)
			{
				stale.push_back(static_cast<CURL *>(entry.first));
				continue;
			}
			handle = static_cast<CURL *>(entry.first);
			break;
		}
	}
	for(CURL *old : stale)
		curl_easy_cleanup(old);
	if(handle)
	{
		// Every option back to its default; the open connection stays.
		curl_easy_reset(handle);
		return handle;
	}
	return curl_easy_init();
}

void FtpClient::giveBack(void *handle, bool reusable)
{
	if(!handle)
		return;
	if(reusable)
	{
		std::lock_guard<std::mutex> lock(poolMutex_);
		if(idle_.size() < kIdleHandles)
		{
			idle_.emplace_back(handle, monotonicMillis());
			return;
		}
	}
	// After a failure the connection may be in any state: not kept.
	curl_easy_cleanup(static_cast<CURL *>(handle));
}

void FtpClient::dropIdleHandles()
{
	std::vector<std::pair<void *, int64_t>> idle;
	{
		std::lock_guard<std::mutex> lock(poolMutex_);
		idle.swap(idle_);
	}
	for(const auto &entry : idle)
		curl_easy_cleanup(static_cast<CURL *>(entry.first));
}

FtpClient::Config FtpClient::config() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return config_;
}

void FtpClient::cancel() { ++cancelGeneration_; }

std::vector<std::string> FtpClient::shortcutPaths()
{
	return { "/data/", "/data/pkg/", "/data/GoldHEN/", "/user/app/", "/mnt/usb0/" };
}

bool FtpClient::isProtectedPath(const std::string &remotePath)
{
	static const char *protectedRoots[] = { "/system", "/system_ex", "/system_data", "/preinst",
		"/preinst2", "/adm", "/dev", "/update", "/hostapp", "/vnn" };
	const std::string path = normalizeRemotePath(remotePath);
	for(const char *root : protectedRoots)
	{
		const std::string prefix(root);
		if(path == prefix || startsWith(path, prefix + "/"))
			return true;
	}
	return false;
}

bool FtpClient::isWriteAllowed(const std::string &remotePath) const
{
	if(!isProtectedPath(remotePath))
		return true;
	std::lock_guard<std::mutex> lock(mutex_);
	return config_.advancedMode;
}

std::string FtpClient::urlFor(const std::string &remotePath) const
{
	Config cfg = config();
	std::string path = remotePath;
	if(path.empty() || path.front() != '/')
		path = "/" + path;
	return "ftp://" + cfg.host + ":" + std::to_string(cfg.port) + urlEncodePath(path);
}

std::vector<FtpEntry> FtpClient::parseListing(const std::string &listing, const std::string &baseDir)
{
	std::vector<FtpEntry> entries;
	std::istringstream stream(listing);
	std::string line;
	const std::string base = normalizeRemotePath(baseDir);
	while(std::getline(stream, line))
	{
		if(!line.empty() && line.back() == '\r')
			line.pop_back();
		if(trim(line).empty())
			continue;

		FtpEntry entry;
		if(!parseUnixLine(line, &entry) && !parseDosLine(line, &entry))
		{
			// Unusual server: takes the line as a plain name instead of losing it.
			entry = FtpEntry();
			entry.name = trim(line);
			if(entry.name.empty())
				continue;
		}
		if(entry.name == "." || entry.name == "..")
			continue;
		entry.rawLine = line;
		entry.path = normalizeRemotePath(base + "/" + entry.name);
		entries.push_back(entry);
	}
	sortEntries(entries);
	return entries;
}

std::vector<FtpEntry> FtpClient::parseMachineListing(const std::string &listing, const std::string &baseDir,
	int64_t nowUnix)
{
	std::vector<FtpEntry> entries;
	std::istringstream stream(listing);
	std::string line;
	const std::string base = normalizeRemotePath(baseDir);
	while(std::getline(stream, line))
	{
		if(!line.empty() && line.back() == '\r')
			line.pop_back();
		// "fact=value;fact=value; name": the facts, one space, the name.
		const size_t space = line.find(' ');
		if(space == std::string::npos || space == 0 || space + 1 >= line.size())
			continue;
		const std::string name = line.substr(space + 1);
		if(name == "." || name == "..")
			continue;
		FtpEntry entry;
		std::string type;
		std::string mode;
		bool typed = false;
		for(const std::string &fact : split(line.substr(0, space), ';', false))
		{
			const size_t equals = fact.find('=');
			if(equals == std::string::npos)
				continue;
			const std::string key = toLower(fact.substr(0, equals));
			const std::string value = fact.substr(equals + 1);
			if(key == "type")
			{
				type = toLower(value);
				typed = true;
			}
			else if(key == "size" || key == "sizd")
			{
				try { entry.size = static_cast<int64_t>(std::stoll(value)); }
				catch(...) { entry.size = 0; }
			}
			else if(key == "modify")
			{
				const int64_t when = machineTime(value);
				if(when > 0)
					entry.modified = listStyleTime(when, nowUnix);
			}
			else if(key == "unix.mode")
				mode = value;
		}
		// Without a type it is not an MLSD line.
		if(!typed)
			continue;
		if(type == "cdir" || type == "pdir")
			continue;
		entry.isDirectory = type == "dir";
		entry.isSymlink = type.find("slink") != std::string::npos || type.find("symlink") != std::string::npos;
		if(entry.isDirectory)
			entry.size = 0;
		entry.permissions = permissionText(mode, entry.isDirectory);
		entry.name = name;
		entry.rawLine = line;
		entry.path = normalizeRemotePath(base + "/" + name);
		entries.push_back(entry);
	}
	sortEntries(entries);
	return entries;
}

FtpResult FtpClient::withRetries(const std::string &what, const std::function<FtpResult()> &operation)
{
	const Config cfg = config();
	FtpResult result;
	for(int attempt = 0; attempt < std::max(1, cfg.maxRetries); ++attempt)
	{
		if(attempt > 0)
		{
			const int delayMs = 500 * (1 << (attempt - 1));
			logWarning("FTP: " + what + " failed (" + result.message + "); retrying in "
				+ std::to_string(delayMs) + " ms.");
			std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
		}
		result = operation();
		if(result.ok || result.cancelled || result.final)
			break;
	}
	// The server saying the file or folder is not there is its answer, often
	// an expected one (a game without metadata, a folder another console
	// has, a drive not plugged in): whoever asked decides whether it is an
	// error and says so. Only the detailed log keeps it.
	if(!result.ok && !result.cancelled)
	{
		if(result.final)
			logDebug("FTP: " + what + ": the console answered \"" + result.message + "\"");
		else
			logError("FTP: " + what + " failed: " + result.message);
	}
	return result;
}

namespace {

// Setup shared by every operation.
void applyCommonOptions(CURL *curl, const FtpClient::Config &cfg, char *errorBuffer)
{
	curl_easy_setopt(curl, CURLOPT_USERNAME, cfg.user.c_str());
	curl_easy_setopt(curl, CURLOPT_PASSWORD, cfg.password.c_str());
	curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, cfg.passive ? 1L : 0L);
	curl_easy_setopt(curl, CURLOPT_FTPPORT, cfg.passive ? nullptr : "-");
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(cfg.connectTimeoutSeconds));
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, static_cast<long>(cfg.idleTimeoutSeconds));
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_PROXY, "");
	// A connection kept from an earlier operation is only reused while
	// fresh; an older one is closed and a new one made.
	curl_easy_setopt(curl, CURLOPT_MAXAGE_CONN, 20L);
}

} // namespace

bool FtpClient::probe(std::string *detail)
{
	const Config cfg = config();
	// ConsoleManager confirms the "220" with a TCP probe; here the anonymous
	// login is confirmed with a PWD.
	ensureCurl();
	CURL *curl = curl_easy_init();
	if(!curl)
		return false;
	char errorBuffer[CURL_ERROR_SIZE] = { 0 };
	std::string response;
	const std::string url = "ftp://" + cfg.host + ":" + std::to_string(cfg.port) + "/";
	applyCommonOptions(curl, cfg, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, appendToString);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
	const CURLcode code = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if(detail)
		*detail = code == CURLE_OK ? trim(response) : curlMessage(code, errorBuffer);
	return code == CURLE_OK;
}

FtpResult FtpClient::list(const std::string &remoteDir, std::vector<FtpEntry> *entries)
{
	const std::string dir = normalizeRemotePath(remoteDir);
	return withRetries("list " + dir, [&]() -> FtpResult {
		const Config cfg = config();
		// The machine-readable listing when the server has it: exact sizes,
		// times and types, whatever the server's LIST looks like.
		const bool machine = !mlsdBroken_.load() && hasFeature("MLSD");
		CURL *curl = static_cast<CURL *>(takeHandle());
		if(!curl)
			return FtpResult::failure("could not initialise libcurl");

		char errorBuffer[CURL_ERROR_SIZE] = { 0 };
		std::string listing;
		std::string url = "ftp://" + cfg.host + ":" + std::to_string(cfg.port) + urlEncodePath(dir);
		if(url.back() != '/')
			url += '/';

		applyCommonOptions(curl, cfg, errorBuffer);
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToString);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &listing);
		// Asks for the full LIST (not just names), to get type and size.
		curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 0L);
		if(machine)
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "MLSD");

		const CURLcode code = curl_easy_perform(curl);
		giveBack(curl, code == CURLE_OK);
		if(code != CURLE_OK)
		{
			// MLSD refused outright: LIST next time, and now.
			if(machine && code != CURLE_REMOTE_ACCESS_DENIED && code != CURLE_REMOTE_FILE_NOT_FOUND)
			{
				mlsdBroken_.store(true);
				logInfo("FTP: the server offers MLSD but did not answer it; listing with LIST.");
			}
			return readFailure(code, errorBuffer);
		}
		std::vector<FtpEntry> parsed = machine ? parseMachineListing(listing, dir, nowUnixSeconds())
											   : parseListing(listing, dir);
		// Something came, and none of it read as MLSD: this server's MLSD is
		// not what it says. LIST from now on.
		if(machine && parsed.empty() && !trim(listing).empty())
		{
			mlsdBroken_.store(true);
			logInfo("FTP: the server's MLSD reply could not be read; listing with LIST.");
			return FtpResult::failure("MLSD reply not understood");
		}
		if(entries)
			*entries = std::move(parsed);
		return FtpResult::success();
	});
}

bool FtpClient::isDirectory(const std::string &remotePath)
{
	const Config cfg = config();
	CURL *curl = static_cast<CURL *>(takeHandle());
	if(!curl)
		return false;
	char errorBuffer[CURL_ERROR_SIZE] = { 0 };
	std::string url = urlFor(normalizeRemotePath(remotePath));
	if(url.back() != '/')
		url += '/';
	applyCommonOptions(curl, cfg, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	// A folder URL with no body: libcurl only goes into it (CWD), which a
	// file refuses.
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
	const CURLcode code = curl_easy_perform(curl);
	giveBack(curl, code == CURLE_OK);
	return code == CURLE_OK;
}

FtpResult FtpClient::remoteSize(const std::string &remotePath, int64_t *size)
{
	const std::string path = normalizeRemotePath(remotePath);
	return withRetries("get size of " + path, [&]() -> FtpResult {
		const Config cfg = config();
		CURL *curl = static_cast<CURL *>(takeHandle());
		if(!curl)
			return FtpResult::failure("could not initialise libcurl");

		char errorBuffer[CURL_ERROR_SIZE] = { 0 };
		applyCommonOptions(curl, cfg, errorBuffer);
		curl_easy_setopt(curl, CURLOPT_URL, urlFor(path).c_str());
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
		curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);
		std::string headers;
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, appendToString);
		curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToString);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &headers);

		const CURLcode code = curl_easy_perform(curl);
		curl_off_t remote = -1;
		if(code == CURLE_OK)
			curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &remote);
		giveBack(curl, code == CURLE_OK);
		if(code != CURLE_OK)
			return readFailure(code, errorBuffer);
		if(size)
			*size = static_cast<int64_t>(remote);
		return FtpResult::success();
	});
}

FtpResult FtpClient::upload(const std::string &localPath, const std::string &remotePath,
	FtpProgressCallback progress, bool resume)
{
	const std::string path = normalizeRemotePath(remotePath);
	if(!isWriteAllowed(path))
		return FtpResult::failure(QT_TRANSLATE_NOOP("Messages", "Protected system area: turn on Advanced "
			"mode in the settings."));
	const int64_t localSize = fileSize(localPath);
	if(localSize < 0)
		return FtpResult::failure(std::string(QT_TRANSLATE_NOOP("Messages", "Local file not accessible")) + ": " + localPath);

	// Resume: only makes sense if the server already has part of the file.
	int64_t alreadyThere = 0;
	if(resume)
	{
		int64_t remote = -1;
		if(remoteSize(path, &remote).ok && remote > 0 && remote < localSize)
			alreadyThere = remote;
	}

	// A large package can lose its connection more than once (Wi-Fi, the
	// console's server); each new try goes on from what the console already
	// has, and tries keep coming while they make headway.
	const uint64_t generation = cancelGeneration_.load();
	const std::string what = "upload " + baseName(localPath);
	FtpResult result;
	int failuresWithoutHeadway = 0;
	bool appendWorks = true;
	int sizeMismatches = 0;
	// Going on from what the console has is only right once those bytes are
	// this file's: asked to resume, or written by an attempt of this call.
	// Before that, what is there may be the file being replaced — appending
	// to it would leave the right size and the wrong content.
	bool remoteIsOurs = resume;
	for(int attempt = 0;; ++attempt)
	{
		if(attempt > 0)
		{
			const int delayMs = 500 * (1 << std::min(failuresWithoutHeadway, 4));
			logWarning("FTP: " + what + " failed (" + result.message + "); retrying in "
				+ std::to_string(delayMs) + " ms.");
			std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
			if(cancelledSince(generation))
			{
				result.cancelled = true;
				result.ok = false;
				result.message = QT_TRANSLATE_NOOP("Messages", "Operation cancelled.");
				break;
			}
			int64_t remote = -1;
			alreadyThere = 0;
			if(remoteIsOurs && appendWorks && querySize(path, &remote) && remote > 0 && remote < localSize)
				alreadyThere = remote;
			if(alreadyThere > 0)
				logInfo("FTP: " + what + " goes on from " + std::to_string(alreadyThere) + " bytes.");
		}

		int64_t reached = alreadyThere;
		result = uploadOnce(localPath, path, progress, localSize, alreadyThere, &reached, generation);
		if(result.ok)
		{
			// The console's own count of what it got: a data connection that
			// closed early can still end in a "226 Transfer complete". A
			// server that does not say (no SIZE) is taken at its word.
			int64_t remote = -1;
			if(!querySize(path, &remote) || remote < 0 || remote == localSize)
				break;
			logWarning("FTP: " + what + " ended with " + std::to_string(remote) + " of "
				+ std::to_string(localSize) + " bytes on the console.");
			result = FtpResult::failure(QT_TRANSLATE_NOOP("Messages", "The copy on the console came out a "
				"different size from the file sent."));
			remoteIsOurs = true;
			if(remote > localSize)
				appendWorks = false; // more than the file: only starting over mends it
			if(++sizeMismatches >= 3)
				break;
			continue;
		}
		if(result.cancelled)
			break;
		if(reached > alreadyThere)
			remoteIsOurs = true;
		if(alreadyThere > 0 && reached == alreadyThere)
			appendWorks = false; // the server may not take APPE: start over next time
		const bool headway = reached > alreadyThere + (1 << 20);
		failuresWithoutHeadway = headway ? 0 : failuresWithoutHeadway + 1;
		if(failuresWithoutHeadway >= std::max(1, config().maxRetries) || attempt >= 50)
			break;
	}
	if(!result.ok && !result.cancelled)
		logError("FTP: " + what + " failed: " + result.message);
	return result;
}

bool FtpClient::querySize(const std::string &path, int64_t *size)
{
	const Config cfg = config();
	CURL *curl = static_cast<CURL *>(takeHandle());
	if(!curl)
		return false;
	char errorBuffer[CURL_ERROR_SIZE] = { 0 };
	applyCommonOptions(curl, cfg, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_URL, urlFor(path).c_str());
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	std::string headers;
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, appendToString);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToString);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &headers);
	const CURLcode code = curl_easy_perform(curl);
	curl_off_t remote = -1;
	if(code == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &remote);
	giveBack(curl, code == CURLE_OK);
	if(code != CURLE_OK)
		return false;
	*size = static_cast<int64_t>(remote);
	return true;
}

FtpResult FtpClient::uploadOnce(const std::string &localPath, const std::string &path,
	const FtpProgressCallback &progress, int64_t localSize, int64_t from, int64_t *reached, uint64_t generation)
{
	Slot slot(this);
	const Config cfg = config();
	std::ifstream input(localPath, std::ios::binary);
	if(!input)
		return FtpResult::failure(std::string(QT_TRANSLATE_NOOP("Messages", "Could not open the file")) + ": " + localPath);
	if(from > 0)
		input.seekg(static_cast<std::streamoff>(from), std::ios::beg);

	CURL *curl = static_cast<CURL *>(takeHandle());
	if(!curl)
		return FtpResult::failure("could not initialise libcurl");

	char errorBuffer[CURL_ERROR_SIZE] = { 0 };
	ProgressState state;
	state.generation = &cancelGeneration_;
	state.startedAt = generation;
	state.knownTotal = localSize;
	state.offset = from;
	state.callback = [&](int64_t done, int64_t total) {
		*reached = std::max(*reached, done);
		return progress ? progress(done, total) : true;
	};

	applyCommonOptions(curl, cfg, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_URL, urlFor(path).c_str());
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, readFromStream);
	curl_easy_setopt(curl, CURLOPT_READDATA, &input);
	curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(localSize - from));
	curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR);
	// Bigger writes: fewer system calls on a fast network.
	curl_easy_setopt(curl, CURLOPT_UPLOAD_BUFFERSIZE, 2L * 1024 * 1024);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
	// APPE after what is already there (the server has to support it); the
	// stream already starts at that point.
	if(from > 0)
		curl_easy_setopt(curl, CURLOPT_APPEND, 1L);

	const CURLcode code = curl_easy_perform(curl);
	giveBack(curl, code == CURLE_OK);
	if(state.cancelled)
	{
		FtpResult result;
		result.cancelled = true;
		result.message = QT_TRANSLATE_NOOP("Messages", "Operation cancelled.");
		return result;
	}
	if(code != CURLE_OK)
		return FtpResult::failure(curlMessage(code, errorBuffer));
	return FtpResult::success();
}

namespace {

// A download's progress: `carried` bytes came in earlier tries of the same
// call, and this try started with `start` bytes in the file.
struct DownloadState
{
	FtpProgressCallback callback;
	const std::atomic<uint64_t> *generation = nullptr;
	uint64_t startedAt = 0;
	int64_t carried = 0;
	int64_t start = 0;
	int64_t *reached = nullptr;
	bool cancelled = false;
};

int downloadProgress(void *userdata, curl_off_t dlTotal, curl_off_t dlNow, curl_off_t, curl_off_t)
{
	auto *state = static_cast<DownloadState *>(userdata);
	if(state->generation && state->generation->load() != state->startedAt)
	{
		state->cancelled = true;
		return 1;
	}
	*state->reached = std::max(*state->reached, state->start + static_cast<int64_t>(dlNow));
	const int64_t done = std::max<int64_t>(0, state->carried + static_cast<int64_t>(dlNow));
	const int64_t total = dlTotal > 0 ? std::max<int64_t>(0, state->carried + static_cast<int64_t>(dlTotal)) : 0;
	if(state->callback && !state->callback(done, total))
	{
		state->cancelled = true;
		return 1;
	}
	return 0;
}

} // namespace

FtpResult FtpClient::download(const std::string &remotePath, const std::string &localPath,
	FtpProgressCallback progress, bool resume)
{
	const std::string path = normalizeRemotePath(remotePath);
	// What is here already counts only when asked to go on from it.
	const int64_t initial = resume ? std::max<int64_t>(0, fileSize(localPath)) : 0;
	int64_t from = initial;
	const uint64_t generation = cancelGeneration_.load();
	const std::string what = "download " + baseName(path);
	FtpResult result;
	int failuresWithoutHeadway = 0;
	bool restWorks = true;
	for(int attempt = 0;; ++attempt)
	{
		if(attempt > 0)
		{
			const int delayMs = 500 * (1 << std::min(failuresWithoutHeadway, 4));
			logWarning("FTP: " + what + " failed (" + result.message + "); retrying in "
				+ std::to_string(delayMs) + " ms.");
			std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
			if(cancelledSince(generation))
			{
				result.cancelled = true;
				result.ok = false;
				result.message = QT_TRANSLATE_NOOP("Messages", "Operation cancelled.");
				break;
			}
			// What the tries before brought is this file's: the next one goes
			// on from there, when the server takes REST.
			from = restWorks ? std::max<int64_t>(0, fileSize(localPath)) : 0;
			if(from > 0)
				logInfo("FTP: " + what + " goes on from " + std::to_string(from) + " bytes.");
		}
		int64_t reached = from;
		bool restRefused = false;
		result = downloadOnce(path, localPath, progress, from, from - initial, &reached, &restRefused, generation);
		if(result.ok || result.cancelled || result.final)
			break;
		if(restRefused)
			restWorks = false;
		const bool headway = reached > from + (1 << 20);
		failuresWithoutHeadway = headway ? 0 : failuresWithoutHeadway + 1;
		if(failuresWithoutHeadway >= std::max(1, config().maxRetries) || attempt >= 50)
			break;
	}
	// The server saying the file is not there (or not readable) is its
	// answer: whoever asked decides whether it is an error.
	if(!result.ok && !result.cancelled)
	{
		if(result.final)
			logDebug("FTP: " + what + ": the console answered \"" + result.message + "\"");
		else
			logError("FTP: " + what + " failed: " + result.message);
	}
	return result;
}

FtpResult FtpClient::downloadOnce(const std::string &path, const std::string &localPath,
	const FtpProgressCallback &progress, int64_t from, int64_t carried, int64_t *reached, bool *restRefused,
	uint64_t generation)
{
	Slot slot(this);
	const Config cfg = config();
	// Big writes to the disk too, set before the file is opened.
	std::vector<char> diskBuffer(1024 * 1024);
	std::ofstream output;
	output.rdbuf()->pubsetbuf(diskBuffer.data(), static_cast<std::streamsize>(diskBuffer.size()));
	output.open(localPath, std::ios::binary | (from > 0 ? std::ios::app : std::ios::trunc));
	if(!output)
		return FtpResult::failure(std::string(QT_TRANSLATE_NOOP("Messages", "Could not write to the file")) + ": " + localPath);

	CURL *curl = static_cast<CURL *>(takeHandle());
	if(!curl)
		return FtpResult::failure("could not initialise libcurl");

	char errorBuffer[CURL_ERROR_SIZE] = { 0 };
	DownloadState state;
	state.callback = progress;
	state.generation = &cancelGeneration_;
	state.startedAt = generation;
	state.carried = carried;
	state.start = from;
	state.reached = reached;

	applyCommonOptions(curl, cfg, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_URL, urlFor(path).c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToStream);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
	// Bigger reads: fewer system calls on a fast network (512 KB is the
	// most every libcurl takes).
	curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 512L * 1024);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, downloadProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
	if(from > 0)
		curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(from));

	const CURLcode code = curl_easy_perform(curl);
	giveBack(curl, code == CURLE_OK);
	output.close();
	if(state.cancelled)
	{
		FtpResult result;
		result.cancelled = true;
		result.message = QT_TRANSLATE_NOOP("Messages", "Operation cancelled.");
		return result;
	}
	if(code == CURLE_FTP_COULDNT_USE_REST)
		*restRefused = true;
	if(code != CURLE_OK)
		return readFailure(code, errorBuffer);
	return FtpResult::success();
}

bool FtpClient::freeSpace(const std::string &remoteDir, int64_t *bytes)
{
	if(noFreeSpace_.load())
		return false;
	const Config cfg = config();
	CURL *curl = static_cast<CURL *>(takeHandle());
	if(!curl)
		return false;
	char errorBuffer[CURL_ERROR_SIZE] = { 0 };
	std::string replies;
	// "*": a server without AVBL answers with an error, and that is no failure.
	struct curl_slist *commands = curl_slist_append(nullptr, ("*AVBL " + normalizeRemotePath(remoteDir)).c_str());
	applyCommonOptions(curl, cfg, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_URL, ("ftp://" + cfg.host + ":" + std::to_string(cfg.port) + "/").c_str());
	curl_easy_setopt(curl, CURLOPT_QUOTE, commands);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, appendToString);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &replies);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	const CURLcode code = curl_easy_perform(curl);
	curl_slist_free_all(commands);
	giveBack(curl, code == CURLE_OK);
	if(code != CURLE_OK)
		return false;
	// "213 123456789": the bytes free.
	std::istringstream lines(replies);
	std::string line;
	while(std::getline(lines, line))
	{
		if(line.size() < 5 || line.compare(0, 4, "213 ") != 0)
			continue;
		const std::string number = trim(line.substr(4));
		if(number.empty() || number.find_first_not_of("0123456789") != std::string::npos)
			continue;
		try
		{
			*bytes = static_cast<int64_t>(std::stoll(number));
			return true;
		}
		catch(...)
		{
		}
	}
	logInfo("FTP: the server does not say how much room is left (AVBL).");
	noFreeSpace_.store(true);
	return false;
}

namespace {

struct RangeState
{
	std::vector<uint8_t> *bytes = nullptr;
	size_t wanted = 0;
};

// Keeps what was asked for, then stops the transfer: a server that ignores
// the end of the range would otherwise send the rest of the file.
size_t appendRange(char *data, size_t size, size_t count, void *userdata)
{
	auto *state = static_cast<RangeState *>(userdata);
	const size_t bytes = size * count;
	const size_t room = state->wanted - std::min(state->wanted, state->bytes->size());
	const size_t taken = std::min(room, bytes);
	state->bytes->insert(state->bytes->end(), reinterpret_cast<const uint8_t *>(data),
		reinterpret_cast<const uint8_t *>(data) + taken);
	return state->bytes->size() >= state->wanted ? 0 : bytes;
}

} // namespace

FtpResult FtpClient::read(const std::string &remotePath, int64_t offset, size_t length, std::vector<uint8_t> *bytes)
{
	const std::string path = normalizeRemotePath(remotePath);
	bytes->clear();
	if(length == 0)
		return FtpResult::success();
	return withRetries("read " + baseName(path), [&]() -> FtpResult {
		Slot slot(this);
		const Config cfg = config();
		bytes->clear();
		CURL *curl = static_cast<CURL *>(takeHandle());
		if(!curl)
			return FtpResult::failure("could not initialise libcurl");
		char errorBuffer[CURL_ERROR_SIZE] = { 0 };
		RangeState state;
		state.bytes = bytes;
		state.wanted = length;
		const std::string range = std::to_string(offset) + "-" + std::to_string(offset + static_cast<int64_t>(length) - 1);
		applyCommonOptions(curl, cfg, errorBuffer);
		curl_easy_setopt(curl, CURLOPT_URL, urlFor(path).c_str());
		curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendRange);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
		const CURLcode code = curl_easy_perform(curl);
		giveBack(curl, code == CURLE_OK);
		// Stopped on purpose once it had everything.
		if(bytes->size() >= length || code == CURLE_OK)
			return FtpResult::success();
		return readFailure(code, errorBuffer);
	});
}

FtpResult FtpClient::runCommands(const std::vector<std::string> &commands, std::string *replies)
{
	const Config cfg = config();
	CURL *curl = static_cast<CURL *>(takeHandle());
	if(!curl)
		return FtpResult::failure("could not initialise libcurl");

	char errorBuffer[CURL_ERROR_SIZE] = { 0 };
	struct curl_slist *list = nullptr;
	for(const std::string &command : commands)
		list = curl_slist_append(list, command.c_str());

	std::string answers;
	applyCommonOptions(curl, cfg, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_URL, ("ftp://" + cfg.host + ":" + std::to_string(cfg.port) + "/").c_str());
	curl_easy_setopt(curl, CURLOPT_POSTQUOTE, list);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, appendToString);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &answers);

	const CURLcode code = curl_easy_perform(curl);
	curl_slist_free_all(list);
	giveBack(curl, code == CURLE_OK);
	if(replies)
		*replies = answers;
	if(code != CURLE_OK)
	{
		FtpResult result = FtpResult::failure(curlMessage(code, errorBuffer));
		// The server refusing the command is its answer (a folder that is
		// not empty, a rename across disks): asking again changes nothing.
		result.final = code == CURLE_QUOTE_ERROR;
		return result;
	}
	return FtpResult::success();
}

FtpResult FtpClient::makeDirectory(const std::string &remotePath)
{
	const std::string path = normalizeRemotePath(remotePath);
	if(!isWriteAllowed(path))
		return FtpResult::failure(QT_TRANSLATE_NOOP("Messages", "Protected system area: turn on Advanced "
			"mode in the settings."));
	return withRetries("create folder " + path, [&]() -> FtpResult {
		const FtpResult made = runCommands({ "MKD " + path });
		// A folder that is there already is what was asked for. Some servers
		// (the PS5's ftpsrv) refuse MKD for it; going into it tells.
		if(!made.ok && runCommands({ "CWD " + path }).ok)
			return FtpResult::success();
		return made;
	});
}

FtpResult FtpClient::removeFile(const std::string &remotePath)
{
	const std::string path = normalizeRemotePath(remotePath);
	if(!isWriteAllowed(path))
		return FtpResult::failure(QT_TRANSLATE_NOOP("Messages", "Protected system area: turn on Advanced "
			"mode in the settings."));
	return withRetries("delete " + path, [&]() -> FtpResult {
		return runCommands({ "DELE " + path });
	});
}

FtpResult FtpClient::removeDirectory(const std::string &remotePath)
{
	const std::string path = normalizeRemotePath(remotePath);
	if(!isWriteAllowed(path))
		return FtpResult::failure(QT_TRANSLATE_NOOP("Messages", "Protected system area: turn on Advanced "
			"mode in the settings."));
	return withRetries("delete folder " + path, [&]() -> FtpResult {
		return runCommands({ "RMD " + path });
	});
}

FtpResult FtpClient::rename(const std::string &fromPath, const std::string &toPath)
{
	const std::string from = normalizeRemotePath(fromPath);
	const std::string to = normalizeRemotePath(toPath);
	if(!isWriteAllowed(from) || !isWriteAllowed(to))
		return FtpResult::failure(QT_TRANSLATE_NOOP("Messages", "Protected system area: turn on Advanced "
			"mode in the settings."));
	return withRetries("rename " + from, [&]() -> FtpResult {
		return runCommands({ "RNFR " + from, "RNTO " + to });
	});
}

FtpResult FtpClient::setPermissions(const std::string &remotePath, const std::string &mode)
{
	const std::string path = normalizeRemotePath(remotePath);
	if(mode.empty() || mode.size() > 4 || mode.find_first_not_of("01234567") != std::string::npos)
		return FtpResult::failure("not an octal mode: " + mode);
	if(!isWriteAllowed(path))
		return FtpResult::failure(QT_TRANSLATE_NOOP("Messages", "Protected system area: turn on Advanced "
			"mode in the settings."));
	return runCommands({ "SITE CHMOD " + mode + " " + path });
}

bool FtpClient::hasFeature(const std::string &feature)
{
	{
		std::lock_guard<std::mutex> lock(featureMutex_);
		if(featuresKnown_)
			return std::find(features_.begin(), features_.end(), upperCase(feature)) != features_.end();
	}
	// "*": a server without FEAT answers with an error, and that is no failure.
	std::string replies;
	const FtpResult asked = runCommands({ "*FEAT" }, &replies);
	std::vector<std::string> found;
	// "211-Features:", then one feature a line, each line starting with a
	// space ("␠MLSD", "␠REST STREAM"), then "211 End".
	std::istringstream lines(replies);
	std::string line;
	while(std::getline(lines, line))
	{
		if(line.size() < 2 || line[0] != ' ')
			continue;
		std::istringstream words(trim(line));
		std::string word;
		if(words >> word)
			found.push_back(upperCase(word));
	}
	std::lock_guard<std::mutex> lock(featureMutex_);
	// Not reached at all (the console away): asked again next time.
	if(!asked.ok && found.empty())
		return false;
	features_ = found;
	featuresKnown_ = true;
	if(!found.empty())
	{
		std::string list;
		for(const std::string &name : found)
			list += (list.empty() ? "" : " ") + name;
		logDebug("FTP: the server offers " + list);
	}
	return std::find(features_.begin(), features_.end(), upperCase(feature)) != features_.end();
}

FtpResult FtpClient::command(const std::string &line, std::string *reply)
{
	std::string replies;
	const FtpResult result = runCommands({ line }, &replies);
	if(reply)
	{
		// Only what answered the command, not the login's lines before it.
		std::string last;
		std::istringstream lines(replies);
		std::string text;
		while(std::getline(lines, text))
		{
			text = trim(text);
			if(text.size() >= 4 && std::isdigit(static_cast<unsigned char>(text[0])))
				last = text;
		}
		*reply = last;
	}
	return result;
}

} // namespace orbislink
