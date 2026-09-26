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

// Estado de um ficheiro exposto à consola.
struct ServedFileStats
{
	std::string token;
	std::string name;    // nome sanitizado que aparece no URL
	std::string path;    // caminho local real (nunca exposto)
	int64_t size = 0;
	int64_t bytesSent = 0;
	int64_t requestCount = 0;
	int64_t registeredAtMs = 0;
	int64_t firstByteAtMs = -1; // -1 = a consola ainda não descarregou nada
	int64_t lastActivityMs = -1;
};

// Servidor HTTP local que serve os pkg à consola (§5.3).
//
// Regras implementadas: suporte obrigatório a Range/206, GET e HEAD,
// ficheiros > 4 GB (int64 em todo o lado), leitura por blocos, várias ligações
// em simultâneo, bind a uma interface concreta, URLs com token aleatório e
// restrição opcional ao IP da consola.
class LocalHttpServer
{
public:
	struct Config
	{
		std::string bindAddress;       // vazio = 127.0.0.1 (nunca 0.0.0.0 por omissão)
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

	// Regista um ficheiro e devolve o token; string vazia se o ficheiro não
	// existir. O token deixa de ser válido em unregisterFile()/clearFiles().
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
