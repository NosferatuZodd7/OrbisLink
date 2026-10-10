// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/queue/install_queue.h"

#include "orbislink/common/json.h"
#include "orbislink/common/log.h"
#include "orbislink/common/tr.h"
#include "orbislink/common/util.h"
#include "orbislink/installer/error_codes.h"

#include <algorithm>
#include <chrono>
#include <fstream>

namespace orbislink {

const char *taskStateName(TaskState state)
{
	switch(state)
	{
		case TaskState::Pending: return "pending";
		case TaskState::Validating: return "validating";
		case TaskState::Sending: return "sending";
		case TaskState::Installing: return "installing";
		case TaskState::Completed: return "completed";
		case TaskState::Error: return "error";
		case TaskState::Cancelled: return "cancelled";
	}
	return "pending";
}

const char *taskStateLabel(TaskState state)
{
	switch(state)
	{
		case TaskState::Pending: return QT_TRANSLATE_NOOP("Messages", "Pending");
		case TaskState::Validating: return QT_TRANSLATE_NOOP("Messages", "Validating");
		case TaskState::Sending: return QT_TRANSLATE_NOOP("Messages", "Sending");
		case TaskState::Installing: return QT_TRANSLATE_NOOP("Messages", "Installing");
		case TaskState::Completed: return QT_TRANSLATE_NOOP("Messages", "Done");
		case TaskState::Error: return QT_TRANSLATE_NOOP("Messages", "Error");
		case TaskState::Cancelled: return QT_TRANSLATE_NOOP("Messages", "Cancelled");
	}
	return QT_TRANSLATE_NOOP("Messages", "Pending");
}

namespace {

TaskState taskStateFromName(const std::string &name)
{
	if(name == "validating") return TaskState::Validating;
	if(name == "sending") return TaskState::Sending;
	if(name == "installing") return TaskState::Installing;
	if(name == "completed") return TaskState::Completed;
	if(name == "error") return TaskState::Error;
	if(name == "cancelled") return TaskState::Cancelled;
	return TaskState::Pending;
}

PkgCategory categoryFromCode(const std::string &code)
{
	if(code == "gd") return PkgCategory::Game;
	if(code == "gp") return PkgCategory::Patch;
	if(code == "gpd") return PkgCategory::DeltaPatch;
	if(code == "ac") return PkgCategory::Dlc;
	if(code == "gdt") return PkgCategory::Theme;
	return PkgCategory::Unknown;
}

Json taskToJson(const QueueTask &task)
{
	Json json = Json::makeObject();
	json.set("id", Json::fromString(task.id));
	json.set("local_path", Json::fromString(task.localPath));
	json.set("mode", Json::fromString(transferModeName(task.mode)));
	json.set("state", Json::fromString(taskStateName(task.state)));
	json.set("title", Json::fromString(task.title));
	json.set("title_id", Json::fromString(task.titleId));
	json.set("content_id", Json::fromString(task.contentId));
	json.set("app_version", Json::fromString(task.appVersion));
	json.set("category", Json::fromString(pkgCategoryCode(task.category)));
	json.set("total_bytes", Json::fromInt(task.totalBytes));
	json.set("done_bytes", Json::fromInt(task.doneBytes));
	json.set("remote_path", Json::fromString(task.remotePath));
	json.set("remote_name", Json::fromString(task.remoteName));
	json.set("cleanup_remote_path", Json::fromString(task.cleanupRemotePath));
	json.set("remote_directory", Json::fromString(task.remoteDirectory));
	json.set("install_after", Json::fromBool(task.installAfter));
	json.set("delete_after_install", Json::fromBool(task.deleteAfterInstall));
	json.set("installed_from_console", Json::fromBool(task.installedFromConsole));
	json.set("message", Json::fromString(task.message));
	json.set("error_code", Json::fromInt(static_cast<int64_t>(task.errorCode)));
	json.set("attempts", Json::fromInt(task.attempts));
	json.set("created_at", Json::fromInt(task.createdAtUnix));
	json.set("started_at", Json::fromInt(task.startedAtUnix));
	json.set("finished_at", Json::fromInt(task.finishedAtUnix));
	return json;
}

QueueTask taskFromJson(const Json &json)
{
	QueueTask task;
	task.id = json["id"].toString();
	task.localPath = json["local_path"].toString();
	task.mode = transferModeFromName(json["mode"].toString(), TransferMode::DirectInstall);
	task.state = taskStateFromName(json["state"].toString());
	task.title = json["title"].toString();
	task.titleId = json["title_id"].toString();
	task.contentId = json["content_id"].toString();
	task.appVersion = json["app_version"].toString();
	task.category = categoryFromCode(json["category"].toString());
	task.totalBytes = json["total_bytes"].toInt();
	task.doneBytes = json["done_bytes"].toInt();
	task.remotePath = json["remote_path"].toString();
	task.remoteName = json["remote_name"].toString();
	task.cleanupRemotePath = json["cleanup_remote_path"].toString();
	task.remoteDirectory = json["remote_directory"].toString();
	task.installAfter = json["install_after"].toLooseBool(false);
	task.deleteAfterInstall = json["delete_after_install"].toLooseBool(false);
	task.installedFromConsole = json["installed_from_console"].toLooseBool(false);
	task.message = json["message"].toString();
	task.errorCode = static_cast<uint32_t>(json["error_code"].toInt());
	task.attempts = static_cast<int>(json["attempts"].toInt());
	task.createdAtUnix = json["created_at"].toInt();
	task.startedAtUnix = json["started_at"].toInt();
	task.finishedAtUnix = json["finished_at"].toInt();
	return task;
}

// What the console's download list calls a package of this kind.
TaskSubType taskSubTypeOf(PkgCategory category)
{
	switch(category)
	{
		case PkgCategory::Patch:
		case PkgCategory::DeltaPatch: return TaskSubType::Patch;
		case PkgCategory::Dlc: return TaskSubType::AdditionalContent;
		default: break;
	}
	return TaskSubType::Game;
}

} // namespace

InstallQueue::InstallQueue(Dependencies dependencies, Settings settings)
	: InstallQueue(dependencies, std::move(settings), Tuning())
{
}

InstallQueue::InstallQueue(Dependencies dependencies, Settings settings, Tuning tuning)
	: deps_(dependencies), tuning_(tuning), settings_(std::move(settings))
{
}

InstallQueue::~InstallQueue() { stop(); }

void InstallQueue::setSettings(const Settings &settings)
{
	std::lock_guard<std::mutex> lock(mutex_);
	settings_ = settings;
}

Settings InstallQueue::settings() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return settings_;
}

