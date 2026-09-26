// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/app_controller.h"

#include "orbislink/qt/diagnostics.h"
#ifdef ORBISLINK_HAS_STREAM
#include "orbislink/stream/chiaki_log_bridge.h"
#endif

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/net/http_client.h"
#include "orbislink/net/net_utils.h"
#include "orbislink/pkg/pkg_inspector.h"
#include "orbislink/update/sha256.h"
#include "orbislink/update/update_checker.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <thread>

#include <algorithm>

namespace orbislink {

namespace {

QString stateName(ServiceState state)
{
	switch(state)
	{
		case ServiceState::Available: return QStringLiteral("available");
		case ServiceState::Unavailable: return QStringLiteral("unavailable");
		case ServiceState::Checking: return QStringLiteral("checking");
		case ServiceState::Unknown: break;
	}
	return QStringLiteral("unknown");
}

} // namespace

AppController::AppController(QObject *parent)
	: QObject(parent), store_(SettingsStore::defaultSettingsPath())
{
	// Marcas em cada passo: se isto falhar numa máquina, o registo diz em
	// que passo parou.
	qInfo("Arranque: pasta de dados");
	SettingsStore::ensureDirectory(SettingsStore::defaultDirectory());
	qInfo("Arranque: a ler definições");
	store_.load(&settings_);
	qInfo("Arranque: a calcular o caminho do registo");
	const std::string coreLogPath = SettingsStore::defaultLogPath();
	qInfo("Arranque: registo do núcleo em %s", coreLogPath.c_str());
	Logger::instance().setLevel(settings_.debugLogging ? LogLevel::Debug : LogLevel::Info);
	qInfo("Arranque: nível de registo definido");
	// A escrita para a consola não serve numa aplicação de janela; o
	// ficheiro chega.
	Logger::instance().setConsoleOutput(false);
	qInfo("Arranque: a abrir o ficheiro de registo");
	if(!Logger::instance().setFile(coreLogPath))
		qWarning("Não foi possível abrir %s", coreLogPath.c_str());
	qInfo("Arranque: ficheiro de registo aberto");

	// Tudo o que for escrito no registo passa também para a janela de
	// diagnóstico. A mensagem que chega aqui já vem mascarada.
	Logger::instance().setSink([this](LogLevel level, const std::string &message) {
		const QString nivel = QString::fromLatin1(logLevelName(level));
		const QString texto = QString::fromStdString(message);
		QMetaObject::invokeMethod(
			this, [this, nivel, texto]() { emit logLine(nivel, texto); },
			Qt::QueuedConnection);
	});

	qInfo("Arranque: a preparar os serviços");
	rebuildBackends();
	qInfo("Arranque: serviços prontos");
	if(!settings_.ftpUploadDirectory.empty())
		ftpPath_ = QString::fromStdString(settings_.ftpUploadDirectory);
}

AppController::~AppController()
{
	if(queue_)
	{
		queue_->save(SettingsStore::defaultQueuePath());
		queue_->stop();
	}
	if(console_)
		console_->stop();
	if(httpServer_)
		httpServer_->stop();
	store_.save(settings_);
}

void AppController::rebuildBackends()
{
	if(queue_)
		queue_->stop();
	if(console_)
		console_->stop();
	if(httpServer_)
		httpServer_->stop();

	qInfo("Serviços: gestor da consola");
	console_ = std::make_unique<ConsoleManager>(settings_);
	// O gestor novo nasce sem saber do Remote Play; devolve-se-lhe o que já
	// se sabia, senão o indicador apaga-se a meio de uma sessão.
	if(!lastRemotePlayState_.isEmpty())
	{
		const QString estado = lastRemotePlayState_;
		const QString detalhe = lastRemotePlayDetail_;
		QTimer::singleShot(0, this, [this, estado, detalhe]() {
			reportRemotePlayState(estado, detalhe);
		});
	}
	console_->setListener([this](const ConsoleStatus &status) {
		QMetaObject::invokeMethod(
			this,
			[this, status]() {
				status_ = status;
				emit statusChanged();
			},
			Qt::QueuedConnection);
	});

	qInfo("Serviços: servidor HTTP local");
	httpServer_ = std::make_unique<LocalHttpServer>();
	LocalHttpServer::Config httpConfig;
	if(settings_.httpBindAddress.empty())
	{
		// Percorre as interfaces de rede do sistema. Se isto falhar, o
		// servidor fica no endereço local em vez de deitar tudo abaixo.
		qInfo("Serviços: a procurar a interface de rede");
		try
		{
			httpConfig.bindAddress = localAddressForConsole(settings_.consoleAddress);
		}
		catch(const std::exception &error)
		{
			qWarning("Não foi possível listar as interfaces de rede: %s", error.what());
		}
		catch(...)
		{
			qWarning("Não foi possível listar as interfaces de rede.");
		}
	}
	else
		httpConfig.bindAddress = settings_.httpBindAddress;
	if(httpConfig.bindAddress.empty())
		httpConfig.bindAddress = "127.0.0.1";
	httpConfig.port = settings_.httpPort;
	httpConfig.allowedClient = settings_.restrictToConsoleIp ? settings_.consoleAddress : std::string();
	qInfo("Serviços: a ligar o servidor HTTP em %s:%u", httpConfig.bindAddress.c_str(),
		static_cast<unsigned>(httpConfig.port));
	std::string httpError;
	if(!httpServer_->start(httpConfig, &httpError))
		setStatusMessage(tr("Servidor HTTP local não arrancou: %1").arg(QString::fromStdString(httpError)));

	qInfo("Serviços: instalador remoto e FTP");
	RpiClient::Config rpiConfig;
	rpiConfig.host = settings_.consoleAddress;
	rpiConfig.port = settings_.installerPort;
	installer_ = std::make_unique<RpiClient>(rpiConfig);

	FtpClient::Config ftpConfig;
	ftpConfig.host = settings_.consoleAddress;
	ftpConfig.port = settings_.ftpPort;
	ftpConfig.maxConnections = settings_.ftpMaxConnections;
	ftpConfig.advancedMode = settings_.ftpAdvancedMode;
	ftp_ = std::make_unique<FtpClient>(ftpConfig);

	InstallQueue::Dependencies deps;
	deps.httpServer = httpServer_.get();
	deps.installer = installer_.get();
	deps.ftp = ftp_.get();
	deps.console = console_.get();
	qInfo("Serviços: fila de instalação");
	queue_ = std::make_unique<InstallQueue>(deps, settings_);
	queue_->load(SettingsStore::defaultQueuePath());
	queue_->setListener([this](const QueueTask &task) {
		const bool terminal = task.isTerminal();
		const QString title = QString::fromStdString(task.title);
		const QString message = QString::fromStdString(task.message);
		const bool failed = task.state == TaskState::Error;
		QMetaObject::invokeMethod(
			this,
			[this, terminal, title, message, failed]() {
				refreshQueueModel();
				emit queueStateChanged();
				if(terminal)
					emit notify(title, message, failed);
			},
			Qt::QueuedConnection);
	});
	queue_->start();
	refreshQueueModel();

	qInfo("Serviços: verificação periódica");
	console_->start(10);
	emit settingsChanged();
	emit statusChanged();
}

QString AppController::consoleName() const { return QString::fromStdString(settings_.consoleName); }

QVariantList AppController::consoles() const
{
	QVariantList lista;
	for(const ConsoleEntry &consola : settings_.consoles)
	{
		QVariantMap entrada;
		entrada[QStringLiteral("name")] = QString::fromStdString(consola.name);
		entrada[QStringLiteral("address")] = QString::fromStdString(consola.address);
		entrada[QStringLiteral("active")] = consola.address == settings_.consoleAddress;
		lista.append(entrada);
	}
	return lista;
}

void AppController::selectConsole(const QString &address)
{
	const std::string endereco = address.trimmed().toStdString();
	if(endereco == settings_.consoleAddress)
		return;
	for(const ConsoleEntry &consola : settings_.consoles)
	{
		if(consola.address != endereco)
			continue;
		settings_.consoleName = consola.name;
		settings_.consoleAddress = consola.address;
		store_.save(settings_);
		// O FTP, o instalador e o servidor HTTP passam a falar com ela.
		rebuildBackends();
		setStatusMessage(tr("A usar %1 (%2).").arg(QString::fromStdString(consola.name),
			QString::fromStdString(consola.address)));
		return;
	}
}

void AppController::addConsole(const QString &name, const QString &address)
{
	const std::string endereco = address.trimmed().toStdString();
	if(endereco.empty())
		return;
	bool existe = false;
	for(const ConsoleEntry &consola : settings_.consoles)
		existe = existe || consola.address == endereco;
	if(!existe)
	{
		std::string nome = name.trimmed().toStdString();
		if(nome.empty())
			nome = endereco;
		settings_.consoles.push_back({ nome, endereco });
	}
	selectConsole(address);
	// selectConsole() não faz nada se ela já estava em uso; a lista mudou
	// na mesma.
	store_.save(settings_);
	emit settingsChanged();
}

void AppController::removeConsole(const QString &address)
{
	const std::string endereco = address.trimmed().toStdString();
	if(endereco == settings_.consoleAddress)
		return;
	auto &lista = settings_.consoles;
	const auto antes = lista.size();
	lista.erase(std::remove_if(lista.begin(), lista.end(),
					[&endereco](const ConsoleEntry &c) { return c.address == endereco; }),
		lista.end());
	if(lista.size() == antes)
		return;
	store_.save(settings_);
	emit settingsChanged();
}

QString AppController::consoleAddress() const
{
	return QString::fromStdString(settings_.consoleAddress);
}

QString AppController::remotePlayState() const { return stateName(status_.remotePlay.state); }
QString AppController::remotePlayHint() const
{
	// Qualquer reconstrução dos serviços repõe o estado em Unknown; sem isto,
	// o indicador diria "Remote Play não integrado" a meio de uma sessão a
	// correr. O estado verdadeiro vem do StreamController.
	if(status_.remotePlay.state == ServiceState::Unknown)
		return tr("Ainda não perguntei à consola. Carrega em Procurar.");
	return QString::fromStdString(status_.remotePlay.hint);
}
QString AppController::ftpState() const { return stateName(status_.ftp.state); }
QString AppController::ftpHint() const
{
	return status_.ftp.state == ServiceState::Available
		? QString::fromStdString(status_.ftp.detail)
		: QString::fromStdString(status_.ftp.hint);
}
QString AppController::installerState() const { return stateName(status_.installer.state); }
QString AppController::installerHint() const
{
	return status_.installer.state == ServiceState::Available
		? QString::fromStdString(status_.installer.detail)
		: QString::fromStdString(status_.installer.hint);
}
bool AppController::canInstallDirectly() const { return status_.canInstallDirectly(); }
bool AppController::canUseFtp() const { return status_.canUseFtp(); }
bool AppController::queuePaused() const { return queue_ && queue_->paused(); }
QString AppController::pauseReason() const
{
	return queue_ ? QString::fromStdString(queue_->pauseReason()) : QString();
}

QString AppController::httpServerAddress() const
{
	if(!httpServer_ || !httpServer_->running())
		return tr("parado");
	return QStringLiteral("http://%1:%2")
		.arg(QString::fromStdString(httpServer_->bindAddress()))
		.arg(httpServer_->port());
}

QStringList AppController::ftpShortcuts() const
{
	QStringList shortcuts;
	for(const std::string &path : FtpClient::shortcutPaths())
		shortcuts << QString::fromStdString(path);
	return shortcuts;
}

QString AppController::version() const { return QCoreApplication::applicationVersion(); }

void AppController::setStatusMessage(const QString &message)
{
	statusMessage_ = message;
	emit statusMessageChanged();
}

void AppController::setFtpBusy(bool busy)
{
	if(ftpBusy_ == busy)
		return;
	ftpBusy_ = busy;
	emit ftpBusyChanged();
}

void AppController::refreshQueueModel()
{
	if(!queue_)
		return;
	// Primeiro as tarefas por fazer/a decorrer, depois o histórico recente.
	std::vector<QueueTask> combined = queue_->tasks();
	const std::vector<QueueTask> history = queue_->history();
	const size_t historyShown = 20;
	for(size_t i = 0; i < history.size() && i < historyShown; ++i)
		combined.push_back(history[i]);
	queueModel_.applySnapshot(combined);
}

QStringList AppController::collectPkgFiles(const QStringList &paths)
{
	QStringList result;
	for(const QString &path : paths)
	{
		const QFileInfo info(path);
		if(info.isDir())
		{
			// Pasta largada: procura .pkg recursivamente (§5.7).
			QDirIterator it(path, QStringList { QStringLiteral("*.pkg") }, QDir::Files,
				QDirIterator::Subdirectories);
			while(it.hasNext())
				result << it.next();
		}
		else if(info.isFile())
			result << info.absoluteFilePath();
	}
	result.sort(Qt::CaseInsensitive);
	return result;
}

void AppController::dropUrls(const QList<QUrl> &urls, int mode)
{
	QStringList paths;
	for(const QUrl &url : urls)
	{
		if(url.isLocalFile())
			paths << url.toLocalFile();
	}
	addPaths(paths, mode);
}

void AppController::registerIcons(const QStringList &paths, const QStringList &taskIds)
{
	// Extrai o ICON0.PNG numa thread de trabalho: é leitura de disco.
	std::thread([this, paths, taskIds]() {
		PkgInspector inspector;
		for(int i = 0; i < paths.size() && i < taskIds.size(); ++i)
		{
			const PkgInfo info = inspector.inspect(paths[i].toStdString());
			if(info.iconPng.empty())
				continue;
			const QByteArray png(reinterpret_cast<const char *>(info.iconPng.data()),
				static_cast<int>(info.iconPng.size()));
			const QString dataUri = QStringLiteral("data:image/png;base64,")
				+ QString::fromLatin1(png.toBase64());
			const QString taskId = taskIds[i];
			QMetaObject::invokeMethod(
				this, [this, taskId, dataUri]() { queueModel_.setIcon(taskId, dataUri); },
				Qt::QueuedConnection);
		}
	}).detach();
}

void AppController::addPaths(const QStringList &paths, int mode)
{
	if(!queue_)
		return;
	const TransferMode transferMode = mode == 1 ? TransferMode::FtpUpload : TransferMode::DirectInstall;
	const QStringList files = collectPkgFiles(paths);
	if(files.isEmpty())
	{
		setStatusMessage(tr("Nenhum ficheiro utilizável foi largado."));
		return;
	}

	std::vector<std::string> nativePaths;
	QStringList accepted;
	for(const QString &file : files)
	{
		if(transferMode == TransferMode::DirectInstall
			&& QFileInfo(file).suffix().toLower() != QStringLiteral("pkg"))
		{
			setStatusMessage(tr("%1 não é um .pkg — só é aceite na zona de FTP.")
					.arg(QFileInfo(file).fileName()));
			continue;
		}
		nativePaths.push_back(file.toStdString());
		accepted << file;
	}
	if(nativePaths.empty())
		return;

	std::vector<std::string> rejected;
	const std::vector<std::string> ids = queue_->enqueue(nativePaths, transferMode, &rejected);

	QStringList taskIds;
	for(const std::string &id : ids)
		taskIds << QString::fromStdString(id);
	registerIcons(accepted, taskIds);

	refreshQueueModel();
	if(!rejected.empty())
		setStatusMessage(QString::fromStdString(rejected.front()));
	else
		setStatusMessage(tr("%n ficheiro(s) na fila.", "", static_cast<int>(ids.size())));
}

void AppController::checkServicesNow()
{
	if(!console_)
		return;
	std::thread([this]() { console_->checkNow(); }).detach();
}

void AppController::cancelTask(const QString &id)
{
	if(queue_ && queue_->cancel(id.toStdString()))
		refreshQueueModel();
}

void AppController::retryTask(const QString &id)
{
	if(queue_ && queue_->retry(id.toStdString()))
		refreshQueueModel();
}

void AppController::removeTask(const QString &id)
{
	if(queue_ && queue_->remove(id.toStdString()))
		refreshQueueModel();
}

void AppController::moveTaskUp(const QString &id)
{
	if(queue_ && queue_->moveUp(id.toStdString()))
		refreshQueueModel();
}

void AppController::moveTaskDown(const QString &id)
{
	if(queue_ && queue_->moveDown(id.toStdString()))
		refreshQueueModel();
}

void AppController::pauseQueue()
{
	if(!queue_)
		return;
	queue_->pause(tr("Pausado pelo utilizador.").toStdString());
	emit queueStateChanged();
}

void AppController::resumeQueue()
{
	if(!queue_)
		return;
	queue_->resume();
	emit queueStateChanged();
}

bool AppController::ftpReady(const QString &operacao)
{
	// Cada operação diz sempre porque é que não pode avançar: um botão que
	// não faz nada e não diz nada parece partido.
	if(!ftp_)
	{
		emit notify(operacao,
			tr("O FTP não está ligado. Confirma o IP da consola e que o servidor FTP do "
			   "GoldHEN está a correr."),
			true);
		return false;
	}
	if(ftpBusy_)
	{
		emit notify(operacao, tr("O FTP está ocupado com outra operação. Espera que acabe."),
			true);
		return false;
	}
	if(status_.ftp.state == ServiceState::Unavailable)
	{
		emit notify(operacao,
			tr("A consola não responde no FTP: %1").arg(QString::fromStdString(status_.ftp.hint)),
			true);
		return false;
	}
	return true;
}

void AppController::ftpNavigate(const QString &path)
{
	ftpPath_ = QString::fromStdString(normalizeRemotePath(path.toStdString()));
	emit ftpPathChanged();
	ftpRefresh();
}

void AppController::ftpUp()
{
	ftpNavigate(ftpPath_ + QStringLiteral("/.."));
}

void AppController::ftpRefresh()
{
	// A actualização é a única que não se queixa: acontece sozinha depois
	// de outras operações, e um aviso a cada uma seria ruído.
	if(!ftp_ || ftpBusy_)
		return;
	setFtpBusy(true);
	const std::string path = ftpPath_.toStdString();
	std::thread([this, path]() {
		std::vector<FtpEntry> entries;
		const FtpResult result = ftp_->list(path, &entries);
		const QString error = QString::fromStdString(result.message);
		QMetaObject::invokeMethod(
			this,
			[this, entries, result, error]() {
				if(result.ok)
				{
					ftpModel_.setEntries(entries);
					setStatusMessage(tr("%1: %2 entradas").arg(ftpPath_).arg(entries.size()));
				}
				else
				{
					ftpModel_.clear();
					setStatusMessage(tr("FTP: %1").arg(error));
					emit notify(tr("FTP"),
						tr("Não consegui listar %1: %2").arg(ftpPath_).arg(error), true);
				}
				setFtpBusy(false);
			},
			Qt::QueuedConnection);
	}).detach();
}

void AppController::ftpDelete(const QString &path, bool isDirectory)
{
	if(!ftpReady(tr("Apagar")))
		return;
	setFtpBusy(true);
	const std::string target = path.toStdString();
	std::thread([this, target, isDirectory]() {
		const FtpResult result = isDirectory ? ftp_->removeDirectory(target) : ftp_->removeFile(target);
		const QString message = QString::fromStdString(result.message);
		const bool ok = result.ok;
		QMetaObject::invokeMethod(
			this,
			[this, ok, message]() {
				setFtpBusy(false);
				setStatusMessage(ok ? tr("Apagado.") : tr("FTP: %1").arg(message));
				// Apagar é destrutivo: confirma-se sempre, deu ou não deu.
				emit notify(tr("Apagar"),
					ok ? tr("Apagado da consola.") : tr("Não consegui apagar: %1").arg(message),
					!ok);
				if(ok)
					ftpRefresh();
			},
			Qt::QueuedConnection);
	}).detach();
}

void AppController::ftpMakeDirectory(const QString &name)
{
	if(name.trimmed().isEmpty())
	{
		emit notify(tr("Criar pasta"), tr("Escreve primeiro o nome da pasta."), true);
		return;
	}
	if(!ftpReady(tr("Criar pasta")))
		return;
	setFtpBusy(true);
	const std::string target =
		normalizeRemotePath((ftpPath_ + "/" + name.trimmed()).toStdString());
	std::thread([this, target]() {
		const FtpResult result = ftp_->makeDirectory(target);
		const QString message = QString::fromStdString(result.message);
		const bool ok = result.ok;
		QMetaObject::invokeMethod(
			this,
			[this, ok, message]() {
				setFtpBusy(false);
				setStatusMessage(ok ? tr("Pasta criada.") : tr("FTP: %1").arg(message));
				emit notify(tr("Criar pasta"),
					ok ? tr("Pasta criada.") : tr("Não consegui criar a pasta: %1").arg(message),
					!ok);
				if(ok)
					ftpRefresh();
			},
			Qt::QueuedConnection);
	}).detach();
}

void AppController::ftpRename(const QString &path, const QString &newName)
{
	const QString trimmed = newName.trimmed();
	if(trimmed.isEmpty() || path.isEmpty())
	{
		emit notify(tr("Mudar o nome"), tr("Escreve o nome novo."), true);
		return;
	}
	// O nome novo fica na mesma pasta: não se muda um ficheiro de sítio por
	// engano ao escrever uma barra.
	if(trimmed.contains(QLatin1Char('/')) || trimmed.contains(QLatin1Char('\\')))
	{
		setStatusMessage(tr("O nome não pode conter barras."));
		emit notify(tr("Mudar o nome"), tr("O nome não pode conter barras."), true);
		return;
	}
	if(!ftpReady(tr("Mudar o nome")))
		return;
	const std::string from = path.toStdString();
	const std::string to = normalizeRemotePath(from + "/../" + trimmed.toStdString());
	setFtpBusy(true);
	std::thread([this, from, to]() {
		const FtpResult result = ftp_->rename(from, to);
		const QString message = QString::fromStdString(result.message);
		const bool ok = result.ok;
		QMetaObject::invokeMethod(
			this,
			[this, ok, message]() {
				setFtpBusy(false);
				setStatusMessage(ok ? tr("Nome mudado.") : tr("FTP: %1").arg(message));
				if(!ok)
					emit notify(tr("Mudar o nome"),
						tr("Não consegui mudar o nome: %1").arg(message), true);
				if(ok)
					ftpRefresh();
			},
			Qt::QueuedConnection);
	}).detach();
}

QString AppController::uniqueLocalPath(const QString &wanted)
{
	if(!QFileInfo::exists(wanted))
		return wanted;
	// Não se apaga o que já lá está: "jogo.pkg" passa a "jogo (2).pkg".
	const QFileInfo info(wanted);
	const QString dir = info.path();
	const QString base = info.completeBaseName();
	const QString suffix = info.suffix().isEmpty() ? QString() : "." + info.suffix();
	for(int n = 2; n < 1000; ++n)
	{
		const QString candidate = QDir(dir).filePath(QStringLiteral("%1 (%2)%3")
				.arg(base).arg(n).arg(suffix));
		if(!QFileInfo::exists(candidate))
			return candidate;
	}
	return wanted;
}

QString AppController::defaultDownloadDirectory() const
{
	QString dir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
	if(dir.isEmpty() || !QDir(dir).exists())
		dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
	if(dir.isEmpty() || !QDir(dir).exists())
		dir = QDir::homePath();
	return dir;
}

QString AppController::dragCacheDirectory()
{
	const QString base = QString::fromStdString(SettingsStore::defaultDirectory());
	return QDir(base).filePath(QStringLiteral("cache/ftp"));
}

QString AppController::cachePathFor(const QString &remotePath)
{
	const QString name = QString::fromStdString(baseName(remotePath.toStdString()));
	if(name.isEmpty())
		return {};
	// Uma subpasta por caminho remoto: dois ficheiros com o mesmo nome em
	// pastas diferentes da consola não podem partilhar a cópia local.
	const QString parent = QString::fromStdString(
		normalizeRemotePath(remotePath.toStdString() + "/.."));
	const QByteArray digest =
		QCryptographicHash::hash(parent.toUtf8(), QCryptographicHash::Sha1).toHex().left(10);
	return QDir(QDir(dragCacheDirectory()).filePath(QString::fromLatin1(digest))).filePath(name);
}

QString AppController::cachedFileUrl(const QString &remotePath, qint64 size) const
{
	const QString cached = cachePathFor(remotePath);
	if(cached.isEmpty())
		return {};
	const QFileInfo info(cached);
	// Só serve se estiver inteiro: um tamanho diferente é uma transferência
	// interrompida e não deve ser entregue ao explorador de ficheiros.
	if(!info.exists() || (size > 0 && info.size() != size))
		return {};
	return QUrl::fromLocalFile(info.absoluteFilePath()).toString();
}

void AppController::ftpDownload(const QString &remotePath, const QString &name,
	const QString &destinationDir)
{
	QString dir = destinationDir;
	if(dir.startsWith(QStringLiteral("file:")))
		dir = QUrl(dir).toLocalFile();
	if(dir.trimmed().isEmpty())
		dir = defaultDownloadDirectory();
	QDir().mkpath(dir);
	startDownload(remotePath, name, uniqueLocalPath(QDir(dir).filePath(name)), false);
}

void AppController::ftpPrepareForDrag(const QString &remotePath, const QString &name, qint64 size)
{
	const QString cached = cachedFileUrl(remotePath, size);
	if(!cached.isEmpty())
	{
		emit dragFileReady(remotePath, cached);
		return;
	}
	const QString target = cachePathFor(remotePath);
	if(target.isEmpty())
		return;
	QDir().mkpath(QFileInfo(target).path());
	startDownload(remotePath, name, target, true);
}

void AppController::startDownload(const QString &remotePath, const QString &name,
	const QString &localPath, bool forDrag)
{
	if(downloadActive_)
	{
		setStatusMessage(tr("Já há uma transferência a decorrer."));
		emit notify(tr("Trazer para o PC"),
			tr("Já há uma transferência a decorrer (%1). Espera que acabe.").arg(downloadName_),
			true);
		return;
	}
	if(!ftp_)
	{
		emit notify(tr("Trazer para o PC"),
			tr("O FTP não está ligado. Confirma o IP da consola e o servidor FTP do GoldHEN."),
			true);
		return;
	}

	downloadCancel_.store(false);
	downloadActive_ = true;
	downloadName_ = name;
	downloadProgress_ = 0.0;
	emit downloadChanged();
	setStatusMessage(tr("A transferir %1…").arg(name));

	const std::string remote = remotePath.toStdString();
	const std::string local = localPath.toStdString();
	std::thread([this, remote, local, remotePath, localPath, name, forDrag]() {
		int64_t lastReported = -1;
		const FtpResult result = ftp_->download(
			remote, local,
			[this, &lastReported](int64_t done, int64_t total) {
				if(downloadCancel_.load())
					return false;
				// A UI só é acordada a cada ponto percentual: numa
				// transferência de gigabytes isto é chamado milhares de vezes.
				const int64_t percent = total > 0 ? (done * 100) / total : 0;
				if(percent != lastReported)
				{
					lastReported = percent;
					const double fraction = total > 0 ? double(done) / double(total) : 0.0;
					QMetaObject::invokeMethod(
						this,
						[this, fraction]() {
							downloadProgress_ = fraction;
							emit downloadChanged();
						},
						Qt::QueuedConnection);
				}
				return true;
			});

		const bool ok = result.ok;
		const bool cancelled = result.cancelled || downloadCancel_.load();
		const QString message = QString::fromStdString(result.message);
		QMetaObject::invokeMethod(
			this,
			[this, ok, cancelled, message, name, remotePath, localPath, forDrag]() {
				downloadActive_ = false;
				downloadProgress_ = ok ? 1.0 : 0.0;
				emit downloadChanged();
				if(cancelled)
				{
					QFile::remove(localPath);
					setStatusMessage(tr("Transferência cancelada."));
					return;
				}
				if(!ok)
				{
					QFile::remove(localPath);
					setStatusMessage(tr("FTP: %1").arg(message));
					emit notify(tr("Transferência falhou"), message, true);
					return;
				}
				if(forDrag)
				{
					setStatusMessage(tr("%1 pronto para arrastar.").arg(name));
					emit dragFileReady(remotePath, QUrl::fromLocalFile(localPath).toString());
				}
				else
				{
					setStatusMessage(tr("%1 guardado em %2").arg(name, QFileInfo(localPath).path()));
					emit notify(tr("Transferência concluída"),
						tr("%1 guardado em %2").arg(name, QFileInfo(localPath).path()), false);
				}
			},
			Qt::QueuedConnection);
	}).detach();
}

void AppController::cancelDownload()
{
	if(!downloadActive_)
		return;
	// Basta a callback de progresso devolver false: aborta só esta
	// transferência. FtpClient::cancel() é para o cliente inteiro e mataria
	// também um envio da fila a decorrer ao mesmo tempo.
	downloadCancel_.store(true);
}

void AppController::openLocalFolder(const QString &path) const
{
	QString target = path;
	if(target.startsWith(QStringLiteral("file:")))
		target = QUrl(target).toLocalFile();
	if(target.isEmpty())
		target = defaultDownloadDirectory();
	const QFileInfo info(target);
	QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() ? info.absoluteFilePath()
															  : info.absolutePath()));
}

