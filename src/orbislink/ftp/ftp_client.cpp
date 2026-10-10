// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/ftp/ftp_client.h"

#include "orbislink/common/log.h"
#include "orbislink/common/tr.h"
#include "orbislink/common/util.h"

#include <algorithm>
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
	const std::atomic<bool> *cancel = nullptr;
	int64_t knownTotal = 0;
	int64_t offset = 0; // already on the other side before this transfer
	bool cancelled = false;
};

int progressCallback(void *userdata, curl_off_t dlTotal, curl_off_t dlNow, curl_off_t ulTotal,
	curl_off_t ulNow)
{
	auto *state = static_cast<ProgressState *>(userdata);
	if(state->cancel && state->cancel->load())
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

FtpClient::~FtpClient() = default;

void FtpClient::setConfig(const Config &config)
{
	std::lock_guard<std::mutex> lock(mutex_);
	config_ = config;
	if(config_.port == 0)
		config_.port = 2121;
	config_.maxConnections = std::max(1, std::min(2, config_.maxConnections));
}

FtpClient::Config FtpClient::config() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return config_;
}

void FtpClient::cancel() { cancel_.store(true); }

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
	std::sort(entries.begin(), entries.end(), [](const FtpEntry &a, const FtpEntry &b) {
		if(a.isDirectory != b.isDirectory)
			return a.isDirectory;
		return toLower(a.name) < toLower(b.name);
	});
	return entries;
}

FtpResult FtpClient::withRetries(const std::string &what, const std::function<FtpResult()> &operation)
{
	cancel_.store(false);
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
	// has): whoever asked decides whether it is an error.
	if(!result.ok && !result.cancelled)
	{
		if(result.final)
			logInfo("FTP: " + what + ": the console answered \"" + result.message + "\"");
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
		CURL *curl = curl_easy_init();
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

		const CURLcode code = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		if(code != CURLE_OK)
			return readFailure(code, errorBuffer);
		if(entries)
			*entries = parseListing(listing, dir);
		return FtpResult::success();
	});
}

FtpResult FtpClient::remoteSize(const std::string &remotePath, int64_t *size)
{
	const std::string path = normalizeRemotePath(remotePath);
	return withRetries("get size of " + path, [&]() -> FtpResult {
		const Config cfg = config();
		CURL *curl = curl_easy_init();
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
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToString);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &headers);

		const CURLcode code = curl_easy_perform(curl);
		curl_off_t remote = -1;
		if(code == CURLE_OK)
			curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &remote);
		curl_easy_cleanup(curl);
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
	cancel_.store(false);
	const std::string what = "upload " + baseName(localPath);
	FtpResult result;
	int failuresWithoutHeadway = 0;
	bool appendWorks = true;
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
			if(cancel_.load())
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
		result = uploadOnce(localPath, path, progress, localSize, alreadyThere, &reached);
		if(result.ok || result.cancelled)
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
	CURL *curl = curl_easy_init();
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
	curl_easy_cleanup(curl);
	if(code != CURLE_OK)
		return false;
	*size = static_cast<int64_t>(remote);
	return true;
}

FtpResult FtpClient::uploadOnce(const std::string &localPath, const std::string &path,
	const FtpProgressCallback &progress, int64_t localSize, int64_t from, int64_t *reached)
{
	Slot slot(this);
	const Config cfg = config();
	std::ifstream input(localPath, std::ios::binary);
	if(!input)
		return FtpResult::failure(std::string(QT_TRANSLATE_NOOP("Messages", "Could not open the file")) + ": " + localPath);
	if(from > 0)
		input.seekg(static_cast<std::streamoff>(from), std::ios::beg);

	CURL *curl = curl_easy_init();
	if(!curl)
		return FtpResult::failure("could not initialise libcurl");

	char errorBuffer[CURL_ERROR_SIZE] = { 0 };
	ProgressState state;
	state.cancel = &cancel_;
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
	curl_easy_cleanup(curl);
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

FtpResult FtpClient::download(const std::string &remotePath, const std::string &localPath,
	FtpProgressCallback progress, bool resume)
{
	const std::string path = normalizeRemotePath(remotePath);
	int64_t existing = resume ? fileSize(localPath) : -1;
	if(existing < 0)
		existing = 0;

	return withRetries("download " + baseName(path), [&]() -> FtpResult {
		Slot slot(this);
		const Config cfg = config();
		std::ofstream output(localPath,
			std::ios::binary | (existing > 0 ? std::ios::app : std::ios::trunc));
		if(!output)
			return FtpResult::failure(std::string(QT_TRANSLATE_NOOP("Messages", "Could not write to the file")) + ": " + localPath);

		CURL *curl = curl_easy_init();
		if(!curl)
			return FtpResult::failure("could not initialise libcurl");

		char errorBuffer[CURL_ERROR_SIZE] = { 0 };
		ProgressState state;
		state.callback = progress;
		state.cancel = &cancel_;

		applyCommonOptions(curl, cfg, errorBuffer);
		curl_easy_setopt(curl, CURLOPT_URL, urlFor(path).c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToStream);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
		if(existing > 0)
			curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(existing));

		const CURLcode code = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		output.close();
		if(state.cancelled)
		{
			FtpResult result;
			result.cancelled = true;
			result.message = QT_TRANSLATE_NOOP("Messages", "Operation cancelled.");
			return result;
		}
		if(code != CURLE_OK)
			return readFailure(code, errorBuffer);
		return FtpResult::success();
	});
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
		CURL *curl = curl_easy_init();
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
		curl_easy_cleanup(curl);
		// Stopped on purpose once it had everything.
		if(bytes->size() >= length || code == CURLE_OK)
			return FtpResult::success();
		return readFailure(code, errorBuffer);
	});
}