void InstallQueue::setListener(std::function<void(const QueueTask &)> listener)
{
	std::lock_guard<std::mutex> lock(mutex_);
	listener_ = std::move(listener);
}

void InstallQueue::setExistingPolicyResolver(std::function<ExistingPolicy(const QueueTask &)> resolver)
{
	std::lock_guard<std::mutex> lock(mutex_);
	existingPolicy_ = std::move(resolver);
}

std::string InstallQueue::pauseReason() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return pauseReason_;
}

void InstallQueue::sortBatch(std::vector<QueueTask> &batch)
{
	// Base game (gd) → patch (gp) → DLC (ac) within the same TITLE_ID,
	// keeping the relative order of the titles as they were dropped.
	std::vector<std::string> titleOrder;
	for(const QueueTask &task : batch)
	{
		if(std::find(titleOrder.begin(), titleOrder.end(), task.titleId) == titleOrder.end())
			titleOrder.push_back(task.titleId);
	}
	std::stable_sort(batch.begin(), batch.end(), [&](const QueueTask &a, const QueueTask &b) {
		const auto indexA = std::find(titleOrder.begin(), titleOrder.end(), a.titleId) - titleOrder.begin();
		const auto indexB = std::find(titleOrder.begin(), titleOrder.end(), b.titleId) - titleOrder.begin();
		if(indexA != indexB)
			return indexA < indexB;
		return pkgCategoryInstallOrder(a.category) < pkgCategoryInstallOrder(b.category);
	});
}

void InstallQueue::setCleanupPath(const std::string &id, const std::string &remotePath)
{
	std::lock_guard<std::mutex> lock(mutex_);
	for(QueueTask &task : tasks_)
	{
		if(task.id == id)
		{
			task.cleanupRemotePath = remotePath;
			return;
		}
	}
}

std::string InstallQueue::enqueueOne(const std::string &path, TransferMode mode, std::string *error)
{
	std::vector<std::string> rejected;
	const std::vector<std::string> ids = enqueue({ path }, mode, &rejected);
	if(ids.empty())
	{
		if(error)
			*error = rejected.empty() ? QT_TRANSLATE_NOOP("Messages", "File rejected.") : rejected.front();
		return std::string();
	}
	return ids.front();
}

std::string InstallQueue::enqueueUploadAndInstall(const std::string &path,
	const std::string &directory, std::string *error)
{
	std::vector<std::string> rejected;
	const std::vector<std::string> ids = enqueue({ path }, TransferMode::FtpUpload, &rejected,
		nullptr, [&](QueueTask &task) {
			task.remoteDirectory = directory;
			task.installAfter = true;
			task.deleteAfterInstall = true;
		});
	if(ids.empty())
	{
		if(error)
			*error = rejected.empty() ? std::string("rejected") : rejected.front();
		return std::string();
	}
	return ids.front();
}

std::vector<std::string> InstallQueue::enqueue(const std::vector<std::string> &paths,
	TransferMode mode, std::vector<std::string> *rejected,
	const std::vector<std::string> *remoteNames)
{
	return enqueue(paths, mode, rejected, remoteNames, nullptr);
}

