// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/saves_controller.h"

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/ftp/ftp_client.h"
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
#include <map>
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
}

SavesController::~SavesController()
{
	if(worker_.joinable())
		worker_.join();
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
		item[QStringLiteral("psidName")] = psidName(psid, app_->settings());
		// Whose console user folder it is: the Account ID linked to it.
		const auto link = app_->settings().saveAccountLinks.find(save.account);
		const QString linked = link == app_->settings().saveAccountLinks.end() ? QString()
			: QString::fromStdString(link->second);
		item[QStringLiteral("userName")] = linked.isEmpty() ? QString() : labelOfAccount(linked, app_->settings());
		item[QStringLiteral("linked")] = !linked.isEmpty();
		// The PSID its backups are kept under (PS4/SAVEDATA/<PSID>).
		item[QStringLiteral("psidFolder")] = QString::fromStdString(save.psid);
		item[QStringLiteral("sync")] = syncName(save.sync());
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
	QMetaObject::invokeMethod(this, [this, items, list]() {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			current_ = list;
		}
		saves_ = items;
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

	const bool online = app_->canUseFtp();
	FtpClient::Config config = app_->ftpClientConfig();
	// This connection only ever writes where saves live and the console's
	// list of saves (/system_data/savedata/<user>/db/user/savedata.db), both
	// chosen here and never typed in: the guard on system folders, meant for
	// the file browser, does not apply to it.
	config.advancedMode = true;
	const std::string root = vaultFolder().toStdString();
	// Console user → PSID folder, from the links to the app's Account IDs.
	std::map<std::string, std::string> links;
	for(const auto &pair : app_->settings().saveAccountLinks)
	{
		const QString folder = psidFolderFromBase64(QString::fromStdString(pair.second));
		if(!folder.isEmpty())
			links[pair.first] = folder.toStdString();
	}
	worker_ = std::thread([this, job, online, config, root, links]() {
		FtpClient ftp(config);
		FtpRemote remote(ftp);
		SaveVault vault(root);
		vault.setLinks(links);
		QString message;
		bool error = false;
		std::vector<SaveInfo> list;
		if(job)
			message = job(remote, vault, list, &error);
		// Every action ends with a fresh look at both sides.
		std::string scanError;
		if(online)
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
		publish(list, vault, online ? &remote : nullptr);
		QMetaObject::invokeMethod(this, [this, message, error, online]() {
			busy_ = false;
			scanned_ = scanned_ || online;
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
		std::lock_guard<std::mutex> lock(mutex_);
		for(const SaveInfo &save : current_)
			if(save.onConsole && save.sync() != SaveSync::Same)
				chosen.push_back(save);
	}
	else
	{
		chosen = pick(keys);
	}
	run(tr("Backing up…"), [this, chosen](SaveRemote &remote, SaveVault &vault, std::vector<SaveInfo> &,
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
			return tr("Everything on the console is already in the vault.");
		if(!failed.isEmpty())
			return tr("%1 of %2 saves backed up. Not done: %3").arg(done).arg(chosen.size()).arg(failed.join(QStringLiteral("; ")));
		return tr("%n save(s) backed up in the vault.", "", done);
	});
}

void SavesController::restore(const QStringList &keys)
{
	const std::vector<SaveInfo> chosen = pick(keys);
	run(tr("Putting saves back…"), [this, chosen](SaveRemote &remote, SaveVault &vault,
										 std::vector<SaveInfo> &, bool *error) -> QString {
		int done = 0;
		QStringList failed;
		// The saves the console no longer listed, by user: their rows go into
		// that user's list of saves afterwards, in one go.
		std::map<std::string, std::vector<SaveDbEntry>> unlisted;
		for(size_t i = 0; i < chosen.size(); ++i)
		{
			const SaveInfo &save = chosen[i];
			if(!save.inVault)
				continue;
			setProgress(tr("Putting back %1 (%2 of %3)…")
					.arg(QString::fromStdString(save.saveTitle.empty() ? save.dir : save.saveTitle))
					.arg(i + 1).arg(chosen.size()),
				static_cast<double>(i) / chosen.size());
			std::string why;
			if(vault.restore(remote, save, save.account, &why))
			{
				++done;
				if(!save.onConsole)
					unlisted[save.account].push_back(vault.dbEntry(save, save.account));
			}
			else
			{
				failed << QString::fromStdString(save.dir + ": " + why);
			}
		}

		int listed = 0;
		QStringList listProblems;
		for(const auto &pair : unlisted)
		{
			setProgress(tr("Adding them to the console's list of saves…"), 0.95);
			QString problem;
			const int changed = addToConsoleList(remote, vault, pair.first, pair.second, &problem);
			if(changed < 0)
				listProblems << problem;
			else
				listed += changed;
		}

		*error = !failed.isEmpty() || !listProblems.isEmpty();
		QString message = failed.isEmpty() ? tr("%n save(s) put back on the console.", "", done)
			: tr("%1 of %2 saves put back. Not done: %3").arg(done).arg(chosen.size())
				  .arg(failed.join(QStringLiteral("; ")));
		if(listed > 0)
			message += QStringLiteral(" ") + tr("%n added to the console's list of saves; if one does not show "
				"yet, restart the console.", "", listed);
		if(!listProblems.isEmpty())
			message += QStringLiteral(" ") + tr("Their files are back, but the console's list of saves could not "
				"be updated: %1").arg(listProblems.join(QStringLiteral("; ")));
		return message;
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
	run(tr("Deleting from the vault…"), [chosen](SaveRemote &, SaveVault &vault, std::vector<SaveInfo> &,
											 bool *error) -> QString {
		int done = 0;
		std::string why;
		for(const SaveInfo &save : chosen)
			if(save.inVault && vault.removeFromVault(save, &why))
				++done;
		*error = !why.empty();
		return why.empty() ? tr("%n save(s) deleted from the vault.", "", done)
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

} // namespace orbislink
