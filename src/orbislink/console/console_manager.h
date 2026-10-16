// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/settings/settings_store.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace orbislink {

// 🟢 available, 🟡 checking, 🔴 unavailable (§5.1).
enum class ServiceState { Unknown, Checking, Available, Unavailable };

const char *serviceStateSymbol(ServiceState state);

struct ServiceStatus
{
	ServiceState state = ServiceState::Unknown;
	std::string detail; // the service's reply (e.g. "220 GoldHEN FTP")
	std::string hint;   // help text for the UI when it is 🔴
	int64_t checkedAtMs = -1;
};

// What one port of the console said when it was asked.
struct PortAnswer
{
	uint16_t port = 0;
	bool open = false;    // it took the connection
	std::string greeting; // the first line it sent on its own, if any
	// It greeted the way an FTP server does ("220 …").
	bool ftp() const { return greeting.compare(0, 3, "220") == 0; }
};

// The ports an FTP server on a jailbroken console listens on, `preferred`
// first: GoldHEN's 2121, etaHEN's 1337 and the standard 21. A PS5 also
// tries 9090, where people look for it; on etaHEN that port is the DPI v1
// installer, which does not greet as FTP and so is never taken for it. On a
// PS4, 9090 is GoldHEN's payload loader and is never knocked on.
std::vector<uint16_t> ftpPortCandidates(uint16_t preferred, bool ps5);

// Asks every port at the same time, waiting up to `timeoutMs` for each to
// connect and up to as long again for its greeting; the answers come back in
// the order the ports were given.
std::vector<PortAnswer> askPorts(const std::string &address, const std::vector<uint16_t> &ports,
	int timeoutMs);

// Where FTP answers, from what the ports said: `preferred` when it greets as
// FTP; otherwise the first other port that does; otherwise `preferred` when
// it took the connection without a word (a single-client server busy with a
// transfer greets late); otherwise 0.
uint16_t pickFtpPort(const std::vector<PortAnswer> &answers, uint16_t preferred);

// What usually listens on a port of a jailbroken console, for the
// diagnostics ("" when nothing is known).
std::string knownPortUse(uint16_t port, bool ps5);

// The version a homebrew enabler gives right after its name in a greeting
// ("220 GoldHEN v2.4b18 …" gives "2.4b18" for "GoldHEN"); "" when the
// greeting does not name it, or names it without a version.
std::string henVersionInGreeting(const std::string &greeting, const std::string &hen);

struct ConsoleStatus
{
	ServiceStatus remotePlay;
	ServiceStatus ftp;
	ServiceStatus installer;
	// The port FTP answered on (0 while it does not answer). When the set
	// port is silent and FTP turns up on another, this is that other port.
	uint16_t ftpPort = 0;
	// What each port said the last time FTP was looked for beyond the set
	// port (empty until that happened).
	std::vector<PortAnswer> ftpSearch;
	int64_t ftpSearchAtMs = -1;

	bool canInstallDirectly() const { return installer.state == ServiceState::Available; }
	bool canUseFtp() const { return ftp.state == ServiceState::Available; }
};

// Result of a one-off check, without touching the manager's state: used to
// confirm an address the user is still typing in the settings, before
// saving it.
struct ProbeResult
{
	bool ftpOk = false;
	bool installerOk = false;
	std::string ftpDetail;       // FTP's greeting (e.g. "220 GoldHEN FTP")
	std::string installerDetail; // reply (or error) from the remote installer
	// The port FTP answered on (0 when it did not), and what each port asked
	// for it said.
	uint16_t ftpPort = 0;
	std::vector<PortAnswer> ftpPorts;
	bool anyOk() const { return ftpOk || installerOk; }
	bool allOk() const { return ftpOk && installerOk; }
};

// Connects to the console and returns what answered. `timeoutMs` is the FTP
// limit; the installer gets one more second because it is a full HTTP request.
// FTP is looked for on `ftpPort` and, at the same time, on `otherFtpPorts`.
// On a PS5 (`ps5`), with no installer on `installerPort`, Prospero Manager's
// package API counts as the installer.
ProbeResult probeConsoleServices(const std::string &address, uint16_t ftpPort,
	uint16_t installerPort, int timeoutMs = 2000, const std::vector<uint16_t> &otherFtpPorts = {},
	bool ps5 = false);

// Remote Play state as chiaki-ng reports it.
enum class RemotePlayState { Unknown, Ready, Standby, Offline };

// Console profile and periodic service checks.
//
// Remote Play itself is chiaki-ng's: its state comes in here through
// setRemotePlayState(), called by the session wrapper. The confirmed Remote
// Play ports (987/UDP PS4 discovery, 9302/UDP PS5 discovery, 9295/TCP control
// and registration, 9296/UDP stream, 9297/UDP senkusha) are in
// docs/validation.md.
class ConsoleManager
{
public:
	explicit ConsoleManager(Settings settings);
	~ConsoleManager();

	void setSettings(const Settings &settings);
	Settings settings() const;
	void setAddress(const std::string &address);
	std::string address() const;

	// Periodic check (every 10 s while the app is open).
	void start(int intervalSeconds = 10);
	void stop();
	bool running() const { return running_.load(); }

	// Synchronous check, used before each queue task.
	ConsoleStatus checkNow();
	ConsoleStatus status() const;

	void setRemotePlayState(RemotePlayState state, const std::string &detail = std::string());
	void setListener(std::function<void(const ConsoleStatus &)> listener);

private:
	void loop(int intervalSeconds);
	void publish(const ConsoleStatus &status);

	mutable std::mutex mutex_;
	Settings settings_;
	ConsoleStatus status_;
	// When FTP was last looked for on the other ports: at most once a
	// minute while the set port does not greet.
	int64_t lastFtpSearchMs_ = -1;
	std::function<void(const ConsoleStatus &)> listener_;

	std::atomic<bool> running_ { false };
	std::condition_variable wakeup_;
	std::mutex wakeupMutex_;
	std::thread thread_;
};

} // namespace orbislink
