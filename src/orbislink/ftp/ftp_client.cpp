// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/ftp/ftp_client.h"

#include "orbislink/common/log.h"
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
	bool cancelled = false;
};

int progressCallback(void *userdata, curl_off_t dlTotal, curl_off_t dlNow, curl_off_t ulTotal,
	curl_off_t ulNow)
{
	auto *state = static_cast<ProgressState *>(userdata);
	if(state->cancel && state->cancel->load())
	{
		state->cancelled = true;
		return 1; // aborta a transferência
	}
	const int64_t done = ulNow > 0 ? static_cast<int64_t>(ulNow) : static_cast<int64_t>(dlNow);
	int64_t total = ulTotal > 0 ? static_cast<int64_t>(ulTotal) : static_cast<int64_t>(dlTotal);
	if(total <= 0)
		total = state->knownTotal;
	if(state->callback && !state->callback(done, total))
	{
		state->cancelled = true;
		return 1;
	}
	return 0;
}

// Ordem dos campos do LIST estilo Unix:
// drwxr-xr-x  2 user group  4096 Jan 01 00:00 nome com espaços
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

// Formato MS-DOS, caso algum servidor o use:
// 01-01-70  00:00AM       <DIR>          nome
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

} // namespace

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
			// Servidor exótico: aceita a linha como nome simples em vez de a perder.
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
			logWarning("FTP: " + what + " falhou (" + result.message + "); nova tentativa em "
				+ std::to_string(delayMs) + " ms.");
			std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
		}
		result = operation();
		if(result.ok || result.cancelled)
			break;
	}
	if(!result.ok && !result.cancelled)
		logError("FTP: " + what + " falhou: " + result.message);
	return result;
}

namespace {

// Configuração comum a todas as operações.
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
	// O ConsoleManager confirma o "220" com um probe TCP; aqui confirma-se o
	// login anónimo com um PWD.
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
	return withRetries("listar " + dir, [&]() -> FtpResult {
		Slot slot(this);
		const Config cfg = config();
		CURL *curl = curl_easy_init();
		if(!curl)
			return FtpResult::failure("não foi possível inicializar o libcurl");

		char errorBuffer[CURL_ERROR_SIZE] = { 0 };
		std::string listing;
		std::string url = "ftp://" + cfg.host + ":" + std::to_string(cfg.port) + urlEncodePath(dir);
		if(url.back() != '/')
			url += '/';

		applyCommonOptions(curl, cfg, errorBuffer);
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToString);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &listing);
		// Pede o LIST completo (não apenas nomes), para obter tipo e tamanho.
		curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 0L);

		const CURLcode code = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		if(code != CURLE_OK)
			return FtpResult::failure(curlMessage(code, errorBuffer));
		if(entries)
			*entries = parseListing(listing, dir);
		return FtpResult::success();
	});
}

FtpResult FtpClient::remoteSize(const std::string &remotePath, int64_t *size)
{
	const std::string path = normalizeRemotePath(remotePath);
	return withRetries("obter tamanho de " + path, [&]() -> FtpResult {
		Slot slot(this);
		const Config cfg = config();
		CURL *curl = curl_easy_init();
		if(!curl)
			return FtpResult::failure("não foi possível inicializar o libcurl");

		char errorBuffer[CURL_ERROR_SIZE] = { 0 };
		applyCommonOptions(curl, cfg, errorBuffer);
		curl_easy_setopt(curl, CURLOPT_URL, urlFor(path).c_str());
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
		curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);

		const CURLcode code = curl_easy_perform(curl);
		curl_off_t remote = -1;
		if(code == CURLE_OK)
			curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &remote);
		curl_easy_cleanup(curl);
		if(code != CURLE_OK)
			return FtpResult::failure(curlMessage(code, errorBuffer));
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
		return FtpResult::failure("Zona protegida do sistema: ativa o Modo avançado nas definições.");
	const int64_t localSize = fileSize(localPath);
	if(localSize < 0)
		return FtpResult::failure("Ficheiro local inacessível: " + localPath);

	// Retoma: só faz sentido se o servidor já tiver parte do ficheiro.
	int64_t alreadyThere = 0;
	if(resume)
	{
		int64_t remote = -1;
		if(remoteSize(path, &remote).ok && remote > 0 && remote < localSize)
			alreadyThere = remote;
	}

	return withRetries("enviar " + baseName(localPath), [&]() -> FtpResult {
		Slot slot(this);
		const Config cfg = config();
		std::ifstream input(localPath, std::ios::binary);
		if(!input)
			return FtpResult::failure("Não foi possível abrir " + localPath);
		if(alreadyThere > 0)
			input.seekg(static_cast<std::streamoff>(alreadyThere), std::ios::beg);

		CURL *curl = curl_easy_init();
		if(!curl)
			return FtpResult::failure("não foi possível inicializar o libcurl");

		char errorBuffer[CURL_ERROR_SIZE] = { 0 };
		ProgressState state;
		state.callback = progress;
		state.cancel = &cancel_;
		state.knownTotal = localSize;

		applyCommonOptions(curl, cfg, errorBuffer);
		curl_easy_setopt(curl, CURLOPT_URL, urlFor(path).c_str());
		curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, readFromStream);
		curl_easy_setopt(curl, CURLOPT_READDATA, &input);
		curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
			static_cast<curl_off_t>(localSize - alreadyThere));
		curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
		if(alreadyThere > 0)
		{
			// APPE a partir do que já lá está (o servidor tem de o suportar).
			curl_easy_setopt(curl, CURLOPT_APPEND, 1L);
			curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(alreadyThere));
		}

		const CURLcode code = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		if(state.cancelled)
		{
			FtpResult result;
			result.cancelled = true;
			result.message = "Operação cancelada.";
			return result;
		}
		if(code != CURLE_OK)
			return FtpResult::failure(curlMessage(code, errorBuffer));
		return FtpResult::success();
	});
}