void AppController::copyToClipboard(const QString &text) const
{
	if(QClipboard *clipboard = QGuiApplication::clipboard())
		clipboard->setText(text);
}

void AppController::setFtpUploadDirectory(const QString &path)
{
	settings_.ftpUploadDirectory = normalizeRemotePath(path.toStdString());
	store_.save(settings_);
	rebuildBackends();
	emit settingsChanged();
	setStatusMessage(tr("Envios por FTP passam a ir para %1")
			.arg(QString::fromStdString(settings_.ftpUploadDirectory)));
}

void AppController::noteDrag(const QString &evento, bool comFicheiros)
{
	if(evento == QLatin1String("entrou"))
	{
		++dragsVistos_;
		if(!comFicheiros)
			++dragsRecusados_;
		logInfo("Arrastar: entrou na janela"
			+ std::string(comFicheiros ? " (com ficheiros)" : " — SEM ficheiros, recusado"));
	}
	else if(evento == QLatin1String("largado"))
	{
		++dragsLargados_;
		logInfo("Arrastar: largado na janela.");
	}
}

QString AppController::dragSummary() const
{
	if(dragsVistos_ == 0)
	{
		return QStringLiteral("nenhum arrasto chegou à janela nesta sessão. Se tentaste "
							  "arrastar e não resultou, o Windows não entregou o evento — a "
							  "causa mais comum é a aplicação estar a correr como "
							  "administrador (ver a linha \"Privilégios\" acima).");
	}
	return QStringLiteral("%1 arrasto(s) vistos, %2 largado(s), %3 recusado(s) por não "
						  "trazerem ficheiros.")
		.arg(dragsVistos_).arg(dragsLargados_).arg(dragsRecusados_);
}

