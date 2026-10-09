// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/saves_controller.h"

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/qt/app_controller.h"
#include "orbislink/qt/save_database.h"

#include <QByteArray>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <memory>
#include <set>
#include <utility>

namespace orbislink {

namespace {

// The console's FTP as the vault sees it.
class FtpRemote : public SaveRemote
{
public:
	explicit FtpRemote(FtpClient &ftp) : ftp_(ftp) {}

	bool list(const std::string &dir, std::vector<FtpEntry> *entries, std::string *error) override
	{
		const FtpResult result = ftp_.list(dir, entries);
		if(!result.ok && error)
			*error = result.message;
		return result.ok;
	}
	bool download(const std::string &remote, const std::string &local, std::string *error) override
	{
		const FtpResult result = ftp_.download(remote, local);
		if(!result.ok && error)
			*error = result.message;
		return result.ok;
	}
	bool upload(const std::string &local, const std::string &remote, std::string *error) override
	{
		const FtpResult result = ftp_.upload(local, remote);
		if(!result.ok && error)
			*error = result.message;
		return result.ok;
	}
	bool makeDirectory(const std::string &dir) override { return ftp_.makeDirectory(dir).ok; }

private:
	FtpClient &ftp_;
};

QString syncName(SaveSync sync)
{
	switch(sync)
	{
		case SaveSync::Same: return QStringLiteral("same");
		case SaveSync::Changed: return QStringLiteral("changed");
		case SaveSync::Older: return QStringLiteral("older");
		case SaveSync::ConsoleOnly: return QStringLiteral("console");
		case SaveSync::VaultOnly: return QStringLiteral("vault");
	}
	return QString();
}

// The save's account in base64, the form the app keeps Account IDs in, and
// the name it has in the app if it is one of them (either byte order).
QString psidOf(const std::string &hex)
{
	if(hex.size() != 16)
		return QString();
	return QString::fromLatin1(QByteArray::fromHex(QByteArray::fromStdString(hex)).toBase64());
}

// The PSID as the PS4 names its folders of saves: the 8 bytes of the
// Account ID the other way round, in hex.
QString psidFolderFromBase64(const QString &accountId)
{
	QByteArray bytes = QByteArray::fromBase64(accountId.toLatin1());
	if(bytes.size() != 8)
		return QString();
	std::reverse(bytes.begin(), bytes.end());
	return QString::fromLatin1(bytes.toHex());
}

// And back: the Account ID (base64) of a PSID folder.
QString accountIdFromPsidFolder(const QString &folder)
{
	QByteArray bytes = QByteArray::fromHex(folder.toLatin1());
	if(folder.size() != 16 || bytes.size() != 8)
		return QString();
	std::reverse(bytes.begin(), bytes.end());
	return QString::fromLatin1(bytes.toBase64());
}

// A PSID as a number (as the console's list of saves keeps it) → its folder.
std::string psidFolderOf(int64_t number)
{
	char text[17] = {};
	std::snprintf(text, sizeof(text), "%016" PRIx64, static_cast<uint64_t>(number));
	return text;
}

QString labelOfAccount(const QString &accountId, const Settings &settings)
{
	for(const SavedAccount &account : settings.accounts)
		if(QString::fromStdString(account.accountId) == accountId)
			return QString::fromStdString(account.label);
	return QString();
}

QString psidName(const QString &psid, const Settings &settings)
{
	if(psid.isEmpty())
		return QString();
	QByteArray reversed = QByteArray::fromBase64(psid.toLatin1());
	std::reverse(reversed.begin(), reversed.end());
	const QString other = QString::fromLatin1(reversed.toBase64());
	for(const SavedAccount &account : settings.accounts)
	{
		const QString known = QString::fromStdString(account.accountId);
		if(known == psid || known == other)
			return QString::fromStdString(account.label);
	}
	return QString();
}

QString sizeText(int64_t bytes)
{
	return bytes > 0 ? QString::fromStdString(humanBytes(bytes)) : QString();
}

} // namespace

SavesController::SavesController(AppController *app, QObject *parent) : QObject(parent), app_(app)
{
	connect(app_, &AppController::settingsChanged, this, &SavesController::vaultFolderChanged);
	// Which consoles there are, and whose FTP answers.
	connect(app_, &AppController::settingsChanged, this, &SavesController::consolesChanged);
	connect(app_, &AppController::statusChanged, this, &SavesController::consolesChanged);
	connect(app_, &AppController::ftpReachableChanged, this, &SavesController::consolesChanged);
}

SavesController::~SavesController()
{
	if(worker_.joinable())
		worker_.join();
	if(inspector_.joinable())
		inspector_.join();
}

QString SavesController::vaultFolder() const
{
	const QString chosen = QString::fromStdString(app_->settings().saveVaultFolder);
	if(!chosen.isEmpty())
		return chosen;
	return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
		.filePath(QStringLiteral("OrbisLink/Saves"));
}

QUrl SavesController::vaultUrl() const { return QUrl::fromLocalFile(vaultFolder()); }

void SavesController::setVaultFolder(const QString &folder)
{
	const QUrl url(folder);
	const QString path = url.isLocalFile() ? QDir::cleanPath(url.toLocalFile()) : QDir::cleanPath(folder);
	if(path.isEmpty())
		return;
	app_->updateSettings([&](Settings &s) { s.saveVaultFolder = path.toStdString(); });
	emit vaultFolderChanged();
	refresh();
}

void SavesController::openVaultFolder() const
{
	QDir().mkpath(vaultFolder());
	QDesktopServices::openUrl(vaultUrl());
}

QVariantList SavesController::consoles() const
{
	QVariantList items;
	const Settings &settings = app_->settings();
	const QString shown = source();
	const QVariantMap reachable = app_->ftpReachable();
	for(const ConsoleEntry &console : settings.consoles)
	{
		const QString address = QString::fromStdString(console.address);
		const bool active = console.address == settings.consoleAddress;
		QVariantMap item;
		item[QStringLiteral("address")] = address;
		item[QStringLiteral("name")] = QString::fromStdString(console.name.empty() ? console.address : console.name);
		item[QStringLiteral("type")] = QString::fromStdString(console.type);
		item[QStringLiteral("active")] = active;
		item[QStringLiteral("ftp")] = active ? app_->canUseFtp() : reachable.value(address).toBool();
		item[QStringLiteral("source")] = address == shown;
		items << item;
	}
	return items;
}

QString SavesController::source() const
{
	const Settings &settings = app_->settings();
	if(!source_.isEmpty())
		for(const ConsoleEntry &console : settings.consoles)
			if(QString::fromStdString(console.address) == source_)
				return source_;
	return QString::fromStdString(settings.consoleAddress);
}

QString SavesController::sourceName() const { return consoleAt(source()).name; }

bool SavesController::online() const
{
	const QString shown = source();
	if(shown.toStdString() == app_->settings().consoleAddress)
		return app_->canUseFtp();
	return app_->ftpReachable().value(shown).toBool();
}

void SavesController::setSource(const QString &address)
{
	const QString chosen = address.trimmed();
	const QString active = QString::fromStdString(app_->settings().consoleAddress);
	const QString next = chosen == active ? QString() : chosen;
	if(next == source_)
		return;
	source_ = next;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		current_.clear();
	}
	saves_.clear();
	users_.clear();
	scanned_ = false;
	emit consolesChanged();
	emit savesChanged();
	refresh();
}