FtpResult FtpClient::download(const std::string &remotePath, const std::string &localPath,
	FtpProgressCallback progress, bool resume)
{
	const std::string path = normalizeRemotePath(remotePath);
	int64_t existing = resume ? fileSize(localPath) : -1;
	if(existing < 0)
		existing = 0;

	return withRetries("descarregar " + baseName(path), [&]() -> FtpResult {
		Slot slot(this);
		const Config cfg = config();
		std::ofstream output(localPath,
			std::ios::binary | (existing > 0 ? std::ios::app : std::ios::trunc));
		if(!output)
			return FtpResult::failure("Não foi possível escrever em " + localPath);

		CURL *curl = curl_easy_init();
		if(!curl)
			return FtpResult::failure("não foi possível inicializar o libcurl");

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
			result.message = "Operação cancelada.";
			return result;
		}
		if(code != CURLE_OK)
			return FtpResult::failure(curlMessage(code, errorBuffer));
		return FtpResult::success();
	});
}

namespace {

FtpResult runQuoteCommands(const FtpClient::Config &cfg, const std::string &host,
	const std::vector<std::string> &commands, const std::string &baseUrl)
{
	CURL *curl = curl_easy_init();
	if(!curl)
		return FtpResult::failure("não foi possível inicializar o libcurl");

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
		return FtpResult::failure("Zona protegida do sistema: ativa o Modo avançado nas definições.");
	return withRetries("criar pasta " + path, [&]() -> FtpResult {
		Slot slot(this);
		const Config cfg = config();
		return runQuoteCommands(cfg, cfg.host, { "MKD " + path },
			"ftp://" + cfg.host + ":" + std::to_string(cfg.port) + "/");
	});
}

FtpResult FtpClient::removeFile(const std::string &remotePath)
{
	const std::string path = normalizeRemotePath(remotePath);
	if(!isWriteAllowed(path))
		return FtpResult::failure("Zona protegida do sistema: ativa o Modo avançado nas definições.");
	return withRetries("apagar " + path, [&]() -> FtpResult {
		Slot slot(this);
		const Config cfg = config();
		return runQuoteCommands(cfg, cfg.host, { "DELE " + path },
			"ftp://" + cfg.host + ":" + std::to_string(cfg.port) + "/");
	});
}

FtpResult FtpClient::removeDirectory(const std::string &remotePath)
{
	const std::string path = normalizeRemotePath(remotePath);
	if(!isWriteAllowed(path))
		return FtpResult::failure("Zona protegida do sistema: ativa o Modo avançado nas definições.");
	return withRetries("apagar pasta " + path, [&]() -> FtpResult {
		Slot slot(this);
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
		return FtpResult::failure("Zona protegida do sistema: ativa o Modo avançado nas definições.");
	return withRetries("mudar nome de " + from, [&]() -> FtpResult {
		Slot slot(this);
		const Config cfg = config();
		return runQuoteCommands(cfg, cfg.host, { "RNFR " + from, "RNTO " + to },
			"ftp://" + cfg.host + ":" + std::to_string(cfg.port) + "/");
	});
}

} // namespace orbislink