void AppController::setAudioProbe(std::function<QString()> probe)
{
	audioProbe_ = std::move(probe);
}

QString AppController::audioProbe() const
{
	return audioProbe_ ? audioProbe_() : QString();
}

void AppController::setVideoProbe(std::function<QString()> probe)
{
	videoProbe_ = std::move(probe);
}

QString AppController::videoProbe() const
{
	return videoProbe_ ? videoProbe_() : QString();
}

void AppController::reportRemotePlayState(const QString &state, const QString &detail)
{
	// Guardado para sobreviver a um rebuildBackends(): o ConsoleManager é
	// criado de novo e não sabe nada do Remote Play, que é vigiado por
	// outro caminho. Sem isto o indicador apagar-se-ia sozinho de vez
	// em quando, a meio de uma sessão.
	lastRemotePlayState_ = state;
	lastRemotePlayDetail_ = detail;
	if(!console_)
		return;
	RemotePlayState mapped = RemotePlayState::Unknown;
	if(state == QLatin1String("ready"))
		mapped = RemotePlayState::Ready;
	else if(state == QLatin1String("standby"))
		mapped = RemotePlayState::Standby;
	else if(state == QLatin1String("offline"))
		mapped = RemotePlayState::Offline;
	console_->setRemotePlayState(mapped, detail.toStdString());
}

