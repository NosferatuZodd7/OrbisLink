// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/common/util.h"
#include "orbislink/http/local_http_server.h"
#include "orbislink/queue/install_queue.h"
#include "test_fixtures.h"
#include "test_support.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <chrono>
#include <sstream>
#include <thread>

using namespace orbislink;
using namespace orbislink_test;

namespace {

// Instalador falso: implementa IInstallerBackend para testar a fila sem PS4.
class FakeInstaller : public IInstallerBackend
{
public:
	std::atomic<bool> available { true };
	std::atomic<bool> alreadyInstalled { false };
	std::atomic<int> failWithCode { 0 };
	std::atomic<int> installCalls { 0 };
	std::atomic<int> stopCalls { 0 };
	std::atomic<int64_t> totalBytes { 0 };
	std::atomic<int64_t> transferred { 0 };
	std::atomic<int> stepBytes { 4096 };
	std::vector<std::string> lastUrls;

	std::string name() const override { return "Instalador de teste"; }
	std::string endpoint() const override { return "http://fake"; }
	bool probe(std::string *detail) override
	{
		if(detail)
			*detail = available.load() ? "ok" : "sem resposta";
		return available.load();
	}

	InstallerResult installDirect(const std::vector<std::string> &urls, InstallTaskHandle *handle) override
	{
		if(!available.load())
			return InstallerResult::failure("Instalador remoto indisponível. Abre o Remote Package Installer na consola.");
		if(failWithCode.load() != 0)
		{
			InstallerResult result;
			result.errorCode = static_cast<uint32_t>(failWithCode.load());
			result.message = "erro simulado";
			return result;
		}
		lastUrls = urls;
		++installCalls;
		transferred.store(0);
		if(handle)
		{
			handle->taskId = 42;
			handle->title = "Título da consola";
		}
		return InstallerResult::success();
	}

	InstallerResult installFromReferenceJson(const std::string &, InstallTaskHandle *) override
	{
		return InstallerResult::failure("não usado");
	}

	InstallerResult isExists(const std::string &, bool *exists, int64_t *size) override
	{
		if(exists)
			*exists = alreadyInstalled.load();
		if(size)
			*size = alreadyInstalled.load() ? 1024 : -1;
		return InstallerResult::success();
	}

	InstallerResult taskProgress(int, TaskProgress *progress) override
	{
		if(!available.load())
			return InstallerResult::failure("Instalador remoto indisponível. Abre o Remote Package Installer na consola.");
		const int64_t total = totalBytes.load();
		int64_t done = transferred.load() + stepBytes.load();
		if(done > total)
			done = total;
		transferred.store(done);
		if(progress)
		{
			progress->lengthTotal = total;
			progress->transferredTotal = done;
			progress->length = total;
			progress->transferred = done;
			progress->restSecTotal = done >= total ? 0 : 5;
		}
		return InstallerResult::success();
	}

	InstallerResult findTask(const std::string &, TaskSubType, int *) override
	{
		return InstallerResult::failure("não usado");
	}
	InstallerResult startTask(int) override { return InstallerResult::success(); }
	InstallerResult stopTask(int) override { ++stopCalls; return InstallerResult::success(); }
	InstallerResult pauseTask(int) override { return InstallerResult::success(); }
	InstallerResult resumeTask(int) override { return InstallerResult::success(); }
	InstallerResult unregisterTask(int) override { return InstallerResult::success(); }
	InstallerResult uninstallGame(const std::string &) override { return InstallerResult::success(); }
	InstallerResult uninstallPatch(const std::string &) override { return InstallerResult::success(); }
	InstallerResult uninstallAdditionalContent(const std::string &) override { return InstallerResult::success(); }
	InstallerResult uninstallTheme(const std::string &) override { return InstallerResult::success(); }
};

struct PkgFile
{
	std::string path;
	explicit PkgFile(const std::string &name, const PkgOptions &options)
	{
		path = writeTempFile(name, buildPkg(options));
	}
	~PkgFile() { removeTempFile(path); }
};

PkgOptions gameOptions(const std::string &titleId, const std::string &title)
{
	PkgOptions options;
	options.contentId = "UP0001-" + titleId + "_00-ORBISLINKTEST001";
	options.sfoEntries = { { "CATEGORY", "gd" }, { "TITLE", title }, { "TITLE_ID", titleId },
		{ "APP_VER", "01.00" } };
	return options;
}

PkgOptions patchOptions(const std::string &titleId, const std::string &title)
{
	PkgOptions options = gameOptions(titleId, title);
	options.contentFlags = 0x00100000;
	options.sfoEntries[0].second = "gp";
	return options;
}

PkgOptions dlcOptions(const std::string &titleId, const std::string &title)
{
	PkgOptions options = gameOptions(titleId, title);
	options.contentType = 0x1B;
	options.sfoEntries[0].second = "ac";
	return options;
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 8000)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	while(std::chrono::steady_clock::now() < deadline)
	{
		if(predicate())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	return predicate();
}

InstallQueue::Tuning fastTuning()
{
	InstallQueue::Tuning tuning;
	tuning.progressPollMs = 20;
	tuning.stallTimeoutMs = 500;
	return tuning;
}

} // namespace

