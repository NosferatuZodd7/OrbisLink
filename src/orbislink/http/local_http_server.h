// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/net/socket_compat.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace orbislink {

// State of a file exposed to the console.
struct ServedFileStats
{
	std::string token;
	std::string name;    // nome sanitizado que aparece no URL
	std::string path;    // caminho local real (nunca exposto)
	int64_t size = 0;
	int64_t bytesSent = 0;
	int64_t requestCount = 0;
	int64_t registeredAtMs = 0;
	int64_t firstByteAtMs = -1; // -1 = the console has not downloaded anything yet
	int64_t lastActivityMs = -1;
};

// Local HTTP server that serves the pkg files to the console (§5.3).
//
// Rules implemented: mandatory Range/206 support, GET and HEAD, files > 4 GB
// (int64 everywhere), reading in blocks, several simultaneous connections,
// binding to a specific interface, URLs with a random token and an optional
// restriction to the console's IP.
class LocalHttpServer
{
public:
	struct Config
	{
		std::string bindAddress;       // empty = 127.0.0.1 (never 0.0.0.0 by default)
		uint16_t port = 8765;
		bool autoSelectPort = true;    // se a porta estiver ocupada, procura outra
		std::string allowedClient;     // IP da consola; vazio = qualquer origem
		bool allowLoopback = true;     // aceita 127.0.0.1 mesmo com allowedClient definido
		size_t chunkSize = 1024 * 1024; // 1 MB
		int backlog = 16;
	};

	LocalHttpServer();
	~LocalHttpServer();
	LocalHttpServer(const LocalHttpServer &) = delete;
	LocalHttpServer &operator=(const LocalHttpServer &) = delete;

	bool start(const Config &config, std::string *error = nullptr);
	void stop();
	bool running() const { return running_.load(); }

	uint16_t port() const;
	std::string bindAddress() const;

	// Registers a file and returns the token; an empty string if the file does
	// not exist. The token stops being valid at unregisterFile()/clearFiles().
	std::string registerFile(const std::string &path, const std::string &displayName = std::string());
	bool unregisterFile(const std::string &token);
	void clearFiles();

	// http://<ip>:<porta>/f/<token>/<nome>.pkg
	std::string urlForToken(const std::string &token) const;
	bool statsForToken(const std::string &token, ServedFileStats *out) const;
	std::vector<ServedFileStats> allStats() const;

	void setAllowedClient(const std::string &ip);
	std::string allowedClient() const;

private:
	void acceptLoop();
	void handleConnection(socket_t client, const std::string &peerAddress);

	Config config_;
	std::atomic<bool> running_ { false };
	std::atomic<bool> stopping_ { false };
	socket_t listenSocket_ = ORBISLINK_INVALID_SOCKET;
	uint16_t boundPort_ = 0;
	std::thread acceptThread_;

	mutable std::mutex mutex_;
	mutable std::condition_variable workersDone_;
	std::map<std::string, ServedFileStats> files_;
	std::set<socket_t> clients_;
	int activeWorkers_ = 0;
};

} // namespace orbislink