void AppController::probeConsole(const QString &address, int ftpPort, int installerPort)
{
	const std::string host = trim(address.toStdString());
	const auto port = [](int value, uint16_t fallback) -> uint16_t {
		return (value > 0 && value < 65536) ? static_cast<uint16_t>(value) : fallback;
	};
	const uint16_t ftp = port(ftpPort, 2121);
	const uint16_t rpi = port(installerPort, 12800);
	const uint64_t generation = ++probeGeneration_;

	std::thread([this, address, host, ftp, rpi, generation]() {
		// Tempo curto: isto corre enquanto se escreve, não pode arrastar-se.
		const ProbeResult probe = probeConsoleServices(host, ftp, rpi, 1200);
		if(generation != probeGeneration_.load())
			return;
		const QString detail =
			QString::fromStdString(probe.ftpOk ? probe.ftpDetail : probe.installerDetail);
		QMetaObject::invokeMethod(
			this,
			[this, address, probe, detail, generation]() {
				if(generation != probeGeneration_.load())
					return;
				emit consoleProbed(address, probe.ftpOk, probe.installerOk, detail);
			},
			Qt::QueuedConnection);
	}).detach();
}

void AppController::rememberAccountId(const QString &accountId)
{
	const std::string valor = trim(accountId.toStdString());
	if(settings_.streamAccountId == valor)
		return;
	settings_.streamAccountId = valor;
	store_.save(settings_);
	emit settingsChanged();
}

