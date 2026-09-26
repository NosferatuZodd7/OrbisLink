// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/queue/install_queue.h"

#include "orbislink/common/json.h"
#include "orbislink/common/log.h"
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

const char *taskStateLabelPt(TaskState state)
{
	switch(state)
	{
		case TaskState::Pending: return "Pendente";
		case TaskState::Validating: return "A validar";
		case TaskState::Sending: return "A enviar";
		case TaskState::Installing: return "A instalar";
		case TaskState::Completed: return "Concluído";
		case TaskState::Error: return "Erro";
		case TaskState::Cancelled: return "Cancelado";
	}
	return "Pendente";
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
	json.set("cleanup_remote_path", Json::fromString(task.cleanupRemotePath));
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
	task.cleanupRemotePath = json["cleanup_remote_path"].toString();
	task.message = json["message"].toString();
	task.errorCode = static_cast<uint32_t>(json["error_code"].toInt());
	task.attempts = static_cast<int>(json["attempts"].toInt());
	task.createdAtUnix = json["created_at"].toInt();
	task.startedAtUnix = json["started_at"].toInt();
	task.finishedAtUnix = json["finished_at"].toInt();
	return task;
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
	// Jogo base (gd) → patch (gp) → DLC (ac) dentro do mesmo TITLE_ID,
	// mantendo a ordem relativa dos títulos tal como foram largados.
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
			*error = rejected.empty() ? "Ficheiro recusado." : rejected.front();
		return std::string();
	}
	return ids.front();
}