std::vector<std::string> InstallQueue::enqueue(const std::vector<std::string> &paths,
	TransferMode mode, std::vector<std::string> *rejected,
	const std::vector<std::string> *remoteNames, const std::function<void(QueueTask &)> &adjust)
{
	PkgInspector inspector;
	std::vector<QueueTask> batch;
	std::vector<std::string> ids;

	for(size_t index = 0; index < paths.size(); ++index)
	{
		const std::string &path = paths[index];
		const PkgInfo info = inspector.inspect(path);
		// Any other file can still be copied over FTP as it is (an ISO for
		// the USB drive, a payload): only a broken .pkg is turned away.
		const bool plainCopy = !info.valid && mode == TransferMode::FtpUpload
			&& fileExtensionLower(path) != ".pkg" && fileSize(path) >= 0;
		if(!info.valid && !plainCopy)
		{
			const std::string reason = info.error.empty() ? QT_TRANSLATE_NOOP("Messages", "This file is not a valid PS4 pkg.")
													  : info.error;
			logWarning("File rejected: " + path + " — " + reason);
			if(rejected)
				rejected->push_back(baseName(path) + ": " + reason);
			continue;
		}

		QueueTask task;
		task.id = randomToken(8);
		task.localPath = path;
		task.mode = mode;
		if(remoteNames && index < remoteNames->size())
			task.remoteName = (*remoteNames)[index];
		task.state = TaskState::Pending;
		task.title = plainCopy ? baseName(path) : info.displayTitle();
		task.titleId = info.titleId;
		task.contentId = info.contentId;
		task.appVersion = info.appVersion;
		task.category = plainCopy ? PkgCategory::Unknown : info.kind;
		task.totalBytes = plainCopy ? fileSize(path) : info.fileSize;
		task.createdAtUnix = nowUnixSeconds();
		if(adjust)
			adjust(task);
		batch.push_back(task);
	}

	sortBatch(batch);

	{
		std::lock_guard<std::mutex> lock(mutex_);
		for(const QueueTask &task : batch)
		{
			tasks_.push_back(task);
			ids.push_back(task.id);
		}
	}
	for(const QueueTask &task : batch)
	{
		logInfo("Queued: " + task.title + " (" + pkgCategoryLabel(task.category) + ", "
			+ humanBytes(task.totalBytes) + ")");
		notify(task);
	}
	wakeup_.notify_all();
	return ids;
}

void InstallQueue::start()
{
	if(running_.exchange(true))
		return;
	worker_ = std::thread(&InstallQueue::workerLoop, this);
}

void InstallQueue::stop()
{
	if(!running_.exchange(false))
		return;
	cancelCurrent_.store(true);
	if(deps_.ftp)
		deps_.ftp->cancel();
	wakeup_.notify_all();
	if(worker_.joinable())
		worker_.join();
}

void InstallQueue::pause(const std::string &reason)
{
	paused_.store(true);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		pauseReason_ = reason;
	}
	if(!reason.empty())
		logWarning("Queue paused: " + reason);
}

void InstallQueue::resume()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		pauseReason_.clear();
	}
	waitingForService_.store(false);
	paused_.store(false);
	wakeup_.notify_all();
}

bool InstallQueue::cancel(const std::string &id)
{
	QueueTask cancelled;
	bool found = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if(currentId_ == id)
		{
			cancelCurrent_.store(true);
			if(deps_.ftp)
				deps_.ftp->cancel();
			return true;
		}
		for(auto it = tasks_.begin(); it != tasks_.end(); ++it)
		{
			if(it->id != id)
				continue;
			it->state = TaskState::Cancelled;
			it->message = QT_TRANSLATE_NOOP("Messages", "Cancelled by the user.");
			it->finishedAtUnix = nowUnixSeconds();
			cancelled = *it;
			tasks_.erase(it);
			history_.push_front(cancelled);
			while(history_.size() > tuning_.historyLimit)
				history_.pop_back();
			found = true;
			break;
		}
	}
	if(found)
		notify(cancelled);
	return found;
}

bool InstallQueue::retry(const std::string &id)
{
	QueueTask task;
	bool found = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for(auto it = history_.begin(); it != history_.end(); ++it)
		{
			if(it->id != id)
				continue;
			task = *it;
			history_.erase(it);
			found = true;
			break;
		}
		if(!found)
		{
			for(auto &queued : tasks_)
			{
				if(queued.id != id || !queued.isTerminal())
					continue;
				task = queued;
				found = true;
				break;
			}
			if(found)
				tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
								 [&](const QueueTask &t) { return t.id == id; }),
					tasks_.end());
		}
		if(found)
		{
			task.state = TaskState::Pending;
			task.message.clear();
			task.errorCode = 0;
			task.doneBytes = 0;
			task.consoleTaskId = -1;
			task.httpToken.clear();
			task.finishedAtUnix = 0;
			tasks_.push_back(task);
		}
	}
	if(found)
	{
		notify(task);
		wakeup_.notify_all();
	}
	return found;
}