QStringList AppController::recentLog(int lines) const
{
	QStringList out;
	const std::vector<std::string> linhas =
		Logger::instance().recent(lines > 0 ? static_cast<size_t>(lines) : 0);
	out.reserve(static_cast<int>(linhas.size()));
	for(const std::string &linha : linhas)
		out << QString::fromStdString(linha);
	return out;
}

QString AppController::diagnosticsReport() const
{
	return Diagnostics::report(const_cast<AppController *>(this));
}

QString AppController::exportDiagnostics(const QString &directory)
{
	QString erro;
	const QString caminho = Diagnostics::write(this, directory, &erro);
	if(caminho.isEmpty())
	{
		setStatusMessage(tr("Não consegui escrever o diagnóstico: %1").arg(erro));
		emit notify(tr("Diagnóstico"), erro, true);
		return {};
	}
	setStatusMessage(tr("Diagnóstico guardado em %1").arg(caminho));
	emit notify(tr("Diagnóstico"), tr("Guardado em %1").arg(caminho), false);
	return caminho;
}

void AppController::copyDiagnosticsToClipboard()
{
	copyToClipboard(diagnosticsReport());
	setStatusMessage(tr("Diagnóstico copiado."));
}

QString AppController::logFilePath() const
{
	return QString::fromStdString(SettingsStore::defaultLogPath());
}