SavesController::Console SavesController::consoleAt(const QString &address) const
{
	const Settings &settings = app_->settings();
	Console console;
	console.address = address.isEmpty() ? source() : address;
	console.name = console.address;
	for(const ConsoleEntry &entry : settings.consoles)
		if(QString::fromStdString(entry.address) == console.address)
		{
			if(!entry.name.empty())
				console.name = QString::fromStdString(entry.name);
			console.type = QString::fromStdString(entry.type);
		}
	FtpClient::Config config = app_->ftpClientConfig();
	if(console.address.toStdString() != settings.consoleAddress)
	{
		// Another console than the one in use: its own address and port.
		config.host = console.address.toStdString();
		config.port = console.type == QLatin1String("ps5") ? settings.ftpPortPs5 : settings.ftpPort;
	}
	// This connection only ever writes where saves live and the console's
	// list of saves (/system_data/savedata/<user>/db/user/savedata.db), both
	// chosen here and never typed in: the guard on system folders, meant for
	// the file browser, does not apply to it.
	config.advancedMode = true;
	console.config = config;
	return console;
}

std::map<std::string, std::string> SavesController::links() const
{
	std::map<std::string, std::string> links;
	for(const auto &pair : app_->settings().saveAccountLinks)
	{
		const QString folder = psidFolderFromBase64(QString::fromStdString(pair.second));
		if(!folder.isEmpty())
			links[pair.first] = folder.toStdString();
	}
	return links;
}

