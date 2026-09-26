// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/http/local_http_server.h"

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/net/net_utils.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace orbislink {

namespace {

constexpr size_t kMaxRequestBytes = 16 * 1024;

struct HttpRequest
{
	std::string method;
	std::string target;
	std::map<std::string, std::string> headers;

	std::string header(const std::string &name) const
	{
		auto it = headers.find(toLower(name));
		return it == headers.end() ? std::string() : it->second;
	}
};

bool sendAll(socket_t sock, const char *data, size_t size)
{
	size_t sent = 0;
	while(sent < size)
	{
		const int chunk = static_cast<int>(std::min<size_t>(size - sent, 64 * 1024));
		const int written = static_cast<int>(send(sock, data + sent, chunk, 0));
		if(written <= 0)
			return false;
		sent += static_cast<size_t>(written);
	}
	return true;
}

bool sendText(socket_t sock, const std::string &text) { return sendAll(sock, text.data(), text.size()); }

std::string statusLine(int code)
{
	switch(code)
	{
		case 200: return "HTTP/1.1 200 OK\r\n";
		case 206: return "HTTP/1.1 206 Partial Content\r\n";
		case 400: return "HTTP/1.1 400 Bad Request\r\n";
		case 403: return "HTTP/1.1 403 Forbidden\r\n";
		case 404: return "HTTP/1.1 404 Not Found\r\n";
		case 405: return "HTTP/1.1 405 Method Not Allowed\r\n";
		case 416: return "HTTP/1.1 416 Range Not Satisfiable\r\n";
		default: return "HTTP/1.1 500 Internal Server Error\r\n";
	}
}

void sendSimpleStatus(socket_t sock, int code, const std::string &extraHeaders = std::string())
{
	std::ostringstream os;
	os << statusLine(code) << "Server: OrbisLink\r\n"
	   << "Content-Length: 0\r\n"
	   << "Connection: close\r\n"
	   << extraHeaders << "\r\n";
	sendText(sock, os.str());
}

// Reads the request up to \r\n\r\n. Returns false if it is invalid or too large.
bool readRequest(socket_t sock, HttpRequest *request)
{
	std::string buffer;
	char chunk[2048];
	size_t headerEnd = std::string::npos;
	while(buffer.size() < kMaxRequestBytes)
	{
		const int received = static_cast<int>(recv(sock, chunk, sizeof(chunk), 0));
		if(received <= 0)
			return false;
		buffer.append(chunk, static_cast<size_t>(received));
		headerEnd = buffer.find("\r\n\r\n");
		if(headerEnd != std::string::npos)
			break;
	}
	if(headerEnd == std::string::npos)
		return false;

	std::istringstream stream(buffer.substr(0, headerEnd));
	std::string line;
	if(!std::getline(stream, line))
		return false;
	if(!line.empty() && line.back() == '\r')
		line.pop_back();
	std::istringstream requestLine(line);
	std::string version;
	if(!(requestLine >> request->method >> request->target >> version))
		return false;

	while(std::getline(stream, line))
	{
		if(!line.empty() && line.back() == '\r')
			line.pop_back();
		if(line.empty())
			continue;
		const size_t colon = line.find(':');
		if(colon == std::string::npos)
			continue;
		request->headers[toLower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
	}
	return true;
}

enum class RangeResult { None, Ok, Unsatisfiable, Malformed };

// Supports "bytes=a-b", "bytes=a-" and "bytes=-n" (suffix). Multiple ranges
// are not supported: the first is served, which is what the console asks for.
RangeResult parseRange(const std::string &value, int64_t fileSize, int64_t *start, int64_t *end)
{
	if(value.empty())
		return RangeResult::None;
	if(!startsWith(toLower(value), "bytes="))
		return RangeResult::Malformed;
	std::string spec = trim(value.substr(6));
	const size_t comma = spec.find(',');
	if(comma != std::string::npos)
		spec = trim(spec.substr(0, comma));
	const size_t dash = spec.find('-');
	if(dash == std::string::npos)
		return RangeResult::Malformed;

	const std::string firstPart = trim(spec.substr(0, dash));
	const std::string lastPart = trim(spec.substr(dash + 1));
	if(firstPart.empty() && lastPart.empty())
		return RangeResult::Malformed;

	auto toInt = [](const std::string &text, int64_t *out) {
		if(text.empty())
			return false;
		for(char c : text)
		{
			if(!std::isdigit(static_cast<unsigned char>(c)))
				return false;
		}
		try { *out = static_cast<int64_t>(std::stoll(text)); }
		catch(...) { return false; }
		return true;
	};

	if(firstPart.empty())
	{
		int64_t suffix = 0;
		if(!toInt(lastPart, &suffix))
			return RangeResult::Malformed;
		if(suffix == 0)
			return RangeResult::Unsatisfiable;
		*start = suffix >= fileSize ? 0 : fileSize - suffix;
		*end = fileSize - 1;
		return RangeResult::Ok;
	}

	int64_t from = 0;
	if(!toInt(firstPart, &from))
		return RangeResult::Malformed;
	if(from >= fileSize)
		return RangeResult::Unsatisfiable;
	int64_t to = fileSize - 1;
	if(!lastPart.empty() && !toInt(lastPart, &to))
		return RangeResult::Malformed;
	if(to >= fileSize)
		to = fileSize - 1;
	if(to < from)
		return RangeResult::Unsatisfiable;
	*start = from;
	*end = to;
	return RangeResult::Ok;
}

} // namespace

LocalHttpServer::LocalHttpServer() { initSocketsOnce(); }

LocalHttpServer::~LocalHttpServer() { stop(); }

bool LocalHttpServer::start(const Config &config, std::string *error)
{
	auto fail = [&](const std::string &message) {
		if(error)
			*error = message;
		logError("Local HTTP server: " + message);
		return false;
	};

	if(running_.load())
		return fail("the server is already running");

	config_ = config;
	if(config_.bindAddress.empty())
		config_.bindAddress = "127.0.0.1";
	if(config_.chunkSize == 0)
		config_.chunkSize = 1024 * 1024;

	uint16_t port = config_.port;
	if(config_.autoSelectPort && !findFreePort(config_.bindAddress, port, &port))
		return fail("no free ports from " + std::to_string(config_.port));

	socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == ORBISLINK_INVALID_SOCKET)
		return fail("could not create the socket: " + socketErrorString(lastSocketError()));

	int reuse = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));

	struct sockaddr_in addr {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if(inet_pton(AF_INET, config_.bindAddress.c_str(), &addr.sin_addr) != 1)
	{
		closeSocketHandle(sock);
		return fail("invalid bind address: " + config_.bindAddress);
	}
	if(bind(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0)
	{
		const std::string message = socketErrorString(lastSocketError());
		closeSocketHandle(sock);
		return fail("bind failed on " + config_.bindAddress + ":" + std::to_string(port) + ": " + message);
	}
	if(listen(sock, config_.backlog) != 0)
	{
		const std::string message = socketErrorString(lastSocketError());
		closeSocketHandle(sock);
		return fail("listen failed: " + message);
	}

	// Confirms the effective port (useful if port 0 is ever used).
	struct sockaddr_in actual {};
#ifdef _WIN32
	int actualLen = sizeof(actual);
#else
	socklen_t actualLen = sizeof(actual);
#endif
	if(getsockname(sock, reinterpret_cast<struct sockaddr *>(&actual), &actualLen) == 0)
		port = ntohs(actual.sin_port);

	listenSocket_ = sock;
	boundPort_ = port;
	stopping_.store(false);
	running_.store(true);
	acceptThread_ = std::thread(&LocalHttpServer::acceptLoop, this);

	logInfo("Local HTTP server at http://" + config_.bindAddress + ":" + std::to_string(boundPort_)
		+ (config_.allowedClient.empty() ? "" : " (restricted to " + config_.allowedClient + ")"));
	if(error)
		error->clear();
	return true;
}

void LocalHttpServer::stop()
{
	if(!running_.load() && listenSocket_ == ORBISLINK_INVALID_SOCKET)
		return;
	stopping_.store(true);
	running_.store(false);

	if(listenSocket_ != ORBISLINK_INVALID_SOCKET)
	{
#ifdef _WIN32
		shutdown(listenSocket_, SD_BOTH);
#else
		shutdown(listenSocket_, SHUT_RDWR);
#endif
		closeSocketHandle(listenSocket_);
		listenSocket_ = ORBISLINK_INVALID_SOCKET;
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		for(socket_t client : clients_)
		{
#ifdef _WIN32
			shutdown(client, SD_BOTH);
#else
			shutdown(client, SHUT_RDWR);
#endif
		}
	}

	if(acceptThread_.joinable())
		acceptThread_.join();

	{
		std::unique_lock<std::mutex> lock(mutex_);
		workersDone_.wait(lock, [this]() { return activeWorkers_ == 0; });
	}
	logInfo("Local HTTP server stopped.");
}

uint16_t LocalHttpServer::port() const { return boundPort_; }

std::string LocalHttpServer::bindAddress() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return config_.bindAddress;
}