bool InstallQueue::remove(const std::string &id)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if(currentId_ == id)
		return false;
	const size_t before = tasks_.size();
	tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
					 [&](const QueueTask &task) { return task.id == id; }),
		tasks_.end());
	if(tasks_.size() != before)
		return true;
	const size_t historyBefore = history_.size();
	history_.erase(std::remove_if(history_.begin(), history_.end(),
					   [&](const QueueTask &task) { return task.id == id; }),
		history_.end());
	return history_.size() != historyBefore;
}

bool InstallQueue::moveUp(const std::string &id)
{
	std::lock_guard<std::mutex> lock(mutex_);
	for(size_t i = 1; i < tasks_.size(); ++i)
	{
		if(tasks_[i].id != id)
			continue;
		if(tasks_[i - 1].id == currentId_)
			return false;
		std::swap(tasks_[i], tasks_[i - 1]);
		return true;
	}
	return false;
}

bool InstallQueue::moveDown(const std::string &id)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if(tasks_.size() < 2)
		return false;
	for(size_t i = 0; i + 1 < tasks_.size(); ++i)
	{
		if(tasks_[i].id != id)
			continue;
		if(tasks_[i].id == currentId_)
			return false;
		std::swap(tasks_[i], tasks_[i + 1]);
		return true;
	}
	return false;
}

std::vector<QueueTask> InstallQueue::tasks() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return std::vector<QueueTask>(tasks_.begin(), tasks_.end());
}

std::vector<QueueTask> InstallQueue::history() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return std::vector<QueueTask>(history_.begin(), history_.end());
}

bool InstallQueue::task(const std::string &id, QueueTask *out) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	for(const QueueTask &task : tasks_)
	{
		if(task.id != id)
			continue;
		if(out)
			*out = task;
		return true;
	}
	for(const QueueTask &task : history_)
	{
		if(task.id != id)
			continue;
		if(out)
			*out = task;
		return true;
	}
	return false;
}

void InstallQueue::notify(const QueueTask &task)
{
	std::function<void(const QueueTask &)> listener;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		listener = listener_;
	}
	if(listener)
		listener(task);
}

void InstallQueue::updateTask(const QueueTask &task)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for(QueueTask &queued : tasks_)
		{
			if(queued.id == task.id)
			{
				queued = task;
				break;
			}
		}
	}
	notify(task);
}

void InstallQueue::finishTask(QueueTask task)
{
	task.finishedAtUnix = nowUnixSeconds();
	{
		std::lock_guard<std::mutex> lock(mutex_);
		tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
						 [&](const QueueTask &queued) { return queued.id == task.id; }),
			tasks_.end());
		history_.push_front(task);
		while(history_.size() > tuning_.historyLimit)
			history_.pop_back();
		currentId_.clear();
	}
	notify(task);
}

void InstallQueue::requeueForServiceLoss(QueueTask task, const std::string &reason)
{
	task.state = TaskState::Pending;
	task.message = reason;
	task.doneBytes = 0;
	task.consoleTaskId = -1;
	if(deps_.httpServer && !task.httpToken.empty())
		deps_.httpServer->unregisterFile(task.httpToken);
	task.httpToken.clear();
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for(QueueTask &queued : tasks_)
		{
			if(queued.id == task.id)
			{
				queued = task;
				break;
			}
		}
		currentId_.clear();
	}
	pause(reason);
	waitingForService_.store(true);
	notify(task);
}

bool InstallQueue::takeNextTask(QueueTask *task)
{
	std::lock_guard<std::mutex> lock(mutex_);
	for(QueueTask &queued : tasks_)
	{
		if(queued.state != TaskState::Pending)
			continue;
		currentId_ = queued.id;
		queued.startedAtUnix = nowUnixSeconds();
		queued.attempts += 1;
		*task = queued;
		return true;
	}
	return false;
}

void InstallQueue::workerLoop()
{
	while(running_.load())
	{
		QueueTask task;
		if(paused_.load() || !takeNextTask(&task))
		{
			std::unique_lock<std::mutex> lock(wakeupMutex_);
			wakeup_.wait_for(lock, std::chrono::milliseconds(500),
				[this]() { return !running_.load(); });
			continue;
		}

		cancelCurrent_.store(false);
		if(task.mode == TransferMode::DirectInstall)
			runDirectInstall(task);
		else
			runFtpUpload(task);
	}
}