std::map<std::string, std::string> SavesController::learnOwners(SaveRemote &remote, std::vector<ConsoleUser> &users,
	const QString &type, const QString &scratch)
{
	std::map<std::string, std::string> found;
	QDir().mkpath(scratch);
	for(ConsoleUser &user : users)
	{
		if(!user.psid.empty())
			continue;
		// Its list of PS4 saves (a PS5 keeps one too), else a PS5's own.
		std::vector<std::string> lists = { SaveVault::saveDbPath(user.folder) };
		if(type != QLatin1String("ps4"))
			lists.push_back("/system_data/savedata_prospero/" + user.folder + "/db/user/savedata.db");
		for(const std::string &path : lists)
		{
			const QString local = QDir(scratch).filePath(QString::fromStdString(user.folder) + QStringLiteral(".db"));
			std::string why;
			const int64_t owner = remote.download(path, local.toStdString(), &why) ? SaveDatabase::owner(local) : 0;
			QFile::remove(local);
			if(owner != 0)
			{
				user.psid = psidFolderOf(owner);
				found[user.folder] = user.psid;
				break;
			}
		}
	}
	if(found.empty())
		return found;
	// Kept: the same link the user would make by hand, never over one made.
	QMetaObject::invokeMethod(this, [this, found]() {
		app_->updateSettings([&found](Settings &s) {
			for(const auto &pair : found)
			{
				const QString accountId = accountIdFromPsidFolder(QString::fromStdString(pair.second));
				if(!accountId.isEmpty() && !s.saveAccountLinks.count(pair.first))
					s.saveAccountLinks[pair.first] = accountId.toStdString();
			}
		});
	}, Qt::QueuedConnection);
	for(const auto &pair : found)
		logInfo("Saves: console user " + pair.first + " is PSID " + pair.second + ", from its list of saves.");
	return found;
}

QVariantList SavesController::userItems(const std::vector<ConsoleUser> &users) const
{
	QVariantList items;
	for(const ConsoleUser &user : users)
	{
		QVariantMap item;
		item[QStringLiteral("user")] = QString::fromStdString(user.folder);
		item[QStringLiteral("name")] = QString::fromStdString(user.name);
		item[QStringLiteral("psidFolder")] = QString::fromStdString(user.psid);
		item[QStringLiteral("linked")] = user.linked;
		items << item;
	}
	return items;
}

void SavesController::setProgress(const QString &status, double progress)
{
	QMetaObject::invokeMethod(this, [this, status, progress]() {
		status_ = status;
		progress_ = progress;
		emit busyChanged();
	}, Qt::QueuedConnection);
}

std::vector<SaveInfo> SavesController::pick(const QStringList &keys) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<SaveInfo> picked;
	const std::set<QString> wanted(keys.begin(), keys.end());
	for(const SaveInfo &save : current_)
		if(wanted.count(QString::fromStdString(save.key())))
			picked.push_back(save);
	return picked;
}

