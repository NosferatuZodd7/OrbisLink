// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace orbislink {

struct FtpEntry
{
	std::string name;
	std::string path;          // caminho absoluto na consola
	int64_t size = 0;
	bool isDirectory = false;
	bool isSymlink = false;
	std::string permissions;   // "drwxr-xr-x", quando o servidor o indica
	std::string modified;      // texto cru da data, tal como vem do LIST
	std::string rawLine;
};

struct FtpResult
{
	bool ok = false;
	std::string message;
	bool cancelled = false;

	static FtpResult success() { FtpResult r; r.ok = true; return r; }
	static FtpResult failure(std::string message)
	{
		FtpResult r;
		r.message = std::move(message);
		return r;
	}
};

// Devolve false para cancelar a operação.
using FtpProgressCallback = std::function<bool(int64_t done, int64_t total)>;

// Cliente FTP sobre libcurl para o servidor do GoldHEN (porta 2121, login
// anónimo, modo passivo) — §5.5.
//
// Uma ligação de cada vez por omissão: os servidores FTP de HEN são frágeis
// com ligações paralelas. O limite é configurável até 2.
class FtpClient
{
public:
	struct Config
	{
		std::string host;
		uint16_t port = 2121;
		std::string user = "anonymous";
		std::string password = "anonymous@orbislink";
		bool passive = true;
		int idleTimeoutSeconds = 30; // §5.5
		int connectTimeoutSeconds = 10;
		int maxRetries = 3;          // reconexão automática
		int maxConnections = 1;      // até 2
		bool advancedMode = false;   // desbloqueia as zonas protegidas
	};

	explicit FtpClient(Config config);
	~FtpClient();

	void setConfig(const Config &config);
	Config config() const;

	// Verifica se o servidor responde (usado pelo ConsoleManager).
	bool probe(std::string *detail = nullptr);

	FtpResult list(const std::string &remoteDir, std::vector<FtpEntry> *entries);
	FtpResult upload(const std::string &localPath, const std::string &remotePath,
		FtpProgressCallback progress = nullptr, bool resume = false);
	FtpResult download(const std::string &remotePath, const std::string &localPath,
		FtpProgressCallback progress = nullptr, bool resume = false);
	FtpResult remoteSize(const std::string &remotePath, int64_t *size);
	FtpResult makeDirectory(const std::string &remotePath);
	FtpResult removeFile(const std::string &remotePath);
	FtpResult removeDirectory(const std::string &remotePath);
	FtpResult rename(const std::string &fromPath, const std::string &toPath);

	// Cancela a operação em curso; volta a false quando a operação termina.
	void cancel();
	bool cancelRequested() const { return cancel_.load(); }

	// Zonas protegidas (§5.5): só de leitura salvo "Modo avançado".
	static bool isProtectedPath(const std::string &remotePath);
	bool isWriteAllowed(const std::string &remotePath) const;

	// Atalhos sugeridos no FtpBrowser.
	static std::vector<std::string> shortcutPaths();

	std::string urlFor(const std::string &remotePath) const;

	// Exposto para testes: parser tolerante da resposta ao LIST.
	static std::vector<FtpEntry> parseListing(const std::string &listing, const std::string &baseDir);

private:
	struct Slot;
	FtpResult withRetries(const std::string &what, const std::function<FtpResult()> &operation);

	mutable std::mutex mutex_;
	Config config_;
	std::atomic<bool> cancel_ { false };
	// Semáforo simples para limitar ligações simultâneas.
	mutable std::mutex slotMutex_;
	int slotsInUse_ = 0;
};

} // namespace orbislink
