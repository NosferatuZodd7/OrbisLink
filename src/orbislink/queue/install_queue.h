// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/console/console_manager.h"
#include "orbislink/ftp/ftp_client.h"
#include "orbislink/http/local_http_server.h"
#include "orbislink/installer/installer_backend.h"
#include "orbislink/pkg/pkg_inspector.h"
#include "orbislink/settings/settings_store.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace orbislink {

// Pending → Validating → Uploading/Installing → Done | Error | Cancelled (§5.6).
enum class TaskState { Pending, Validating, Sending, Installing, Completed, Error, Cancelled };

const char *taskStateName(TaskState state);      // stable identifier (persistence)
const char *taskStateLabel(TaskState state);   // text for the UI (translated there)

// What to do when the title already exists on the console.
enum class ExistingPolicy { Reinstall, Skip };

struct QueueTask
{
	std::string id;
	std::string localPath;
	TransferMode mode = TransferMode::DirectInstall;
	TaskState state = TaskState::Pending;

	// Metadados do pkg (preenchidos pelo PkgInspector).
	std::string title;
	std::string titleId;
	std::string contentId;
	std::string appVersion;
	PkgCategory category = PkgCategory::Unknown;

	int64_t totalBytes = 0;
	int64_t doneBytes = 0;
	double bytesPerSecond = 0.0;
	int64_t etaSeconds = -1;

	int consoleTaskId = -1;
	std::string httpToken;
	std::string remotePath; // destino no FTP (modo B)
	// File to delete from the console when this task succeeds. Only set
	// by "upload and install" with "delete afterwards" enabled.
	std::string cleanupRemotePath;
	std::string message;    // error or information for the UI
	uint32_t errorCode = 0;
	int attempts = 0;

	int64_t createdAtUnix = 0;
	int64_t startedAtUnix = 0;
	int64_t finishedAtUnix = 0;

	double percent() const
	{
		if(totalBytes <= 0)
			return 0.0;
		const double value = 100.0 * static_cast<double>(doneBytes) / static_cast<double>(totalBytes);
		return value < 0.0 ? 0.0 : (value > 100.0 ? 100.0 : value);
	}
	bool isTerminal() const
	{
		return state == TaskState::Completed || state == TaskState::Error
			|| state == TaskState::Cancelled;
	}
};

// Sequential queue of installs/uploads (§5.6).
class InstallQueue
{
public:
	struct Dependencies
	{
		LocalHttpServer *httpServer = nullptr;
		IInstallerBackend *installer = nullptr;
		FtpClient *ftp = nullptr;
		ConsoleManager *console = nullptr; // optional: pauses the queue if the services go down
	};

	struct Tuning
	{
		int progressPollMs = 1000;  // §5.4: polling de 1 s
		int stallTimeoutMs = 20000; // §7: task created but 0 bytes after 20 s
		size_t historyLimit = 100;  // §5.7: history of the last 100 tasks
	};

	InstallQueue(Dependencies dependencies, Settings settings);
	InstallQueue(Dependencies dependencies, Settings settings, Tuning tuning);
	~InstallQueue();

	void setSettings(const Settings &settings);
	Settings settings() const;

	// Valida e acrescenta. Ordena o lote por jogo → patch → DLC dentro do
	// mesmo TITLE_ID. `rejected` recebe "ficheiro: motivo" por cada recusa.
	std::vector<std::string> enqueue(const std::vector<std::string> &paths, TransferMode mode,
		std::vector<std::string> *rejected = nullptr);
	std::string enqueueOne(const std::string &path, TransferMode mode, std::string *error = nullptr);

	void start();
	void stop();
	void pause(const std::string &reason = std::string());
	void resume();
	bool paused() const { return paused_.load(); }
	std::string pauseReason() const;

	bool cancel(const std::string &id);
	bool retry(const std::string &id);
	bool remove(const std::string &id);
	bool moveUp(const std::string &id);
	bool moveDown(const std::string &id);

	std::vector<QueueTask> tasks() const;
	std::vector<QueueTask> history() const;
	bool task(const std::string &id, QueueTask *out) const;

	// Notified on every state/progress change (the UI hooks in here).
	void setListener(std::function<void(const QueueTask &)> listener);
	// Decision when the title already exists on the console; reinstall by default.
	void setExistingPolicyResolver(std::function<ExistingPolicy(const QueueTask &)> resolver);

	bool save(const std::string &path) const;
	bool load(const std::string &path);

	// Serialisation exposed for tests.
	std::string toJson() const;
	bool fromJson(const std::string &text);

private:
	void workerLoop();
	bool takeNextTask(QueueTask *task);
	void runDirectInstall(QueueTask task);
	// The install task created by "install after upload" needs to know
	// which file to delete on the console when it finishes.
	void setCleanupPath(const std::string &id, const std::string &remotePath);
	void runFtpUpload(QueueTask task);
	void finishTask(QueueTask task);
	void updateTask(const QueueTask &task);
	void notify(const QueueTask &task);
	void requeueForServiceLoss(QueueTask task, const std::string &reason);
	static void sortBatch(std::vector<QueueTask> &batch);

	Dependencies deps_;
	Tuning tuning_;

	mutable std::mutex mutex_;
	Settings settings_;
	std::deque<QueueTask> tasks_;
	std::deque<QueueTask> history_;
	std::string currentId_;
	std::string pauseReason_;
	std::function<void(const QueueTask &)> listener_;
	std::function<ExistingPolicy(const QueueTask &)> existingPolicy_;

	std::atomic<bool> running_ { false };
	std::atomic<bool> paused_ { false };
	std::atomic<bool> cancelCurrent_ { false };
	std::condition_variable wakeup_;
	std::mutex wakeupMutex_;
	std::thread worker_;
};

} // namespace orbislink