void SavesController::publish(const std::vector<SaveInfo> &list, SaveVault &vault, SaveRemote *remote)
{
	QVariantList items;
	std::map<std::string, std::string> gameIcons;
	std::map<std::string, std::string> userNames;
	if(remote)
		for(const ConsoleUser &user : vault.users())
			userNames[user.folder] = user.name;
	for(const SaveInfo &save : list)
	{
		if(!gameIcons.count(save.titleId))
			gameIcons[save.titleId] = vault.gameIcon(remote, save.titleId);
		QVariantMap item;
		item[QStringLiteral("key")] = QString::fromStdString(save.key());
		item[QStringLiteral("account")] = QString::fromStdString(save.account);
		item[QStringLiteral("titleId")] = QString::fromStdString(save.titleId);
		item[QStringLiteral("dir")] = QString::fromStdString(save.dir);
		item[QStringLiteral("gameTitle")] = QString::fromStdString(
			save.gameTitle.empty() ? save.titleId : save.gameTitle);
		item[QStringLiteral("saveTitle")] = QString::fromStdString(
			save.saveTitle.empty() ? save.dir : save.saveTitle);
		item[QStringLiteral("detail")] = QString::fromStdString(save.detail);
		const QString psid = psidOf(save.accountId);
		item[QStringLiteral("psid")] = psid;
		// The PSID its backups are kept under (PS4/SAVEDATA/<PSID>), and the
		// name of that PSN account in the app.
		const QString folder = QString::fromStdString(save.psid);
		item[QStringLiteral("psidFolder")] = folder;
		QString owner = psidName(psid, app_->settings());
		if(owner.isEmpty() && !folder.isEmpty())
			owner = psidName(accountIdFromPsidFolder(folder), app_->settings());
		item[QStringLiteral("psidName")] = owner;
		// Whose console user folder it is: the Account ID linked to it.
		const auto link = app_->settings().saveAccountLinks.find(save.account);
		const QString linked = link == app_->settings().saveAccountLinks.end() ? QString()
			: QString::fromStdString(link->second);
		item[QStringLiteral("userName")] = linked.isEmpty() ? QString() : labelOfAccount(linked, app_->settings());
		item[QStringLiteral("linked")] = !linked.isEmpty();
		// The console's name for the user it is in (or would go to).
		const auto named = userNames.find(save.account);
		item[QStringLiteral("consoleUser")] = named == userNames.end() ? QString() : QString::fromStdString(named->second);
		// Saves are grouped by owner: the PSID, or the console user while it
		// is not known.
		item[QStringLiteral("group")] = !folder.isEmpty() ? folder
			: QStringLiteral("user:") + QString::fromStdString(save.account);
		item[QStringLiteral("sync")] = syncName(save.sync());
		// On this console before (what it lost), or never (from another).
		bool wasHere = save.onConsole;
		for(const std::string &user : save.users)
			wasHere = wasHere || userNames.count(user) > 0;
		item[QStringLiteral("wasHere")] = wasHere;
		item[QStringLiteral("onConsole")] = save.onConsole;
		item[QStringLiteral("inVault")] = save.inVault;
		item[QStringLiteral("size")] = sizeText(save.onConsole ? save.consoleBytes() : save.vaultBytes());
		item[QStringLiteral("backedUpAt")] = QString::fromStdString(save.backedUpAt);
		item[QStringLiteral("versions")] = save.versions;
		item[QStringLiteral("icon")] = save.iconPath.empty() ? QUrl()
			: QUrl::fromLocalFile(QString::fromStdString(save.iconPath));
		const std::string &game = gameIcons[save.titleId];
		item[QStringLiteral("gameIcon")] = game.empty() ? QUrl()
			: QUrl::fromLocalFile(QString::fromStdString(game));
		items << item;
	}
	const std::vector<ConsoleUser> users = remote ? vault.users() : std::vector<ConsoleUser>();
	QMetaObject::invokeMethod(this, [this, items, list, users]() {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			current_ = list;
		}
		saves_ = items;
		users_ = userItems(users);
		// The names the app gives those PSIDs.
		for(QVariant &entry : users_)
		{
			QVariantMap user = entry.toMap();
			user[QStringLiteral("accountLabel")] =
				psidName(accountIdFromPsidFolder(user.value(QStringLiteral("psidFolder")).toString()), app_->settings());
			entry = user;
		}
		emit savesChanged();
	}, Qt::QueuedConnection);
}

