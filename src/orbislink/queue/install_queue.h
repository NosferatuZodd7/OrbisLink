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

// Pendente → A validar → A enviar/A instalar → Concluído | Erro | Cancelado (§5.6).
enum class TaskState { Pending, Validating, Sending, Installing, Completed, Error, Cancelled };

const char *taskStateName(TaskState state);      // identificador estável (persistência)
const char *taskStateLabelPt(TaskState state);   // texto para a UI

// O que fazer quando o título já existe na consola.
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
	// Ficheiro a apagar da consola quando esta tarefa acabar bem. Só é
	// preenchido pelo "enviar e instalar" com "apagar depois" ligado.
	std::string cleanupRemotePath;
	std::string message;    // erro ou informação para a UI
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

// Fila sequencial de instalações/envios (§5.6).
class InstallQueue
{
public:
	struct Dependencies
	{
		LocalHttpServer *httpServer = nullptr;
		IInstallerBackend *installer = nullptr;
		FtpClient *ftp = nullptr;
		ConsoleManager *console = nullptr; // opcional: pausa a fila se os serviços caírem
	};

	struct Tuning
	{
		int progressPollMs = 1000;  // §5.4: polling de 1 s
		int stallTimeoutMs = 20000; // §7: tarefa criada mas 0 bytes após 20 s
		size_t historyLimit = 100;  // §5.7: histórico das últimas 100 tarefas
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

	// Notificado a cada mudança de estado/progresso (a UI liga-se aqui).
	void setListener(std::function<void(const QueueTask &)> listener);
	// Decisão quando o título já existe na consola; por omissão, reinstalar.
	void setExistingPolicyResolver(std::function<ExistingPolicy(const QueueTask &)> resolver);

	bool save(const std::string &path) const;
	bool load(const std::string &path);

	// Serialização exposta para testes.
	std::string toJson() const;
	bool fromJson(const std::string &text);

private:
	void workerLoop();
	bool takeNextTask(QueueTask *task);
	void runDirectInstall(QueueTask task);
	// A tarefa de instalação criada por "instalar depois de enviar" precisa
	// de saber que ficheiro apagar na consola quando acabar.
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