void LocalHttpServer::setAllowedClient(const std::string &ip)
{
	std::lock_guard<std::mutex> lock(mutex_);
	config_.allowedClient = ip;
}

std::string LocalHttpServer::allowedClient() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return config_.allowedClient;
}

std::string LocalHttpServer::registerFile(const std::string &path, const std::string &displayName)
{
	const int64_t size = fileSize(path);
	if(size < 0)
	{
		logError("registerFile: file not accessible: " + path);
		return std::string();
	}

	ServedFileStats stats;
	stats.token = randomToken(16);
	stats.name = sanitizeFileName(displayName.empty() ? path : displayName);
	stats.path = path;
	stats.size = size;
	stats.registeredAtMs = monotonicMillis();

	std::lock_guard<std::mutex> lock(mutex_);
	files_[stats.token] = stats;
	logDebug("File registered on the HTTP server: " + stats.name + " (" + humanBytes(size) + ")");
	return stats.token;
}

bool LocalHttpServer::unregisterFile(const std::string &token)
{
	std::lock_guard<std::mutex> lock(mutex_);
	return files_.erase(token) > 0;
}

void LocalHttpServer::clearFiles()
{
	std::lock_guard<std::mutex> lock(mutex_);
	files_.clear();
}

std::string LocalHttpServer::urlForToken(const std::string &token) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = files_.find(token);
	if(it == files_.end())
		return std::string();
	return "http://" + config_.bindAddress + ":" + std::to_string(boundPort_) + "/f/" + token + "/"
		+ urlEncodePath(it->second.name);
}