void SavesController::run(const QString &what, Job job)
{
	if(busy_)
		return;
	busy_ = true;
	status_ = what;
	progress_ = 0.0;
	emit busyChanged();
	if(worker_.joinable())
		worker_.join();

	const bool reachable = online();
	const Console shown = consoleAt(source());
	const std::string root = vaultFolder().toStdString();
	const std::map<std::string, std::string> known = links();
	const QString scratch = QDir(vaultFolder()).filePath(QStringLiteral(".cache/owners"));
	worker_ = std::thread([this, job, reachable, shown, root, known, scratch]() {
		FtpClient ftp(shown.config);
		FtpRemote remote(ftp);
		SaveVault vault(root);
		vault.setLinks(known);
		if(reachable)
		{
			// Whose each console user is, first: the saves are filed by it.
			std::string usersError;
			std::vector<ConsoleUser> users = vault.readUsers(remote, &usersError);
			const std::map<std::string, std::string> learned = learnOwners(remote, users, shown.type, scratch);
			if(!learned.empty())
			{
				std::map<std::string, std::string> all = known;
				for(const auto &pair : learned)
					all.emplace(pair.first, pair.second);
				vault.setLinks(all);
			}
		}
		QString message;
		bool error = false;
		std::vector<SaveInfo> list;
		if(job)
			message = job(remote, vault, list, &error);
		// Every action ends with a fresh look at both sides.
		std::string scanError;
		if(reachable)
		{
			list = vault.scan(remote, &scanError, [this](const std::string &title, double fraction) {
				setProgress(tr("Reading the console's saves… %1").arg(QString::fromStdString(title)), fraction);
			});
			if(!scanError.empty())
			{
				logWarning("Saves: the console could not be read: " + scanError);
				list = vault.vaultSaves();
				if(message.isEmpty())
				{
					message = tr("The console's saves could not be read: %1").arg(QString::fromStdString(scanError));
					error = true;
				}
			}
		}
		else
		{
			list = vault.vaultSaves();
		}
		publish(list, vault, reachable && scanError.empty() ? &remote : nullptr);
		QMetaObject::invokeMethod(this, [this, message, error, reachable]() {
			busy_ = false;
			scanned_ = scanned_ || reachable;
			status_.clear();
			progress_ = 0.0;
			emit busyChanged();
			emit savesChanged();
			if(!message.isEmpty())
				emit finished(message, error);
		}, Qt::QueuedConnection);
	});
}

void SavesController::refresh()
{
	run(tr("Reading the console's saves…"), nullptr);
}

void SavesController::backup(const QStringList &keys)
{
	std::vector<SaveInfo> chosen;
	if(keys.isEmpty())
	{
		// What is new or changed; never an earlier copy over a newer backup.
		std::lock_guard<std::mutex> lock(mutex_);
		for(const SaveInfo &save : current_)
			if(save.onConsole && (save.sync() == SaveSync::Changed || save.sync() == SaveSync::ConsoleOnly))
				chosen.push_back(save);
	}
	else
	{
		chosen = pick(keys);
	}
	run(tr("Backing up to the PC…"), [this, chosen](SaveRemote &remote, SaveVault &vault, std::vector<SaveInfo> &,
							   bool *error) -> QString {
		int done = 0;
		QStringList failed;
		for(size_t i = 0; i < chosen.size(); ++i)
		{
			SaveInfo save = chosen[i];
			if(!save.onConsole)
				continue;
			setProgress(tr("Backing up %1 (%2 of %3)…")
					.arg(QString::fromStdString(save.saveTitle.empty() ? save.dir : save.saveTitle))
					.arg(i + 1).arg(chosen.size()),
				static_cast<double>(i) / chosen.size());
			std::string why;
			if(vault.backup(remote, save, &why))
				++done;
			else
				failed << QString::fromStdString(save.key() + ": " + why);
		}
		*error = !failed.isEmpty();
		if(chosen.empty())
			return tr("Everything on the console is already on the PC.");
		if(!failed.isEmpty())
			return tr("%1 of %2 saves backed up. Not done: %3").arg(done).arg(chosen.size()).arg(failed.join(QStringLiteral("; ")));
		return tr("%n save(s) backed up to the PC.", "", done);
	});
}

