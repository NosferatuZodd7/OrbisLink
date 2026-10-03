// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/qt/saves_controller.h"

#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/ftp/ftp_client.h"
#include "orbislink/qt/app_controller.h"

#include <QByteArray>
#include <QDesktopServices>
#include <QDir>
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
	bool removeFile(const std::string &path, std::string *error) override
	{
		const FtpResult result = ftp_.removeFile(path);
		if(!result.ok && error)
			*error = result.message;
		return result.ok;
	}
	bool removeDirectory(const std::string &path) override { return ftp_.removeDirectory(path).ok; }

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

// The PSID as the PS4 names its USB folders: the 8 bytes the other way
// round from how they are stored, in hex.
QString psidFolderFromStored(const std::string &hex)
{
	if(hex.size() != 16)
		return QString();
	QByteArray bytes = QByteArray::fromHex(QByteArray::fromStdString(hex));
	std::reverse(bytes.begin(), bytes.end());
	return QString::fromLatin1(bytes.toHex());
}

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
		// The PSID folder of the PS4's USB copies: from the save itself, or
		// else from the account linked to its user.
		QString folder = psidFolderFromStored(save.accountId);
		if(folder.isEmpty() && !linked.isEmpty())
			folder = psidFolderFromBase64(linked);
		item[QStringLiteral("psidFolder")] = folder;
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
	const FtpClient::Config config = app_->ftpClientConfig();
	const std::string root = vaultFolder().toStdString();
	worker_ = std::thread([this, job, online, config, root]() {
		FtpClient ftp(config);
		FtpRemote remote(ftp);
		SaveVault vault(root);
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
			if(vault.restore(remote, save, &why))
				++done;
			else
				failed << QString::fromStdString(save.key() + ": " + why);
		}
		*error = !failed.isEmpty();
		if(!failed.isEmpty())
			return tr("%1 of %2 saves put back. Not done: %3").arg(done).arg(chosen.size()).arg(failed.join(QStringLiteral("; ")));
		return tr("%n save(s) put back on the console.", "", done);
	});
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

void SavesController::exportToUsb(const QStringList &keys, const QString &where)
{
	const std::vector<SaveInfo> chosen = pick(keys);
	std::map<std::string, QString> folders; // key → PSID folder
	for(const QVariant &v : saves_)
	{
		const QVariantMap item = v.toMap();
		folders[item.value(QStringLiteral("key")).toString().toStdString()] =
			item.value(QStringLiteral("psidFolder")).toString();
	}
	const bool console = where == QLatin1String("console");
	const QUrl url(where);
	const QString local = url.isLocalFile() ? url.toLocalFile() : where;
	run(tr("Copying to the USB drive…"), [this, chosen, folders, console, local](SaveRemote &remote,
											 SaveVault &vault, std::vector<SaveInfo> &, bool *error) -> QString {
		FolderRemote folder(local.toStdString());
		SaveRemote &target = console ? remote : static_cast<SaveRemote &>(folder);
		const std::string base = console ? "/mnt/usb0" : "/";
		if(console)
		{
			std::vector<FtpEntry> probe;
			std::string why;
			if(!remote.list(base, &probe, &why))
			{
				*error = true;
				return tr("No USB drive found in the PS4. Plug one in (FAT32 or exFAT) and try again.");
			}
		}
		int done = 0;
		QStringList failed;
		for(size_t i = 0; i < chosen.size(); ++i)
		{
			const SaveInfo &save = chosen[i];
			setProgress(tr("Copying %1 to the USB drive (%2 of %3)…")
					.arg(QString::fromStdString(save.saveTitle.empty() ? save.dir : save.saveTitle))
					.arg(i + 1).arg(chosen.size()),
				static_cast<double>(i) / chosen.size());
			const auto psid = folders.find(save.key());
			std::string why;
			if(psid == folders.end() || psid->second.isEmpty())
				failed << tr("%1: link its console user to an Account ID first").arg(QString::fromStdString(save.dir));
			else if(vault.exportToUsb(target, base, save, psid->second.toStdString(), &why))
				++done;
			else
				failed << QString::fromStdString(save.dir + ": " + why);
		}
		*error = !failed.isEmpty();
		if(!failed.isEmpty())
			return tr("%1 of %2 copied to the USB drive. Not done: %3").arg(done).arg(chosen.size())
				.arg(failed.join(QStringLiteral("; ")));
		return tr("%n save(s) on the USB drive. On the PS4: Settings → Application Saved Data Management → "
				  "Saved Data on USB Storage → Copy to System Storage.", "", done);
	});
}

