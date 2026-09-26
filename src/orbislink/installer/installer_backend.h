// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orbislink {

// Resultado de qualquer chamada ao instalador da consola.
struct InstallerResult
{
	bool ok = false;
	uint32_t errorCode = 0;   // código devolvido pela consola (0 se não houver)
	std::string message;      // mensagem já traduzida para o utilizador
	long httpStatus = 0;
	std::string rawBody;      // resposta crua, para o log/diagnóstico

	static InstallerResult success() { InstallerResult r; r.ok = true; return r; }
	static InstallerResult failure(const std::string &message, uint32_t code = 0)
	{
		InstallerResult r;
		r.message = message;
		r.errorCode = code;
		return r;
	}
};

struct InstallTaskHandle
{
	int taskId = -1;
	std::string title;
};

// Campos devolvidos por /api/get_task_progress (ver server.c do Remote
// Package Installer). Os valores vêm em hexadecimal sem aspas no JSON.
struct TaskProgress
{
	uint32_t bits = 0;
	int32_t errorResult = 0;
	int64_t length = 0;
	int64_t transferred = 0;
	int64_t lengthTotal = 0;
	int64_t transferredTotal = 0;
	uint32_t numIndex = 0;
	uint32_t numTotal = 0;
	uint32_t restSec = 0;
	uint32_t restSecTotal = 0;
	int32_t preparingPercent = 0;
	int32_t localCopyPercent = 0;

	// 0..100 com base no total transferido.
	double percent() const
	{
		if(lengthTotal <= 0)
			return 0.0;
		const double value = 100.0 * static_cast<double>(transferredTotal) / static_cast<double>(lengthTotal);
		return value < 0.0 ? 0.0 : (value > 100.0 ? 100.0 : value);
	}

	bool finished() const { return lengthTotal > 0 && transferredTotal >= lengthTotal; }
};

// Sub-tipos de tarefa aceites por /api/find_task (README do instalador).
enum class TaskSubType { Game = 6, AdditionalContent = 7, Patch = 8, License = 9 };

// Interface abstrata (§5.4): permite trocar de instalador sem mexer na UI.
class IInstallerBackend
{
public:
	virtual ~IInstallerBackend() = default;

	virtual std::string name() const = 0;
	virtual std::string endpoint() const = 0;

	// Disponibilidade: qualquer resposta HTTP conta como disponível (§5.1).
	virtual bool probe(std::string *detail = nullptr) = 0;

	virtual InstallerResult installDirect(const std::vector<std::string> &packageUrls,
		InstallTaskHandle *handle) = 0;
	virtual InstallerResult installFromReferenceJson(const std::string &url,
		InstallTaskHandle *handle) = 0;

	virtual InstallerResult isExists(const std::string &titleId, bool *exists, int64_t *size) = 0;
	virtual InstallerResult taskProgress(int taskId, TaskProgress *progress) = 0;
	virtual InstallerResult findTask(const std::string &contentId, TaskSubType subType, int *taskId) = 0;

	virtual InstallerResult startTask(int taskId) = 0;
	virtual InstallerResult stopTask(int taskId) = 0;
	virtual InstallerResult pauseTask(int taskId) = 0;
	virtual InstallerResult resumeTask(int taskId) = 0;
	virtual InstallerResult unregisterTask(int taskId) = 0;

	virtual InstallerResult uninstallGame(const std::string &titleId) = 0;
	virtual InstallerResult uninstallPatch(const std::string &titleId) = 0;
	virtual InstallerResult uninstallAdditionalContent(const std::string &contentId) = 0;
	virtual InstallerResult uninstallTheme(const std::string &contentId) = 0;
};

} // namespace orbislink