void SavesController::send(const QStringList &keys, const QString &address)
{
	const std::vector<SaveInfo> chosen = pick(keys);
	const Console to = consoleAt(address);
	const bool elsewhere = to.address != source();
	const QString scratch = QDir(vaultFolder()).filePath(QStringLiteral(".cache/owners"));
	run(tr("Sending to %1…").arg(to.name), [this, chosen, to, elsewhere, scratch](SaveRemote &here, SaveVault &vault,
												  std::vector<SaveInfo> &, bool *error) -> QString {
		// Another console than the one shown: a connection of its own, and
		// its users, by PSID.
		std::unique_ptr<FtpClient> ftp;
		std::unique_ptr<FtpRemote> there;
		SaveRemote *remote = &here;
		std::map<std::string, std::string> psidOfUser;
		if(elsewhere)
		{
			ftp = std::make_unique<FtpClient>(to.config);
			there = std::make_unique<FtpRemote>(*ftp);
			remote = there.get();
			std::string usersError;
			std::vector<ConsoleUser> users = vault.readUsers(*remote, &usersError);
			if(!usersError.empty())
			{
				*error = true;
				return tr("%1 could not be read: %2").arg(to.name, QString::fromStdString(usersError));
			}
			learnOwners(*remote, users, to.type, scratch);
			for(const ConsoleUser &user : users)
				psidOfUser[user.folder] = user.psid;
		}

		int done = 0;
		int noUser = 0;
		QStringList failed;
		// What went to each user, for that user's list of saves afterwards.
		std::map<std::string, std::vector<SaveDbEntry>> sent;
		for(size_t i = 0; i < chosen.size(); ++i)
		{
			const SaveInfo &save = chosen[i];
			if(!save.inVault)
				continue;
			std::string user = save.account;
			if(elsewhere)
			{
				// Its user there: the one with its PSID; else one it was on
				// before whose PSID is not known.
				user.clear();
				for(const auto &pair : psidOfUser)
					if(!save.psid.empty() && pair.second == save.psid)
						user = pair.first;
				if(user.empty())
					for(const std::string &been : save.users)
						if(psidOfUser.count(been) && psidOfUser[been].empty())
							user = been;
			}
			if(user.empty())
			{
				++noUser;
				continue;
			}
			setProgress(tr("Sending %1 to %2 (%3 of %4)…")
					.arg(QString::fromStdString(save.saveTitle.empty() ? save.dir : save.saveTitle), to.name)
					.arg(i + 1).arg(chosen.size()),
				static_cast<double>(i) / chosen.size());
			std::string why;
			if(vault.restore(*remote, save, user, &why))
			{
				++done;
				sent[user].push_back(vault.dbEntry(save, user));
			}
			else
			{
				failed << QString::fromStdString(save.dir + ": " + why);
			}
		}

		// Each in the console's list of saves: a row for those it does not
		// list (rows it has are left as they are).
		int listed = 0;
		QStringList listProblems;
		for(const auto &pair : sent)
		{
			setProgress(tr("Adding them to the console's list of saves…"), 0.95);
			QString problem;
			const int changed = addToConsoleList(*remote, vault, pair.first, pair.second, &problem);
			if(changed < 0)
				listProblems << problem;
			else
				listed += changed;
		}

		*error = !failed.isEmpty() || noUser > 0 || !listProblems.isEmpty();
		QString message = failed.isEmpty() ? tr("%n save(s) sent to %1.", "", done).arg(to.name)
			: tr("%1 of %2 saves sent to %3. Not done: %4").arg(done).arg(chosen.size()).arg(to.name)
				  .arg(failed.join(QStringLiteral("; ")));
		if(noUser > 0)
			message += QStringLiteral(" ") + tr("%n not sent: no user on %1 has their PSID. Link one first.", "", noUser)
				.arg(to.name);
		if(listed > 0)
			message += QStringLiteral(" ") + tr("%n added to the console's list of saves; if one does not show "
				"yet, restart the console.", "", listed);
		if(!listProblems.isEmpty())
		{
			message += QStringLiteral(" ") + tr("Their files are there, but the console's list of saves could not "
				"be updated: %1").arg(listProblems.join(QStringLiteral("; ")));
			if(to.type == QLatin1String("ps5"))
				message += QStringLiteral(" ") + tr("On a PS5, start the PS4 game once and save, then send again.");
		}
		return message;
	});
}