void AppController::setStreamVerbose(bool verbose)
{
	streamVerbose_ = verbose;
#ifdef ORBISLINK_HAS_STREAM
	// O registo detalhado do chiaki é muito falador; só se liga quando
	// alguém está mesmo a diagnosticar.
	setChiakiVerbose(verbose);
#endif
	Logger::instance().setLevel(verbose || settings_.debugLogging ? LogLevel::Debug
																  : LogLevel::Info);
	// Com o registo detalhado ligado, guardam-se mais linhas em memória:
	// o handshake do Remote Play sozinho enche as 500 do costume.
	Logger::instance().setRecentCapacity(verbose ? 4000 : 500);
	logInfo(verbose ? "Registo detalhado do Remote Play ligado."
					: "Registo detalhado do Remote Play desligado.");
}


// ───────────────────────────────── actualizações

void AppController::setUpdateState(const QString &state, const QString &message)
{
	updateState_ = state;
	updateMessage_ = message;
	emit updateChanged();
}

void AppController::checkForUpdatesNow(bool silentWhenUpToDate)
{
	if(updateBusy_.exchange(true))
		return;

	UpdateChecker::Config config;
	config.repository = settings_.updateRepository;
	config.channel = updateChannelFromName(settings_.updateChannel, UpdateChannel::Stable);
	config.currentVersion = version().toStdString();
	config.assetSuffix = platformAssetSuffix();

	setUpdateState(QStringLiteral("a-verificar"), tr("A procurar versões novas…"));

	std::thread([this, config, silentWhenUpToDate]() {
		const UpdateCheckResult result = UpdateChecker(config).check();
		QMetaObject::invokeMethod(
			this,
			[this, result, silentWhenUpToDate]() {
				updateBusy_.store(false);
				const QString mensagem = QString::fromStdString(result.message);
				if(!result.ok)
				{
					setUpdateState(QStringLiteral("erro"), mensagem);
					if(!silentWhenUpToDate)
						emit notify(tr("Actualizações"), mensagem, true);
					return;
				}
				if(!result.updateAvailable)
				{
					updateVersion_.clear();
					updateAssetUrl_.clear();
					setUpdateState(QStringLiteral("sem-novidades"), mensagem);
					if(!silentWhenUpToDate)
						emit notify(tr("Actualizações"), mensagem, false);
					return;
				}
				updateVersion_ = QString::fromStdString(result.release.version().toString());
				updateNotes_ = QString::fromStdString(result.release.notes);
				updatePageUrl_ = QString::fromStdString(result.release.pageUrl);
				updateAssetUrl_ = QString::fromStdString(result.release.assetUrl);
				updateAssetName_ = QString::fromStdString(result.release.assetName);
				updateAssetSha256_ = QString::fromStdString(result.release.assetSha256);
				updateAssetSha256Url_ = QString::fromStdString(result.release.assetSha256Url);
				updateAssetSize_ = result.release.assetSize;
				updateProgress_ = 0.0;
				setUpdateState(QStringLiteral("disponivel"), mensagem);
				emit updateAvailable(updateVersion_);
			},
			Qt::QueuedConnection);
	}).detach();
}

void AppController::openUpdatePage() const
{
	if(!updatePageUrl_.isEmpty())
		QDesktopServices::openUrl(QUrl(updatePageUrl_));
}

void AppController::dismissUpdate()
{
	// Não esquece que há uma versão nova — só deixa de a mostrar à frente.
	setUpdateState(QStringLiteral("disponivel"), updateMessage_);
}

