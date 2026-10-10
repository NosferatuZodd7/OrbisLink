// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orbislink {

// Result of any call to the console's installer.
struct InstallerResult
{
	bool ok = false;
	uint32_t errorCode = 0;   // code returned by the console (0 if none)
	std::string message;      // message already worded for the user
	long httpStatus = 0;
	std::string rawBody;      // raw reply, for the log/diagnostics

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

// Fields returned by /api/get_task_progress (see Remote Package Installer's
// server.c). The values come in hexadecimal without quotes in the JSON.
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

	// 0..100 based on the total transferred.
	double percent() const
	{
		if(lengthTotal <= 0)
			return 0.0;
		const double value = 100.0 * static_cast<double>(transferredTotal) / static_cast<double>(lengthTotal);
		return value < 0.0 ? 0.0 : (value > 100.0 ? 100.0 : value);
	}

	bool finished() const { return lengthTotal > 0 && transferredTotal >= lengthTotal; }
};

// Task sub-types /api/find_task takes (the installer's README).
enum class TaskSubType { Game = 6, AdditionalContent = 7, Patch = 8, License = 9 };

// Abstract interface (§5.4): lets the installer be swapped without touching the UI.
class IInstallerBackend
{
public:
	virtual ~IInstallerBackend() = default;

	virtual std::string name() const = 0;
	virtual std::string endpoint() const = 0;

	// Availability: any HTTP reply counts as available (§5.1).
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

	// Whether an install it starts can be followed (taskProgress and the
	// task commands). An installer that only starts installs (etaHEN's
	// DPI v2) is followed by what the PC serves.
	virtual bool followsTasks() const { return true; }
	// Whether it can install a package already on the console, by its path
	// there (the PS5's installers can; the PS4's takes only http://).
	virtual bool installsFromConsole() const { return false; }
};

} // namespace orbislink