void InstallQueue::runDirectInstall(QueueTask task)
{
	const Settings cfg = settings();

	task.state = TaskState::Validating;
	task.message.clear();
	updateTask(task);

	if(!deps_.installer || !deps_.httpServer)
	{
		task.state = TaskState::Error;
		task.message = QT_TRANSLATE_NOOP("Messages", "Direct install unavailable (HTTP server or "
			"installer missing).");
		finishTask(task);
		return;
	}

	PkgInspector inspector(PkgInspector::Options { false, 4u * 1024 * 1024, 0 });
	const PkgInfo info = inspector.inspect(task.localPath);
	if(!info.valid)
	{
		task.state = TaskState::Error;
		task.message = info.error.empty() ? QT_TRANSLATE_NOOP("Messages", "This file is not a valid PS4 pkg.") : info.error;
		finishTask(task);
		return;
	}
	task.totalBytes = info.fileSize;

	// Already on the console? (§5.6, optional step)
	if(cfg.checkAlreadyInstalled && !task.titleId.empty())
	{
		bool exists = false;
		int64_t installedSize = -1;
		const InstallerResult result = deps_.installer->isExists(task.titleId, &exists, &installedSize);
		if(result.ok && exists)
		{
			std::function<ExistingPolicy(const QueueTask &)> resolver;
			{
				std::lock_guard<std::mutex> lock(mutex_);
				resolver = existingPolicy_;
			}
			const ExistingPolicy policy = resolver ? resolver(task) : ExistingPolicy::Reinstall;
			if(policy == ExistingPolicy::Skip)
			{
				task.state = TaskState::Cancelled;
				task.message = QT_TRANSLATE_NOOP("Messages", "Already on the console — skipped.");
				finishTask(task);
				return;
			}
			logInfo(task.titleId + " is already on the console; reinstalling.");
		}
	}

	if(cancelCurrent_.load())
	{
		task.state = TaskState::Cancelled;
		task.message = QT_TRANSLATE_NOOP("Messages", "Cancelled by the user.");
		finishTask(task);
		return;
	}

	task.httpToken = deps_.httpServer->registerFile(task.localPath, baseName(task.localPath));
	if(task.httpToken.empty())
	{
		task.state = TaskState::Error;
		task.message = QT_TRANSLATE_NOOP("Messages", "Could not expose the file to the local HTTP "
			"server.");
		finishTask(task);
		return;
	}
	const std::string url = deps_.httpServer->urlForToken(task.httpToken);

	task.state = TaskState::Installing;
	updateTask(task);

	InstallTaskHandle handle;
	const InstallerResult install = startInstall(task, url, &handle);
	if(!install.ok)
	{
		deps_.httpServer->unregisterFile(task.httpToken);
		task.httpToken.clear();
		task.errorCode = install.errorCode;
		if(install.errorCode != 0 && isOutOfSpaceError(install.errorCode))
			task.message = describeConsoleError(install.errorCode);
		else
			task.message = install.message;
		// Service down: pause the queue instead of burning the task (§6.3).
		if(install.errorCode == 0 && startsWith(install.message, "Remote installer unavailable"))
		{
			requeueForServiceLoss(task, install.message);
			return;
		}
		task.state = TaskState::Error;
		finishTask(task);
		return;
	}

	task.consoleTaskId = handle.taskId;
	if(!handle.title.empty())
		task.title = handle.title;
	updateTask(task);

	if(!deps_.installer->followsTasks())
	{
		followServedInstall(task);
		return;
	}

	// Poll progress every second, with stall detection.
	const int64_t startedMs = monotonicMillis();
	int64_t lastBytes = 0;
	int64_t lastSampleMs = startedMs;
	int consecutiveProgressFailures = 0;

	for(;;)
	{
		if(!running_.load() || cancelCurrent_.load())
		{
			if(task.consoleTaskId >= 0)
			{
				deps_.installer->stopTask(task.consoleTaskId);
				deps_.installer->unregisterTask(task.consoleTaskId);
			}
			deps_.httpServer->unregisterFile(task.httpToken);
			task.httpToken.clear();
			task.state = TaskState::Cancelled;
			task.message = QT_TRANSLATE_NOOP("Messages", "Cancelled by the user.");
			finishTask(task);
			return;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(tuning_.progressPollMs));

		TaskProgress progress;
		const InstallerResult result = deps_.installer->taskProgress(task.consoleTaskId, &progress);

		ServedFileStats stats;
		const bool haveStats = deps_.httpServer->statsForToken(task.httpToken, &stats);

		if(!result.ok)
		{
			// The task may have disappeared because it already finished.
			if(haveStats && stats.bytesSent >= task.totalBytes && task.totalBytes > 0)
			{
				task.doneBytes = task.totalBytes;
				task.state = TaskState::Completed;
				task.message = QT_TRANSLATE_NOOP("Messages", "Installation complete.");
				deps_.httpServer->unregisterFile(task.httpToken);
				task.httpToken.clear();
				finishTask(task);
				return;
			}
			if(++consecutiveProgressFailures >= 3)
			{
				deps_.httpServer->unregisterFile(task.httpToken);
				task.httpToken.clear();
				task.errorCode = result.errorCode;
				task.message = result.message;
				if(result.errorCode == 0 && startsWith(result.message, "Remote installer unavailable"))
				{
					requeueForServiceLoss(task, result.message);
					return;
				}
				task.state = TaskState::Error;
				finishTask(task);
				return;
			}
			continue;
		}
		consecutiveProgressFailures = 0;

		if(progress.errorResult != 0)
		{
			const uint32_t code = static_cast<uint32_t>(progress.errorResult);
			deps_.httpServer->unregisterFile(task.httpToken);
			task.httpToken.clear();
			task.errorCode = code;
			task.message = describeConsoleError(code);
			task.state = TaskState::Error;
			finishTask(task);
			return;
		}

		const int64_t nowMs = monotonicMillis();
		task.doneBytes = progress.transferredTotal > 0 ? progress.transferredTotal
													   : (haveStats ? stats.bytesSent : 0);
		if(progress.lengthTotal > 0)
			task.totalBytes = progress.lengthTotal;

		const double elapsedSeconds = static_cast<double>(nowMs - lastSampleMs) / 1000.0;
		if(elapsedSeconds >= 0.5)
		{
			const double delta = static_cast<double>(task.doneBytes - lastBytes);
			task.bytesPerSecond = delta > 0 ? delta / elapsedSeconds : 0.0;
			lastBytes = task.doneBytes;
			lastSampleMs = nowMs;
		}
		task.etaSeconds = progress.restSecTotal > 0 ? static_cast<int64_t>(progress.restSecTotal)
			: (task.bytesPerSecond > 1.0 && task.totalBytes > task.doneBytes
					  ? static_cast<int64_t>((task.totalBytes - task.doneBytes) / task.bytesPerSecond)
					  : -1);
		updateTask(task);

		// §7: task created but 0 bytes after 20 s = the console cannot reach the PC.
		const bool nothingServed = !haveStats || stats.bytesSent == 0;
		if(nothingServed && task.doneBytes == 0 && nowMs - startedMs > tuning_.stallTimeoutMs)
		{
			deps_.installer->stopTask(task.consoleTaskId);
			deps_.installer->unregisterTask(task.consoleTaskId);
			deps_.httpServer->unregisterFile(task.httpToken);
			task.httpToken.clear();
			task.state = TaskState::Error;
			task.message = QT_TRANSLATE_NOOP("Messages", "The console could not download from the PC. "
				"Check the Windows firewall and that both are "
				"on the same network.");
			finishTask(task);
			return;
		}

		if(progress.finished())
		{
			task.doneBytes = task.totalBytes;
			task.state = TaskState::Completed;
			task.message = QT_TRANSLATE_NOOP("Messages", "Installation complete.");
			deps_.httpServer->unregisterFile(task.httpToken);
			task.httpToken.clear();
			// Installed from the PC: the copy left on the console is no
			// longer useful and takes up space.
			if(!task.cleanupRemotePath.empty() && deps_.ftp)
			{
				const FtpResult removed = deps_.ftp->removeFile(task.cleanupRemotePath);
				if(removed.ok)
					task.message += " Copy deleted from the console.";
				else
					task.message += " (could not delete " + task.cleanupRemotePath + ": "
						+ removed.message + ")";
			}
			finishTask(task);
			return;
		}
	}
}