bool LocalHttpServer::statsForToken(const std::string &token, ServedFileStats *out) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = files_.find(token);
	if(it == files_.end())
		return false;
	if(out)
		*out = it->second;
	return true;
}

std::vector<ServedFileStats> LocalHttpServer::allStats() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<ServedFileStats> result;
	result.reserve(files_.size());
	for(const auto &kv : files_)
		result.push_back(kv.second);
	return result;
}

void LocalHttpServer::acceptLoop()
{
	while(!stopping_.load())
	{
		struct sockaddr_in peer {};
#ifdef _WIN32
		int peerLen = sizeof(peer);
#else
		socklen_t peerLen = sizeof(peer);
#endif
		socket_t client = accept(listenSocket_, reinterpret_cast<struct sockaddr *>(&peer), &peerLen);
		if(client == ORBISLINK_INVALID_SOCKET)
		{
			if(stopping_.load())
				break;
			continue;
		}

		char addressText[INET_ADDRSTRLEN] = { 0 };
		inet_ntop(AF_INET, &peer.sin_addr, addressText, sizeof(addressText));
		const std::string peerAddress = addressText;

		{
			std::lock_guard<std::mutex> lock(mutex_);
			clients_.insert(client);
			++activeWorkers_;
		}
		// One thread per connection (the console opens several in parallel);
		// they are detached and stop() waits for the counter to reach zero.
		std::thread([this, client, peerAddress]() {
			handleConnection(client, peerAddress);
			std::lock_guard<std::mutex> lock(mutex_);
			clients_.erase(client);
			if(--activeWorkers_ == 0)
				workersDone_.notify_all();
		}).detach();
	}
}

