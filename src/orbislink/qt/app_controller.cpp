// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/app_controller.h"

#include "orbislink/library/console_library.h"
#include "orbislink/qt/shadowmount_control.h"

#include "orbislink/qt/backdrop.h"
#include "orbislink/qt/diagnostics.h"
#ifdef ORBISLINK_HAS_STREAM
#include "orbislink/qt/translate_message.h"
#include "orbislink/stream/chiaki_log_bridge.h"
#endif

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/ftp/remote_move.h"
#include "orbislink/common/json.h"
#include "orbislink/net/http_client.h"
#include "orbislink/net/lan_scan.h"
#include "orbislink/net/net_utils.h"
#include "orbislink/pkg/pkg_inspector.h"
#include "orbislink/update/sha256.h"
#include "orbislink/update/update_checker.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QColor>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QMimeData>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTimer>
#include <QUrl>
#include <thread>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <map>
#include <set>

namespace orbislink {

namespace {

std::string asciiUpper(std::string text)
{
	for(char &c : text)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return text;
}

} // namespace


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
	prepareBackdrop();
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
	Logger::instance().setSink([this, life = lifeline_](LogLevel level, const std::string &message) {
		const QString levelName = QString::fromLatin1(logLevelName(level));
		const QString lineText = QString::fromStdString(message);
		life->post([this, levelName, lineText]() { emit logLine(levelName, lineText); });
	});

	qInfo("Startup: preparing the services");
	rebuildBackends();
	qInfo("Startup: services ready");
	if(!settings_.ftpUploadDirectory.empty())
		ftpPath_ = QString::fromStdString(settings_.ftpUploadDirectory);

	// Now and then, whether each saved console is still at its address
	// (findMovedConsoles): a first look shortly after starting, then every
	// minute. A look is a knock at each saved address and a read of the
	// neighbour table; the network is only searched when one is missing.
	auto *addressTimer = new QTimer(this);
	addressTimer->setInterval(60 * 1000);
	connect(addressTimer, &QTimer::timeout, this, [this]() { checkConsoleAddresses({}, false, {}); });
	addressTimer->start();
	QTimer::singleShot(20 * 1000, this, [this]() { checkConsoleAddresses({}, false, {}); });
}