ORBISLINK_TEST(ordena_jogo_patch_dlc_do_mesmo_title_id)
{
	PkgFile dlc("ordem-dlc.pkg", dlcOptions("CUSA00001", "Jogo A"));
	PkgFile patch("ordem-patch.pkg", patchOptions("CUSA00001", "Jogo A"));
	PkgFile game("ordem-jogo.pkg", gameOptions("CUSA00001", "Jogo A"));

	InstallQueue queue(InstallQueue::Dependencies {}, Settings {});
	std::vector<std::string> rejected;
	// Largados fora de ordem, de propósito.
	queue.enqueue({ dlc.path, patch.path, game.path }, TransferMode::DirectInstall, &rejected);
	CHECK(rejected.empty());

	const auto tasks = queue.tasks();
	CHECK_EQ(tasks.size(), static_cast<size_t>(3));
	CHECK(tasks[0].category == PkgCategory::Game);
	CHECK(tasks[1].category == PkgCategory::Patch);
	CHECK(tasks[2].category == PkgCategory::Dlc);
}

ORBISLINK_TEST(mantem_ordem_entre_titulos_diferentes)
{
	PkgFile patchB("ordem2-patchb.pkg", patchOptions("CUSA00002", "Jogo B"));
	PkgFile gameA("ordem2-jogoa.pkg", gameOptions("CUSA00001", "Jogo A"));
	PkgFile gameB("ordem2-jogob.pkg", gameOptions("CUSA00002", "Jogo B"));

	InstallQueue queue(InstallQueue::Dependencies {}, Settings {});
	queue.enqueue({ patchB.path, gameA.path, gameB.path }, TransferMode::DirectInstall);

	const auto tasks = queue.tasks();
	CHECK_EQ(tasks.size(), static_cast<size_t>(3));
	CHECK_EQ(tasks[0].titleId, std::string("CUSA00002"));
	CHECK(tasks[0].category == PkgCategory::Game); // o jogo passa à frente do patch
	CHECK_EQ(tasks[1].titleId, std::string("CUSA00002"));
	CHECK_EQ(tasks[2].titleId, std::string("CUSA00001"));
}

ORBISLINK_TEST(recusa_ficheiros_invalidos)
{
	PkgOptions broken;
	broken.validMagic = false;
	const std::string path = writeTempFile("invalido.pkg", buildPkg(broken));

	InstallQueue queue(InstallQueue::Dependencies {}, Settings {});
	std::vector<std::string> rejected;
	const auto ids = queue.enqueue({ path }, TransferMode::DirectInstall, &rejected);
	CHECK(ids.empty());
	CHECK_EQ(rejected.size(), static_cast<size_t>(1));
	CHECK(rejected[0].find("pkg PS4 válido") != std::string::npos);
	removeTempFile(path);
}