void SavesController::importFromUsb(const QString &where)
{
	// PSID folder → console user, from the links.
	std::map<std::string, std::string> userOf;
	for(const auto &pair : app_->settings().saveAccountLinks)
	{
		const QString folder = psidFolderFromBase64(QString::fromStdString(pair.second));
		if(!folder.isEmpty())
			userOf[folder.toStdString()] = pair.first;
	}
	// Key → sizes of the image and key file of its latest backup.
	std::map<std::string, std::pair<int64_t, int64_t>> inVault;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for(const SaveInfo &save : current_)
		{
			if(save.inVault)
			{
				std::pair<int64_t, int64_t> sizes{-1, -1};
				for(const SaveFile &file : save.vaultFiles)
				{
					if(file.relative == "savedata/sdimg_" + save.dir)
						sizes.first = file.size;
					else if(file.relative == "savedata/" + save.dir + ".bin")
						sizes.second = file.size;
				}
				inVault[save.key()] = sizes;
			}
			// A user whose saves name their PSID needs no link.
			const QString folder = psidFolderFromStored(save.accountId);
			if(!folder.isEmpty() && !userOf.count(folder.toStdString()))
				userOf[folder.toStdString()] = save.account;
		}
	}
	const bool console = where == QLatin1String("console");
	const QUrl url(where);
	const QString local = url.isLocalFile() ? url.toLocalFile() : where;
	run(tr("Reading the USB drive…"), [this, userOf, inVault, console, local](SaveRemote &remote,
										  SaveVault &vault, std::vector<SaveInfo> &, bool *error) -> QString {
		FolderRemote folder(local.toStdString());
		SaveRemote &source = console ? remote : static_cast<SaveRemote &>(folder);
		const std::string base = console ? "/mnt/usb0" : "/";
		std::string why;
		const std::vector<UsbSave> found = vault.usbSaves(source, base, &why);
		if(found.empty())
		{
			*error = true;
			return tr("No saves found on the USB drive (it looks for PS4/SAVEDATA, as the PS4 copies them).");
		}
		int done = 0;
		int same = 0;
		QStringList unlinked;
		QStringList failed;
		for(size_t i = 0; i < found.size(); ++i)
		{
			const UsbSave &usb = found[i];
			const auto user = userOf.find(usb.psid);
			if(user == userOf.end())
			{
				if(!unlinked.contains(QString::fromStdString(usb.psid)))
					unlinked << QString::fromStdString(usb.psid);
				continue;
			}
			// Already in the vault as it is: not again.
			const auto kept = inVault.find(user->second + "/" + usb.titleId + "/" + usb.dir);
			if(kept != inVault.end() && kept->second == std::make_pair(usb.imageSize, usb.keySize))
			{
				++same;
				continue;
			}
			setProgress(tr("Bringing %1 from the USB drive (%2 of %3)…").arg(QString::fromStdString(usb.dir))
					.arg(i + 1).arg(found.size()),
				static_cast<double>(i) / found.size());
			std::string reason;
			if(vault.importFromUsb(source, base, usb, user->second, &reason))
				++done;
			else
				failed << QString::fromStdString(usb.dir + ": " + reason);
		}
		*error = !failed.isEmpty() || !unlinked.isEmpty();
		QString message = tr("%n save(s) brought into the vault.", "", done);
		if(same > 0)
			message += QStringLiteral(" ") + tr("%n already there.", "", same);
		if(!unlinked.isEmpty())
			message += QStringLiteral(" ") + tr("PSID %1: link it to a console user (the user chips at the top) to bring its saves.")
				.arg(unlinked.join(QStringLiteral(", ")));
		if(!failed.isEmpty())
			message += QStringLiteral(" ") + tr("Not done: %1").arg(failed.join(QStringLiteral("; ")));
		return message;
	});
}

} // namespace orbislink
