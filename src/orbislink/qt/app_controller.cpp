// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/app_controller.h"

#include "orbislink/qt/diagnostics.h"
#ifdef ORBISLINK_HAS_STREAM
#include "orbislink/qt/translate_message.h"
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
#include <map>
#include <set>

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
	// Markers at each step: if this fails on some machine, the log says
	// which step it stopped at.
	qInfo("Startup: data folder");
	SettingsStore::ensureDirectory(SettingsStore::defaultDirectory());
	qInfo("Startup: reading settings");
	store_.load(&settings_);
	qInfo("Startup: working out the log path");
	const std::string coreLogPath = SettingsStore::defaultLogPath();
	qInfo("Startup: core log at %s", coreLogPath.c_str());
	Logger::instance().setLevel(settings_.debugLogging ? LogLevel::Debug : LogLevel::Info);
	qInfo("Startup: log level set");
	// Writing to the console is useless in a windowed application; the
	// file is enough.
	Logger::instance().setConsoleOutput(false);
	qInfo("Startup: opening the log file");
	if(!Logger::instance().setFile(coreLogPath))
		qWarning("Could not open %s", coreLogPath.c_str());
	qInfo("Startup: log file open");

	// Everything written to the log also goes to the diagnostics window.
	// The message arriving here is already masked.
	Logger::instance().setSink([this](LogLevel level, const std::string &message) {
		const QString levelName = QString::fromLatin1(logLevelName(level));
		const QString lineText = QString::fromStdString(message);
		QMetaObject::invokeMethod(
			this, [this, levelName, lineText]() { emit logLine(levelName, lineText); },
			Qt::QueuedConnection);
	});

	qInfo("Startup: preparing the services");
	rebuildBackends();
	qInfo("Startup: services ready");
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

	qInfo("Services: console manager");
	// The services talk to the console in use, on its FTP port.
	Settings effective = settings_;
	effective.ftpPort = activeFtpPort();
	console_ = std::make_unique<ConsoleManager>(effective);
	// The new manager is born knowing nothing about Remote Play; give it
	// what was already known, otherwise the indicator goes blank mid-session.
	if(!lastRemotePlayState_.isEmpty())
	{
		const QString stateCopy = lastRemotePlayState_;
		const QString detailCopy = lastRemotePlayDetail_;
		QTimer::singleShot(0, this, [this, stateCopy, detailCopy]() {
			reportRemotePlayState(stateCopy, detailCopy);
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

	qInfo("Services: local HTTP server");
	httpServer_ = std::make_unique<LocalHttpServer>();
	LocalHttpServer::Config httpConfig;
	if(settings_.httpBindAddress.empty())
	{
		// Walks the system's network interfaces. If this fails, the server
		// stays on the local address instead of bringing everything down.
		qInfo("Services: looking for the network interface");
		try
		{
			httpConfig.bindAddress = localAddressForConsole(settings_.consoleAddress);
		}
		catch(const std::exception &error)
		{
			qWarning("Could not list the network interfaces: %s", error.what());
		}
		catch(...)
		{
			qWarning("Could not list the network interfaces.");
		}
	}
	else
		httpConfig.bindAddress = settings_.httpBindAddress;
	if(httpConfig.bindAddress.empty())
		httpConfig.bindAddress = "127.0.0.1";
	httpConfig.port = settings_.httpPort;
	httpConfig.allowedClient = settings_.restrictToConsoleIp ? settings_.consoleAddress : std::string();
	qInfo("Services: starting the HTTP server on %s:%u", httpConfig.bindAddress.c_str(),
		static_cast<unsigned>(httpConfig.port));
	std::string httpError;
	if(!httpServer_->start(httpConfig, &httpError))
		setStatusMessage(tr("The local HTTP server did not start: %1").arg(QString::fromStdString(httpError)));

	qInfo("Services: remote installer and FTP");
	RpiClient::Config rpiConfig;
	rpiConfig.host = settings_.consoleAddress;
	rpiConfig.port = settings_.installerPort;
	installer_ = std::make_unique<RpiClient>(rpiConfig);

	FtpClient::Config ftpConfig;
	ftpConfig.host = settings_.consoleAddress;
	ftpConfig.port = activeFtpPort();
	ftpConfig.maxConnections = settings_.ftpMaxConnections;
	ftpConfig.advancedMode = settings_.ftpAdvancedMode;
	ftp_ = std::make_unique<FtpClient>(ftpConfig);

	InstallQueue::Dependencies deps;
	deps.httpServer = httpServer_.get();
	deps.installer = installer_.get();
	deps.ftp = ftp_.get();
	deps.console = console_.get();
	qInfo("Services: install queue");
	queue_ = std::make_unique<InstallQueue>(deps, effective);
	queue_->load(SettingsStore::defaultQueuePath());
	queue_->setListener([this](const QueueTask &task) {
		const bool terminal = task.isTerminal();
		const QString title = QString::fromStdString(task.title);
		const QString message = translateMessage(task.message);
		const bool failed = task.state == TaskState::Error;
		// An upload that reached the console: the folder it went into.
		QString landedIn;
		if(task.mode == TransferMode::FtpUpload && task.state == TaskState::Completed)
		{
			const std::string &path = task.remotePath;
			landedIn = QString::fromStdString(path.substr(0, path.find_last_of('/') + 1));
		}
		QMetaObject::invokeMethod(
			this,
			[this, terminal, title, message, failed, landedIn]() {
				refreshQueueModel();
				emit queueStateChanged();
				if(terminal)
					emit notify(title, message, failed);
				// The file list shows it without anyone pressing refresh.
				if(!landedIn.isEmpty()
					&& normalizeRemotePath(landedIn.toStdString())
						== normalizeRemotePath(ftpPath_.toStdString()))
					refreshFtpListing(false);
			},
			Qt::QueuedConnection);
	});
	queue_->start();
	refreshQueueModel();

	qInfo("Services: periodic check");
	console_->start(10);
	emit settingsChanged();
	emit statusChanged();
}

QString AppController::consoleName() const { return QString::fromStdString(settings_.consoleName); }

QVariantList AppController::consoles() const
{
	QVariantList items;
	for(const ConsoleEntry &console : settings_.consoles)
	{
		QVariantMap input;
		input[QStringLiteral("name")] = QString::fromStdString(console.name);
		input[QStringLiteral("address")] = QString::fromStdString(console.address);
		input[QStringLiteral("active")] = console.address == settings_.consoleAddress;
		input[QStringLiteral("type")] = QString::fromStdString(console.type);
		input[QStringLiteral("hostId")] = QString::fromStdString(console.hostId);
		input[QStringLiteral("startMode")] = console.startMode == "ftp"
			? QStringLiteral("ftp") : QStringLiteral("remoteplay");
		input[QStringLiteral("accountId")] = QString::fromStdString(console.accountId);
		QString accountLabel;
		for(const SavedAccount &account : settings_.accounts)
			if(account.accountId == console.accountId)
				accountLabel = QString::fromStdString(account.label);
		input[QStringLiteral("accountLabel")] = accountLabel;
		items.append(input);
	}
	return items;
}

QVariantList AppController::accounts() const
{
	QVariantList items;
	for(const SavedAccount &account : settings_.accounts)
	{
		QStringList usedBy;
		for(const ConsoleEntry &console : settings_.consoles)
			if(console.accountId == account.accountId)
				usedBy << QString::fromStdString(console.name);
		QVariantMap entry;
		entry[QStringLiteral("label")] = QString::fromStdString(account.label);
		entry[QStringLiteral("accountId")] = QString::fromStdString(account.accountId);
		entry[QStringLiteral("usedBy")] = usedBy;
		items.append(entry);
	}
	return items;
}

bool AppController::saveAccount(const QString &oldAccountId, const QString &label, const QString &accountId)
{
	const std::string from = trim(oldAccountId.toStdString());
	const std::string id = trim(accountId.toStdString());
	std::string name = trim(label.toStdString());
	if(id.empty())
	{
		setStatusMessage(tr("The Account ID is empty or not valid."));
		return false;
	}
	SavedAccount *target = nullptr;
	for(SavedAccount &account : settings_.accounts)
	{
		if(!from.empty() && account.accountId == from)
			target = &account;
		else if(account.accountId == id)
		{
			setStatusMessage(tr("That Account ID is already saved as \"%1\".")
				.arg(QString::fromStdString(account.label)));
			return false;
		}
	}
	if(name.empty())
		name = tr("PSN account %1").arg(settings_.accounts.size() + (target ? 0 : 1)).toStdString();
	if(target)
	{
		target->label = name;
		target->accountId = id;
		// Consoles using the old ID follow the edit.
		for(ConsoleEntry &console : settings_.consoles)
			if(console.accountId == from)
				console.accountId = id;
		if(settings_.streamAccountId == from)
			settings_.streamAccountId = id;
	}
	else
	{
		SavedAccount account;
		account.label = name;
		account.accountId = id;
		settings_.accounts.push_back(account);
	}
	store_.save(settings_);
	emit settingsChanged();
	setStatusMessage(tr("Account ID \"%1\" saved.").arg(QString::fromStdString(name)));
	return true;
}

void AppController::removeAccount(const QString &accountId)
{
	const std::string id = trim(accountId.toStdString());
	auto &items = settings_.accounts;
	const auto before = items.size();
	items.erase(std::remove_if(items.begin(), items.end(),
					[&id](const SavedAccount &a) { return a.accountId == id; }),
		items.end());
	if(items.size() == before)
		return;
	for(ConsoleEntry &console : settings_.consoles)
		if(console.accountId == id)
			console.accountId.clear();
	if(settings_.streamAccountId == id)
		settings_.streamAccountId.clear();
	store_.save(settings_);
	emit settingsChanged();
}

void AppController::setConsoleAccount(const QString &address, const QString &accountId)
{
	const std::string target = address.trimmed().toStdString();
	const std::string id = trim(accountId.toStdString());
	for(ConsoleEntry &console : settings_.consoles)
	{
		if(console.address != target || console.accountId == id)
			continue;
		console.accountId = id;
		store_.save(settings_);
		emit settingsChanged();
		return;
	}
}

void AppController::selectConsole(const QString &address)
{
	const std::string trimmedAddress = address.trimmed().toStdString();
	if(trimmedAddress == settings_.consoleAddress)
		return;
	for(const ConsoleEntry &console : settings_.consoles)
	{
		if(console.address != trimmedAddress)
			continue;
		settings_.consoleName = console.name;
		settings_.consoleAddress = console.address;
		store_.save(settings_);
		// FTP, the installer and the HTTP server now talk to it.
		rebuildBackends();
		setStatusMessage(tr("Using %1 (%2).").arg(QString::fromStdString(console.name),
			QString::fromStdString(console.address)));
		return;
	}
}

void AppController::rememberConsoleType(const QString &address, bool ps5, const QString &hostId)
{
	const std::string trimmedAddress = address.trimmed().toStdString();
	const std::string kind = ps5 ? "ps5" : "ps4";
	const std::string id = hostId.trimmed().toStdString();
	for(ConsoleEntry &console : settings_.consoles)
	{
		if(console.address != trimmedAddress)
			continue;
		if(console.type == kind && (id.empty() || console.hostId == id))
			return;
		console.type = kind;
		if(!id.empty())
			console.hostId = id;
		store_.save(settings_);
		emit settingsChanged();
		emit statusChanged();
		return;
	}
}

bool AppController::updateConsole(const QString &oldAddress, const QString &name, const QString &address)
{
	const std::string from = oldAddress.trimmed().toStdString();
	const std::string to = address.trimmed().toStdString();
	const std::string newName = name.trimmed().toStdString();
	if(to.empty())
	{
		setStatusMessage(tr("The IP address cannot be empty."));
		return false;
	}
	ConsoleEntry *target = nullptr;
	for(ConsoleEntry &console : settings_.consoles)
	{
		if(console.address == from)
			target = &console;
		else if(console.address == to)
		{
			setStatusMessage(tr("Another console in the list already uses %1.").arg(address.trimmed()));
			return false;
		}
	}
	if(!target)
		return false;
	const bool addressChanged = target->address != to;
	target->name = newName.empty() ? to : newName;
	target->address = to;
	// A new address may be a different console: what was learned about
	// the old one (type, host-id) no longer applies.
	if(addressChanged)
	{
		target->type.clear();
		target->hostId.clear();
	}
	const bool active = from == settings_.consoleAddress;
	if(active)
	{
		settings_.consoleName = target->name;
		settings_.consoleAddress = target->address;
	}
	store_.save(settings_);
	if(active && addressChanged)
		rebuildBackends();
	emit settingsChanged();
	emit statusChanged();
	setStatusMessage(tr("Console %1 saved.").arg(QString::fromStdString(target->name)));
	return true;
}

void AppController::addConsole(const QString &name, const QString &address, const QString &type,
	bool select, const QString &startMode)
{
	const std::string trimmedAddress = address.trimmed().toStdString();
	if(trimmedAddress.empty())
		return;
	bool alreadyListed = false;
	for(const ConsoleEntry &console : settings_.consoles)
		alreadyListed = alreadyListed || console.address == trimmedAddress;
	if(!alreadyListed)
	{
		std::string entryName = name.trimmed().toStdString();
		if(entryName.empty())
			entryName = trimmedAddress;
		const std::string kind = type == QStringLiteral("ps5") ? "ps5"
			: type == QStringLiteral("ps4") ? "ps4" : "";
		ConsoleEntry entry;
		entry.name = entryName;
		entry.address = trimmedAddress;
		entry.type = kind;
		entry.startMode = startMode == QStringLiteral("ftp") ? "ftp" : "";
		settings_.consoles.push_back(entry);
	}
	if(select)
		selectConsole(address);
	// selectConsole() does nothing if it was already in use; the list
	// changed anyway.
	store_.save(settings_);
	emit settingsChanged();
}

void AppController::setConsoleStartMode(const QString &address, const QString &startMode)
{
	const std::string trimmedAddress = address.trimmed().toStdString();
	const std::string mode = startMode == QStringLiteral("ftp") ? "ftp" : "";
	for(ConsoleEntry &console : settings_.consoles)
	{
		if(console.address != trimmedAddress || console.startMode == mode)
			continue;
		console.startMode = mode;
		store_.save(settings_);
		emit settingsChanged();
		return;
	}
}

void AppController::probeFtp(const QStringList &addresses)
{
	if(probingFtp_ || addresses.isEmpty())
		return;
	probingFtp_ = true;
	std::vector<std::pair<std::string, uint16_t>> targets;
	for(const QString &address : addresses)
	{
		const std::string trimmedAddress = address.trimmed().toStdString();
		uint16_t port = settings_.ftpPort;
		for(const ConsoleEntry &console : settings_.consoles)
			if(console.address == trimmedAddress && console.type == "ps5")
				port = settings_.ftpPortPs5;
		targets.emplace_back(trimmedAddress, port);
	}
	std::thread([this, targets]() {
		QVariantMap answers;
		for(const auto &target : targets)
			answers[QString::fromStdString(target.first)] = tcpProbe(target.first, target.second, 1200);
		QMetaObject::invokeMethod(
			this,
			[this, answers]() {
				probingFtp_ = false;
				QVariantMap merged = ftpReachable_;
				for(auto it = answers.begin(); it != answers.end(); ++it)
					merged[it.key()] = it.value();
				if(merged != ftpReachable_)
				{
					ftpReachable_ = merged;
					emit ftpReachableChanged();
				}
			},
			Qt::QueuedConnection);
	}).detach();
}

void AppController::openFiles()
{
	emit showPanel(QStringLiteral("files"));
	// Always listed again: after switching console, what is on screen may
	// be the previous console's folder. If FTP does not answer, the listing
	// says so.
	ftpNavigate(uploadDirectory());
}

void AppController::removeConsole(const QString &address)
{
	const std::string trimmedAddress = address.trimmed().toStdString();
	if(trimmedAddress == settings_.consoleAddress)
	{
		// The console in use can go too, as long as another one takes its
		// place: the services always need somewhere to point.
		const ConsoleEntry *next = nullptr;
		for(const ConsoleEntry &console : settings_.consoles)
			if(console.address != trimmedAddress)
			{
				next = &console;
				break;
			}
		if(!next)
		{
			setStatusMessage(tr("This is the only console in the list: add another one before removing it."));
			return;
		}
		selectConsole(QString::fromStdString(next->address));
	}
	auto &items = settings_.consoles;
	const auto before = items.size();
	items.erase(std::remove_if(items.begin(), items.end(),
					[&trimmedAddress](const ConsoleEntry &c) { return c.address == trimmedAddress; }),
		items.end());
	if(items.size() == before)
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
	// Any rebuild of the services resets the state to Unknown; without this,
	// the indicator would say "Remote Play not built in" in the middle of a
	// running session. The real state comes from StreamController.
	if(status_.remotePlay.state == ServiceState::Unknown)
		return tr("I have not asked the console yet. Click its box to search.");
	return translateMessage(status_.remotePlay.hint);
}
std::string AppController::activeAccountId() const
{
	for(const ConsoleEntry &console : settings_.consoles)
		if(console.address == settings_.consoleAddress && !console.accountId.empty())
			return console.accountId;
	return settings_.streamAccountId;
}

bool AppController::activeIsPs5() const
{
	for(const ConsoleEntry &console : settings_.consoles)
		if(console.address == settings_.consoleAddress)
			return console.type == "ps5";
	return false;
}

uint16_t AppController::activeFtpPort() const
{
	return activeIsPs5() ? settings_.ftpPortPs5 : settings_.ftpPort;
}

// On a PS5, FTP and the installer only exist with a jailbreak (etaHEN has
// both, off by default). When they answer they are used as on the PS4;
// when they do not, they are not "broken" — most likely they do not exist —
// and the indicator stays grey explaining what is missing.
QString AppController::ftpState() const
{
	if(activeIsPs5() && status_.ftp.state == ServiceState::Unavailable)
		return QStringLiteral("not-applicable");
	return stateName(status_.ftp.state);
}
QString AppController::ftpHint() const
{
	if(activeIsPs5() && status_.ftp.state != ServiceState::Available)
		return tr("On the PS5, FTP only exists with a jailbreak. In etaHEN turn it on with FTP=1 in "
			"config.ini; it listens on port %1 (change it in the settings if yours differs).")
			.arg(activeFtpPort());
	return status_.ftp.state == ServiceState::Available
		? QString::fromStdString(status_.ftp.detail)
		: translateMessage(status_.ftp.hint);
}
QString AppController::installerState() const
{
	if(activeIsPs5() && status_.installer.state == ServiceState::Unavailable)
		return QStringLiteral("not-applicable");
	return stateName(status_.installer.state);
}
QString AppController::installerHint() const
{
	if(activeIsPs5() && status_.installer.state != ServiceState::Available)
		return tr("On the PS5, installing packages needs a jailbreak with an installer on port %1 — in "
			"etaHEN, DPI v2 (DPI_v2=1 in config.ini).")
			.arg(settings_.installerPort);
	return status_.installer.state == ServiceState::Available
		? QString::fromStdString(status_.installer.detail)
		: translateMessage(status_.installer.hint);
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
		return tr("stopped");
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
	// Pending/running tasks first, then the recent history.
	std::vector<QueueTask> combined = queue_->tasks();
	const std::vector<QueueTask> history = queue_->history();
	const size_t historyShown = 20;
	for(size_t i = 0; i < history.size() && i < historyShown; ++i)
		combined.push_back(history[i]);
	queueModel_.applySnapshot(combined);

	// The uploads still on their way, for the file list.
	QVariantList uploads;
	for(const QueueTask &task : queue_->tasks())
	{
		if(task.mode != TransferMode::FtpUpload || task.isTerminal())
			continue;
		QVariantMap item;
		item[QStringLiteral("name")] = QString::fromStdString(
			sanitizeFileName(task.remoteName.empty() ? task.localPath : task.remoteName));
		item[QStringLiteral("directory")] = task.remotePath.empty()
			? uploadDirectory()
			: QString::fromStdString(normalizeRemotePath(
				task.remotePath.substr(0, task.remotePath.find_last_of('/') + 1)));
		item[QStringLiteral("percent")] = task.totalBytes > 0
			? 100.0 * static_cast<double>(task.doneBytes) / static_cast<double>(task.totalBytes) : 0.0;
		item[QStringLiteral("sending")] = task.state == TaskState::Sending;
		uploads << item;
	}
	if(uploads != ftpUploads_)
	{
		ftpUploads_ = uploads;
		emit ftpUploadsChanged();
	}
}

QString AppController::uploadDirectory() const
{
	return QString::fromStdString(normalizeRemotePath(
		settings_.ftpUploadDirectory.empty() ? std::string("/data/pkg/") : settings_.ftpUploadDirectory));
}

QStringList AppController::collectPkgFiles(const QStringList &paths)
{
	QStringList result;
	for(const QString &path : paths)
	{
		const QFileInfo info(path);
		if(info.isDir())
		{
			// Dropped folder: look for .pkg recursively (§5.7).
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
	// Extracts ICON0.PNG on a worker thread: it is disk reading.
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
	if(activeIsPs5() && !status_.canInstallDirectly() && !status_.canUseFtp())
	{
		emit notify(tr("Install"), installerHint(), true);
		return;
	}
	const TransferMode transferMode = mode == 1 ? TransferMode::FtpUpload : TransferMode::DirectInstall;
	const QStringList files = collectPkgFiles(paths);
	if(files.isEmpty())
	{
		setStatusMessage(tr("None of the dropped files can be used."));
		return;
	}

	std::vector<std::string> nativePaths;
	QStringList accepted;
	for(const QString &file : files)
	{
		if(transferMode == TransferMode::DirectInstall
			&& QFileInfo(file).suffix().toLower() != QStringLiteral("pkg"))
		{
			setStatusMessage(tr("%1 is not a .pkg — only the FTP zone takes it.")
					.arg(QFileInfo(file).fileName()));
			continue;
		}
		nativePaths.push_back(file.toStdString());
		accepted << file;
	}
	if(nativePaths.empty())
		return;

	// Going over FTP: first see what is already in the upload folder.
	if(transferMode == TransferMode::FtpUpload && ftp_)
	{
		checkUploadConflicts(accepted);
		return;
	}
	enqueueFiles(accepted, transferMode);
}

void AppController::enqueueFiles(const QStringList &files, TransferMode transferMode,
	const std::vector<std::string> &remoteNames)
{
	if(!queue_ || files.isEmpty())
		return;
	std::vector<std::string> nativePaths;
	for(const QString &file : files)
		nativePaths.push_back(file.toStdString());
	const QStringList &accepted = files;

	std::vector<std::string> rejected;
	const std::vector<std::string> ids = queue_->enqueue(nativePaths, transferMode, &rejected,
		remoteNames.empty() ? nullptr : &remoteNames);

	QStringList taskIds;
	for(const std::string &id : ids)
		taskIds << QString::fromStdString(id);
	registerIcons(accepted, taskIds);

	refreshQueueModel();
	if(!rejected.empty())
		setStatusMessage(translateMessage(rejected.front()));
	else
		setStatusMessage(tr("%n file(s) queued.", "", static_cast<int>(ids.size())));
	if(ids.empty())
		return;

	// Show where they can be followed: the queue for installs; for uploads,
	// the file list in the folder they are going to.
	if(transferMode == TransferMode::FtpUpload)
	{
		const QString directory = uploadDirectory();
		if(normalizeRemotePath(ftpPath_.toStdString()) != directory.toStdString())
			ftpNavigate(directory);
		emit showPanel(QStringLiteral("files"));
	}
	else
	{
		emit showPanel(QStringLiteral("queue"));
	}
}

namespace {

// "name.pkg" → "name-1.pkg", "name-2.pkg"… the first one not taken.
std::string freeName(const std::string &name, const std::set<std::string> &taken)
{
	const size_t dot = name.find_last_of('.');
	const std::string stem = dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
	const std::string extension = dot == std::string::npos || dot == 0 ? std::string() : name.substr(dot);
	for(int n = 1; n < 10000; ++n)
	{
		const std::string candidate = stem + "-" + std::to_string(n) + extension;
		if(!taken.count(candidate))
			return candidate;
	}
	return stem + "-" + randomToken(4) + extension;
}

} // namespace

void AppController::checkUploadConflicts(const QStringList &files)
{
	const std::string directory = uploadDirectory().toStdString();
	setStatusMessage(tr("Checking what is already on the console…"));
	std::thread([this, files, directory]() {
		std::vector<FtpEntry> entries;
		const FtpResult result = ftp_->list(directory, &entries);
		QMetaObject::invokeMethod(
			this,
			[this, files, entries, result]() {
				// Without a listing there is nothing to compare with: the files
				// go as they always did (the upload itself will report a problem).
				if(!result.ok)
				{
					enqueueFiles(files, TransferMode::FtpUpload);
					return;
				}
				std::set<std::string> taken;
				std::map<std::string, int64_t> sizes;
				for(const FtpEntry &entry : entries)
				{
					taken.insert(entry.name);
					sizes[entry.name] = entry.size;
				}
				// Uploads still in the queue for this folder will land there too.
				const QString directory = uploadDirectory();
				for(const QVariant &value : ftpUploads_)
				{
					const QVariantMap upload = value.toMap();
					if(upload.value(QStringLiteral("directory")).toString() != directory)
						continue;
					const std::string name = upload.value(QStringLiteral("name")).toString().toStdString();
					if(!taken.count(name))
					{
						taken.insert(name);
						sizes[name] = -1;
					}
				}
				QVariantList conflicts;
				std::set<std::string> suggested = taken;
				for(int i = 0; i < files.size(); ++i)
				{
					const std::string name = sanitizeFileName(files[i].toStdString());
					if(!taken.count(name))
						continue;
					const std::string suggestion = freeName(name, suggested);
					suggested.insert(suggestion);
					QVariantMap item;
					item[QStringLiteral("index")] = i;
					item[QStringLiteral("name")] = QString::fromStdString(name);
					item[QStringLiteral("localSize")] = QString::fromStdString(humanBytes(QFileInfo(files[i]).size()));
					// -1: not there yet, but already queued to go there.
					item[QStringLiteral("remoteSize")] = sizes[name] < 0
						? tr("queued") : QString::fromStdString(humanBytes(sizes[name]));
					item[QStringLiteral("suggestion")] = QString::fromStdString(suggestion);
					conflicts << item;
				}
				if(conflicts.isEmpty())
				{
					enqueueFiles(files, TransferMode::FtpUpload);
					return;
				}
				pendingUploads_ = files;
				setStatusMessage(tr("%n file(s) already on the console.", "",
					static_cast<int>(conflicts.size())));
				emit uploadConflicts(conflicts);
			},
			Qt::QueuedConnection);
	}).detach();
}

void AppController::resolveUploadConflicts(const QVariantList &decisions)
{
	const QStringList files = pendingUploads_;
	pendingUploads_.clear();
	std::map<int, QVariantMap> byIndex;
	for(const QVariant &value : decisions)
	{
		const QVariantMap decision = value.toMap();
		byIndex[decision.value(QStringLiteral("index")).toInt()] = decision;
	}
	QStringList chosen;
	std::vector<std::string> remoteNames;
	for(int i = 0; i < files.size(); ++i)
	{
		const auto found = byIndex.find(i);
		std::string remoteName;
		if(found != byIndex.end())
		{
			const QString action = found->second.value(QStringLiteral("action")).toString();
			if(action == QLatin1String("skip"))
				continue;
			if(action == QLatin1String("rename"))
				remoteName = sanitizeFileName(
					found->second.value(QStringLiteral("name")).toString().trimmed().toStdString());
		}
		chosen << files[i];
		remoteNames.push_back(remoteName);
	}
	if(chosen.isEmpty())
	{
		setStatusMessage(tr("Nothing was sent: every file was skipped."));
		return;
	}
	enqueueFiles(chosen, TransferMode::FtpUpload, remoteNames);
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
	queue_->pause(tr("Paused by you.").toStdString());
	emit queueStateChanged();
}

void AppController::resumeQueue()
{
	if(!queue_)
		return;
	queue_->resume();
	emit queueStateChanged();
}

bool AppController::ftpReady(const QString &operation)
{
	// Every operation always says why it cannot go ahead: a button that
	// does nothing and says nothing looks broken.
	if(!ftp_)
	{
		emit notify(operation,
			tr("FTP is not connected. Check the console IP and that the GoldHEN FTP server is running."),
			true);
		return false;
	}
	if(ftpBusy_)
	{
		emit notify(operation, tr("FTP is busy with another operation. Wait for it to finish."),
			true);
		return false;
	}
	if(status_.ftp.state == ServiceState::Unavailable)
	{
		emit notify(operation,
			tr("The console is not answering on FTP: %1").arg(translateMessage(status_.ftp.hint)),
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
	refreshFtpListing(true);
}

void AppController::refreshFtpListing(bool announce)
{
	// Refresh is the only one that does not complain: it happens by itself
	// after other operations, and a notice for each one would be noise.
	if(!ftp_ || ftpBusy_)
		return;
	setFtpBusy(true);
	const std::string path = ftpPath_.toStdString();
	std::thread([this, path, announce]() {
		std::vector<FtpEntry> entries;
		const FtpResult result = ftp_->list(path, &entries);
		const QString error = translateMessage(result.message);
		QMetaObject::invokeMethod(
			this,
			[this, entries, result, error, announce]() {
				if(result.ok)
				{
					ftpModel_.setEntries(entries);
					if(announce)
						setStatusMessage(tr("%1: %2 entries").arg(ftpPath_).arg(entries.size()));
				}
				else if(!announce)
				{
					// A refresh nobody asked for keeps what was on screen.
				}
				else
				{
					ftpModel_.clear();
					setStatusMessage(tr("FTP: %1").arg(error));
					emit notify(tr("FTP"),
						tr("I could not list %1: %2").arg(ftpPath_).arg(error), true);
				}
				setFtpBusy(false);
			},
			Qt::QueuedConnection);
	}).detach();
}

void AppController::ftpDelete(const QString &path, bool isDirectory)
{
	if(!ftpReady(tr("Delete")))
		return;
	setFtpBusy(true);
	const std::string target = path.toStdString();
	std::thread([this, target, isDirectory]() {
		const FtpResult result = isDirectory ? ftp_->removeDirectory(target) : ftp_->removeFile(target);
		const QString message = translateMessage(result.message);
		const bool ok = result.ok;
		QMetaObject::invokeMethod(
			this,
			[this, ok, message]() {
				setFtpBusy(false);
				setStatusMessage(ok ? tr("Deleted.") : tr("FTP: %1").arg(message));
				// Deleting is destructive: always confirm, whether it worked or not.
				emit notify(tr("Delete"),
					ok ? tr("Deleted from the console.") : tr("I could not delete it: %1").arg(message),
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
		emit notify(tr("Create folder"), tr("Type the folder name first."), true);
		return;
	}
	if(!ftpReady(tr("Create folder")))
		return;
	setFtpBusy(true);
	const std::string target =
		normalizeRemotePath((ftpPath_ + "/" + name.trimmed()).toStdString());
	std::thread([this, target]() {
		const FtpResult result = ftp_->makeDirectory(target);
		const QString message = translateMessage(result.message);
		const bool ok = result.ok;
		QMetaObject::invokeMethod(
			this,
			[this, ok, message]() {
				setFtpBusy(false);
				setStatusMessage(ok ? tr("Folder created.") : tr("FTP: %1").arg(message));
				emit notify(tr("Create folder"),
					ok ? tr("Folder created.") : tr("I could not create the folder: %1").arg(message),
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
		emit notify(tr("Rename"), tr("Type the new name."), true);
		return;
	}
	// The new name stays in the same folder: a file is not moved by
	// mistake when typing a slash.
	if(trimmed.contains(QLatin1Char('/')) || trimmed.contains(QLatin1Char('\\')))
	{
		setStatusMessage(tr("The name cannot contain slashes."));
		emit notify(tr("Rename"), tr("The name cannot contain slashes."), true);
		return;
	}
	if(!ftpReady(tr("Rename")))
		return;
	const std::string from = path.toStdString();
	const std::string to = normalizeRemotePath(from + "/../" + trimmed.toStdString());
	setFtpBusy(true);
	std::thread([this, from, to]() {
		const FtpResult result = ftp_->rename(from, to);
		const QString message = translateMessage(result.message);
		const bool ok = result.ok;
		QMetaObject::invokeMethod(
			this,
			[this, ok, message]() {
				setFtpBusy(false);
				setStatusMessage(ok ? tr("Renamed.") : tr("FTP: %1").arg(message));
				if(!ok)
					emit notify(tr("Rename"),
						tr("I could not rename it: %1").arg(message), true);
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
	// What is already there is not overwritten: "game.pkg" becomes "game (2).pkg".
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
	// One subfolder per remote path: two files with the same name in
	// different folders on the console cannot share the local copy.
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
	// Only usable if complete: a different size is an interrupted transfer
	// and must not be handed to the file manager.
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
		setStatusMessage(tr("A download is already running."));
		emit notify(tr("Bring to the PC"),
			tr("A transfer is already running (%1). Wait for it to finish.").arg(downloadName_),
			true);
		return;
	}
	if(!ftp_)
	{
		emit notify(tr("Bring to the PC"),
			tr("FTP is not connected. Check the console IP and the GoldHEN FTP server."),
			true);
		return;
	}

	downloadCancel_.store(false);
	downloadActive_ = true;
	downloadName_ = name;
	downloadProgress_ = 0.0;
	emit downloadChanged();
	setStatusMessage(tr("Downloading %1…").arg(name));

	const std::string remote = remotePath.toStdString();
	const std::string local = localPath.toStdString();
	std::thread([this, remote, local, remotePath, localPath, name, forDrag]() {
		int64_t lastReported = -1;
		const FtpResult result = ftp_->download(
			remote, local,
			[this, &lastReported](int64_t done, int64_t total) {
				if(downloadCancel_.load())
					return false;
				// The UI is only woken on each percentage point: in a
				// gigabyte transfer this is called thousands of times.
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
		const QString message = translateMessage(result.message);
		QMetaObject::invokeMethod(
			this,
			[this, ok, cancelled, message, name, remotePath, localPath, forDrag]() {
				downloadActive_ = false;
				downloadProgress_ = ok ? 1.0 : 0.0;
				emit downloadChanged();
				if(cancelled)
				{
					QFile::remove(localPath);
					setStatusMessage(tr("Download cancelled."));
					return;
				}
				if(!ok)
				{
					QFile::remove(localPath);
					setStatusMessage(tr("FTP: %1").arg(message));
					emit notify(tr("Download failed"), message, true);
					return;
				}
				if(forDrag)
				{
					setStatusMessage(tr("%1 is ready to drag.").arg(name));
					emit dragFileReady(remotePath, QUrl::fromLocalFile(localPath).toString());
				}
				else
				{
					setStatusMessage(tr("%1 saved to %2").arg(name, QFileInfo(localPath).path()));
					emit notify(tr("Download finished"),
						tr("%1 saved to %2").arg(name, QFileInfo(localPath).path()), false);
				}
			},
			Qt::QueuedConnection);
	}).detach();
}

void AppController::cancelDownload()
{
	if(!downloadActive_)
		return;
	// It is enough for the progress callback to return false: it aborts
	// only this transfer. FtpClient::cancel() is for the whole client and
	// would also kill a queue upload running at the same time.
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
	setStatusMessage(tr("FTP uploads will now go to %1")
			.arg(QString::fromStdString(settings_.ftpUploadDirectory)));
}

void AppController::noteDrag(const QString &eventName, bool withFiles)
{
	if(eventName == QLatin1String("entered"))
	{
		++dragsSeen_;
		if(!withFiles)
			++dragsRefused_;
		logInfo("Drag: entered the window"
			+ std::string(withFiles ? " (with files)" : " — NO files, refused"));
	}
	else if(eventName == QLatin1String("dropped"))
	{
		++dragsDropped_;
		logInfo("Drag: dropped on the window.");
	}
}

QString AppController::dragSummary() const
{
	if(dragsSeen_ == 0)
	{
		return QStringLiteral("no drag reached the window in this session. If you tried "
							  "dragging and it did not work, Windows did not deliver the "
							  "event — the most common cause is the app running as "
							  "administrator (see the \"Privileges\" line above).");
	}
	return QStringLiteral("%1 drag(s) seen, %2 dropped, %3 refused for not carrying "
						  "files.")
		.arg(dragsSeen_).arg(dragsDropped_).arg(dragsRefused_);
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
	// Kept to survive a rebuildBackends(): the ConsoleManager is created
	// anew and knows nothing about Remote Play, which is watched through
	// another path. Without this the indicator would go blank by itself
	// now and then, in the middle of a session.
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
		// Short timeout: this runs while typing, it must not drag on.
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
	// Stored on the console in use (the one that just accepted it) and as
	// the last used, which is what shows up when registering a new console.
	const std::string trimmedId = trim(accountId.toStdString());
	bool changed = settings_.streamAccountId != trimmedId;
	settings_.streamAccountId = trimmedId;
	for(ConsoleEntry &console : settings_.consoles)
	{
		if(console.address != settings_.consoleAddress || console.accountId == trimmedId)
			continue;
		console.accountId = trimmedId;
		changed = true;
	}
	// An ID the console just accepted goes into the saved list too.
	const size_t savedBefore = settings_.accounts.size();
	normaliseAccounts(settings_);
	changed = changed || settings_.accounts.size() != savedBefore;
	if(!changed)
		return;
	store_.save(settings_);
	emit settingsChanged();
}

QStringList AppController::recentLog(int lines) const
{
	QStringList out;
	const std::vector<std::string> tail =
		Logger::instance().recent(lines > 0 ? static_cast<size_t>(lines) : 0);
	out.reserve(static_cast<int>(tail.size()));
	for(const std::string &line : tail)
		out << QString::fromStdString(line);
	return out;
}

QString AppController::diagnosticsReport() const
{
	return Diagnostics::report(const_cast<AppController *>(this));
}

QString AppController::exportDiagnostics(const QString &directory)
{
	QString err;
	const QString savedPath = Diagnostics::write(this, directory, &err);
	if(savedPath.isEmpty())
	{
		setStatusMessage(tr("Could not write the diagnostics file: %1").arg(err));
		emit notify(tr("Diagnostics"), err, true);
		return {};
	}
	setStatusMessage(tr("Diagnostics saved to %1").arg(savedPath));
	emit notify(tr("Diagnostics"), tr("Saved to %1").arg(savedPath), false);
	return savedPath;
}

void AppController::copyDiagnosticsToClipboard()
{
	copyToClipboard(diagnosticsReport());
	setStatusMessage(tr("Diagnostics copied."));
}

QString AppController::logFilePath() const
{
	return QString::fromStdString(SettingsStore::defaultLogPath());
}

void AppController::setStreamVerbose(bool verbose)
{
	streamVerbose_ = verbose;
#ifdef ORBISLINK_HAS_STREAM
	// chiaki's verbose log is very chatty; it is only enabled when
	// someone is actually diagnosing.
	setChiakiVerbose(verbose);
#endif
	Logger::instance().setLevel(verbose || settings_.debugLogging ? LogLevel::Debug
																  : LogLevel::Info);
	// With verbose logging on, more lines are kept in memory: the Remote
	// Play handshake alone fills the usual 500.
	Logger::instance().setRecentCapacity(verbose ? 4000 : 500);
	logInfo(verbose ? "Detailed Remote Play log on."
					: "Detailed Remote Play log off.");
}


// ───────────────────────────────── updates

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

	setUpdateState(QStringLiteral("checking"), tr("Looking for new versions…"));

	std::thread([this, config, silentWhenUpToDate]() {
		const UpdateCheckResult result = UpdateChecker(config).check();
		QMetaObject::invokeMethod(
			this,
			[this, result, silentWhenUpToDate]() {
				updateBusy_.store(false);
				const QString text = translateMessage(result.message);
				if(!result.ok)
				{
					setUpdateState(QStringLiteral("error"), text);
					if(!silentWhenUpToDate)
						emit notify(tr("Updates"), text, true);
					return;
				}
				if(!result.updateAvailable)
				{
					updateVersion_.clear();
					updateAssetUrl_.clear();
					setUpdateState(QStringLiteral("up-to-date"), text);
					if(!silentWhenUpToDate)
						emit notify(tr("Updates"), text, false);
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
				setUpdateState(QStringLiteral("available"), text);
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
	// It does not forget there is a new version — it just stops showing it up front.
	setUpdateState(QStringLiteral("available"), updateMessage_);
}

void AppController::loadDemoUpdate()
{
	static const char *sample = R"([{
	  "tag_name": "v0.1.9",
	  "name": "v0.1.9",
	  "body": "Remote Play confirmed on a real PS4.\n\n- Hardware decoding on Windows\n- Full screen with F11\n- Controller rumble\n- First-run wizard\n\nOrbisLink-0.1.9-setup.exe\n",
	  "html_url": "https://github.com/example/orbislink/releases/tag/v0.1.9",
	  "draft": false, "prerelease": false,
	  "assets": [{ "name": "OrbisLink-0.1.9-setup.exe",
	    "browser_download_url": "https://exemplo/OrbisLink-0.1.9-setup.exe", "size": 48234496 }]
	}])";
	const auto releases = UpdateChecker::parseReleases(sample, "-setup.exe");
	if(releases.empty())
		return;
	const ReleaseInfo &info = releases.front();
	updateVersion_ = QString::fromStdString(info.version().toString());
	updateNotes_ = QString::fromStdString(info.notes);
	updatePageUrl_ = QString::fromStdString(info.pageUrl);
	updateAssetUrl_ = QString::fromStdString(info.assetUrl);
	updateAssetName_ = QString::fromStdString(info.assetName);
	updateAssetSize_ = info.assetSize;
	setUpdateState(QStringLiteral("available"),
		tr("There is a new version: %1.").arg(updateVersion_));
}

void AppController::installUpdate()
{
	if(updateAssetUrl_.isEmpty())
	{
		// Without an installer for this platform, the best that can be done
		// is to take the person there.
		openUpdatePage();
		return;
	}
	if(updateBusy_.exchange(true))
		return;

	const QString destination = QDir(QDir::tempPath()).filePath(
		updateAssetName_.isEmpty() ? QStringLiteral("orbislink-update.exe") : updateAssetName_);
	const QString url = updateAssetUrl_;
	const QString shaUrl = updateAssetSha256Url_;
	const QString expectedSha = updateAssetSha256_;

	updateProgress_ = 0.0;
	setUpdateState(QStringLiteral("downloading"), tr("Downloading %1…").arg(updateAssetName_));

	std::thread([this, url, destination, shaUrl, expectedSha]() {
		HttpClient client(20000);

		// The hash may come in a separate asset. It is fetched before
		// downloading 80 MB, so as not to find out at the end that there is
		// nothing to compare against.
		std::string expected = expectedSha.toStdString();
		if(expected.empty() && !shaUrl.isEmpty())
		{
			HttpClient::FetchOptions options;
			const HttpResponse reply = client.fetch(shaUrl.toStdString(), options);
			if(reply.transportOk && reply.status >= 200 && reply.status < 300)
			{
				// sha256sum format: "<hash>  <name>".
				const std::string body = trim(reply.body);
				const size_t space = body.find_first_of(" \t");
				const std::string firstWord =
					space == std::string::npos ? body : body.substr(0, space);
				if(firstWord.size() == 64)
					expected = toLower(firstWord);
			}
		}

		const auto outcome = client.download(
			url.toStdString(), destination.toStdString(),
			[this](int64_t received, int64_t total) {
				const double fraction = total > 0
					? static_cast<double>(received) / static_cast<double>(total)
					: 0.0;
				QMetaObject::invokeMethod(
					this,
					[this, fraction]() {
						updateProgress_ = fraction;
						emit updateChanged();
					},
					Qt::QueuedConnection);
				return true;
			});

		QString err;
		if(!outcome.ok)
			err = tr("The download failed: %1").arg(translateMessage(outcome.error));
		else if(!expected.empty())
		{
			const std::string actual = sha256File(destination.toStdString());
			if(actual != expected)
			{
				err = tr("The downloaded file does not match the published SHA-256. I will not install it.");
				logError("Update SHA-256 does not match: expected " + expected + ", got " + actual);
				QFile::remove(destination);
			}
		}

		QMetaObject::invokeMethod(
			this,
			[this, destination, err, expected]() {
				updateBusy_.store(false);
				if(!err.isEmpty())
				{
					setUpdateState(QStringLiteral("error"), err);
					emit notify(tr("Update"), err, true);
					return;
				}
				if(expected.empty())
				{
					// Saying this is the minimum: without a published hash,
					// the only guarantee is HTTPS.
					logWarning("The release did not publish a SHA-256; only HTTPS vouched for "
							   "the file.");
				}
				setUpdateState(QStringLiteral("ready"),
					tr("Downloaded. The installer will open and the app will close."));
				emit updateChanged();

				// The installer cannot replace a running executable, so it
				// is launched and the app quits. UAC appears here, because
				// the installer asks for elevation — there is no avoiding it
				// without switching to a per-user install.
				if(!QProcess::startDetached(destination, QStringList()))
				{
					setUpdateState(QStringLiteral("error"),
						tr("I could not open the installer at %1.").arg(destination));
					return;
				}
				logInfo("Update installer launched; closing the app.");
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
	// The FTP port shown and edited is the one of the console in use.
	map[QStringLiteral("ftpPort")] = activeFtpPort();
	map[QStringLiteral("consoleIsPs5")] = activeIsPs5();
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
	// The settings show the one for the console in use, which is what goes
	// to it on the next registration.
	map[QStringLiteral("streamAccountId")] = QString::fromStdString(activeAccountId());
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

	// Changing the name or IP in the settings edits the console in use, and
	// does not add another to the list: its entry takes the new values.
	const std::string oldAddress = settings_.consoleAddress;
	settings_.consoleName = stringOr("consoleName", settings_.consoleName);
	settings_.consoleAddress = stringOr("consoleAddress", settings_.consoleAddress);
	for(ConsoleEntry &console : settings_.consoles)
	{
		if(console.address == oldAddress)
		{
			console.address = trim(settings_.consoleAddress);
			console.name = settings_.consoleName;
		}
	}
	normaliseConsoles(settings_);
	if(activeIsPs5())
		settings_.ftpPortPs5 = static_cast<uint16_t>(intOr("ftpPort", settings_.ftpPortPs5));
	else
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
	{
		const std::string before = activeAccountId();
		const std::string after = trim(stringOr("streamAccountId", before));
		if(after != before)
		{
			settings_.streamAccountId = after;
			for(ConsoleEntry &console : settings_.consoles)
				if(console.address == settings_.consoleAddress)
					console.accountId = after;
		}
	}
	normaliseAccounts(settings_);
	settings_.debugLogging = boolOr("debugLogging", settings_.debugLogging);
	settings_.language = stringOr("language", settings_.language);
	settings_.firstRunDone = boolOr("firstRunDone", settings_.firstRunDone);
	settings_.checkForUpdates = boolOr("checkForUpdates", settings_.checkForUpdates);
	settings_.updateRepository = stringOr("updateRepository", settings_.updateRepository);
	settings_.updateChannel = stringOr("updateChannel", settings_.updateChannel);

	store_.save(settings_);
	Logger::instance().setLevel(settings_.debugLogging ? LogLevel::Debug : LogLevel::Info);
	rebuildBackends();
	setStatusMessage(tr("Settings saved."));
}

void AppController::setTheme(const QString &theme)
{
	const std::string themeName = theme.toStdString();
	if(themeName != "dark" && themeName != "glass" && themeName != "light")
		return;
	if(settings_.theme == themeName)
		return;
	settings_.theme = themeName;
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
	settings_.updateChannel = channel == QStringLiteral("testing") ? "testing" : "stable";
	store_.save(settings_);
}

} // namespace orbislink