InstallerResult InstallQueue::startInstall(const QueueTask &task, const std::string &uri, InstallTaskHandle *handle)
{
	InstallerResult install = deps_.installer->installDirect({ uri }, handle);
	// An earlier try left its task in the console's downloads (BGFT "task
	// duplicated"): that task goes, and the install is asked for once more.
	if(!install.ok && install.errorCode == kBgftTaskDuplicated && !task.contentId.empty())
	{
		int stale = -1;
		if(deps_.installer->findTask(task.contentId, taskSubTypeOf(task.category), &stale).ok && stale >= 0)
		{
			logInfo("The console still had a task for " + task.contentId + " (" + std::to_string(stale)
				+ "); removing it and asking again.");
			deps_.installer->stopTask(stale);
			deps_.installer->unregisterTask(stale);
			install = deps_.installer->installDirect({ uri }, handle);
		}
		else
			logWarning("The console has a task for " + task.contentId + " that could not be found to remove.");
	}
	return install;
}

void InstallQueue::followServedInstall(QueueTask task)
{
	const int64_t startedMs = monotonicMillis();
	int64_t lastBytes = 0;
	int64_t lastSampleMs = startedMs;
	int64_t lastMoveMs = startedMs;

	const auto stop = [&](TaskState state, const std::string &message) {
		deps_.httpServer->unregisterFile(task.httpToken);
		task.httpToken.clear();
		task.state = state;
		task.message = message;
		finishTask(task);
	};

	for(;;)
	{
		if(!running_.load() || cancelCurrent_.load())
			return stop(TaskState::Cancelled, QT_TRANSLATE_NOOP("Messages", "Cancelled by the user."));

		std::this_thread::sleep_for(std::chrono::milliseconds(tuning_.progressPollMs));

		ServedFileStats stats;
		const int64_t sent = deps_.httpServer->statsForToken(task.httpToken, &stats) ? stats.bytesSent : 0;
		const int64_t nowMs = monotonicMillis();
		if(sent > task.doneBytes)
			lastMoveMs = nowMs;
		task.doneBytes = task.totalBytes > 0 ? std::min(sent, task.totalBytes) : sent;

		const double elapsedSeconds = static_cast<double>(nowMs - lastSampleMs) / 1000.0;
		if(elapsedSeconds >= 0.5)
		{
			const double delta = static_cast<double>(task.doneBytes - lastBytes);
			task.bytesPerSecond = delta > 0 ? delta / elapsedSeconds : 0.0;
			lastBytes = task.doneBytes;
			lastSampleMs = nowMs;
		}
		task.etaSeconds = task.bytesPerSecond > 1.0 && task.totalBytes > task.doneBytes
			? static_cast<int64_t>((task.totalBytes - task.doneBytes) / task.bytesPerSecond)
			: -1;
		updateTask(task);

		if(task.totalBytes > 0 && sent >= task.totalBytes)
		{
			task.doneBytes = task.totalBytes;
			std::string message = QT_TRANSLATE_NOOP("Messages",
				"Sent: the console finishes installing it (see its notifications).");
			if(!task.cleanupRemotePath.empty() && deps_.ftp && deps_.ftp->removeFile(task.cleanupRemotePath).ok)
				message += " Copy deleted from the console.";
			return stop(TaskState::Completed, message);
		}
		// §7: nothing taken after the stall time = the console cannot reach the PC.
		if(sent == 0 && nowMs - startedMs > tuning_.stallTimeoutMs)
			return stop(TaskState::Error, QT_TRANSLATE_NOOP("Messages", "The console could not download "
				"from the PC. Check the Windows firewall and that both are on the same network."));
		if(sent > 0 && nowMs - lastMoveMs > 3 * static_cast<int64_t>(tuning_.stallTimeoutMs))
			return stop(TaskState::Error, QT_TRANSLATE_NOOP("Messages", "The console stopped downloading "
				"the package. See its Downloads list."));
	}
}