AppController::~AppController()
{
	// Threads still running answer to nobody from here on, and the log
	// stops pointing at this object: a line written by a late thread would
	// otherwise post to freed memory and crash the app on its way out.
	lifeline_->cut();
	Logger::instance().setSink(nullptr);
	downloadCancel_->store(true);
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

	if(settings_.consoleAddress != listedConsole_)
	{
		ftpModel_.clear();
		listedConsole_ = settings_.consoleAddress;
		// What the services said was about the console before: until this
		// one is checked, nothing counts as answering, so no page asks its
		// FTP (or its installer) in the meantime.
		const bool hadStatus = status_.ftp.state != ServiceState::Unknown
			|| status_.installer.state != ServiceState::Unknown;
		status_ = ConsoleStatus();
		status_.ftp.state = ServiceState::Checking;
		status_.installer.state = ServiceState::Checking;
		if(hadStatus)
			emit statusChanged();
		// Another console: the firmware it last reported, until its own
		// discovery says it again.
		uint16_t known = 0;
		for(const ConsoleEntry &console : settings_.consoles)
			if(console.address == settings_.consoleAddress)
				known = static_cast<uint16_t>(console.firmware);
		if(consoleFirmware_ != known)
		{
			consoleFirmware_ = known;
			emit consoleFirmwareChanged();
			refreshPackageWarnings();
		}
	}

	qInfo("Services: console manager");
	// The services talk to the console in use, on its FTP port.
	Settings effective = settings_;
	effective.ftpPort = activeFtpPort();
	console_ = std::make_shared<ConsoleManager>(effective);
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
	const uint64_t generation = ++consoleGeneration_;
	console_->setListener([this, life = lifeline_, generation](const ConsoleStatus &status) {
		life->post([this, status, generation]() {
			// Only from the manager of the console in use: a late answer from
			// the one before a console switch speaks of another console.
			if(generation != consoleGeneration_)
				return;
			const bool ftpWas = status_.canUseFtp();
			status_ = status;
			if(status.canUseFtp() && status.ftpPort != 0 && status.ftpPort != activeFtpPort())
				adoptFtpPort(status.ftpPort);
			emit statusChanged();
			resumeWhenInstallerIsBack();
			// "Connects directly" follows FTP coming and going.
			if(ftpWas != status.canUseFtp() && lastRemotePlayState_ == QLatin1String("direct"))
				reportRemotePlayState(lastRemotePlayState_, lastRemotePlayDetail_);
			// FTP has just answered: the file list is read now, instead of
			// staying on what it said while FTP was not there.
			if(!ftpWas && status.canUseFtp())
				refreshFtpListing(false);
			// It stopped answering (the console turned off or went to rest):
			// the list does not keep showing what is no longer reachable.
			else if(ftpWas && !status.canUseFtp())
				refreshFtpListing(false);
		});
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
	rpiConfig.ps5 = activeIsPs5();
	installer_ = std::make_unique<RpiClient>(rpiConfig);

	FtpClient::Config ftpConfig;
	ftpConfig.host = settings_.consoleAddress;
	ftpConfig.port = activeFtpPort();
	ftpConfig.maxConnections = settings_.ftpMaxConnections;
	ftpConfig.advancedMode = settings_.ftpAdvancedMode;
	ftp_ = std::make_shared<FtpClient>(ftpConfig);
	// A transfer of this app keeping the console's FTP busy is not taken
	// for an FTP that stopped answering.
	console_->setFtpAnsweredAt([ftp = ftp_]() { return ftp->lastAnsweredMs(); });

	InstallQueue::Dependencies deps;
	deps.httpServer = httpServer_.get();
	deps.installer = installer_.get();
	deps.ftp = ftp_.get();
	deps.console = console_.get();
	qInfo("Services: install queue");
	queue_ = std::make_unique<InstallQueue>(deps, effective);
	queue_->load(SettingsStore::defaultQueuePath());
	queue_->setListener([this, life = lifeline_](const QueueTask &task) {
		const bool terminal = task.isTerminal();
		const QString title = QString::fromStdString(task.title);
		const QString message = translateMessage(task.message);
		const bool failed = task.state == TaskState::Error;
		// A PS5 game (PPSA) the console installed: followed once, to say
		// what starts it (and Titan Core's progress preparing it).
		const bool ps5Installed = task.state == TaskState::Completed
			&& (task.mode == TransferMode::DirectInstall || task.installedFromConsole)
			&& startsWith(asciiUpper(task.titleId), "PPSA");
		const QString taskId = QString::fromStdString(task.id);
		const QString titleId = QString::fromStdString(task.titleId);
		// An upload that reached the console: the folder it went into.
		QString landedIn;
		// A PS5 game image put in a folder ShadowMountPlus looks in (its
		// first level): it is asked to look now.
		QString imageLanded;
		if(task.mode == TransferMode::FtpUpload && task.state == TaskState::Completed)
		{
			const std::string &path = task.remotePath;
			const std::string folder = path.substr(0, path.find_last_of('/') + 1);
			landedIn = QString::fromStdString(folder);
			const std::string name = path.substr(folder.size());
			if(library::kindOf(name, false, task.totalBytes) == library::Kind::Image
				&& library::mountFolderFor(path) + "/" == folder)
				imageLanded = QString::fromStdString(path);
		}
		life->post(
			[this, terminal, title, message, failed, landedIn, ps5Installed, taskId, titleId, imageLanded]() {
				refreshQueueModel();
				if(ps5Installed && activeIsPs5() && !ps5Followed_.contains(taskId))
				{
					ps5Followed_.insert(taskId);
					followPs5Install(taskId, titleId);
				}
				if(!imageLanded.isEmpty() && activeIsPs5() && !ps5Followed_.contains(taskId))
				{
					ps5Followed_.insert(taskId);
					handImageToShadowMount(taskId, imageLanded);
				}
				emit queueStateChanged();
				// Waiting for the installer: a fresh look at the console
				// decides when to go on.
				const bool waiting = queue_ && queue_->waitingForService();
				if(waiting && !installerRecheckPending_)
					checkServicesNow();
				// Once, as the queue starts waiting, what to do: on a PS4 open
				// Remote Package Installer; on a PS5 the installer is a payload
				// or an etaHEN setting.
				if(waiting && !wasWaiting_ && !canInstallDirectly())
					emit serviceHelp(installerHelpKind(), installerHelpTitle(), installerHelpText());
				wasWaiting_ = waiting;
				if(terminal)
				{
					emit notify(title, message, failed);
					emit jobFinished(title, message, failed);
				}
				// The file list shows it without anyone pressing refresh.
				if(!landedIn.isEmpty()
					&& normalizeRemotePath(landedIn.toStdString())
						== normalizeRemotePath(ftpPath_.toStdString()))
					refreshFtpListing(false);
			});
	});
	queue_->start();
	refreshQueueModel();

	qInfo("Services: periodic check");
	console_->start(10);
	emit settingsChanged();
	emit statusChanged();
	// The pinned folders are the console's kind's: a PS5 keeps things elsewhere.
	emit ftpShortcutsChanged();
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
		input[QStringLiteral("remotePlayPin")] = QString::fromStdString(console.remotePlayPin);
		input[QStringLiteral("connectsDirectly")] = console.connectsDirectly;
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
		entry[QStringLiteral("remotePlayPin")] = QString::fromStdString(account.remotePlayPin);
		entry[QStringLiteral("usedBy")] = usedBy;
		items.append(entry);
	}
	return items;
}

bool AppController::saveAccount(const QString &oldAccountId, const QString &label, const QString &accountId,
	const QString &remotePlayPin)
{
	const std::string from = trim(oldAccountId.toStdString());
	const std::string id = trim(accountId.toStdString());
	std::string name = trim(label.toStdString());
	const std::string pin = trim(remotePlayPin.toStdString());
	if(id.empty())
	{
		setStatusMessage(tr("The Account ID is empty or not valid."));
		return false;
	}
	// The passcode has four digits, the pairing PIN eight.
	if(!pin.empty() && ((pin.size() != 4 && pin.size() != 8)
						   || pin.find_first_not_of("0123456789") != std::string::npos))
	{
		setStatusMessage(tr("The Remote Play PIN is 4 digits (the account's passcode) or 8 (the "
			"pairing PIN the console shows)."));
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
		target->remotePlayPin = pin;
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
		account.remotePlayPin = pin;
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
		// Another console answers at this one's address: the router gave it
		// the address this one had. This one is looked for, instead of
		// taking the newcomer for it.
		if(!id.empty() && !console.hostId.empty()
			&& normaliseHardwareId(console.hostId) != normaliseHardwareId(id))
		{
			checkConsoleAddresses({ address.trimmed() }, true, {});
			return;
		}
		// It answered Remote Play as itself here: its hardware address, from
		// the neighbour table, is kept to find it again if it moves.
		if(!id.empty() && console.mac.empty())
		{
			const auto neighbours = neighbourMacs();
			const auto mac = neighbours.find(trimmedAddress);
			if(mac != neighbours.end())
				learnConsoleMac(trimmedAddress, mac->second);
		}
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
		target->mac.clear();
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

bool AppController::setConsolePin(const QString &address, const QString &pin)
{
	const std::string trimmedAddress = address.trimmed().toStdString();
	const std::string digits = trim(pin.toStdString());
	if(!digits.empty() && ((digits.size() != 4 && digits.size() != 8)
							  || digits.find_first_not_of("0123456789") != std::string::npos))
	{
		setStatusMessage(tr("The console's Remote Play PIN is 8 digits (the one it shows to register this "
			"PC) or 4 (the passcode it asks for when connecting)."));
		return false;
	}
	for(ConsoleEntry &console : settings_.consoles)
	{
		if(console.address != trimmedAddress)
			continue;
		if(console.remotePlayPin != digits)
		{
			console.remotePlayPin = digits;
			store_.save(settings_);
			emit settingsChanged();
		}
		return true;
	}
	return false;
}

// GoldHEN keeps /data/GoldHEN and etaHEN /data/etaHEN; anything else with
// an FTP server is some other HEN.
std::string AppController::detectJailbreak(const std::string &address, uint16_t port,
	const std::string &greeting, std::string *version)
{
	version->clear();
	FtpClient::Config config;
	config.host = address;
	config.port = port;
	config.connectTimeoutSeconds = 3;
	config.idleTimeoutSeconds = 5;
	config.maxRetries = 0;
	FtpClient client(config);
	std::vector<FtpEntry> entries;
	if(!client.list("/data/", &entries).ok)
		return "HEN";
	for(const FtpEntry &entry : entries)
	{
		std::string name = entry.name;
		for(char &c : name)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		for(const char *hen : { "GoldHEN", "etaHEN" })
			if(name == toLower(hen))
			{
				*version = henVersionInGreeting(greeting, hen);
				return hen;
			}
	}
	return "HEN";
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
	const QVariantMap known = jailbreaks_;
	std::thread([this, life = lifeline_, targets, known]() {
		QVariantMap answers;
		QVariantMap found;
		QVariantMap versions;
		for(const auto &target : targets)
		{
			const QString address = QString::fromStdString(target.first);
			// Once per console: which enabler it runs, from the folder each
			// one leaves in /data, and its version if its greeting says it.
			const bool ask = !known.contains(address);
			std::string greeting;
			const bool reachable = tcpProbe(target.first, target.second, 1200, ask ? &greeting : nullptr);
			answers[address] = reachable;
			if(reachable && ask)
			{
				std::string version;
				found[address] = QString::fromStdString(
					detectJailbreak(target.first, target.second, greeting, &version));
				versions[address] = QString::fromStdString(version);
			}
		}
		life->post(
			[this, answers, found, versions]() {
				probingFtp_ = false;
				QVariantMap merged = ftpReachable_;
				for(auto it = answers.begin(); it != answers.end(); ++it)
					merged[it.key()] = it.value();
				QVariantMap jailbreaks = jailbreaks_;
				for(auto it = found.begin(); it != found.end(); ++it)
					jailbreaks[it.key()] = it.value();
				QVariantMap henVersions = henVersions_;
				for(auto it = versions.begin(); it != versions.end(); ++it)
					henVersions[it.key()] = it.value();
				if(merged != ftpReachable_ || jailbreaks != jailbreaks_ || henVersions != henVersions_)
				{
					ftpReachable_ = merged;
					jailbreaks_ = jailbreaks;
					henVersions_ = henVersions;
					emit ftpReachableChanged();
				}
			});
	}).detach();
}

void AppController::openFiles()
{
	emit showPanel(QStringLiteral("files"));
	if(status_.ftp.state == ServiceState::Unavailable)
		emit serviceHelp(QStringLiteral("ftp"), ftpHelpTitle(), ftpHelpText());
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

void AppController::adoptFtpPort(uint16_t port)
{
	const uint16_t before = activeFtpPort();
	(activeIsPs5() ? settings_.ftpPortPs5 : settings_.ftpPort) = port;
	store_.save(settings_);
	// The console manager moved to it already; the FTP client follows. The
	// rest asks for the port each time it connects.
	if(ftp_)
	{
		FtpClient::Config config = ftp_->config();
		config.port = port;
		ftp_->setConfig(config);
	}
	qInfo("FTP: using port %u from now on (it did not answer on %u)", static_cast<unsigned>(port),
		static_cast<unsigned>(before));
	setStatusMessage(tr("FTP answered on port %1, not on %2: OrbisLink uses port %1 from now on.")
						 .arg(port)
						 .arg(before));
	emit settingsChanged();
}

QString AppController::ftpSearchReport() const
{
	if(status_.ftpSearch.empty())
		return QString();
	const bool ps5 = activeIsPs5();
	QString report;
	for(const PortAnswer &answer : status_.ftpSearch)
	{
		QString said;
		if(!answer.open)
			said = QStringLiteral("closed");
		else if(answer.ftp())
			said = QStringLiteral("FTP — ") + QString::fromStdString(answer.greeting);
		else if(answer.greeting.empty())
			said = QStringLiteral("open, but it did not greet as FTP");
		else
			said = QStringLiteral("open, not FTP — ") + QString::fromStdString(answer.greeting);
		const std::string use = knownPortUse(answer.port, ps5);
		report += QStringLiteral("  %1  %2").arg(answer.port, -5).arg(said, -36);
		if(!use.empty())
			report += QStringLiteral("  %1").arg(QString::fromStdString(use));
		report += QLatin1Char('\n');
	}
	return report;
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
	{
		QStringList tried;
		for(const PortAnswer &answer : status_.ftpSearch)
			tried << QString::number(answer.port);
		if(tried.isEmpty())
			tried << QString::number(activeFtpPort());
		return tr("FTP is not running on the console: load the ftpsrv payload (with your payload loader, or "
			"Payloads → Send from this PC…), or turn on etaHEN's FTP (FTP=1 in config.ini). Nothing answered "
			"on %1.")
			.arg(tried.join(QStringLiteral(", ")));
	}
	return status_.ftp.state == ServiceState::Available
		? QString::fromStdString(status_.ftp.detail)
		: translateMessage(status_.ftp.hint);
}
QString AppController::installerHelpKind() const
{
	// "installer": a PS5's, where Payloads can start it; "rpi": a PS4's app.
	return activeIsPs5() ? QStringLiteral("installer") : QStringLiteral("rpi");
}

QString AppController::installerHelpTitle() const
{
	if(!activeIsPs5())
		return tr("Remote Package Installer is not open");
	return tr("No installer running on the console");
}

QString AppController::installerHelpText() const
{
	if(!activeIsPs5())
		return tr("To install packages, the PS4 needs the Remote Package Installer app open. Do this:\n"
			"• On the PS4, open Remote Package Installer.\n"
			"• Keep it in the foreground while installs are sent: in the background it does not answer.\n"
			"The install waits in the queue and starts by itself once it answers.");
	return tr("To install packages, the PS5 needs an installer running on it. Do one of these:\n"
		"• In Payloads, press ▶ on Prospero Manager (when it is on the console).\n"
		"• In etaHEN, turn on DPI v2 (DPI_v2=1 in config.ini).\n"
		"The install waits in the queue and goes on by itself once one of them answers. To have it every "
		"time, turn on Prospero Manager's switch in Payloads: it then starts with each jailbreak.");
}

QString AppController::ftpHelpTitle() const
{
	return tr("FTP is not running on the console");
}

QString AppController::ftpHelpText() const
{
	if(!activeIsPs5())
		return tr("OrbisLink reaches the PS4's files over GoldHEN's FTP server, and nothing answers. Check this:\n"
			"• GoldHEN is loaded: after the console restarts, the jailbreak has to be run again.\n"
			"• Its FTP server is on (it is by default): in GoldHEN's settings, the server settings.\n"
			"OrbisLink finds it by itself on the usual ports (2121, 1337, 21).");
	return tr("OrbisLink reaches the PS5's files over FTP, and nothing answers. Do one of these:\n"
		"• Load the ftpsrv payload with your payload loader, or from this PC in Payloads → Send from this "
		"PC…\n"
		"• In etaHEN, turn on its FTP (FTP=1 in config.ini).\n"
		"OrbisLink finds it by itself on the usual ports (1337, 2121, 21).");
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
		return tr("No installer is running on the console: in Payloads, press ▶ on Prospero Manager "
			"(port 7070), or turn on etaHEN's DPI v2 (DPI_v2=1 in config.ini, port %1).")
			.arg(settings_.installerPort);
	return status_.installer.state == ServiceState::Available
		? QString::fromStdString(status_.installer.detail)
		: translateMessage(status_.installer.hint);
}
bool AppController::canInstallDirectly() const { return status_.canInstallDirectly(); }
bool AppController::canUseFtp() const { return status_.canUseFtp(); }
bool AppController::queuePaused() const { return queue_ && queue_->paused(); }
bool AppController::waitingForInstaller() const { return queue_ && queue_->waitingForService(); }
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

namespace {

// A folder as the pinned chips show it: "/data/pkg/" (the root stays "/").
std::string pinnedForm(const std::string &path)
{
	const std::string folder = normalizeRemotePath(path);
	return folder == "/" ? folder : folder + "/";
}

} // namespace

std::vector<std::string> &AppController::pinnedFolders()
{
	return activeIsPs5() ? settings_.ftpPinnedFoldersPs5 : settings_.ftpPinnedFolders;
}

const std::vector<std::string> &AppController::pinnedFolders() const
{
	return activeIsPs5() ? settings_.ftpPinnedFoldersPs5 : settings_.ftpPinnedFolders;
}

QStringList AppController::ftpShortcuts() const
{
	QStringList shortcuts;
	for(const std::string &path : pinnedFolders())
		shortcuts << QString::fromStdString(pinnedForm(path));
	return shortcuts;
}

void AppController::setStreamAspect(const QString &aspect)
{
	if(aspect != QLatin1String("fit") && aspect != QLatin1String("4:3") && aspect != QLatin1String("fill"))
		return;
	if(settings_.streamAspect == aspect.toStdString())
		return;
	settings_.streamAspect = aspect.toStdString();
	store_.save(settings_);
	emit streamAspectChanged();
}

namespace {

const QStringList &themeColorKeys()
{
	static const QStringList keys = { QStringLiteral("accent"), QStringLiteral("hen"), QStringLiteral("ok"),
		QStringLiteral("warn"), QStringLiteral("error"), QStringLiteral("background"),
		QStringLiteral("panel"), QStringLiteral("text") };
	return keys;
}

QVariantMap colorsToMap(const std::map<std::string, std::string> &colors)
{
	QVariantMap map;
	for(const auto &pair : colors)
		map.insert(QString::fromStdString(pair.first), QString::fromStdString(pair.second));
	return map;
}

} // namespace

FtpClient::Config AppController::ftpClientConfig() const
{
	if(ftp_)
		return ftp_->config();
	FtpClient::Config config;
	config.host = settings_.consoleAddress;
	config.port = activeFtpPort();
	return config;
}

FtpClient::Config AppController::ftpClientConfigFor(const std::string &address) const
{
	FtpClient::Config config = ftpClientConfig();
	if(address == settings_.consoleAddress)
		return config;
	config.host = address;
	config.port = settings_.ftpPort;
	for(const ConsoleEntry &console : settings_.consoles)
		if(console.address == address && console.type == "ps5")
			config.port = settings_.ftpPortPs5;
	return config;
}

bool AppController::ftpAnswers(const std::string &address) const
{
	if(address == settings_.consoleAddress)
		return canUseFtp();
	return ftpReachable_.value(QString::fromStdString(address)).toBool();
}

QVariantMap AppController::themeColors() const { return colorsToMap(settings_.themeColors); }

QVariantList AppController::themePresets() const
{
	QVariantList list;
	for(const ThemePreset &preset : settings_.themePresets)
	{
		QVariantMap item;
		item[QStringLiteral("name")] = QString::fromStdString(preset.name);
		item[QStringLiteral("theme")] = QString::fromStdString(preset.theme);
		item[QStringLiteral("colors")] = colorsToMap(preset.colors);
		list << item;
	}
	return list;
}

void AppController::setThemeColor(const QString &key, const QString &color)
{
	if(!themeColorKeys().contains(key))
		return;
	const QColor parsed(color);
	if(color.isEmpty() || !parsed.isValid())
		settings_.themeColors.erase(key.toStdString());
	else
		settings_.themeColors[key.toStdString()] = parsed.name(QColor::HexRgb).toUpper().toStdString();
	// Changed by hand: it is no longer exactly the saved look.
	settings_.activePreset.clear();
	store_.save(settings_);
	emit themeColorsChanged();
}

namespace {

QString backdropFolder()
{
	return QDir(QString::fromStdString(SettingsStore::defaultDirectory())).filePath(QStringLiteral("backgrounds"));
}

} // namespace

void AppController::prepareBackdrop()
{
	backdropPath_.clear();
	if(settings_.backgroundImage.empty())
		return;
	const QFileInfo source(QString::fromStdString(settings_.backgroundImage));
	if(!source.exists())
	{
		logWarning("The background picture is gone (" + settings_.backgroundImage + "); back to the room.");
		settings_.backgroundImage.clear();
		settings_.backgroundTones.clear();
		return;
	}
	QDir folder = source.dir();
	const QString name = source.completeBaseName() + QStringLiteral("-blur%1.jpg").arg(settings_.backgroundBlur);
	const QString made = folder.filePath(name);
	if(!QFileInfo::exists(made))
	{
		const QImage picture = backdrop::load(source.filePath());
		if(picture.isNull() || !backdrop::blurred(picture, settings_.backgroundBlur).save(made, "JPG", 90))
		{
			logWarning("Could not prepare the background picture.");
			return;
		}
	}
	// Only the version in use is kept.
	const QStringList others = folder.entryList({ source.completeBaseName() + QStringLiteral("-blur*.jpg") }, QDir::Files);
	for(const QString &other : others)
		if(other != name)
			folder.remove(other);
	backdropPath_ = made;
}

QString AppController::backgroundUrl() const
{
	return backdropPath_.isEmpty() ? QString() : QUrl::fromLocalFile(backdropPath_).toString();
}

QVariantMap AppController::backgroundTones() const
{
	QVariantMap map;
	if(backdropPath_.isEmpty())
		return map;
	for(const auto &pair : settings_.backgroundTones)
		map.insert(QString::fromStdString(pair.first), QString::fromStdString(pair.second));
	return map;
}

bool AppController::chooseBackground(const QString &file)
{
	const QUrl url(file);
	const QString path = url.isLocalFile() ? url.toLocalFile() : file;
	const QImage picture = backdrop::load(path);
	if(picture.isNull())
	{
		emit notify(tr("Background"), tr("That file could not be read as a picture."), true);
		return false;
	}
	// A copy of its own: the original may move or be deleted.
	const QDir folder(backdropFolder());
	folder.mkpath(QStringLiteral("."));
	const QString copy = folder.filePath(QStringLiteral("picture-%1.jpg").arg(QDateTime::currentMSecsSinceEpoch()));
	if(!picture.save(copy, "JPG", 92))
	{
		emit notify(tr("Background"), tr("The picture could not be saved in the data folder."), true);
		return false;
	}
	const std::string previous = settings_.backgroundImage;
	const backdrop::Tones tones = backdrop::analyse(picture);
	settings_.backgroundImage = copy.toStdString();
	settings_.backgroundTones.clear();
	if(tones.average.isValid())
		settings_.backgroundTones["average"] = tones.average.name(QColor::HexRgb).toUpper().toStdString();
	if(tones.primary.isValid())
		settings_.backgroundTones["primary"] = tones.primary.name(QColor::HexRgb).toUpper().toStdString();
	if(tones.secondary.isValid())
		settings_.backgroundTones["secondary"] = tones.secondary.name(QColor::HexRgb).toUpper().toStdString();
	prepareBackdrop();
	// The picture chosen before, and what was made from it, go.
	if(!previous.empty() && previous != settings_.backgroundImage)
	{
		const QFileInfo old(QString::fromStdString(previous));
		for(const QString &made : old.dir().entryList({ old.completeBaseName() + QStringLiteral("*") }, QDir::Files))
			old.dir().remove(made);
	}
	store_.save(settings_);
	emit backgroundChanged();
	return true;
}

void AppController::clearBackground()
{
	if(settings_.backgroundImage.empty())
		return;
	const QFileInfo old(QString::fromStdString(settings_.backgroundImage));
	for(const QString &made : old.dir().entryList({ old.completeBaseName() + QStringLiteral("*") }, QDir::Files))
		old.dir().remove(made);
	settings_.backgroundImage.clear();
	settings_.backgroundTones.clear();
	backdropPath_.clear();
	store_.save(settings_);
	emit backgroundChanged();
}

void AppController::setBackgroundBlur(int strength)
{
	strength = std::clamp(strength, 0, 100);
	if(strength == settings_.backgroundBlur)
		return;
	settings_.backgroundBlur = strength;
	prepareBackdrop();
	store_.save(settings_);
	emit backgroundChanged();
}

void AppController::setBackgroundPalette(bool fromPicture)
{
	if(fromPicture == settings_.backgroundPalette)
		return;
	settings_.backgroundPalette = fromPicture;
	store_.save(settings_);
	emit backgroundChanged();
}

void AppController::setBackgroundMotion(bool moving)
{
	if(moving == settings_.backgroundMotion)
		return;
	settings_.backgroundMotion = moving;
	store_.save(settings_);
	emit backgroundChanged();
}

void AppController::resetThemeColors()
{
	if(settings_.themeColors.empty())
		return;
	settings_.themeColors.clear();
	settings_.activePreset.clear();
	store_.save(settings_);
	emit themeColorsChanged();
}

void AppController::saveThemePreset(const QString &name)
{
	const std::string trimmed = name.trimmed().toStdString();
	if(trimmed.empty())
		return;
	ThemePreset preset { trimmed, settings_.theme, settings_.themeColors };
	auto &presets = settings_.themePresets;
	auto same = std::find_if(presets.begin(), presets.end(),
		[&](const ThemePreset &p) { return p.name == trimmed; });
	if(same != presets.end())
		*same = preset;
	else
		presets.push_back(preset);
	settings_.activePreset = trimmed;
	store_.save(settings_);
	emit themePresetsChanged();
	emit themeColorsChanged();
	setStatusMessage(tr("Look \"%1\" saved.").arg(name.trimmed()));
}

void AppController::applyThemePreset(const QString &name)
{
	for(const ThemePreset &preset : settings_.themePresets)
	{
		if(preset.name != name.toStdString())
			continue;
		settings_.theme = preset.theme == "glass" || preset.theme == "light" ? preset.theme : "dark";
		settings_.themeColors = preset.colors;
		settings_.activePreset = preset.name;
		store_.save(settings_);
		emit themeColorsChanged();
		emit settingsChanged();
		return;
	}
}

bool AppController::applyCustomLook()
{
	for(const ThemePreset &preset : settings_.themePresets)
	{
		if(preset.name == settings_.activePreset && !preset.colors.empty())
		{
			applyThemePreset(QString::fromStdString(preset.name));
			return true;
		}
	}
	if(settings_.lastCustomColors.empty())
		return false;
	settings_.themeColors = settings_.lastCustomColors;
	if(!settings_.lastCustomTheme.empty())
		settings_.theme = settings_.lastCustomTheme;
	store_.save(settings_);
	emit themeColorsChanged();
	emit settingsChanged();
	return true;
}

void AppController::deleteThemePreset(const QString &name)
{
	auto &presets = settings_.themePresets;
	const auto before = presets.size();
	presets.erase(std::remove_if(presets.begin(), presets.end(),
					  [&](const ThemePreset &p) { return p.name == name.toStdString(); }),
		presets.end());
	if(presets.size() == before)
		return;
	if(settings_.activePreset == name.toStdString())
		settings_.activePreset.clear();
	store_.save(settings_);
	emit themePresetsChanged();
}

bool AppController::isFtpPinned(const QString &path) const
{
	const std::string folder = pinnedForm(path.toStdString());
	for(const std::string &pinned : pinnedFolders())
		if(pinnedForm(pinned) == folder)
			return true;
	return false;
}

void AppController::pinFtpFolder(const QString &path)
{
	if(path.trimmed().isEmpty() || isFtpPinned(path))
		return;
	pinnedFolders().push_back(pinnedForm(path.toStdString()));
	store_.save(settings_);
	emit ftpShortcutsChanged();
	setStatusMessage(tr("%1 pinned to the top.").arg(QString::fromStdString(pinnedForm(path.toStdString()))));
}

void AppController::unpinFtpFolder(const QString &path)
{
	const std::string folder = pinnedForm(path.toStdString());
	auto &pinned = pinnedFolders();
	const auto before = pinned.size();
	pinned.erase(std::remove_if(pinned.begin(), pinned.end(),
					 [&](const std::string &p) { return pinnedForm(p) == folder; }),
		pinned.end());
	if(pinned.size() == before)
		return;
	store_.save(settings_);
	emit ftpShortcutsChanged();
	setStatusMessage(tr("%1 removed from the top.").arg(QString::fromStdString(folder)));
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
		// One that starts going into the folder on screen: the list is read
		// again, so it shows there at once, with how far it is.
		if(task.state == TaskState::Sending && !uploadsListed_.contains(QString::fromStdString(task.id))
			&& normalizeRemotePath(item.value(QStringLiteral("directory")).toString().toStdString())
				== normalizeRemotePath(ftpPath_.toStdString()))
		{
			uploadsListed_.insert(QString::fromStdString(task.id));
			QTimer::singleShot(1500, this, [this]() { refreshFtpListing(false); });
		}
	}
	if(uploads != ftpUploads_)
	{
		ftpUploads_ = uploads;
		emit ftpUploadsChanged();
	}

	// Each file's latest news: a task under way wins (installing over
	// sending), otherwise the newest finished one says how it ended.
	QVariantMap transfers;
	auto percentOf = [](const QueueTask &task) {
		return task.totalBytes > 0
			? 100.0 * static_cast<double>(task.doneBytes) / static_cast<double>(task.totalBytes) : 0.0;
	};
	for(const QueueTask &task : queue_->tasks())
	{
		if(task.isTerminal())
			continue;
		const QString key = transferKey(QString::fromStdString(task.localPath));
		const bool installing = task.mode != TransferMode::FtpUpload;
		if(transfers.contains(key) && !installing)
			continue;
		QVariantMap item;
		item[QStringLiteral("stage")] = installing ? QStringLiteral("installing") : QStringLiteral("sending");
		item[QStringLiteral("percent")] = percentOf(task);
		item[QStringLiteral("waiting")] = task.state == TaskState::Pending;
		// How fast it goes and how long it has left, while it moves.
		const bool moving = task.state == TaskState::Sending || task.state == TaskState::Installing;
		item[QStringLiteral("speedText")] = moving && task.bytesPerSecond > 1.0
			? QString::fromStdString(humanBytes(static_cast<int64_t>(task.bytesPerSecond))) + QStringLiteral("/s")
			: QString();
		item[QStringLiteral("etaText")] = moving && task.etaSeconds > 0
			? QString::fromStdString(humanDuration(task.etaSeconds))
			: QString();
		item[QStringLiteral("taskId")] = QString::fromStdString(task.id);
		item[QStringLiteral("mode")] =
			task.mode != TransferMode::FtpUpload ? QStringLiteral("install") : QStringLiteral("send");
		transfers[key] = item;
	}
	for(const QueueTask &task : queue_->history())
	{
		const QString key = transferKey(QString::fromStdString(task.localPath));
		if(transfers.contains(key))
			continue;
		QVariantMap item;
		const bool installing = task.mode != TransferMode::FtpUpload || task.installedFromConsole;
		item[QStringLiteral("stage")] = task.state == TaskState::Completed
			? (installing ? QStringLiteral("installed") : QStringLiteral("sent"))
			: task.state == TaskState::Cancelled ? QStringLiteral("cancelled") : QStringLiteral("error");
		item[QStringLiteral("percent")] = 100.0;
		item[QStringLiteral("message")] = translateMessage(task.message);
		item[QStringLiteral("taskId")] = QString::fromStdString(task.id);
		item[QStringLiteral("mode")] =
			task.mode != TransferMode::FtpUpload ? QStringLiteral("install") : QStringLiteral("send");
		item[QStringLiteral("note")] = queueModel_.note(QString::fromStdString(task.id));
		transfers[key] = item;
	}
	if(transfers != transfers_)
	{
		transfers_ = transfers;
		emit transfersChanged();
	}
}

QString AppController::transferKey(const QString &localPath)
{
	return QueueModel::fileKey(localPath);
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
	addPaths(paths, mode, mode == 1 ? ftpPath_ : QString());
}

void AppController::readPackageFacts(const QStringList &paths, const QStringList &taskIds)
{
	// On a worker thread: it is disk reading.
	std::thread([this, life = lifeline_, paths, taskIds]() {
		PkgInspector inspector;
		for(int i = 0; i < paths.size() && i < taskIds.size(); ++i)
		{
			const PkgInfo info = inspector.inspect(paths[i].toStdString());
			const QString taskId = taskIds[i];
			if(!info.iconPng.empty())
			{
				const QByteArray png(reinterpret_cast<const char *>(info.iconPng.data()),
					static_cast<int>(info.iconPng.size()));
				const QString dataUri = QStringLiteral("data:image/png;base64,")
					+ QString::fromLatin1(png.toBase64());
				life->post([this, taskId, dataUri]() { queueModel_.setIcon(taskId, dataUri); });
			}
			if(!info.valid)
				continue;
			const bool official = info.official;
			const bool ps5 = info.ps5;
			const int required = info.requiredFirmware;
			const QString category = QString::fromLatin1(pkgCategoryCode(info.kind));
			const QString title = QString::fromStdString(info.displayTitle());
			life->post([this, taskId, official, ps5, required, category, title]() {
				PackageFacts facts;
				facts.official = official;
				facts.ps5 = ps5;
				facts.requiredFirmware = required;
				facts.category = category;
				facts.title = title;
				facts.warning = packageWarning(official, required, category, ps5);
				packageFacts_.insert(taskId, facts);
				queueModel_.setPackageFacts(taskId,
					QString::fromStdString(firmwareText(static_cast<uint16_t>(required))), facts.warning);
				if(!facts.warning.isEmpty())
					emit notify(title, facts.warning, true);
			});
		}
	}).detach();
}

QString AppController::consoleFirmware() const
{
	return QString::fromStdString(firmwareText(consoleFirmware_));
}

void AppController::reportConsoleFirmware(const QString &address, const QString &systemVersion)
{
	if(address.toStdString() != settings_.consoleAddress)
		return;
	const uint16_t version = firmwareFromDiscovery(systemVersion.toStdString());
	// Not answering is not a new firmware: the last one known stays.
	if(version == 0 || version == consoleFirmware_)
		return;
	consoleFirmware_ = version;
	emit consoleFirmwareChanged();
	refreshPackageWarnings();
	// Kept with the console: it shows next time too, even when the console
	// does not answer the search then.
	for(ConsoleEntry &console : settings_.consoles)
		if(console.address == settings_.consoleAddress && console.firmware != version)
		{
			console.firmware = version;
			store_.save(settings_);
		}
}

QStringList AppController::directConsoles() const
{
	QStringList addresses;
	for(const ConsoleEntry &console : settings_.consoles)
		if(console.connectsDirectly)
			addresses.append(QString::fromStdString(console.address));
	return addresses;
}

void AppController::rememberDirectConsole(const QString &address)
{
	const std::string wanted = address.trimmed().toStdString();
	for(ConsoleEntry &console : settings_.consoles)
		if(console.address == wanted && !console.connectsDirectly)
		{
			console.connectsDirectly = true;
			store_.save(settings_);
			logInfo("Remote Play: " + wanted + " does not answer the search but takes a direct connection; "
				"its card says so from now on.");
			emit settingsChanged();
		}
}

void AppController::refreshPackageWarnings()
{
	for(auto it = packageFacts_.begin(); it != packageFacts_.end(); ++it)
	{
		PackageFacts &facts = it.value();
		const QString warning = packageWarning(facts.official, facts.requiredFirmware, facts.category, facts.ps5);
		if(warning == facts.warning)
			continue;
		facts.warning = warning;
		queueModel_.setPackageFacts(it.key(),
			QString::fromStdString(firmwareText(static_cast<uint16_t>(facts.requiredFirmware))), warning);
		if(!warning.isEmpty())
			emit notify(facts.title, warning, true);
	}
}

QString AppController::packageWarning(bool official, int requiredFirmware, const QString &category,
	bool ps5Package) const
{
	if(official)
		return tr("An official store package: it installs only with its licence. A jailbroken console "
			"installs fake packages (fpkg).");
	if(ps5Package)
	{
		if(!activeIsPs5())
			return tr("A PS5 package: only a PS5 installs it.");
		// A PS5 game made for a newer system software does not start either.
		if(consoleFirmware_ != 0 && requiredFirmware > consoleFirmware_)
			return tr("Needs firmware %1, and this PS5 runs %2: it installs, but does not start. Look for a "
				"backport of it.")
				.arg(QString::fromStdString(firmwareText(static_cast<uint16_t>(requiredFirmware))), consoleFirmware());
		return QString();
	}
	// Only a PS4 refuses to start what was made for a newer firmware; a
	// PS5 runs PS4 games whatever their firmware. A game or its update.
	const bool startable = category == QLatin1String("gd") || category == QLatin1String("gp");
	if(!activeIsPs5() && startable && consoleFirmware_ != 0 && requiredFirmware > consoleFirmware_)
		return tr("Needs firmware %1, and this PS4 runs %2: it installs, but does not start. Look for a "
			"version backported to %2.")
			.arg(QString::fromStdString(firmwareText(static_cast<uint16_t>(requiredFirmware))),
				consoleFirmware());
	return QString();
}

QString AppController::ps5StartHint() const
{
	const QString firmware = consoleFirmware();
	if(consoleFirmware_ == 0)
		return tr("A PS5 fake package starts with kstuff loaded on firmware up to 11.60; on 12.00–13.60, with the "
			"Titan Core payload loaded (Payloads).");
	if(consoleFirmware_ <= 0x1160)
		return tr("Installed: on firmware %1 it starts with kstuff loaded (etaHEN or kstuff-lite).").arg(firmware);
	if(consoleFirmware_ <= 0x1360)
		return tr("On firmware %1 it starts only with the Titan Core payload loaded (Payloads): it prepares the "
			"installed fake packages to start.").arg(firmware);
	return tr("On firmware %1 no public method starts a PS5 fake package yet.").arg(firmware);
}

bool AppController::queueBusy() const
{
	if(!queue_)
		return false;
	for(const QueueTask &task : queue_->tasks())
		if(!task.isTerminal())
			return true;
	return false;
}

void AppController::setTaskNote(const QString &taskId, const QString &text)
{
	queueModel_.setNote(taskId, text);
	// A package or image made here shows it on its conversion card too.
	bool changed = false;
	for(auto it = transfers_.begin(); it != transfers_.end(); ++it)
	{
		QVariantMap item = it.value().toMap();
		if(item.value(QStringLiteral("taskId")).toString() != taskId
			|| item.value(QStringLiteral("note")).toString() == text)
			continue;
		item[QStringLiteral("note")] = text;
		it.value() = item;
		changed = true;
	}
	if(changed)
		emit transfersChanged();
}

void AppController::handImageToShadowMount(const QString &taskId, const QString &remotePath)
{
	const std::string address = settings_.consoleAddress;
	const FtpClient::Config config = ftpClientConfigFor(address);
	setTaskNote(taskId, tr("ShadowMountPlus: asking it to look…"));
	std::thread([this, life = lifeline_, taskId, remotePath, address, config]() {
		const std::atomic<bool> never { false };
		const std::string how = shadowmount::rescan(config, address, &never);
		logInfo("ShadowMountPlus: " + remotePath.toStdString() + " is where it looks; "
			+ (how == "api" ? std::string("its API was asked to scan.")
				: how == "restarted" ? std::string("it was started again, which scans everything.")
				: std::string("neither its API nor a restart answered: it finds it at its next scan, if it runs.")));
		const QString text = how == "api"
			? tr("ShadowMountPlus: it puts it on the home screen now.")
			: how == "restarted"
			? tr("ShadowMountPlus was started again: it is on the home screen in a few seconds.")
			: tr("ShadowMountPlus did not answer: it puts it on the home screen at its next scan, if it runs "
				 "(load it in Payloads).");
		life->post([this, taskId, text]() { setTaskNote(taskId, text); });
	}).detach();
}

void AppController::followPs5Install(const QString &taskId, const QString &titleId)
{
	const std::string address = settings_.consoleAddress;
	const QString hint = ps5StartHint();
	const std::string wanted = asciiUpper(titleId.toStdString());
	const std::string firmware = consoleFirmware().toStdString();
	std::thread([this, life = lifeline_, taskId, wanted, address, hint, firmware]() {
		const std::string base = "http://" + address + ":8097";
		HttpClient http(2500);
		// Why the last read failed, for the log: no answer, or an answer that
		// is not Titan Core's (another program on the port, another version).
		std::string why;
		auto readState = [&http, &base, &why](Json *state) {
			const HttpResponse reply = http.get(base + "/api/state");
			if(!reply.transportOk)
			{
				why = "no answer (" + reply.error + ")";
				return false;
			}
			if(reply.status != 200)
			{
				why = "HTTP " + std::to_string(reply.status) + " to /api/state";
				return false;
			}
			*state = Json::parse(reply.body);
			if(state->isObject() && (*state)["games"].isArray())
				return true;
			why = "a reply that is not Titan Core's state: " + reply.body.substr(0, 160);
			return false;
		};
		auto note = [this, &life, &taskId](const QString &text) {
			life->post([this, taskId, text]() { setTaskNote(taskId, text); });
		};
		auto report = [this, &life](const std::string &text) {
			const QString line = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ")
				+ QString::fromStdString(text);
			life->post([this, line]() { titanCoreReport_ = line; });
		};
		Json state;
		if(!readState(&state))
		{
			// No Titan Core there: what starts it on this firmware.
			logInfo("Titan Core on " + base + ": " + why + ". " + wanted + " installed on firmware "
				+ (firmware.empty() ? std::string("unknown") : firmware) + "; the card says what starts it.");
			report(wanted + ": Titan Core did not answer on 8097 — " + why);
			note(hint);
			return;
		}
		const std::string version = state["version"].toString();
		logInfo("Titan Core " + (version.empty() ? std::string("(no version)") : version) + " answers on " + base
			+ ": asking it to look for " + wanted + ".");
		note(tr("Titan Core: looking for it…"));
		auto rescan = [&http, &base]() {
			const HttpResponse reply = http.post(base + "/api/rescan", std::string(), "text/plain");
			if(!reply.transportOk || reply.status >= 300)
				logDebug("Titan Core: rescan answered " + (reply.transportOk ? "HTTP " + std::to_string(reply.status)
					: "nothing (" + reply.error + ")"));
		};
		rescan();
		const int64_t started = monotonicMillis();
		int64_t lastRescan = started;
		bool announcedWait = false;
		bool lostIt = false;
		std::string lastStatus;
		while(life->alive() && monotonicMillis() - started < 30 * 60 * 1000)
		{
			std::this_thread::sleep_for(std::chrono::seconds(3));
			if(!readState(&state))
			{
				if(!lostIt)
				{
					lostIt = true;
					logInfo("Titan Core stopped answering while following " + wanted + ": " + why + ".");
				}
				continue;
			}
			if(lostIt)
			{
				lostIt = false;
				logInfo("Titan Core answers again.");
			}
			const Json *game = nullptr;
			for(const Json &item : state["games"].items())
				if(asciiUpper(item["title_id"].toString()) == wanted)
				{
					game = &item;
					// The installed package (not an image of it elsewhere).
					if(item["kind"].toString() == "pkg")
						break;
				}
			if(!game)
			{
				// Not seen yet: the console may still be installing it.
				if(monotonicMillis() - lastRescan > 15000)
				{
					rescan();
					lastRescan = monotonicMillis();
				}
				if(!announcedWait && monotonicMillis() - started > 60000)
				{
					announcedWait = true;
					std::string seen;
					int count = 0;
					for(const Json &item : state["games"].items())
						if(++count <= 12)
							seen += (seen.empty() ? "" : ", ") + item["title_id"].toString();
					logInfo("Titan Core has not listed " + wanted + " after a minute; it lists " + std::to_string(count)
						+ (count ? ": " + seen + (count > 12 ? "…" : "") : std::string()) + ".");
					report(wanted + ": not listed by Titan Core " + version + " after a minute");
					note(tr("Titan Core has not found it yet: once the console finishes installing it, Titan Core "
							"prepares it."));
				}
				continue;
			}
			const std::string status = (*game)["status"].toString();
			if(status == lastStatus)
				continue;
			lastStatus = status;
			const std::string error = (*game)["error"].toString();
			logInfo("Titan Core: " + wanted + " is " + status + (error.empty() ? std::string() : " — " + error)
				+ " (" + (*game)["kind"].toString() + ", " + (*game)["path"].toString() + ").");
			report(wanted + ": " + status + " (Titan Core " + version + ")" + (error.empty() ? "" : " — " + error));
			if(status == "ready")
			{
				note(tr("Titan Core: ready — it starts from the home screen."));
				return;
			}
			if(status == "error")
			{
				logWarning("Titan Core could not prepare " + wanted + ": " + error);
				note(error.find("belongs to another source") != std::string::npos
						? tr("Titan Core could not prepare it: the game was moved (to the M.2?) after it was "
							 "prepared. Move it back to the console's storage.")
						: tr("Titan Core could not prepare it: %1").arg(QString::fromStdString(error)));
				return;
			}
			if(status == "duplicate")
			{
				note(tr("Titan Core: another copy of this game is already there."));
				return;
			}
			note(status == "building" ? tr("Titan Core: preparing it to start (building its cache)…")
									  : tr("Titan Core: checking it…"));
		}
		if(life->alive())
		{
			logInfo("Titan Core: stopped following " + wanted + " after 30 minutes (last: "
				+ (lastStatus.empty() ? std::string("never listed") : lastStatus) + ").");
			report(wanted + ": still " + (lastStatus.empty() ? std::string("not listed") : lastStatus)
				+ " after 30 minutes");
		}
	}).detach();
}

void AppController::addPaths(const QStringList &paths, int mode, const QString &directory)
{
	if(!queue_)
		return;
	// Known not to answer (not merely unchecked yet): what to do, instead of
	// queueing what cannot go. An install with only the installer missing
	// is queued: it waits for it, and the queue says what to do.
	const bool ftpDown = status_.ftp.state == ServiceState::Unavailable;
	const bool installerDown = status_.installer.state == ServiceState::Unavailable;
	if(mode == 1 && ftpDown)
	{
		emit serviceHelp(QStringLiteral("ftp"), ftpHelpTitle(), ftpHelpText());
		return;
	}
	if(mode != 1 && activeIsPs5() && installerDown && ftpDown)
	{
		emit serviceHelp(installerHelpKind(), installerHelpTitle(), installerHelpText());
		return;
	}
	const TransferMode transferMode = mode == 1 ? TransferMode::FtpUpload : TransferMode::DirectInstall;
	// Over FTP a folder goes as it is — every file, in its tree (a homebrew
	// app's folder, a save, a dump). To install, only its packages count.
	QStringList loose = paths;
	if(transferMode == TransferMode::FtpUpload)
	{
		QStringList folders;
		loose.clear();
		for(const QString &path : paths)
			(QFileInfo(path).isDir() ? folders : loose) << path;
		if(!folders.isEmpty())
			enqueueFolderUploads(folders, directory.trimmed().isEmpty()
					? uploadDirectory()
					: QString::fromStdString(normalizeRemotePath(directory.toStdString())));
		if(loose.isEmpty())
			return;
	}
	const QStringList files = collectPkgFiles(loose);
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

	// Going over FTP: first see what is already in the folder it goes to.
	if(transferMode == TransferMode::FtpUpload && ftp_)
	{
		checkUploadConflicts(accepted, directory.trimmed().isEmpty()
				? uploadDirectory()
				: QString::fromStdString(normalizeRemotePath(directory.toStdString())));
		return;
	}
	enqueueFiles(accepted, transferMode);
}

void AppController::enqueueFolderUploads(const QStringList &folders, const QString &baseDir)
{
	if(!queue_)
		return;
	// Each file goes to its own folder on the console: the dropped folder's
	// name, then its path inside it. Names are made safe one part at a time,
	// as a single file's are; the upload creates the folders that are missing.
	auto safePath = [](const QString &relative) {
		QStringList parts;
		for(const QString &part : relative.split(QLatin1Char('/'), Qt::SkipEmptyParts))
			if(part != QLatin1String("."))
				parts << QString::fromStdString(sanitizeFileName(part.toStdString()));
		return parts.join(QLatin1Char('/'));
	};
	std::vector<std::string> paths;
	std::map<std::string, std::string> into;
	const std::string base = normalizeRemotePath(baseDir.toStdString());
	for(const QString &folder : folders)
	{
		const QDir root(folder);
		const QString top = safePath(root.dirName());
		QDirIterator it(folder, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
		while(it.hasNext())
		{
			const QString file = it.next();
			const QString inside = safePath(root.relativeFilePath(QFileInfo(file).path()));
			const std::string local = QDir::toNativeSeparators(file).toStdString();
			paths.push_back(local);
			into[local] = normalizeRemotePath(base + "/" + top.toStdString()
				+ (inside.isEmpty() ? std::string() : "/" + inside.toStdString()));
		}
	}
	if(paths.empty())
	{
		setStatusMessage(tr("The dropped folder has no files to send."));
		return;
	}
	std::vector<std::string> rejected;
	const std::vector<std::string> ids = queue_->enqueue(paths, TransferMode::FtpUpload, &rejected, nullptr,
		[&into](QueueTask &task) {
			const auto found = into.find(task.localPath);
			if(found != into.end())
				task.remoteDirectory = found->second;
		});
	refreshQueueModel();
	if(!rejected.empty())
		setStatusMessage(translateMessage(rejected.front()));
	else
		setStatusMessage(tr("%n file(s) queued, with their folders, for %1.", "", static_cast<int>(ids.size()))
				.arg(QString::fromStdString(base)));
	if(!ids.empty())
		emit showPanel(QStringLiteral("queue"));
}

void AppController::enqueueFiles(const QStringList &files, TransferMode transferMode,
	const std::vector<std::string> &remoteNames, const QString &directory)
{
	if(!queue_ || files.isEmpty())
		return;
	std::vector<std::string> nativePaths;
	for(const QString &file : files)
		nativePaths.push_back(file.toStdString());
	const QStringList &accepted = files;

	// Another folder than the upload folder: the uploads go there.
	const std::string into = directory.toStdString();
	const bool elsewhere = transferMode == TransferMode::FtpUpload && !into.empty()
		&& normalizeRemotePath(into) != uploadDirectory().toStdString();
	std::vector<std::string> rejected;
	const std::vector<std::string> ids = queue_->enqueue(nativePaths, transferMode, &rejected,
		remoteNames.empty() ? nullptr : &remoteNames,
		elsewhere ? std::function<void(QueueTask &)>([&into](QueueTask &task) { task.remoteDirectory = into; })
				  : std::function<void(QueueTask &)>());

	QStringList taskIds;
	for(const std::string &id : ids)
		taskIds << QString::fromStdString(id);
	readPackageFacts(accepted, taskIds);

	refreshQueueModel();
	// To install, a file has to be a PS4 package; any other file (a PS3
	// package for RPCS3, an ISO) goes over FTP as it is.
	if(!rejected.empty())
		setStatusMessage(translateMessage(rejected.front())
			+ (transferMode == TransferMode::DirectInstall
					? QStringLiteral(" ") + tr("To copy a file as it is, drop it on Send over FTP.")
					: QString()));
	else
		setStatusMessage(tr("%n file(s) queued.", "", static_cast<int>(ids.size())));
	if(ids.empty())
		return;

	// Uploads and installs alike are followed in the queue, with their
	// bars; the file list keeps showing the console's folders.
	emit showPanel(QStringLiteral("queue"));
}

void AppController::installFromConsole(const QString &remotePath)
{
	if(!queue_ || remotePath.isEmpty())
		return;
	const QString name = QFileInfo(remotePath).fileName();
	setStatusMessage(tr("Reading %1 on the console…").arg(name));
	const FtpClient::Config config = ftpClientConfigFor(settings_.consoleAddress);
	// What it is, read there in a few pieces; then a card in the queue that
	// follows the install.
	std::thread([this, life = lifeline_, config, remotePath, name]() {
		FtpClient ftp(config);
		int64_t size = 0;
		PkgInfo info;
		if(ftp.remoteSize(remotePath.toStdString(), &size).ok && size > 0)
		{
			library::BlockReader reader([&ftp, remotePath](int64_t offset, size_t length, std::vector<uint8_t> *bytes) {
				return ftp.read(remotePath.toStdString(), offset, length, bytes).ok;
			}, size);
			PkgInspector::Options options;
			options.maxIconBytes = 1024 * 1024;
			info = PkgInspector(options).inspect(remotePath.toStdString(), size,
				[&reader](int64_t offset, void *buffer, size_t length) { return reader.read(offset, buffer, length); });
		}
		QString icon;
		if(!info.iconPng.empty())
			icon = QStringLiteral("data:image/png;base64,")
				+ QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(info.iconPng.data()),
					static_cast<int>(info.iconPng.size())).toBase64());
		life->post([this, info, icon, remotePath, name]() {
			if(!info.valid)
			{
				const QString message = tr("%1 was not installed: %2").arg(name,
					translateMessage(info.error.empty() ? std::string("Could not read the file.") : info.error));
				setStatusMessage(message);
				emit notify(tr("Install"), message, true);
				return;
			}
			const std::string id = queue_->enqueueConsoleInstall(remotePath.toStdString(), info);
			if(!icon.isEmpty())
				queueModel_.setIcon(QString::fromStdString(id), icon);
			refreshQueueModel();
			setStatusMessage(tr("%1: queued to install on the console.").arg(name));
			emit showPanel(QStringLiteral("queue"));
		});
	}).detach();
}

void AppController::sendAndInstall(const QString &pkg, const QString &directory)
{
	sendTo(pkg, directory, true);
}

void AppController::sendTo(const QString &pkg, const QString &directory, bool install)
{
	if(!queue_)
		return;
	std::string error;
	const std::string id = queue_->enqueueUploadTo(pkg.toStdString(), directory.toStdString(), install, &error);
	if(id.empty())
	{
		setStatusMessage(translateMessage(error));
		return;
	}
	readPackageFacts(QStringList { pkg }, QStringList { QString::fromStdString(id) });
	refreshQueueModel();
	emit showPanel(QStringLiteral("queue"));
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

void AppController::checkUploadConflicts(const QStringList &files, const QString &into)
{
	const std::string directory = into.toStdString();
	setStatusMessage(tr("Checking what is already on the console…"));
	std::thread([this, life = lifeline_, ftp = ftp_, files, directory, into]() {
		std::vector<FtpEntry> entries;
		const FtpResult result = ftp->list(directory, &entries);
		life->post(
			[this, files, entries, result, into]() {
				// Without a listing there is nothing to compare with: the files
				// go as they always did (the upload itself will report a problem).
				if(!result.ok)
				{
					enqueueFiles(files, TransferMode::FtpUpload, {}, into);
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
				for(const QVariant &value : ftpUploads_)
				{
					const QVariantMap upload = value.toMap();
					if(upload.value(QStringLiteral("directory")).toString() != into)
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
					enqueueFiles(files, TransferMode::FtpUpload, {}, into);
					return;
				}
				pendingUploads_ = files;
				pendingUploadDirectory_ = into;
				setStatusMessage(tr("%n file(s) already on the console.", "",
					static_cast<int>(conflicts.size())));
				emit uploadConflicts(conflicts);
			});
	}).detach();
}

void AppController::resolveUploadConflicts(const QVariantList &decisions)
{
	const QStringList files = pendingUploads_;
	const QString directory = pendingUploadDirectory_;
	pendingUploads_.clear();
	pendingUploadDirectory_.clear();
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
	enqueueFiles(chosen, TransferMode::FtpUpload, remoteNames, directory);
}

void AppController::resumeWhenInstallerIsBack()
{
	if(!queue_ || !queue_->waitingForService())
		return;
	if(status_.installer.state == ServiceState::Available)
	{
		// Checked: the installer answers again, so the queue goes on.
		queue_->resume();
		emit queueStateChanged();
		emit notify(tr("Install"), tr("Remote Package Installer answers: the install goes on."), false);
		return;
	}
	// Until then, ask again every few seconds.
	if(installerRecheckPending_)
		return;
	installerRecheckPending_ = true;
	QTimer::singleShot(3000, this, [this]() {
		installerRecheckPending_ = false;
		if(queue_ && queue_->waitingForService())
			checkServicesNow();
	});
}

void AppController::checkServicesNow()
{
	if(!console_)
		return;
	std::thread([console = console_]() { console->checkNow(); }).detach();
	// Asked for by hand: if the console is not where it was saved, it is
	// looked for now, without waiting for the next turn.
	if(!settings_.consoleAddress.empty())
		checkConsoleAddresses({ QString::fromStdString(settings_.consoleAddress) }, true, {});
}

void AppController::findMovedConsoles(const QStringList &addresses, bool urgent, LocateDone done)
{
	checkConsoleAddresses(addresses, urgent, std::move(done));
}

void AppController::checkConsoleAddresses(const QStringList &only, bool urgent, LocateDone done)
{
	auto finishEmpty = [&only, &done]() {
		if(done)
			for(const QString &address : only)
				done(address, QString());
	};
	if(streamBusy_ && !urgent)
		return finishEmpty();
	if(locating_)
	{
		// One look at a time; an urgent question waits for the look under
		// way and then asks again.
		if(urgent)
			locateWaiters_.emplace_back(only, std::move(done));
		return;
	}

	// The saved consoles to look at, with what identifies each; and those
	// whose FTP answered at their address, whose hardware address can be
	// learned there.
	std::vector<SoughtConsole> sought;
	std::vector<std::string> answering;
	std::map<std::string, int64_t> notBefore;
	for(const ConsoleEntry &console : settings_.consoles)
	{
		const QString address = QString::fromStdString(console.address);
		if(!only.isEmpty() && !only.contains(address))
			continue;
		const bool ftpAnswers = ftpReachable_.value(address).toBool()
			|| (console.address == settings_.consoleAddress && status_.ftp.state == ServiceState::Available);
		if(ftpAnswers && console.mac.empty())
			answering.push_back(console.address);
		SoughtConsole item;
		item.address = console.address;
		if(!console.hostId.empty())
			item.ids.push_back(console.hostId);
		if(!console.mac.empty())
			item.ids.push_back(console.mac);
		if(item.ids.empty())
			continue;
		sought.push_back(item);
		const auto wait = locateWaits_.find(console.address);
		notBefore[console.address] = urgent || wait == locateWaits_.end() ? 0 : wait->second.nextMs;
	}
	if(sought.empty() && answering.empty())
		return finishEmpty();

	locating_ = true;
	const auto search = networkSearch_;
	std::thread([this, life = lifeline_, sought, answering, notBefore, search, only, done]() {
		// A knock at each address (any port: what counts is that the PC asks
		// who has it), then the neighbour table says who answered.
		std::set<std::string> knocked;
		for(const SoughtConsole &console : sought)
			knocked.insert(console.address);
		for(const std::string &address : answering)
			knocked.insert(address);
		for(const std::string &address : knocked)
			tcpProbe(address, 9, 300);
		std::map<std::string, std::string> neighbours = neighbourMacs();

		const int64_t now = monotonicMillis();
		std::vector<SoughtConsole> missing;
		std::vector<std::string> present;
		for(const SoughtConsole &console : sought)
		{
			const AddressCheck check = checkAddress(console, neighbours);
			if(check == AddressCheck::Present)
				present.push_back(console.address);
			// Another machine has its address: it has certainly moved.
			// Nothing answers there: it is off, or elsewhere — looked for
			// when its wait is over.
			else if(check == AddressCheck::Taken || now >= notBefore.at(console.address))
				missing.push_back(console);
		}

		std::vector<Sighting> sightings;
		std::map<std::string, std::string> moved;
		if(!missing.empty())
		{
			logInfo("Looking on the network for " + std::to_string(missing.size())
				+ " saved console(s) that did not answer at their address.");
			if(search)
				sightings = search();
			else
			{
				// Without Remote Play's search, every address of the network is
				// knocked at instead, which fills the neighbour table.
				std::vector<std::string> hosts = lanTargets(localInterfaces()).hosts;
				if(hosts.size() > 1024)
					hosts.resize(1024);
				hostsWithPort(hosts, 9, 600);
			}
			neighbours = neighbourMacs();
			moved = locateMoved(missing, sightings, neighbours);
		}

		// What each console's hardware address is, where it answered as
		// itself: by FTP, or to Remote Play's search with its host-id.
		std::map<std::string, std::string> learned;
		for(const std::string &address : answering)
		{
			const auto mac = neighbours.find(address);
			if(mac != neighbours.end())
				learned[address] = mac->second;
		}
		for(const SoughtConsole &console : missing)
			for(const Sighting &sighting : sightings)
				if(sighting.address == console.address
					&& !console.ids.empty() && normaliseHardwareId(sighting.id) == normaliseHardwareId(console.ids.front()))
				{
					const auto mac = neighbours.find(console.address);
					if(mac != neighbours.end())
						learned[console.address] = mac->second;
				}

		life->post([this, missing, present, moved, learned, only, done]() {
			locating_ = false;
			for(const auto &entry : learned)
				learnConsoleMac(entry.first, entry.second);
			for(const std::string &address : present)
				locateWaits_.erase(address);
			const int64_t now = monotonicMillis();
			for(const SoughtConsole &console : missing)
			{
				if(moved.count(console.address))
				{
					locateWaits_.erase(console.address);
					continue;
				}
				// Not found anywhere: wait longer before the next search.
				LocateWait &wait = locateWaits_[console.address];
				wait.delayMs = std::min<int64_t>(30 * 60 * 1000,
					wait.delayMs == 0 ? 2 * 60 * 1000 : wait.delayMs * 5 / 2);
				wait.nextMs = now + wait.delayMs;
			}
			for(const auto &entry : moved)
				relocateConsole(entry.first, entry.second);
			if(done)
				for(const QString &address : only)
				{
					const auto found = moved.find(address.toStdString());
					done(address, found == moved.end() ? QString() : QString::fromStdString(found->second));
				}
			if(!locateWaiters_.empty())
			{
				auto next = std::move(locateWaiters_.front());
				locateWaiters_.erase(locateWaiters_.begin());
				checkConsoleAddresses(next.first, true, std::move(next.second));
			}
		});
	}).detach();
}

void AppController::learnConsoleMac(const std::string &address, const std::string &mac)
{
	const std::string normal = normaliseHardwareId(mac);
	if(normal.empty())
		return;
	for(ConsoleEntry &console : settings_.consoles)
	{
		// Only the first time: a console that already has one keeps it, so
		// another machine taking its address never becomes it.
		if(console.address != address || !console.mac.empty())
			continue;
		console.mac = normal;
		store_.save(settings_);
		return;
	}
}

void AppController::relocateConsole(const std::string &from, const std::string &to)
{
	ConsoleEntry *entry = nullptr;
	for(ConsoleEntry &console : settings_.consoles)
	{
		if(console.address == from)
			entry = &console;
		else if(console.address == to)
		{
			// Two saved consoles cannot share an address: say so, and leave
			// the choice to the person.
			logWarning("A saved console now answers at " + to + ", where another saved console is.");
			emit notify(tr("Consoles"),
				tr("A saved console now answers at %1, which another saved console also uses. Check the "
					"addresses in Settings → Consoles.").arg(QString::fromStdString(to)),
				true);
			return;
		}
	}
	if(!entry)
		return;
	const QString name = QString::fromStdString(entry->name);
	entry->address = to;
	const bool active = settings_.consoleAddress == from;
	if(active)
		settings_.consoleAddress = to;
	// What was learned about the console goes with it.
	const QString oldKey = QString::fromStdString(from);
	const QString newKey = QString::fromStdString(to);
	for(QVariantMap *map : { &ftpReachable_, &jailbreaks_, &henVersions_ })
		if(map->contains(oldKey))
			map->insert(newKey, map->take(oldKey));
	store_.save(settings_);
	logInfo(entry->name + " moved on the network: " + from + " → " + to + ". Address updated.");
	if(active)
		rebuildBackends();
	emit settingsChanged();
	emit statusChanged();
	emit ftpReachableChanged();
	emit notify(tr("Consoles"),
		tr("%1 has a new address on the network: it was at %2 and is now at %3. Updated by itself.")
			.arg(name, oldKey, newKey),
		false);
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

void AppController::clearFinishedTasks()
{
	if(!queue_)
		return;
	bool removed = false;
	std::vector<QueueTask> all = queue_->tasks();
	const std::vector<QueueTask> done = queue_->history();
	all.insert(all.end(), done.begin(), done.end());
	for(const QueueTask &task : all)
		if(task.isTerminal())
			removed = queue_->remove(task.id) || removed;
	if(removed)
		refreshQueueModel();
}

void AppController::removeFinishedTasksOf(const QString &localPath)
{
	if(!queue_ || localPath.isEmpty())
		return;
	const QString key = transferKey(localPath);
	bool removed = false;
	std::vector<QueueTask> all = queue_->tasks();
	const std::vector<QueueTask> done = queue_->history();
	all.insert(all.end(), done.begin(), done.end());
	for(const QueueTask &task : all)
	{
		const bool finished = task.state == TaskState::Completed || task.state == TaskState::Error
			|| task.state == TaskState::Cancelled;
		if(finished && transferKey(QString::fromStdString(task.localPath)) == key)
			removed = queue_->remove(task.id) || removed;
	}
	if(removed)
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
		// What to do: GoldHEN's FTP on a PS4, a payload or an etaHEN
		// setting on a PS5.
		emit serviceHelp(QStringLiteral("ftp"), ftpHelpTitle(), ftpHelpText());
		return false;
	}
	return true;
}

void AppController::ftpNavigate(const QString &path)
{
	// A filter is for the folder it was typed in.
	ftpModel_.setFilter(QString());
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
	if(!ftp_)
		return;
	// FTP is asked only once it has answered the check: not while that is
	// still going on (it is read as soon as it answers), and not when it
	// does not answer, which the list says.
	if(!canUseFtp())
	{
		ftpModel_.clear();
		setFtpListError(status_.ftp.state == ServiceState::Unavailable
				? tr("The console's FTP is not answering: start it on the console (GoldHEN, etaHEN or the "
					 "ftpsrv payload) and try again.")
				: QString());
		return;
	}
	// A listing under way: another one once it is in.
	if(ftpBusy_)
	{
		ftpRelist_ = true;
		return;
	}
	setFtpBusy(true);
	const std::string path = ftpPath_.toStdString();
	std::thread([this, life = lifeline_, ftp = ftp_, path, announce]() {
		std::vector<FtpEntry> entries;
		FtpResult result = ftp->list(path, &entries);
		// A folder this console does not have (the PS4's /data/pkg on a
		// PS5): the nearest one up that it has, instead of an empty list.
		std::string shown = path;
		while(!result.ok && result.final && shown != "/")
		{
			shown = normalizeRemotePath(shown + "/..");
			entries.clear();
			result = ftp->list(shown, &entries);
		}
		if(!result.ok)
			shown = path;
		// A symbolic link may lead to a folder: asked, so it opens as one
		// (a few at most — each question is a connection of its own).
		int asked = 0;
		bool turned = false;
		for(FtpEntry &entry : entries)
			if(entry.isSymlink && !entry.isDirectory && asked++ < 16 && ftp->isDirectory(entry.path))
				entry.isDirectory = turned = true;
		// Folders first, as the listing has them.
		if(turned)
			std::stable_sort(entries.begin(), entries.end(), [](const FtpEntry &a, const FtpEntry &b) {
				if(a.isDirectory != b.isDirectory)
					return a.isDirectory;
				return toLower(a.name) < toLower(b.name);
			});
		const QString error = translateMessage(result.message);
		const QString missing = shown != path ? QString::fromStdString(path) : QString();
		const QString landed = QString::fromStdString(shown);
		life->post(
			[this, entries, result, error, announce, missing, landed]() {
				if(result.ok && !missing.isEmpty())
				{
					ftpPath_ = landed;
					emit ftpPathChanged();
					setStatusMessage(tr("%1 is not on this console: showing %2.").arg(missing, landed));
				}
				if(result.ok)
				{
					setFtpListError(QString());
					ftpModel_.setEntries(entries);
					updateFtpSpace(entries);
					if(announce && missing.isEmpty())
						setStatusMessage(tr("%1: %2 entries").arg(ftpPath_).arg(entries.size()));
				}
				else if(!announce)
				{
					// A refresh nobody asked for keeps what was on screen —
					// and says why, when there was nothing on screen.
					if(ftpModel_.rowCount() == 0)
						setFtpListError(error);
				}
				else
				{
					ftpModel_.clear();
					setFtpListError(error);
					setStatusMessage(tr("FTP: %1").arg(error));
					emit notify(tr("FTP"),
						tr("I could not list %1: %2").arg(ftpPath_).arg(error), true);
				}
				setFtpBusy(false);
				if(ftpRelist_)
				{
					ftpRelist_ = false;
					refreshFtpListing(false);
				}
			});
	}).detach();
}

namespace {

// The disk a path of the console is on, as its first folders say: "/data",
// "/user", "/mnt/usb0".
QString diskOf(const QString &path)
{
	const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
	if(parts.isEmpty())
		return QStringLiteral("/");
	if(parts.first() == QLatin1String("mnt") && parts.size() > 1)
		return QStringLiteral("/mnt/") + parts.at(1);
	return QLatin1Char('/') + parts.first();
}

} // namespace

void AppController::updateFtpSpace(const std::vector<FtpEntry> &entries)
{
	int folders = 0;
	int files = 0;
	qint64 bytes = 0;
	for(const FtpEntry &entry : entries)
	{
		if(entry.isDirectory)
			++folders;
		else
		{
			++files;
			bytes += entry.size;
		}
	}
	const QString disk = diskOf(ftpPath_);
	QVariantMap space;
	space[QStringLiteral("folders")] = folders;
	space[QStringLiteral("files")] = files;
	space[QStringLiteral("bytes")] = static_cast<double>(bytes);
	const auto known = ftpFree_.constFind(disk);
	space[QStringLiteral("free")] = known != ftpFree_.cend() ? static_cast<double>(known->first) : -1.0;
	if(space != ftpSpace_)
	{
		ftpSpace_ = space;
		emit ftpSpaceChanged();
	}
	// The room left, asked once in a while for each disk ("/" is not one).
	const qint64 now = QDateTime::currentSecsSinceEpoch();
	if(!ftp_ || disk == QLatin1String("/") || (known != ftpFree_.cend() && now - known->second < 30))
		return;
	ftpFree_[disk] = qMakePair(known != ftpFree_.cend() ? known->first : qint64(-1), now);
	std::thread([this, life = lifeline_, ftp = ftp_, disk]() {
		int64_t free = -1;
		if(!ftp->freeSpace(disk.toStdString(), &free))
			return;
		life->post([this, disk, free]() {
			ftpFree_[disk] = qMakePair(static_cast<qint64>(free), QDateTime::currentSecsSinceEpoch());
			if(diskOf(ftpPath_) != disk)
				return;
			ftpSpace_[QStringLiteral("free")] = static_cast<double>(free);
			emit ftpSpaceChanged();
		});
	}).detach();
}

void AppController::ftpDelete(const QString &path, bool isDirectory)
{
	ftpDeleteMany({ QVariantMap { { QStringLiteral("path"), path }, { QStringLiteral("isDirectory"), isDirectory } } });
}

void AppController::ftpDeleteMany(const QVariantList &items)
{
	if(items.isEmpty() || !ftpReady(tr("Delete")))
		return;
	std::vector<std::pair<std::string, bool>> targets;
	for(const QVariant &value : items)
	{
		const QVariantMap item = value.toMap();
		const QString path = item.value(QStringLiteral("path")).toString();
		if(path.isEmpty() || ftpDeleting_.contains(path))
			continue;
		targets.emplace_back(path.toStdString(), item.value(QStringLiteral("isDirectory")).toBool());
		ftpDeleting_ << path;
	}
	if(targets.empty())
		return;
	emit ftpDeletingChanged();
	std::thread([this, life = lifeline_, ftp = ftp_, targets]() {
		int deleted = 0;
		QString failure;
		for(const auto &[target, isDirectory] : targets)
		{
			// A folder goes with everything in it: a console's server refuses
			// RMD for one that is not empty ("QUOT string not accepted").
			const FtpResult result = removeOnConsole(*ftp, target, isDirectory);
			const QString path = QString::fromStdString(target);
			if(result.ok)
			{
				++deleted;
				logInfo("FTP: deleted " + target);
			}
			else if(failure.isEmpty())
				failure = tr("%1: %2").arg(QString::fromStdString(baseName(target)), translateMessage(result.message));
			life->post([this, path]() {
				ftpDeleting_.removeAll(path);
				emit ftpDeletingChanged();
				refreshFtpListing(false);
			});
		}
		const int count = static_cast<int>(targets.size());
		life->post([this, deleted, count, failure]() {
			const bool ok = failure.isEmpty();
			setStatusMessage(ok ? tr("Deleted.") : tr("FTP: %1").arg(failure));
			// Deleting is destructive: always confirm, whether it worked or not.
			emit notify(tr("Delete"),
				ok ? tr("%n item(s) deleted from the console.", "", deleted)
				   : count > 1 ? tr("%1 of %2 deleted. I could not delete %3").arg(deleted).arg(count).arg(failure)
							   : tr("I could not delete it: %1").arg(failure),
				!ok);
			refreshFtpListing(false);
		});
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
	std::thread([this, life = lifeline_, ftp = ftp_, target]() {
		const FtpResult result = ftp->makeDirectory(target);
		const QString message = translateMessage(result.message);
		const bool ok = result.ok;
		life->post(
			[this, ok, message]() {
				setFtpBusy(false);
				setStatusMessage(ok ? tr("Folder created.") : tr("FTP: %1").arg(message));
				emit notify(tr("Create folder"),
					ok ? tr("Folder created.") : tr("I could not create the folder: %1").arg(message),
					!ok);
				if(ok)
					ftpRefresh();
			});
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
	std::thread([this, life = lifeline_, ftp = ftp_, from, to]() {
		const FtpResult result = ftp->rename(from, to);
		const QString message = translateMessage(result.message);
		const bool ok = result.ok;
		life->post(
			[this, ok, message]() {
				setFtpBusy(false);
				setStatusMessage(ok ? tr("Renamed.") : tr("FTP: %1").arg(message));
				if(!ok)
					emit notify(tr("Rename"),
						tr("I could not rename it: %1").arg(message), true);
				if(ok)
					ftpRefresh();
			});
	}).detach();
}

void AppController::ftpMove(const QString &path, const QString &destinationDir)
{
	ftpMoveMany({ path }, destinationDir);
}

void AppController::ftpMoveMany(const QStringList &paths, const QString &destinationDir)
{
	transferOnConsole(paths, destinationDir, false);
}

void AppController::ftpCopyMany(const QStringList &paths, const QString &destinationDir)
{
	transferOnConsole(paths, destinationDir, true);
}

void AppController::transferOnConsole(const QStringList &paths, const QString &destinationDir, bool copy)
{
	const QString title = copy ? tr("Copy") : tr("Move");
	const QString dir = destinationDir.trimmed();
	if(dir.isEmpty() || paths.isEmpty())
	{
		emit notify(title, copy ? tr("Type the folder to copy it to.") : tr("Type the folder to move it to."), true);
		return;
	}
	if(!ftpReady(title))
		return;
	// Names the folder open already has (a copy into it must not take one).
	const std::string here = normalizeRemotePath(ftpPath_.toStdString());
	auto taken = [this, &here](const std::string &dirPath, const std::string &name) {
		return normalizeRemotePath(dirPath) == here
			&& ftpModel_.containsPath(QString::fromStdString(normalizeRemotePath(here + "/" + name)));
	};
	std::vector<std::pair<std::string, std::string>> moves;
	for(const QString &path : paths)
	{
		const std::string from = normalizeRemotePath(path.toStdString());
		std::string name = from;
		while(!name.empty() && name.back() == '/')
			name.pop_back();
		name = name.substr(name.find_last_of('/') + 1);
		if(name.empty())
			continue;
		std::string to = normalizeRemotePath(dir.toStdString() + "/" + name);
		// A copy into its own folder: "name copy.ext", "name copy 2.ext"…
		if(copy && (to == from || taken(dir.toStdString(), name)))
		{
			const size_t dot = name.find_last_of('.');
			const bool keepExtension = dot != std::string::npos && dot > 0;
			const std::string stem = keepExtension ? name.substr(0, dot) : name;
			const std::string extension = keepExtension ? name.substr(dot) : std::string();
			for(int n = 1; n < 100; ++n)
			{
				const std::string candidate = stem + (n == 1 ? " copy" : " copy " + std::to_string(n)) + extension;
				if(!taken(dir.toStdString(), candidate))
				{
					to = normalizeRemotePath(dir.toStdString() + "/" + candidate);
					break;
				}
			}
		}
		if(to != from)
			moves.emplace_back(from, to);
	}
	if(moves.empty())
		return;
	// Between two disks (and every copy) it goes through this PC, on the
	// transfer bar: one transfer at a time there.
	if(downloadActive_)
	{
		emit notify(title,
			tr("A transfer is already running (%1). Wait for it to finish.").arg(downloadName_), true);
		return;
	}
	setFtpBusy(true);
	downloadCancel_ = std::make_shared<std::atomic<bool>>(false);
	const std::string scratch = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
									.filePath(QStringLiteral("moving"))
									.toStdString();
	std::thread([this, life = lifeline_, ftp = ftp_, cancel = downloadCancel_, moves, scratch, copy]() {
		bool shown = false;
		bool cancelled = false;
		int moved = 0;
		QString failure;
		QString lastTarget;
		for(const auto &[from, to] : moves)
		{
			const QString shownName = QString::fromStdString(baseName(from));
			int64_t lastPost = 0;
			bool announced = false;
			bool throughPc = false;
			const FtpProgressCallback progress = [&](int64_t done, int64_t total) {
				if(cancel->load())
					return false;
				const int64_t now = monotonicMillis();
				if(!announced || now - lastPost >= 200)
				{
					const double fraction = total > 0 ? static_cast<double>(done) / static_cast<double>(total) : 0.0;
					const bool first = !announced;
					announced = true;
					shown = true;
					lastPost = now;
					life->post([this, fraction, first, shownName, copy]() {
						if(first)
						{
							downloadActive_ = true;
							downloadMoving_ = !copy;
							downloadCopying_ = copy;
							downloadName_ = shownName;
							setStatusMessage(copy ? tr("Copying %1 through this PC…").arg(shownName)
												  : tr("Moving %1 to another disk through this PC…").arg(shownName));
						}
						downloadProgress_ = fraction;
						emit downloadChanged();
					});
				}
				return true;
			};
			const FtpResult result = copy ? copyOnConsole(*ftp, from, to, scratch, progress)
										  : moveOnConsole(*ftp, from, to, scratch, progress, &throughPc);
			if(result.cancelled || cancel->load())
			{
				cancelled = true;
				break;
			}
			if(!result.ok)
			{
				if(failure.isEmpty())
					failure = tr("%1: %2").arg(shownName, translateMessage(result.message));
				continue;
			}
			++moved;
			lastTarget = QString::fromStdString(to);
		}
		const int count = static_cast<int>(moves.size());
		life->post(
			[this, moved, count, cancelled, failure, lastTarget, shown, copy]() {
				setFtpBusy(false);
				if(shown)
				{
					downloadActive_ = false;
					downloadMoving_ = false;
					downloadCopying_ = false;
					downloadProgress_ = 0.0;
					emit downloadChanged();
				}
				if(cancelled)
					setStatusMessage(copy ? tr("Copy cancelled: the original is where it was.")
										  : tr("Move cancelled: the original is where it was."));
				else if(!failure.isEmpty())
				{
					setStatusMessage(tr("FTP: %1").arg(failure));
					if(copy)
						emit notify(tr("Copy"), count > 1 ? tr("%1 of %2 copied. I could not copy %3").arg(moved).arg(count).arg(failure)
														 : tr("I could not copy it: %1").arg(failure), true);
					else
						emit notify(tr("Move"), count > 1 ? tr("%1 of %2 moved. I could not move %3").arg(moved).arg(count).arg(failure)
														 : tr("I could not move it: %1").arg(failure), true);
				}
				else if(copy)
					setStatusMessage(count > 1 ? tr("%n item(s) copied.", "", moved) : tr("Copied to %1").arg(lastTarget));
				else
					setStatusMessage(count > 1 ? tr("%n item(s) moved.", "", moved) : tr("Moved to %1").arg(lastTarget));
				ftpRefresh();
			});
	}).detach();
}

void AppController::ftpChmod(const QVariantList &items, const QString &mode, bool everythingInside)
{
	const std::string octal = mode.trimmed().toStdString();
	if(octal.empty() || octal.size() > 4 || octal.find_first_not_of("01234567") != std::string::npos)
	{
		emit notify(tr("Permissions"), tr("The permissions are three digits from 0 to 7, as 755 or 777."), true);
		return;
	}
	if(!ftpReady(tr("Permissions")))
		return;
	std::vector<std::pair<std::string, bool>> targets;
	for(const QVariant &item : items)
	{
		const QVariantMap map = item.toMap();
		const std::string path = map.value(QStringLiteral("path")).toString().toStdString();
		if(!path.empty())
			targets.emplace_back(path, map.value(QStringLiteral("isDirectory")).toBool());
	}
	if(targets.empty())
		return;
	setFtpBusy(true);
	std::thread([this, life = lifeline_, ftp = ftp_, targets, octal, everythingInside]() {
		int changed = 0;
		QString failure;
		for(const auto &[path, isDirectory] : targets)
		{
			const FtpResult result = chmodOnConsole(*ftp, path, isDirectory, octal, everythingInside);
			if(result.ok)
				++changed;
			else if(failure.isEmpty())
				failure = tr("%1: %2").arg(QString::fromStdString(baseName(path)), translateMessage(result.message));
		}
		const int count = static_cast<int>(targets.size());
		const QString shownMode = QString::fromStdString(octal);
		life->post([this, changed, count, failure, shownMode]() {
			setFtpBusy(false);
			if(!failure.isEmpty())
			{
				setStatusMessage(tr("FTP: %1").arg(failure));
				emit notify(tr("Permissions"),
					tr("The console did not change the permissions: %1. Its FTP server may not have SITE CHMOD.")
						.arg(failure),
					true);
			}
			else
				setStatusMessage(tr("Permissions %1 set on %n item(s).", "", changed).arg(shownMode));
			(void)count;
			ftpRefresh();
		});
	}).detach();
}

void AppController::ftpLoadText(const QString &path)
{
	if(!ftp_)
		return;
	const std::string remote = normalizeRemotePath(path.toStdString());
	const QString local = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
							  .filePath(QStringLiteral("edit-read.txt"));
	QDir().mkpath(QFileInfo(local).absolutePath());
	std::thread([this, life = lifeline_, ftp = ftp_, remote, local, path]() {
		QString text;
		QString error;
		int64_t size = -1;
		if(ftp->remoteSize(remote, &size).ok && size > 1024 * 1024)
			error = tr("It is too big to edit here (%1; up to 1 MB).").arg(QString::fromStdString(humanBytes(size)));
		else
		{
			const FtpResult got = ftp->download(remote, local.toStdString());
			if(!got.ok)
				error = translateMessage(got.message);
			else
			{
				QFile file(local);
				QByteArray bytes;
				if(file.open(QIODevice::ReadOnly))
					bytes = file.read(1024 * 1024 + 1);
				file.close();
				QFile::remove(local);
				QStringDecoder utf8(QStringDecoder::Utf8);
				const QString decoded = utf8(bytes);
				// Read only up to 1 MB: a bigger one (its server did not say
				// its size) would be cut short when saved back.
				if(bytes.size() > 1024 * 1024)
					error = tr("It is too big to edit here (%1; up to 1 MB).").arg(tr("over 1 MB"));
				// A NUL byte, or bytes that are not UTF-8: saving it back
				// from here would spoil it.
				else if(bytes.contains('\0'))
					error = tr("It is not a text file.");
				else if(utf8.hasError())
					error = tr("It is not UTF-8 text: saving it from here could spoil it.");
				else
					text = decoded;
			}
		}
		life->post([this, path, text, error]() { emit ftpTextLoaded(path, text, error); });
	}).detach();
}

void AppController::ftpSaveText(const QString &path, const QString &text)
{
	if(!ftpReady(tr("Save")))
		return;
	const std::string remote = normalizeRemotePath(path.toStdString());
	const QString local = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
							  .filePath(QStringLiteral("edit-write.txt"));
	QDir().mkpath(QFileInfo(local).absolutePath());
	{
		QFile file(local);
		if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		{
			emit ftpTextSaved(path, tr("Could not write a temporary file on this PC."));
			return;
		}
		file.write(text.toUtf8());
	}
	setFtpBusy(true);
	std::thread([this, life = lifeline_, ftp = ftp_, remote, local, path]() {
		const FtpResult sent = ftp->upload(local.toStdString(), remote);
		QFile::remove(local);
		const QString error = sent.ok ? QString() : translateMessage(sent.message);
		life->post([this, path, error]() {
			setFtpBusy(false);
			if(error.isEmpty())
				setStatusMessage(tr("%1 saved on the console.").arg(QFileInfo(path).fileName()));
			else
				emit notify(tr("Save"), tr("Could not save %1: %2").arg(QFileInfo(path).fileName(), error), true);
			emit ftpTextSaved(path, error);
			ftpRefresh();
		});
	}).detach();
}

void AppController::ftpServerCommand(const QString &command)
{
	// Only these, and only in Advanced mode: they act on the console itself.
	const QString name = command.trimmed().toUpper();
	if(!settings_.ftpAdvancedMode || (name != QLatin1String("MTRW") && name != QLatin1String("KILL")))
		return;
	if(!ftpReady(tr("FTP server")))
		return;
	std::thread([this, life = lifeline_, ftp = ftp_, name]() {
		std::string reply;
		const FtpResult result = ftp->command(name.toStdString(), &reply);
		logInfo("FTP: " + name.toStdString() + " -> " + (reply.empty() ? result.message : reply));
		const QString answer = QString::fromStdString(reply.empty() ? result.message : reply);
		life->post([this, name, result, answer]() {
			if(result.ok)
				emit notify(tr("FTP server"), tr("The console's FTP server answered %1: %2").arg(name, answer), false);
			else
				emit notify(tr("FTP server"),
					tr("The console's FTP server does not take %1 (%2). Only ftpsrv on a PS5 has it.").arg(name, answer),
					true);
			if(name == QLatin1String("KILL"))
				checkServicesNow();
		});
	}).detach();
}

void AppController::ftpDownloadFolder(const QString &remotePath, const QString &name,
	const QString &destinationDir)
{
	if(downloadActive_)
	{
		emit notify(tr("Bring to the PC"),
			tr("A transfer is already running (%1). Wait for it to finish.").arg(downloadName_), true);
		return;
	}
	if(!ftpReady(tr("Bring to the PC")))
		return;
	QString dir = destinationDir;
	if(dir.startsWith(QStringLiteral("file:")))
		dir = QUrl(dir).toLocalFile();
	if(dir.trimmed().isEmpty())
		dir = defaultDownloadDirectory();
	const QString root = uniqueLocalPath(QDir(dir).filePath(name));
	QDir().mkpath(root);

	downloadCancel_ = std::make_shared<std::atomic<bool>>(false);
	downloadActive_ = true;
	downloadName_ = name;
	downloadProgress_ = 0.0;
	emit downloadChanged();
	setStatusMessage(tr("Downloading %1…").arg(name));

	const std::string remoteRoot = normalizeRemotePath(remotePath.toStdString());
	const std::string localRoot = root.toStdString();
	std::thread([this, life = lifeline_, ftp = ftp_, cancel = downloadCancel_, remoteRoot, localRoot, name, root]() {
		// First everything that is inside, to know the total.
		struct Item { std::string remote; std::string local; int64_t size; };
		std::vector<Item> files;
		std::vector<std::pair<std::string, std::string>> pending { { remoteRoot, localRoot } };
		std::string failure;
		while(!pending.empty() && failure.empty() && !cancel->load())
		{
			const auto [remoteDir, localDir] = pending.back();
			pending.pop_back();
			QDir().mkpath(QString::fromStdString(localDir));
			std::vector<FtpEntry> entries;
			const FtpResult listed = ftp->list(remoteDir, &entries);
			if(!listed.ok)
			{
				failure = listed.message;
				break;
			}
			for(const FtpEntry &e : entries)
			{
				if(e.name == "." || e.name == "..")
					continue;
				const std::string local = localDir + "/" + e.name;
				if(e.isDirectory)
					pending.emplace_back(e.path, local);
				else
					files.push_back({ e.path, local, e.size });
			}
		}
		int64_t total = 0;
		for(const Item &f : files)
			total += f.size;
		int64_t before = 0;
		int lastPercent = -1;
		for(const Item &f : files)
		{
			if(!failure.empty() || cancel->load())
				break;
			const FtpResult result = ftp->download(f.remote, f.local,
				[&](int64_t done, int64_t) {
					if(cancel->load())
						return false;
					const double fraction = total > 0 ? double(before + done) / double(total) : 0.0;
					const int percent = static_cast<int>(fraction * 100);
					if(percent != lastPercent)
					{
						lastPercent = percent;
						life->post([this, fraction]() {
							downloadProgress_ = fraction;
							emit downloadChanged();
						});
					}
					return true;
				});
			if(!result.ok && !result.cancelled)
				failure = result.message;
			before += f.size;
		}
		const bool cancelled = cancel->load();
		const QString message = translateMessage(failure);
		const int count = static_cast<int>(files.size());
		life->post([this, cancelled, message, name, root, count]() {
			downloadActive_ = false;
			downloadProgress_ = message.isEmpty() && !cancelled ? 1.0 : 0.0;
			emit downloadChanged();
			if(cancelled)
				setStatusMessage(tr("Download cancelled."));
			else if(!message.isEmpty())
			{
				setStatusMessage(tr("FTP: %1").arg(message));
				emit notify(tr("Download failed"), message, true);
				emit jobFinished(tr("Download failed"), message, true);
			}
			else
			{
				setStatusMessage(tr("Saved to %1").arg(root));
				const QString done = tr("%1 (%n file(s)) saved to %2", "", count).arg(name, root);
				emit notify(tr("Download finished"), done, false);
				emit jobFinished(tr("Download finished"), done, false);
			}
		});
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

void AppController::ftpListFolders(const QString &path)
{
	if(!ftp_)
	{
		emit ftpFoldersListed(path, {}, tr("FTP is not connected."));
		return;
	}
	const std::string dir = normalizeRemotePath(path.toStdString());
	std::thread([this, life = lifeline_, ftp = ftp_, path, dir]() {
		std::vector<FtpEntry> entries;
		const FtpResult result = ftp->list(dir, &entries);
		QStringList folders;
		for(const FtpEntry &e : entries)
			if(e.isDirectory || e.isSymlink)
				folders << QString::fromStdString(e.name);
		const QString error = result.ok ? QString() : translateMessage(result.message);
		life->post([this, path, folders, error]() {
			emit ftpFoldersListed(path, folders, error);
		});
	}).detach();
}

void AppController::ftpCreateFolder(const QString &path)
{
	if(!ftp_)
	{
		emit ftpFolderCreated(path, tr("FTP is not connected."));
		return;
	}
	const std::string target = normalizeRemotePath(path.toStdString());
	std::thread([this, life = lifeline_, ftp = ftp_, path, target]() {
		const FtpResult result = ftp->makeDirectory(target);
		const QString error = result.ok ? QString() : translateMessage(result.message);
		life->post([this, path, error]() {
			emit ftpFolderCreated(path, error);
			if(error.isEmpty())
				refreshFtpListing(false);
		});
	}).detach();
}

void AppController::startFtpDrag(const QString &remotePath, const QString &localUrl)
{
	// A real system drag: the file (when it is on the PC) for the desktop and
	// other programs, and the console path for the folders of the list.
	auto *data = new QMimeData;
	data->setData(QStringLiteral("application/x-orbislink-ftp-path"), remotePath.toUtf8());
	if(!localUrl.isEmpty())
		data->setUrls({ QUrl(localUrl) });
	auto *drag = new QDrag(this);
	drag->setMimeData(data);
	drag->setPixmap(QPixmap(QStringLiteral(":/icons/logo.png"))
		.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
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

	downloadCancel_ = std::make_shared<std::atomic<bool>>(false);
	downloadActive_ = true;
	downloadName_ = name;
	downloadProgress_ = 0.0;
	emit downloadChanged();
	setStatusMessage(tr("Downloading %1…").arg(name));

	const std::string remote = remotePath.toStdString();
	const std::string local = localPath.toStdString();
	std::thread([this, life = lifeline_, ftp = ftp_, cancel = downloadCancel_, remote, local, remotePath, localPath, name, forDrag]() {
		int64_t lastReported = -1;
		const FtpResult result = ftp->download(
			remote, local,
			[this, life, cancel, &lastReported](int64_t done, int64_t total) {
				if(cancel->load())
					return false;
				// The UI is only woken on each percentage point: in a
				// gigabyte transfer this is called thousands of times.
				const int64_t percent = total > 0 ? (done * 100) / total : 0;
				if(percent != lastReported)
				{
					lastReported = percent;
					const double fraction = total > 0 ? double(done) / double(total) : 0.0;
					life->post(
						[this, fraction]() {
							downloadProgress_ = fraction;
							emit downloadChanged();
						});
				}
				return true;
			});

		const bool ok = result.ok;
		const bool cancelled = result.cancelled || cancel->load();
		const QString message = translateMessage(result.message);
		life->post(
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
					emit jobFinished(tr("Download failed"), message, true);
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
					const QString done = tr("%1 saved to %2").arg(name, QFileInfo(localPath).path());
					emit notify(tr("Download finished"), done, false);
					emit jobFinished(tr("Download finished"), done, false);
				}
			});
	}).detach();
}

void AppController::cancelDownload()
{
	if(!downloadActive_)
		return;
	// It is enough for the progress callback to return false: it aborts
	// only this transfer. FtpClient::cancel() is for the whole client and
	// would also kill a queue upload running at the same time.
	downloadCancel_->store(true);
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

void AppController::setOverlayOpacity(double opacity)
{
	opacity = std::min(1.0, std::max(overlayMinOpacity(), opacity));
	if(std::abs(opacity - settings_.overlayOpacity) < 0.001)
		return;
	updateSettings([opacity](Settings &s) { s.overlayOpacity = opacity; });
}

void AppController::updateSettings(const std::function<void(Settings &)> &change)
{
	change(settings_);
	store_.save(settings_);
	emit settingsChanged();
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
	// Taking a direct connection says nothing while the console is off: its
	// FTP answering is what says it is on.
	else if(state == QLatin1String("direct"))
		mapped = canUseFtp() ? RemotePlayState::Direct : RemotePlayState::Offline;
	console_->setRemotePlayState(mapped, detail.toStdString());
}

void AppController::probeConsole(const QString &address, int ftpPort, int installerPort, bool ps5)
{
	const std::string host = trim(address.toStdString());
	const auto port = [](int value, uint16_t fallback) -> uint16_t {
		return (value > 0 && value < 65536) ? static_cast<uint16_t>(value) : fallback;
	};
	const uint16_t ftp = port(ftpPort, 2121);
	const uint16_t rpi = port(installerPort, 12800);
	const uint64_t generation = ++probeGeneration_;

	std::thread([this, life = lifeline_, address, host, ftp, rpi, ps5, generation]() {
		// Short timeout: this runs while typing, it must not drag on. The
		// other FTP ports are asked at the same time, so it takes no longer.
		const ProbeResult probe = probeConsoleServices(host, ftp, rpi, 1200, ftpPortCandidates(ftp, ps5), ps5);
		const QString detail =
			QString::fromStdString(probe.ftpOk ? probe.ftpDetail : probe.installerDetail);
		const int foundPort = probe.ftpPort;
		life->post(
			[this, address, probe, detail, foundPort, generation]() {
				if(generation != probeGeneration_.load())
					return;
				emit consoleProbed(address, probe.ftpOk, probe.installerOk, detail, foundPort);
			});
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

	std::thread([this, life = lifeline_, config, silentWhenUpToDate]() {
		const UpdateCheckResult result = UpdateChecker(config).check();
		life->post(
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
			});
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

	std::thread([this, life = lifeline_, url, destination, shaUrl, expectedSha]() {
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
			[this, life](int64_t received, int64_t total) {
				const double fraction = total > 0
					? static_cast<double>(received) / static_cast<double>(total)
					: 0.0;
				life->post(
					[this, fraction]() {
						updateProgress_ = fraction;
						emit updateChanged();
					});
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

		life->post(
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
			});
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
	map[QStringLiteral("keepConvertedPackages")] = settings_.keepConvertedPackages;
	map[QStringLiteral("autoSaveBackup")] = settings_.autoSaveBackup;
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
	settings_.keepConvertedPackages = boolOr("keepConvertedPackages", settings_.keepConvertedPackages);
	settings_.autoSaveBackup = boolOr("autoSaveBackup", settings_.autoSaveBackup);
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
	// A plain theme is just that: the custom colours step aside (kept for
	// the "custom" button) instead of being mixed with it.
	const bool hadCustom = !settings_.themeColors.empty();
	if(hadCustom)
	{
		settings_.lastCustomColors = settings_.themeColors;
		settings_.lastCustomTheme = settings_.theme;
		settings_.themeColors.clear();
	}
	if(settings_.theme == themeName && !hadCustom)
		return;
	settings_.theme = themeName;
	store_.save(settings_);
	if(hadCustom)
		emit themeColorsChanged();
	emit settingsChanged();
}

void AppController::saveKeyBindings(const std::map<std::string, int> &bindings)
{
	settings_.keyboardBindings = bindings;
	store_.save(settings_);
	emit settingsChanged();
}

void AppController::savePadBindings(const std::map<std::string, std::string> &bindings)
{
	settings_.padBindings = bindings;
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