ORBISLINK_TEST(instalacao_direta_do_principio_ao_fim)
{
	PkgFile game("fluxo-jogo.pkg", gameOptions("CUSA00010", "Jogo Fluxo"));

	LocalHttpServer server;
	LocalHttpServer::Config config;
	config.bindAddress = "127.0.0.1";
	config.port = 0;
	config.autoSelectPort = false;
	std::string error;
	CHECK(server.start(config, &error));

	FakeInstaller installer;
	installer.totalBytes.store(fileSize(game.path));

	InstallQueue::Dependencies deps;
	deps.httpServer = &server;
	deps.installer = &installer;

	Settings settings;
	settings.checkAlreadyInstalled = false;

	InstallQueue queue(deps, settings, fastTuning());
	std::atomic<int> notifications { 0 };
	queue.setListener([&](const QueueTask &) { ++notifications; });

	const std::string id = queue.enqueueOne(game.path, TransferMode::DirectInstall);
	CHECK(!id.empty());
	queue.start();

	CHECK(waitFor([&]() {
		QueueTask task;
		return queue.task(id, &task) && task.state == TaskState::Completed;
	}));
	queue.stop();

	QueueTask task;
	CHECK(queue.task(id, &task));
	CHECK(task.state == TaskState::Completed);
	CHECK_EQ(task.title, std::string("Título da consola"));
	CHECK_EQ(task.doneBytes, task.totalBytes);
	CHECK(notifications.load() > 0);
	CHECK_EQ(installer.installCalls.load(), 1);
	// O URL enviado à consola aponta para o token do servidor local.
	CHECK_EQ(installer.lastUrls.size(), static_cast<size_t>(1));
	CHECK(installer.lastUrls[0].find("/f/") != std::string::npos);
	// O token é removido quando a tarefa acaba.
	CHECK(server.allStats().empty());
	server.stop();
}

ORBISLINK_TEST(deteta_que_a_consola_nao_alcanca_o_pc)
{
	PkgFile game("parado-jogo.pkg", gameOptions("CUSA00011", "Jogo Parado"));

	LocalHttpServer server;
	LocalHttpServer::Config config;
	config.bindAddress = "127.0.0.1";
	config.port = 0;
	config.autoSelectPort = false;
	CHECK(server.start(config, nullptr));

	FakeInstaller installer;
	installer.totalBytes.store(fileSize(game.path));
	installer.stepBytes.store(0); // a consola nunca descarrega nada

	InstallQueue::Dependencies deps;
	deps.httpServer = &server;
	deps.installer = &installer;
	Settings settings;
	settings.checkAlreadyInstalled = false;

	InstallQueue queue(deps, settings, fastTuning());
	const std::string id = queue.enqueueOne(game.path, TransferMode::DirectInstall);
	queue.start();

	CHECK(waitFor([&]() {
		QueueTask task;
		return queue.task(id, &task) && task.state == TaskState::Error;
	}));
	queue.stop();

	QueueTask task;
	CHECK(queue.task(id, &task));
	CHECK(task.message.find("não conseguiu descarregar do PC") != std::string::npos);
	CHECK(installer.stopCalls.load() > 0);
	server.stop();
}

ORBISLINK_TEST(pausa_a_fila_quando_o_instalador_cai)
{
	PkgFile game("queda-jogo.pkg", gameOptions("CUSA00012", "Jogo Queda"));

	LocalHttpServer server;
	LocalHttpServer::Config config;
	config.bindAddress = "127.0.0.1";
	config.port = 0;
	config.autoSelectPort = false;
	CHECK(server.start(config, nullptr));

	FakeInstaller installer;
	installer.available.store(false); // consola adormeceu
	installer.totalBytes.store(fileSize(game.path));

	InstallQueue::Dependencies deps;
	deps.httpServer = &server;
	deps.installer = &installer;
	Settings settings;
	settings.checkAlreadyInstalled = false;

	InstallQueue queue(deps, settings, fastTuning());
	const std::string id = queue.enqueueOne(game.path, TransferMode::DirectInstall);
	queue.start();

	CHECK(waitFor([&]() { return queue.paused(); }));
	QueueTask task;
	CHECK(queue.task(id, &task));
	// A tarefa volta a "Pendente" e a fila fica em pausa (§6.3).
	CHECK(task.state == TaskState::Pending);
	CHECK(!queue.pauseReason().empty());

	// Quando o serviço volta, a fila retoma e a tarefa acaba.
	installer.available.store(true);
	queue.resume();
	CHECK(waitFor([&]() {
		QueueTask current;
		return queue.task(id, &current) && current.state == TaskState::Completed;
	}));
	queue.stop();
	server.stop();
}