void InstallQueue::runFtpUpload(QueueTask task)
{
	const Settings cfg = settings();

	task.state = TaskState::Validating;
	updateTask(task);

	if(!deps_.ftp)
	{
		task.state = TaskState::Error;
		task.message = QT_TRANSLATE_NOOP("Messages", "FTP client unavailable.");
		finishTask(task);
		return;
	}

	PkgInspector inspector(PkgInspector::Options { false, 4u * 1024 * 1024, 0 });
	const PkgInfo info = inspector.inspect(task.localPath);
	if(!info.valid && fileExtensionLower(task.localPath) == ".pkg")
	{
		task.state = TaskState::Error;
		task.message = info.error.empty() ? QT_TRANSLATE_NOOP("Messages", "This file is not a valid PS4 pkg.") : info.error;
		finishTask(task);
		return;
	}
	if(info.valid)
		task.totalBytes = info.fileSize;

	std::string directory = cfg.ftpUploadDirectory.empty() ? "/data/pkg/" : cfg.ftpUploadDirectory;
	if(!task.remoteDirectory.empty())
	{
		directory = task.remoteDirectory;
		// Already there is fine; anything else shows up in the upload.
		deps_.ftp->makeDirectory(directory);
	}
	task.remotePath = normalizeRemotePath(directory + "/"
		+ sanitizeFileName(task.remoteName.empty() ? task.localPath : task.remoteName));

	task.state = TaskState::Sending;
	updateTask(task);

	const int64_t startMs = monotonicMillis();
	int64_t lastBytes = 0;
	int64_t lastSampleMs = startMs;

	const FtpResult result = deps_.ftp->upload(task.localPath, task.remotePath,
		[&](int64_t done, int64_t total) {
			if(!running_.load() || cancelCurrent_.load())
				return false;
			task.doneBytes = done;
			if(total > 0)
				task.totalBytes = total;
			const int64_t nowMs = monotonicMillis();
			const double elapsed = static_cast<double>(nowMs - lastSampleMs) / 1000.0;
			if(elapsed >= 0.5)
			{
				const double delta = static_cast<double>(done - lastBytes);
				task.bytesPerSecond = delta > 0 ? delta / elapsed : 0.0;
				lastBytes = done;
				lastSampleMs = nowMs;
				task.etaSeconds = task.bytesPerSecond > 1.0 && task.totalBytes > done
					? static_cast<int64_t>((task.totalBytes - done) / task.bytesPerSecond)
					: -1;
				updateTask(task);
			}
			return true;
		});

	if(result.cancelled)
	{
		task.state = TaskState::Cancelled;
		task.message = QT_TRANSLATE_NOOP("Messages", "Cancelled by the user.");
		finishTask(task);
		return;
	}
	if(!result.ok)
	{
		task.state = TaskState::Error;
		task.message = result.message;
		finishTask(task);
		return;
	}

	task.doneBytes = task.totalBytes;
	task.state = TaskState::Completed;
	task.message = std::string(QT_TRANSLATE_NOOP("Messages", "Sent to the console")) + ": " + task.remotePath;

	// "Install after upload". The remote installer only accepts HTTP URLs
	// (see docs/validation.md), so it cannot be pointed at the file that
	// was just placed on the console. Instead, a direct install of the same
	// file is queued, served by the local HTTP server — the pkg stays
	// stored on the console and gets installed, which is what the option promises.
	// The install is queued before the upload is marked done, so nothing
	// sees the package as only "sent" in between.
	// Only a package installs; any other file was just a copy.
	bool installAfter = info.valid && (cfg.installAfterUpload || task.installAfter);
	const bool deleteAfter = task.installAfter ? task.deleteAfterInstall : cfg.deleteFromConsoleAfterInstall;
	if(installAfter && deps_.installer && deps_.installer->installsFromConsole())
	{
		// A PS5 installs the copy just put there, by its path: nothing is
		// sent twice. (The copy stays: the install reads it as it goes.)
		InstallTaskHandle handle;
		const InstallerResult started = startInstall(task, task.remotePath, &handle);
		if(started.ok)
		{
			installAfter = false;
			task.installedFromConsole = true;
			task.message = std::string(QT_TRANSLATE_NOOP("Messages",
				"Sent; the console is installing it (see its notifications)")) + ": " + task.remotePath;
			logInfo("Uploaded; the console installs " + task.remotePath + ".");
		}
		else
			logWarning("The console did not install " + task.remotePath + " by its path (" + started.message
				+ "); installing it from the PC.");
	}
	if(installAfter)
	{
		std::string err;
		const std::string remotePath = task.remotePath;
		const std::vector<std::string> ids = enqueue({ task.localPath }, TransferMode::DirectInstall,
			nullptr, nullptr, [&](QueueTask &install) {
				if(deleteAfter)
					install.cleanupRemotePath = remotePath;
			});
		if(ids.empty())
			logWarning("Could not install " + baseName(task.localPath) + " after the upload.");
		else
			logInfo("Uploaded; installing " + task.title + " from the PC.");
	}
	finishTask(task);
}