std::vector<std::string> InstallQueue::enqueue(const std::vector<std::string> &paths,
	TransferMode mode, std::vector<std::string> *rejected)
{
	PkgInspector inspector;
	std::vector<QueueTask> batch;
	std::vector<std::string> ids;

	for(const std::string &path : paths)
	{
		const PkgInfo info = inspector.inspect(path);
		if(!info.valid)
		{
			const std::string reason = info.error.empty() ? "Este ficheiro não é um pkg PS4 válido."
													  : info.error;
			logWarning("Ficheiro recusado: " + path + " — " + reason);
			if(rejected)
				rejected->push_back(baseName(path) + ": " + reason);
			continue;
		}

		QueueTask task;
		task.id = randomToken(8);
		task.localPath = path;
		task.mode = mode;
		task.state = TaskState::Pending;
		task.title = info.displayTitle();
		task.titleId = info.titleId;
		task.contentId = info.contentId;
		task.appVersion = info.appVersion;
		task.category = info.kind;
		task.totalBytes = info.fileSize;
		task.createdAtUnix = nowUnixSeconds();
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
		logInfo("Na fila: " + task.title + " (" + pkgCategoryLabelPt(task.category) + ", "
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
		logWarning("Fila em pausa: " + reason);
}

void InstallQueue::resume()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		pauseReason_.clear();
	}
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
			it->message = "Cancelado pelo utilizador.";
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
		task.message = "Instalação direta indisponível (servidor HTTP ou instalador em falta).";
		finishTask(task);
		return;
	}

	PkgInspector inspector(PkgInspector::Options { false, 4u * 1024 * 1024, 0 });
	const PkgInfo info = inspector.inspect(task.localPath);
	if(!info.valid)
	{
		task.state = TaskState::Error;
		task.message = info.error.empty() ? "Este ficheiro não é um pkg PS4 válido." : info.error;
		finishTask(task);
		return;
	}
	task.totalBytes = info.fileSize;

	// Já existe na consola? (§5.6, passo opcional)
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
				task.message = "Já existe na consola — saltado.";
				finishTask(task);
				return;
			}
			logInfo(task.titleId + " já existe na consola; a reinstalar.");
		}
	}

	if(cancelCurrent_.load())
	{
		task.state = TaskState::Cancelled;
		task.message = "Cancelado pelo utilizador.";
		finishTask(task);
		return;
	}

	task.httpToken = deps_.httpServer->registerFile(task.localPath, baseName(task.localPath));
	if(task.httpToken.empty())
	{
		task.state = TaskState::Error;
		task.message = "Não foi possível expor o ficheiro ao servidor HTTP local.";
		finishTask(task);
		return;
	}
	const std::string url = deps_.httpServer->urlForToken(task.httpToken);

	task.state = TaskState::Installing;
	updateTask(task);

	InstallTaskHandle handle;
	const InstallerResult install = deps_.installer->installDirect({ url }, &handle);
	if(!install.ok)
	{
		deps_.httpServer->unregisterFile(task.httpToken);
		task.httpToken.clear();
		task.errorCode = install.errorCode;
		if(install.errorCode != 0 && isOutOfSpaceError(install.errorCode))
			task.message = describeConsoleError(install.errorCode);
		else
			task.message = install.message;
		// Serviço em baixo: pausa a fila em vez de queimar a tarefa (§6.3).
		if(install.errorCode == 0 && startsWith(install.message, "Instalador remoto indisponível"))
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

	// Polling de progresso a cada segundo, com deteção de bloqueio.
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
			task.message = "Cancelado pelo utilizador.";
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
			// A tarefa pode ter desaparecido por já estar concluída.
			if(haveStats && stats.bytesSent >= task.totalBytes && task.totalBytes > 0)
			{
				task.doneBytes = task.totalBytes;
				task.state = TaskState::Completed;
				task.message = "Instalação concluída.";
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
				if(result.errorCode == 0 && startsWith(result.message, "Instalador remoto indisponível"))
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

		// §7: tarefa criada mas 0 bytes após 20 s = a consola não alcança o PC.
		const bool nothingServed = !haveStats || stats.bytesSent == 0;
		if(nothingServed && task.doneBytes == 0 && nowMs - startedMs > tuning_.stallTimeoutMs)
		{
			deps_.installer->stopTask(task.consoleTaskId);
			deps_.installer->unregisterTask(task.consoleTaskId);
			deps_.httpServer->unregisterFile(task.httpToken);
			task.httpToken.clear();
			task.state = TaskState::Error;
			task.message = "A consola não conseguiu descarregar do PC. Verifica a firewall do "
						   "Windows e se estão na mesma rede.";
			finishTask(task);
			return;
		}

		if(progress.finished())
		{
			task.doneBytes = task.totalBytes;
			task.state = TaskState::Completed;
			task.message = "Instalação concluída.";
			deps_.httpServer->unregisterFile(task.httpToken);
			task.httpToken.clear();
			// Instalado a partir do PC: a cópia que ficou na consola já não
			// serve para nada e ocupa espaço.
			if(!task.cleanupRemotePath.empty() && deps_.ftp)
			{
				const FtpResult removed = deps_.ftp->removeFile(task.cleanupRemotePath);
				if(removed.ok)
					task.message += " Cópia apagada da consola.";
				else
					task.message += " (não foi possível apagar " + task.cleanupRemotePath + ": "
						+ removed.message + ")";
			}
			finishTask(task);
			return;
		}
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
		task.message = "Cliente FTP indisponível.";
		finishTask(task);
		return;
	}

	PkgInspector inspector(PkgInspector::Options { false, 4u * 1024 * 1024, 0 });
	const PkgInfo info = inspector.inspect(task.localPath);
	if(!info.valid && fileExtensionLower(task.localPath) == ".pkg")
	{
		task.state = TaskState::Error;
		task.message = info.error.empty() ? "Este ficheiro não é um pkg PS4 válido." : info.error;
		finishTask(task);
		return;
	}
	if(info.valid)
		task.totalBytes = info.fileSize;

	std::string directory = cfg.ftpUploadDirectory.empty() ? "/data/pkg/" : cfg.ftpUploadDirectory;
	task.remotePath = normalizeRemotePath(directory + "/" + sanitizeFileName(task.localPath));

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
		task.message = "Cancelado pelo utilizador.";
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
	task.message = "Enviado para " + task.remotePath + ".";

	// "Instalar após upload". O instalador remoto só aceita URLs HTTP (ver
	// docs/validacao.md), por isso não se lhe pode apontar o ficheiro que
	// acabou de ficar na consola. O que se faz é pôr na fila uma instalação
	// direta do mesmo ficheiro, que é servido pelo HTTP local — o pkg fica
	// guardado na consola e instalado, que é o que a opção promete.
	const bool instalarDepois = cfg.installAfterUpload;
	finishTask(task);

	if(instalarDepois)
	{
		std::string erro;
		const std::string id = enqueueOne(task.localPath, TransferMode::DirectInstall, &erro);
		if(id.empty())
			logWarning("Não foi possível instalar " + baseName(task.localPath)
				+ " depois do envio: " + erro);
		else
		{
			if(cfg.deleteFromConsoleAfterInstall)
				setCleanupPath(id, task.remotePath);
			logInfo("Enviado; a instalar " + task.title + " a partir do PC.");
		}
	}
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
		logWarning("Fila persistida ilegível: " + error);
		return false;
	}

	std::deque<QueueTask> tasks;
	for(const Json &item : root["tasks"].items())
	{
		QueueTask task = taskFromJson(item);
		if(task.id.empty() || task.localPath.empty())
			continue;
		// Tarefas interrompidas voltam como "Pendente" (§5.6).
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
		logError("Não foi possível guardar a fila em " + path);
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