namespace {

FtpResult runQuoteCommands(const FtpClient::Config &cfg, const std::string &host,
	const std::vector<std::string> &commands, const std::string &baseUrl)
{
	CURL *curl = curl_easy_init();
	if(!curl)
		return FtpResult::failure("could not initialise libcurl");

	char errorBuffer[CURL_ERROR_SIZE] = { 0 };
	struct curl_slist *list = nullptr;
	for(const std::string &command : commands)
		list = curl_slist_append(list, command.c_str());

	applyCommonOptions(curl, cfg, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_URL, baseUrl.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTQUOTE, list);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);

	const CURLcode code = curl_easy_perform(curl);
	curl_slist_free_all(list);
	curl_easy_cleanup(curl);
	(void)host;
	if(code != CURLE_OK)
		return FtpResult::failure(curlMessage(code, errorBuffer));
	return FtpResult::success();
}

} // namespace

FtpResult FtpClient::makeDirectory(const std::string &remotePath)
{
	const std::string path = normalizeRemotePath(remotePath);
	if(!isWriteAllowed(path))
		return FtpResult::failure(QT_TRANSLATE_NOOP("Messages", "Protected system area: turn on Advanced "
			"mode in the settings."));
	return withRetries("create folder " + path, [&]() -> FtpResult {
		const Config cfg = config();
		return runQuoteCommands(cfg, cfg.host, { "MKD " + path },
			"ftp://" + cfg.host + ":" + std::to_string(cfg.port) + "/");
	});
}

FtpResult FtpClient::removeFile(const std::string &remotePath)
{
	const std::string path = normalizeRemotePath(remotePath);
	if(!isWriteAllowed(path))
		return FtpResult::failure(QT_TRANSLATE_NOOP("Messages", "Protected system area: turn on Advanced "
			"mode in the settings."));
	return withRetries("delete " + path, [&]() -> FtpResult {
		const Config cfg = config();
		return runQuoteCommands(cfg, cfg.host, { "DELE " + path },
			"ftp://" + cfg.host + ":" + std::to_string(cfg.port) + "/");
	});
}

FtpResult FtpClient::removeDirectory(const std::string &remotePath)
{
	const std::string path = normalizeRemotePath(remotePath);
	if(!isWriteAllowed(path))
		return FtpResult::failure(QT_TRANSLATE_NOOP("Messages", "Protected system area: turn on Advanced "
			"mode in the settings."));
	return withRetries("delete folder " + path, [&]() -> FtpResult {
		const Config cfg = config();
		return runQuoteCommands(cfg, cfg.host, { "RMD " + path },
			"ftp://" + cfg.host + ":" + std::to_string(cfg.port) + "/");
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
		const Config cfg = config();
		return runQuoteCommands(cfg, cfg.host, { "RNFR " + from, "RNTO " + to },
			"ftp://" + cfg.host + ":" + std::to_string(cfg.port) + "/");
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
	const Config cfg = config();
	return runQuoteCommands(cfg, cfg.host, { "SITE CHMOD " + mode + " " + path },
		"ftp://" + cfg.host + ":" + std::to_string(cfg.port) + "/");
}

} // namespace orbislink