ORBISLINK_TEST(salta_titulo_ja_instalado_quando_a_politica_o_diz)
{
	PkgFile game("existente-jogo.pkg", gameOptions("CUSA00013", "Jogo Existente"));

	LocalHttpServer server;
	LocalHttpServer::Config config;
	config.bindAddress = "127.0.0.1";
	config.port = 0;
	config.autoSelectPort = false;
	CHECK(server.start(config, nullptr));

	FakeInstaller installer;
	installer.alreadyInstalled.store(true);
	installer.totalBytes.store(fileSize(game.path));

	InstallQueue::Dependencies deps;
	deps.httpServer = &server;
	deps.installer = &installer;
	Settings settings;
	settings.checkAlreadyInstalled = true;

	InstallQueue queue(deps, settings, fastTuning());
	queue.setExistingPolicyResolver([](const QueueTask &) { return ExistingPolicy::Skip; });
	const std::string id = queue.enqueueOne(game.path, TransferMode::DirectInstall);
	queue.start();

	CHECK(waitFor([&]() {
		QueueTask task;
		return queue.task(id, &task) && task.state == TaskState::Cancelled;
	}));
	queue.stop();

	QueueTask task;
	CHECK(queue.task(id, &task));
	CHECK(task.message.find("Já existe na consola") != std::string::npos);
	CHECK_EQ(installer.installCalls.load(), 0);
	server.stop();
}

ORBISLINK_TEST(persistencia_repoe_tarefas_interrompidas_como_pendentes)
{
	PkgFile game("persist-jogo.pkg", gameOptions("CUSA00014", "Jogo Persistido"));
	PkgFile patch("persist-patch.pkg", patchOptions("CUSA00014", "Jogo Persistido"));

	InstallQueue original(InstallQueue::Dependencies {}, Settings {});
	original.enqueue({ game.path, patch.path }, TransferMode::DirectInstall);
	const std::string path = ".orbislink-test-queue.json";
	CHECK(original.save(path));

	InstallQueue restored(InstallQueue::Dependencies {}, Settings {});
	CHECK(restored.load(path));
	const auto tasks = restored.tasks();
	CHECK_EQ(tasks.size(), static_cast<size_t>(2));
	CHECK(tasks[0].state == TaskState::Pending);
	CHECK_EQ(tasks[0].title, std::string("Jogo Persistido"));
	CHECK(tasks[0].category == PkgCategory::Game);
	CHECK_EQ(tasks[0].totalBytes, fileSize(game.path));
	std::remove(path.c_str());
}

ORBISLINK_TEST(reordenar_cancelar_e_repetir)
{
	PkgFile a("mover-a.pkg", gameOptions("CUSA00021", "Jogo A"));
	PkgFile b("mover-b.pkg", gameOptions("CUSA00022", "Jogo B"));

	InstallQueue queue(InstallQueue::Dependencies {}, Settings {});
	const auto ids = queue.enqueue({ a.path, b.path }, TransferMode::DirectInstall);
	CHECK_EQ(ids.size(), static_cast<size_t>(2));

	CHECK(queue.moveUp(ids[1]));
	std::string firstId = queue.tasks()[0].id;
	CHECK_EQ(firstId, ids[1]);
	CHECK(queue.moveDown(ids[1]));
	firstId = queue.tasks()[0].id;
	CHECK_EQ(firstId, ids[0]);

	CHECK(queue.cancel(ids[0]));
	CHECK_EQ(queue.tasks().size(), static_cast<size_t>(1));
	CHECK_EQ(queue.history().size(), static_cast<size_t>(1));
	const auto historyAfterCancel = queue.history();
	CHECK(historyAfterCancel[0].state == TaskState::Cancelled);

	CHECK(queue.retry(ids[0]));
	CHECK_EQ(queue.tasks().size(), static_cast<size_t>(2));
	QueueTask task;
	CHECK(queue.task(ids[0], &task));
	CHECK(task.state == TaskState::Pending);

	CHECK(queue.remove(ids[0]));
	CHECK_EQ(queue.tasks().size(), static_cast<size_t>(1));
}

TEST_MAIN()