std::string InstallQueue::toJson() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	Json root = Json::makeObject();
	root.set("version", Json::fromInt(1));
	Json pending = Json::makeArray();
	for(const QueueTask &task : tasks_)
		pending.push(taskToJson(task));
	root.set("tasks", pending);
	Json past = Json::makeArray();
	for(const QueueTask &task : history_)
		past.push(taskToJson(task));
	root.set("history", past);
	return root.dump();
}

bool InstallQueue::fromJson(const std::string &text)
{
	std::string error;
	const Json root = Json::parse(text, &error);
	if(!root.isObject())
	{
		logWarning("Unreadable saved queue: " + error);
		return false;
	}

	std::deque<QueueTask> tasks;
	for(const Json &item : root["tasks"].items())
	{
		QueueTask task = taskFromJson(item);
		if(task.id.empty() || task.localPath.empty())
			continue;
		// Interrupted tasks come back as QT_TRANSLATE_NOOP("Messages", "Pending") (§5.6).
		if(!task.isTerminal())
		{
			task.state = TaskState::Pending;
			task.doneBytes = 0;
			task.consoleTaskId = -1;
			task.httpToken.clear();
		}
		tasks.push_back(task);
	}

	std::deque<QueueTask> history;
	for(const Json &item : root["history"].items())
	{
		QueueTask task = taskFromJson(item);
		if(!task.id.empty())
			history.push_back(task);
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		tasks_ = std::move(tasks);
		history_ = std::move(history);
		while(history_.size() > tuning_.historyLimit)
			history_.pop_back();
	}
	wakeup_.notify_all();
	return true;
}

bool InstallQueue::save(const std::string &path) const
{
	const size_t slash = path.find_last_of("/\\");
	if(slash != std::string::npos)
		SettingsStore::ensureDirectory(path.substr(0, slash));
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if(!file)
	{
		logError("Could not save the queue to " + path);
		return false;
	}
	file << toJson();
	return file.good();
}

bool InstallQueue::load(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	if(!file)
		return false;
	const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	return fromJson(text);
}

} // namespace orbislink