void AppController::loadDemoUpdate()
{
	static const char *exemplo = R"([{
	  "tag_name": "v0.1.9",
	  "name": "v0.1.9",
	  "body": "Remote Play confirmado numa PS4 real.\n\n- Descodificacao por hardware em Windows\n- Ecra inteiro com F11\n- Vibracao no comando\n- Assistente de primeira utilizacao\n\nOrbisLink-0.1.9-setup.exe\n",
	  "html_url": "https://github.com/exemplo/orbislink/releases/tag/v0.1.9",
	  "draft": false, "prerelease": false,
	  "assets": [{ "name": "OrbisLink-0.1.9-setup.exe",
	    "browser_download_url": "https://exemplo/OrbisLink-0.1.9-setup.exe", "size": 48234496 }]
	}])";
	const auto releases = UpdateChecker::parseReleases(exemplo, "-setup.exe");
	if(releases.empty())
		return;
	const ReleaseInfo &info = releases.front();
	updateVersion_ = QString::fromStdString(info.version().toString());
	updateNotes_ = QString::fromStdString(info.notes);
	updatePageUrl_ = QString::fromStdString(info.pageUrl);
	updateAssetUrl_ = QString::fromStdString(info.assetUrl);
	updateAssetName_ = QString::fromStdString(info.assetName);
	updateAssetSize_ = info.assetSize;
	setUpdateState(QStringLiteral("disponivel"),
		tr("Há uma versão nova: %1.").arg(updateVersion_));
}

void AppController::installUpdate()
{
	if(updateAssetUrl_.isEmpty())
	{
		// Sem instalador para esta plataforma, o melhor que se pode fazer é
		// levar lá a pessoa.
		openUpdatePage();
		return;
	}
	if(updateBusy_.exchange(true))
		return;

	const QString destino = QDir(QDir::tempPath()).filePath(
		updateAssetName_.isEmpty() ? QStringLiteral("orbislink-update.exe") : updateAssetName_);
	const QString url = updateAssetUrl_;
	const QString shaUrl = updateAssetSha256Url_;
	const QString shaEsperado = updateAssetSha256_;

	updateProgress_ = 0.0;
	setUpdateState(QStringLiteral("a-descarregar"), tr("A descarregar %1…").arg(updateAssetName_));

	std::thread([this, url, destino, shaUrl, shaEsperado]() {
		HttpClient client(20000);

		// O hash pode vir num anexo à parte. Vai-se buscar antes de
		// descarregar 80 MB, para não se descobrir no fim que não há nada
		// com que comparar.
		std::string esperado = shaEsperado.toStdString();
		if(esperado.empty() && !shaUrl.isEmpty())
		{
			HttpClient::FetchOptions options;
			const HttpResponse resposta = client.fetch(shaUrl.toStdString(), options);
			if(resposta.transportOk && resposta.status >= 200 && resposta.status < 300)
			{
				// Formato do sha256sum: "<hash>  <nome>".
				const std::string corpo = trim(resposta.body);
				const size_t espaco = corpo.find_first_of(" \t");
				const std::string primeiro =
					espaco == std::string::npos ? corpo : corpo.substr(0, espaco);
				if(primeiro.size() == 64)
					esperado = toLower(primeiro);
			}
		}

		const auto resultado = client.download(
			url.toStdString(), destino.toStdString(),
			[this](int64_t feito, int64_t total) {
				const double fraccao = total > 0
					? static_cast<double>(feito) / static_cast<double>(total)
					: 0.0;
				QMetaObject::invokeMethod(
					this,
					[this, fraccao]() {
						updateProgress_ = fraccao;
						emit updateChanged();
					},
					Qt::QueuedConnection);
				return true;
			});

		QString erro;
		if(!resultado.ok)
			erro = tr("A descarga falhou: %1").arg(QString::fromStdString(resultado.error));
		else if(!esperado.empty())
		{
			const std::string obtido = sha256File(destino.toStdString());
			if(obtido != esperado)
			{
				erro = tr("O ficheiro descarregado não corresponde ao SHA-256 publicado. "
						  "Não vou instalá-lo.");
				logError("SHA-256 do update não bate: esperado " + esperado + ", obtido " + obtido);
				QFile::remove(destino);
			}
		}

		QMetaObject::invokeMethod(
			this,
			[this, destino, erro, esperado]() {
				updateBusy_.store(false);
				if(!erro.isEmpty())
				{
					setUpdateState(QStringLiteral("erro"), erro);
					emit notify(tr("Actualização"), erro, true);
					return;
				}
				if(esperado.empty())
				{
					// Dizer isto é o mínimo: sem hash publicado, a única
					// garantia é o HTTPS.
					logWarning("O lançamento não publicou SHA-256; só o HTTPS garantiu o "
							   "ficheiro.");
				}
				setUpdateState(QStringLiteral("pronto"),
					tr("Descarregado. O instalador vai abrir e a aplicação fecha-se."));
				emit updateChanged();

				// O instalador não pode substituir um executável a correr,
				// por isso arranca-se e sai-se. O UAC aparece aqui, porque o
				// instalador pede elevação — não há como o evitar sem
				// mudar para uma instalação por utilizador.
				if(!QProcess::startDetached(destino, QStringList()))
				{
					setUpdateState(QStringLiteral("erro"),
						tr("Não consegui abrir o instalador em %1.").arg(destino));
					return;
				}
				logInfo("Instalador de actualização lançado; a fechar a aplicação.");
				QTimer::singleShot(500, qApp, &QCoreApplication::quit);
			},
			Qt::QueuedConnection);
	}).detach();
}

QVariantMap AppController::settingsMap() const
{
	QVariantMap map;
	map[QStringLiteral("consoleName")] = QString::fromStdString(settings_.consoleName);
	map[QStringLiteral("consoleAddress")] = QString::fromStdString(settings_.consoleAddress);
	map[QStringLiteral("ftpPort")] = settings_.ftpPort;
	map[QStringLiteral("installerPort")] = settings_.installerPort;
	map[QStringLiteral("httpPort")] = settings_.httpPort;
	map[QStringLiteral("httpBindAddress")] = QString::fromStdString(settings_.httpBindAddress);
	map[QStringLiteral("restrictToConsoleIp")] = settings_.restrictToConsoleIp;
	map[QStringLiteral("defaultMode")] = settings_.defaultMode == TransferMode::FtpUpload ? 1 : 0;
	map[QStringLiteral("ftpUploadDirectory")] = QString::fromStdString(settings_.ftpUploadDirectory);
	map[QStringLiteral("checkAlreadyInstalled")] = settings_.checkAlreadyInstalled;
	map[QStringLiteral("installAfterUpload")] = settings_.installAfterUpload;
	map[QStringLiteral("deleteFromConsoleAfterInstall")] = settings_.deleteFromConsoleAfterInstall;
	map[QStringLiteral("ftpMaxConnections")] = settings_.ftpMaxConnections;
	map[QStringLiteral("ftpAdvancedMode")] = settings_.ftpAdvancedMode;
	map[QStringLiteral("debugLogging")] = settings_.debugLogging;
	map[QStringLiteral("language")] = QString::fromStdString(settings_.language);
	map[QStringLiteral("theme")] = QString::fromStdString(settings_.theme);
	map[QStringLiteral("streamResolution")] = settings_.streamResolution;
	map[QStringLiteral("streamFps")] = settings_.streamFps;
	map[QStringLiteral("streamBitrateKbps")] = settings_.streamBitrateKbps;
	map[QStringLiteral("streamHardwareDecode")] = settings_.streamHardwareDecode;
	map[QStringLiteral("streamFullscreenOnConnect")] = settings_.streamFullscreenOnConnect;
	map[QStringLiteral("streamRumble")] = settings_.streamRumble;
	map[QStringLiteral("streamTouchpadFromMouse")] = settings_.streamTouchpadFromMouse;
	map[QStringLiteral("streamAccountId")] = QString::fromStdString(settings_.streamAccountId);
	map[QStringLiteral("firstRunDone")] = settings_.firstRunDone;
	map[QStringLiteral("checkForUpdates")] = settings_.checkForUpdates;
	map[QStringLiteral("updateRepository")] = QString::fromStdString(settings_.updateRepository);
	map[QStringLiteral("updateChannel")] = QString::fromStdString(settings_.updateChannel);
	return map;
}