void SavesController::inspectTarget(const QString &address)
{
	const Console to = consoleAt(address);
	QVariantMap pending;
	pending[QStringLiteral("address")] = to.address;
	pending[QStringLiteral("name")] = to.name;
	pending[QStringLiteral("type")] = to.type;
	pending[QStringLiteral("ready")] = false;
	pending[QStringLiteral("ok")] = false;
	pending[QStringLiteral("users")] = QVariantList();
	target_ = pending;
	emit targetChanged();
	// One at a time: the latest asked for is read when the one under way ends.
	if(inspecting_)
	{
		nextInspection_ = to.address;
		return;
	}
	inspecting_ = true;
	if(inspector_.joinable())
		inspector_.join();
	const std::string root = vaultFolder().toStdString();
	const std::map<std::string, std::string> known = links();
	const QString scratch = QDir(vaultFolder()).filePath(QStringLiteral(".cache/owners"));
	inspector_ = std::thread([this, to, pending, root, known, scratch]() {
		FtpClient ftp(to.config);
		FtpRemote remote(ftp);
		SaveVault vault(root);
		vault.setLinks(known);
		std::string error;
		std::vector<ConsoleUser> users = vault.readUsers(remote, &error);
		if(error.empty())
			learnOwners(remote, users, to.type, scratch);
		QVariantMap result = pending;
		result[QStringLiteral("ready")] = true;
		result[QStringLiteral("ok")] = error.empty();
		result[QStringLiteral("error")] = QString::fromStdString(error);
		const QVariantList items = userItems(users);
		QMetaObject::invokeMethod(this, [this, result, items]() mutable {
			inspecting_ = false;
			if(inspector_.joinable())
				inspector_.join();
			if(target_.value(QStringLiteral("address")) == result.value(QStringLiteral("address")))
			{
				QVariantList named = items;
				for(QVariant &entry : named)
				{
					QVariantMap user = entry.toMap();
					user[QStringLiteral("accountLabel")] = psidName(
						accountIdFromPsidFolder(user.value(QStringLiteral("psidFolder")).toString()), app_->settings());
					entry = user;
				}
				result[QStringLiteral("users")] = named;
				target_ = result;
				emit targetChanged();
			}
			if(!nextInspection_.isEmpty())
			{
				const QString next = nextInspection_;
				nextInspection_.clear();
				if(target_.value(QStringLiteral("address")).toString() == next)
					inspectTarget(next);
			}
		}, Qt::QueuedConnection);
	});
}

