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
	// When the server last answered this client (monotonicMillis(); -1:
	// never). A transfer under way counts: a server busy with it may not
	// greet another connection.
	int64_t lastAnsweredMs() const { return lastAnsweredMs_.load(); }

	FtpResult list(const std::string &remoteDir, std::vector<FtpEntry> *entries);
	// Whether the server can go into `remotePath` as a folder. A listing does
	// not say where a symbolic link points; this asks.
	bool isDirectory(const std::string &remotePath);
	// Whether the server names `feature` ("MLSD", "MDTM", "UTF8") in its
	// FEAT reply. Asked once per server; a server without FEAT has none.
	bool hasFeature(const std::string &feature);
	// One command as it is — a server's own, like ftpsrv's MTRW — and what
	// the server answered (the reply lines).
	FtpResult command(const std::string &line, std::string *reply = nullptr);
	FtpResult upload(const std::string &localPath, const std::string &remotePath,
		FtpProgressCallback progress = nullptr, bool resume = false);
	// A dropped transfer (the console's "426", a lost connection) goes on
	// from the byte it reached, again and again while each try makes
	// headway. `progress` counts the bytes this call brings (not those
	// already here when resuming).
	FtpResult download(const std::string &remotePath, const std::string &localPath,
		FtpProgressCallback progress = nullptr, bool resume = false);
	FtpResult remoteSize(const std::string &remotePath, int64_t *size);
	// The room left on the disk `remoteDir` is on (AVBL), from a server that
	// says; false when it does not (the consoles' servers mostly do not):
	// it is not asked again then.
	bool freeSpace(const std::string &remoteDir, int64_t *bytes);
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

	// Stops every transfer under way at this moment (closing the app, the
	// queue stopping). One transfer alone is stopped by its own progress
	// callback returning false: the queue and the file browser share this
	// client, and stopping one must never stop the other. Operations that
	// start afterwards are not touched — there is no flag to put back.
	void cancel();

	// Protected areas (§5.5): read-only unless "Advanced mode" is on.
	static bool isProtectedPath(const std::string &remotePath);
	bool isWriteAllowed(const std::string &remotePath) const;

	// Shortcuts suggested in FtpBrowser.
	static std::vector<std::string> shortcutPaths();

	std::string urlFor(const std::string &remotePath) const;

	// Exposed for tests: lenient parser of the LIST reply.
	static std::vector<FtpEntry> parseListing(const std::string &listing, const std::string &baseDir);
	// Exposed for tests: the MLSD reply ("type=dir;size=0;modify=…; name").
	// Its exact time is kept, and written as LIST writes it too, so code that
	// reads either kind of listing reads the same thing.
	static std::vector<FtpEntry> parseMachineListing(const std::string &listing, const std::string &baseDir,
		int64_t nowUnix);

private:
	struct Slot;
	FtpResult withRetries(const std::string &what, const std::function<FtpResult()> &operation);
	bool querySize(const std::string &path, int64_t *size);
	FtpResult uploadOnce(const std::string &localPath, const std::string &path,
		const FtpProgressCallback &progress, int64_t localSize, int64_t from, int64_t *reached, uint64_t generation);
	FtpResult downloadOnce(const std::string &path, const std::string &localPath,
		const FtpProgressCallback &progress, int64_t from, int64_t carried, int64_t *reached, bool *restRefused,
		uint64_t generation);
	bool cancelledSince(uint64_t generation) const { return cancelGeneration_.load() != generation; }
	// Quote commands (MKD, DELE, RNFR/RNTO, SITE…), on a connection of the pool.
	FtpResult runCommands(const std::vector<std::string> &commands, std::string *replies = nullptr);
	// The connections kept open between operations (see takeHandle).
	void *takeHandle();
	void giveBack(void *handle, bool reusable);
	void dropIdleHandles();

	mutable std::mutex mutex_;
	Config config_;
	// Goes up with each cancel(): a transfer stops when it changed since it began.
	std::atomic<uint64_t> cancelGeneration_ { 0 };
	// AVBL was answered with an error once: the server has no such command.
	std::atomic<bool> noFreeSpace_ { false };
	// Easy handles at rest, each keeping its connection (and login) to the
	// console, with when each was put down. Reused for the next operation
	// instead of a new connection each time; let go after a short while.
	std::mutex poolMutex_;
	std::vector<std::pair<void *, int64_t>> idle_;
	// FEAT, asked once per server.
	std::mutex featureMutex_;
	bool featuresKnown_ = false;
	std::vector<std::string> features_;
	// MLSD offered but answered with nothing usable: LIST from then on.
	std::atomic<bool> mlsdBroken_ { false };
	// When the server last answered an operation of this client (or moved
	// bytes of a transfer), on monotonicMillis(); -1 before that.
	std::atomic<int64_t> lastAnsweredMs_ { -1 };
	// Simple semaphore to limit simultaneous connections.
	mutable std::mutex slotMutex_;
	int slotsInUse_ = 0;
};

} // namespace orbislink
