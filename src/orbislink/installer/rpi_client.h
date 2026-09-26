// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/installer/installer_backend.h"

#include <string>

namespace orbislink {

// Cliente da API HTTP do Remote Package Installer (flatz), porta 12800.
//
// Todos os endpoints, campos e formatos foram confirmados no código-fonte do
// instalador (server.c) e no seu README — ver docs/validacao.md. Atenção:
// as respostas usam números hexadecimais sem aspas e o campo "exists" é uma
// string ("true"/"false"), por isso o parser de JSON do OrbisLink é tolerante.
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
	// Faz o POST com retentativas e devolve o corpo já validado.
	InstallerResult call(const std::string &path, const std::string &jsonBody, std::string *body);
	InstallerResult taskCommand(const std::string &path, int taskId);

	Config config_;
};

} // namespace orbislink
