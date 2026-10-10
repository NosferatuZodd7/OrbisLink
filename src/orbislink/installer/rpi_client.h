// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/installer/installer_backend.h"

#include <atomic>
#include <string>

namespace orbislink {

// Client for Remote Package Installer's (flatz) HTTP API, port 12800.
//
// On a PS5 the same port may instead be etaHEN's DPI v2, which only starts
// installs: a form POST with "url" (an http:// address, or a path on the
// console such as /data/pkg/game.pkg) that answers "SUCCESS: …" or
// "FAILED: … (0x8099…)" in plain text (etaHEN's DirectPKGInstaller.cpp).
// With `ps5` set, the client finds out which one answers and speaks it.
//
// Every endpoint, field and format was confirmed in the installer's source
// code (server.c) and its README — see docs/validation.md. Note: the replies
// use hexadecimal numbers without quotes and the "exists" field is a string
// ("true"/"false"), which is why OrbisLink's JSON parser is tolerant.
class RpiClient : public IInstallerBackend
{
public:
	struct Config
	{
		std::string host;
		uint16_t port = 12800;
		int timeoutMs = 10000; // §5.4
		int maxAttempts = 3;   // 3 attempts with 1 s, 2 s, 4 s backoff
		int backoffBaseMs = 1000;
		// A PS5: the installer may be etaHEN's DPI v2.
		bool ps5 = false;
	};

	explicit RpiClient(Config config);

	std::string name() const override { return "Remote Package Installer"; }
	std::string endpoint() const override;

	bool probe(std::string *detail = nullptr) override;

	InstallerResult installDirect(const std::vector<std::string> &packageUrls,
		InstallTaskHandle *handle) override;
	InstallerResult installFromReferenceJson(const std::string &url, InstallTaskHandle *handle) override;

	InstallerResult isExists(const std::string &titleId, bool *exists, int64_t *size) override;
	InstallerResult taskProgress(int taskId, TaskProgress *progress) override;
	InstallerResult findTask(const std::string &contentId, TaskSubType subType, int *taskId) override;

	InstallerResult startTask(int taskId) override;
	InstallerResult stopTask(int taskId) override;
	InstallerResult pauseTask(int taskId) override;
	InstallerResult resumeTask(int taskId) override;
	InstallerResult unregisterTask(int taskId) override;

	InstallerResult uninstallGame(const std::string &titleId) override;
	InstallerResult uninstallPatch(const std::string &titleId) override;
	InstallerResult uninstallAdditionalContent(const std::string &contentId) override;
	InstallerResult uninstallTheme(const std::string &contentId) override;

	const Config &config() const { return config_; }

	bool followsTasks() const override { return protocol_.load() != static_cast<int>(Protocol::DpiV2); }
	bool installsFromConsole() const override { return config_.ps5; }

private:
	enum class Protocol
	{
		Unknown,
		Rpi,
		DpiV2,
	};
	// Which installer answers (asked once, then remembered).
	Protocol protocol();
	InstallerResult installDpiV2(const std::string &url);

	// Sends the POST with retries and returns the already validated body.
	InstallerResult call(const std::string &path, const std::string &jsonBody, std::string *body);
	InstallerResult taskCommand(const std::string &path, int taskId);

	Config config_;
	std::atomic<int> protocol_ { 0 };
};

} // namespace orbislink