int SavesController::addToConsoleList(SaveRemote &remote, SaveVault &vault, const std::string &user,
	const std::vector<SaveDbEntry> &entries, QString *problem)
{
	// A copy of the list as the console had it stays on this PC, so nothing
	// done here is without a way back.
	const std::string remotePath = SaveVault::saveDbPath(user);
	const QString folder = QDir(QString::fromStdString(vault.root()))
		.filePath(QStringLiteral(".vault/console-lists/") + QString::fromStdString(user));
	QDir().mkpath(folder);
	const QString original = QDir(folder).filePath(
		QStringLiteral("savedata-%1.db").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
	const QString work = QDir(folder).filePath(QStringLiteral("savedata-edited.db"));
	std::string why;
	if(!remote.download(remotePath, original.toStdString(), &why))
	{
		QFile::remove(original);
		*problem = tr("it could not be read (%1)").arg(QString::fromStdString(why));
		return -1;
	}
	QFile::remove(work);
	if(!QFile::copy(original, work))
	{
		*problem = tr("no room for a copy on this PC");
		return -1;
	}
	const SaveDatabase::Outcome outcome = SaveDatabase::registerSaves(work, entries);
	if(!outcome.ok || !outcome.changed())
	{
		QFile::remove(work);
		// Nothing changed: the copy taken first is not needed either.
		if(outcome.ok)
			QFile::remove(original);
		if(!outcome.ok)
		{
			*problem = outcome.error;
			return -1;
		}
		return 0;
	}

	// Back on the console, whole — or the console's own copy goes back.
	const qint64 size = QFileInfo(work).size();
	if(!remote.upload(work.toStdString(), remotePath, &why))
	{
		QFile::remove(work);
		std::string restoreError;
		remote.upload(original.toStdString(), remotePath, &restoreError);
		*problem = tr("it could not be written back (%1)").arg(QString::fromStdString(why));
		return -1;
	}
	// Read back and compared byte for byte: it is small, and a list of saves
	// that is not exactly the edited one must not stay on the console.
	bool whole = false;
	{
		const QString check = QDir(folder).filePath(QStringLiteral("savedata-check.db"));
		std::string checkError;
		if(remote.download(remotePath, check.toStdString(), &checkError))
		{
			QFile a(work);
			QFile b(check);
			whole = a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly) && a.size() == size
				&& a.readAll() == b.readAll();
		}
		QFile::remove(check);
	}
	QFile::remove(work);
	if(!whole)
	{
		std::string restoreError;
		remote.upload(original.toStdString(), remotePath, &restoreError);
		logWarning("Saves: the console's list of saves did not go back whole; its own copy was put back.");
		*problem = tr("the edited list did not arrive whole, so the console's own was put back");
		return -1;
	}

	// The last few copies are enough.
	QStringList copies = QDir(folder).entryList({ QStringLiteral("savedata-2*.db") }, QDir::Files, QDir::Name);
	while(copies.size() > SaveVault::kVersionsKept)
		QFile::remove(QDir(folder).filePath(copies.takeFirst()));
	logInfo("Saves: added " + std::to_string(outcome.added) + " row(s) to user " + user
		+ "'s list of saves (" + std::to_string(outcome.repaired) + " no longer marked broken).");
	return outcome.added + outcome.repaired;
}

void SavesController::removeFromVault(const QStringList &keys)
{
	const std::vector<SaveInfo> chosen = pick(keys);
	run(tr("Deleting from the PC…"), [chosen](SaveRemote &, SaveVault &vault, std::vector<SaveInfo> &,
											 bool *error) -> QString {
		int done = 0;
		std::string why;
		for(const SaveInfo &save : chosen)
			if(save.inVault && vault.removeFromVault(save, &why))
				++done;
		*error = !why.empty();
		return why.empty() ? tr("%n save(s) deleted from the PC.", "", done)
						   : tr("Not everything was deleted: %1").arg(QString::fromStdString(why));
	});
}

void SavesController::linkAccount(const QString &user, const QString &accountId)
{
	app_->updateSettings([&](Settings &s) {
		if(accountId.isEmpty())
			s.saveAccountLinks.erase(user.toStdString());
		else
			s.saveAccountLinks[user.toStdString()] = accountId.toStdString();
	});
	// The names and PSIDs on the page come from the link.
	refresh();
}

void SavesController::linkPsid(const QString &user, const QString &psidFolder)
{
	const QString folder = psidFolder.trimmed().toLower();
	const QString accountId = accountIdFromPsidFolder(folder);
	if(accountId.isEmpty() || user.isEmpty())
		return;
	app_->updateSettings([&](Settings &s) { s.saveAccountLinks[user.toStdString()] = accountId.toStdString(); });
	// The console being sent to knows it now.
	QVariantList users = target_.value(QStringLiteral("users")).toList();
	for(QVariant &entry : users)
	{
		QVariantMap item = entry.toMap();
		if(item.value(QStringLiteral("user")).toString() == user)
		{
			item[QStringLiteral("psidFolder")] = folder;
			item[QStringLiteral("linked")] = true;
			item[QStringLiteral("accountLabel")] = psidName(accountId, app_->settings());
			entry = item;
		}
	}
	target_[QStringLiteral("users")] = users;
	emit targetChanged();
	// A user of the console shown: its saves are filed under it now.
	for(const QVariant &entry : users_)
		if(entry.toMap().value(QStringLiteral("user")).toString() == user)
		{
			refresh();
			break;
		}
}

} // namespace orbislink