void LocalHttpServer::handleConnection(socket_t client, const std::string &peerAddress)
{
	struct SocketGuard
	{
		socket_t sock;
		~SocketGuard() { closeSocketHandle(sock); }
	} guard { client };

	HttpRequest request;
	if(!readRequest(client, &request))
		return;

	bool allowLoopback = true;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		allowLoopback = config_.allowLoopback;
	}
	const std::string allowed = allowedClient();
	if(!allowed.empty() && peerAddress != allowed && !(allowLoopback && peerAddress == "127.0.0.1"))
	{
		logWarning("HTTP request refused from " + peerAddress + " (only " + allowed + " is accepted).");
		sendSimpleStatus(client, 403);
		return;
	}

	const bool isHead = iequals(request.method, "HEAD");
	if(!isHead && !iequals(request.method, "GET"))
	{
		sendSimpleStatus(client, 405, "Allow: GET, HEAD\r\n");
		return;
	}

	// Only /f/<token>/<name> is served; everything else is 404 (§5.3: no exposed folders).
	std::string path = request.target;
	const size_t query = path.find('?');
	if(query != std::string::npos)
		path.resize(query);
	path = urlDecode(path);

	std::string token;
	if(startsWith(path, "/f/"))
	{
		const std::string rest = path.substr(3);
		const size_t slash = rest.find('/');
		token = slash == std::string::npos ? rest : rest.substr(0, slash);
	}

	ServedFileStats stats;
	if(token.empty() || !statsForToken(token, &stats))
	{
		logDebug("404 for " + path + " from " + peerAddress);
		sendSimpleStatus(client, 404);
		return;
	}

	std::ifstream file(stats.path, std::ios::binary);
	if(!file)
	{
		logError("Registered file disappeared from disk: " + stats.path);
		sendSimpleStatus(client, 404);
		return;
	}

	const int64_t totalSize = stats.size;
	int64_t start = 0;
	int64_t end = totalSize > 0 ? totalSize - 1 : 0;
	const RangeResult rangeResult = parseRange(request.header("Range"), totalSize, &start, &end);
	if(rangeResult == RangeResult::Malformed)
	{
		sendSimpleStatus(client, 400);
		return;
	}
	if(rangeResult == RangeResult::Unsatisfiable)
	{
		sendSimpleStatus(client, 416, "Content-Range: bytes */" + std::to_string(totalSize) + "\r\n");
		return;
	}
	const bool partial = rangeResult == RangeResult::Ok;
	const int64_t contentLength = totalSize == 0 ? 0 : (end - start + 1);

	std::ostringstream head;
	head << statusLine(partial ? 206 : 200) << "Server: OrbisLink\r\n"
		 << "Content-Type: application/octet-stream\r\n"
		 << "Accept-Ranges: bytes\r\n"
		 << "Content-Length: " << contentLength << "\r\n";
	if(partial)
		head << "Content-Range: bytes " << start << "-" << end << "/" << totalSize << "\r\n";
	head << "Connection: close\r\n\r\n";

	if(!sendText(client, head.str()))
		return;
	if(isHead)
		return;

	file.seekg(static_cast<std::streamoff>(start), std::ios::beg);
	std::vector<char> buffer(config_.chunkSize);
	int64_t remaining = contentLength;
	int64_t sentTotal = 0;
	while(remaining > 0 && !stopping_.load())
	{
		const std::streamsize want =
			static_cast<std::streamsize>(std::min<int64_t>(remaining, static_cast<int64_t>(buffer.size())));
		file.read(buffer.data(), want);
		const std::streamsize got = file.gcount();
		if(got <= 0)
			break;
		if(!sendAll(client, buffer.data(), static_cast<size_t>(got)))
			break;
		remaining -= got;
		sentTotal += got;

		std::lock_guard<std::mutex> lock(mutex_);
		auto it = files_.find(token);
		if(it != files_.end())
		{
			it->second.bytesSent += got;
			it->second.lastActivityMs = monotonicMillis();
			if(it->second.firstByteAtMs < 0)
				it->second.firstByteAtMs = it->second.lastActivityMs;
		}
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = files_.find(token);
		if(it != files_.end())
			it->second.requestCount += 1;
	}

	logDebug("Served " + humanBytes(sentTotal) + " of " + stats.name + " to " + peerAddress
		+ (partial ? " (Range " + std::to_string(start) + "-" + std::to_string(end) + ")" : ""));
}

} // namespace orbislink
