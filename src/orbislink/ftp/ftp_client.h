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
	std::string path;          // absolute path on the console
	int64_t size = 0;
	bool isDirectory = false;
	bool isSymlink = false;
	std::string permissions;   // "drwxr-xr-x", when the server gives it
	std::string modified;      // raw date text, as it comes from LIST
	std::string rawLine;
};

struct FtpResult
{
	bool ok = false;
	std::string message;
	bool cancelled = false;
	// The server's own answer, which asking again will not change: access
	// refused, or no such file. Not retried.
	bool final = false;

	static FtpResult success() { FtpResult r; r.ok = true; return r; }
	static FtpResult failure(std::string message)
	{
		FtpResult r;
		r.message = std::move(message);
		return r;
	}
};

// Return false to cancel the operation.
using FtpProgressCallback = std::function<bool(int64_t done, int64_t total)>;

// FTP client on top of libcurl for GoldHEN's server (port 2121, anonymous
// login, passive mode) — §5.5 — and etaHEN's on the PS5 (port 1337).
//
// One connection at a time by default: HEN FTP servers are fragile with
// parallel connections. The limit can be raised to 2.
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
		int maxRetries = 3;          // automatic reconnection
		int maxConnections = 1;      // up to 2
		bool advancedMode = false;   // desbloqueia as zonas protegidas
	};

	explicit FtpClient(Config config);
	~FtpClient();

	void setConfig(const Config &config);
	Config config() const;

	// Checks whether the server answers (used by ConsoleManager).
	bool probe(std::string *detail = nullptr);

	FtpResult list(const std::string &remoteDir, std::vector<FtpEntry> *entries);
	FtpResult upload(const std::string &localPath, const std::string &remotePath,
		FtpProgressCallback progress = nullptr, bool resume = false);
	FtpResult download(const std::string &remotePath, const std::string &localPath,
		FtpProgressCallback progress = nullptr, bool resume = false);
	FtpResult remoteSize(const std::string &remotePath, int64_t *size);
	// `length` bytes of a file from `offset` (fewer at its end), into memory:
	// enough to read a package's or a disc's header without fetching it.
	FtpResult read(const std::string &remotePath, int64_t offset, size_t length, std::vector<uint8_t> *bytes);
	FtpResult makeDirectory(const std::string &remotePath);
	FtpResult removeFile(const std::string &remotePath);
	FtpResult removeDirectory(const std::string &remotePath);
	FtpResult rename(const std::string &fromPath, const std::string &toPath);
	// SITE CHMOD, once: a server without it says so at the first try. `mode`
	// is octal, "777".
	FtpResult setPermissions(const std::string &remotePath, const std::string &mode);

	// Cancels the current operation; goes back to false when the operation ends.
	void cancel();
	bool cancelRequested() const { return cancel_.load(); }

	// Protected areas (§5.5): read-only unless "Advanced mode" is on.
	static bool isProtectedPath(const std::string &remotePath);
	bool isWriteAllowed(const std::string &remotePath) const;

	// Shortcuts suggested in FtpBrowser.
	static std::vector<std::string> shortcutPaths();

	std::string urlFor(const std::string &remotePath) const;

	// Exposed for tests: lenient parser of the LIST reply.
	static std::vector<FtpEntry> parseListing(const std::string &listing, const std::string &baseDir);

private:
	struct Slot;
	FtpResult withRetries(const std::string &what, const std::function<FtpResult()> &operation);
	bool querySize(const std::string &path, int64_t *size);
	FtpResult uploadOnce(const std::string &localPath, const std::string &path,
		const FtpProgressCallback &progress, int64_t localSize, int64_t from, int64_t *reached);

	mutable std::mutex mutex_;
	Config config_;
	std::atomic<bool> cancel_ { false };
	// Simple semaphore to limit simultaneous connections.
	mutable std::mutex slotMutex_;
	int slotsInUse_ = 0;
};

} // namespace orbislink