void AppController::applySettings(const QVariantMap &values)
{
	auto stringOr = [&](const char *key, const std::string &fallback) {
		return values.contains(QString::fromLatin1(key))
			? values.value(QString::fromLatin1(key)).toString().toStdString()
			: fallback;
	};
	auto intOr = [&](const char *key, int fallback) {
		return values.contains(QString::fromLatin1(key))
			? values.value(QString::fromLatin1(key)).toInt()
			: fallback;
	};
	auto boolOr = [&](const char *key, bool fallback) {
		return values.contains(QString::fromLatin1(key))
			? values.value(QString::fromLatin1(key)).toBool()
			: fallback;
	};

	// Mudar o nome ou o IP nas definições é editar a consola em uso, e não
	// juntar outra à lista: a entrada dela passa a ter os valores novos.
	const std::string enderecoAntigo = settings_.consoleAddress;
	settings_.consoleName = stringOr("consoleName", settings_.consoleName);
	settings_.consoleAddress = stringOr("consoleAddress", settings_.consoleAddress);
	for(ConsoleEntry &consola : settings_.consoles)
	{
		if(consola.address == enderecoAntigo)
		{
			consola.address = trim(settings_.consoleAddress);
			consola.name = settings_.consoleName;
		}
	}
	normaliseConsoles(settings_);
	settings_.ftpPort = static_cast<uint16_t>(intOr("ftpPort", settings_.ftpPort));
	settings_.installerPort = static_cast<uint16_t>(intOr("installerPort", settings_.installerPort));
	settings_.httpPort = static_cast<uint16_t>(intOr("httpPort", settings_.httpPort));
	settings_.httpBindAddress = stringOr("httpBindAddress", settings_.httpBindAddress);
	settings_.restrictToConsoleIp = boolOr("restrictToConsoleIp", settings_.restrictToConsoleIp);
	settings_.defaultMode = intOr("defaultMode", settings_.defaultMode == TransferMode::FtpUpload ? 1 : 0) == 1
		? TransferMode::FtpUpload
		: TransferMode::DirectInstall;
	settings_.ftpUploadDirectory = stringOr("ftpUploadDirectory", settings_.ftpUploadDirectory);
	settings_.checkAlreadyInstalled = boolOr("checkAlreadyInstalled", settings_.checkAlreadyInstalled);
	settings_.installAfterUpload = boolOr("installAfterUpload", settings_.installAfterUpload);
	settings_.deleteFromConsoleAfterInstall =
		boolOr("deleteFromConsoleAfterInstall", settings_.deleteFromConsoleAfterInstall);
	settings_.ftpMaxConnections = intOr("ftpMaxConnections", settings_.ftpMaxConnections);
	settings_.ftpAdvancedMode = boolOr("ftpAdvancedMode", settings_.ftpAdvancedMode);
	settings_.theme = stringOr("theme", settings_.theme);
	settings_.streamResolution = intOr("streamResolution", settings_.streamResolution);
	settings_.streamFps = intOr("streamFps", settings_.streamFps);
	settings_.streamBitrateKbps = intOr("streamBitrateKbps", settings_.streamBitrateKbps);
	settings_.streamHardwareDecode =
		boolOr("streamHardwareDecode", settings_.streamHardwareDecode);
	settings_.streamFullscreenOnConnect =
		boolOr("streamFullscreenOnConnect", settings_.streamFullscreenOnConnect);
	settings_.streamRumble = boolOr("streamRumble", settings_.streamRumble);
	settings_.streamTouchpadFromMouse =
		boolOr("streamTouchpadFromMouse", settings_.streamTouchpadFromMouse);
	settings_.streamAccountId = stringOr("streamAccountId", settings_.streamAccountId);
	settings_.debugLogging = boolOr("debugLogging", settings_.debugLogging);
	settings_.language = stringOr("language", settings_.language);
	settings_.firstRunDone = boolOr("firstRunDone", settings_.firstRunDone);
	settings_.checkForUpdates = boolOr("checkForUpdates", settings_.checkForUpdates);
	settings_.updateRepository = stringOr("updateRepository", settings_.updateRepository);
	settings_.updateChannel = stringOr("updateChannel", settings_.updateChannel);

	store_.save(settings_);
	Logger::instance().setLevel(settings_.debugLogging ? LogLevel::Debug : LogLevel::Info);
	rebuildBackends();
	setStatusMessage(tr("Definições guardadas."));
}

void AppController::setTheme(const QString &theme)
{
	const std::string nome = theme.toStdString();
	if(nome != "escuro" && nome != "vidro" && nome != "claro")
		return;
	if(settings_.theme == nome)
		return;
	settings_.theme = nome;
	store_.save(settings_);
	emit settingsChanged();
}

void AppController::saveKeyBindings(const std::map<std::string, int> &bindings)
{
	settings_.keyboardBindings = bindings;
	store_.save(settings_);
	emit settingsChanged();
}

void AppController::saveUpdateSettings(bool checkForUpdates, const QString &repository,
	const QString &channel)
{
	settings_.checkForUpdates = checkForUpdates;
	settings_.updateRepository = repository.trimmed().toStdString();
	settings_.updateChannel = channel == QStringLiteral("testes") ? "testes" : "estavel";
	store_.save(settings_);
}

} // namespace orbislink
