// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/installer/installer_backend.h"

#include <string>

namespace orbislink {

// Client for Remote Package Installer's (flatz) HTTP API, port 12800. etaHEN's
// DPI v2 on the PS5 speaks the same install API.
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
		int maxAttempts = 3;   // 3 tentativas com backoff 1 s, 2 s, 4 s
		int backoffBaseMs = 1000;
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

private:
	// Sends the POST with retries and returns the already validated body.
	InstallerResult call(const std::string &path, const std::string &jsonBody, std::string *body);
	InstallerResult taskCommand(const std::string &path, int taskId);

	Config config_;
};

} // namespace orbislink
