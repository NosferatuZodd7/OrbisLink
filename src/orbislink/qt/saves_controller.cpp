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
	const FtpClient::Config config = app_->ftpClientConfig();
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
