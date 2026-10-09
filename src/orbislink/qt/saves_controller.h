// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/ftp/ftp_client.h"
#include "orbislink/saves/save_vault.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace orbislink {

class AppController;

// The save vault for QML (`saves`): a console's saves next to the ones kept
// on this PC; backing them up to the PC, sending them to a console (the
// one they came from or another of the same PSN account) and deleting
// backups. One action at a time, on a thread of its own, with its own FTP
// connection.
class SavesController : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QVariantList saves READ saves NOTIFY savesChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(QString status READ status NOTIFY busyChanged)
	Q_PROPERTY(double progress READ progress NOTIFY busyChanged)
	// The console was read at least once in this session.
	Q_PROPERTY(bool scanned READ scanned NOTIFY savesChanged)
	Q_PROPERTY(QString vaultFolder READ vaultFolder NOTIFY vaultFolderChanged)
	Q_PROPERTY(QUrl vaultUrl READ vaultUrl NOTIFY vaultFolderChanged)
	// The saved consoles: [{ address, name, type, active, ftp, source }];
	// `ftp` when its FTP answered, `source` for the one whose saves are shown.
	Q_PROPERTY(QVariantList consoles READ consoles NOTIFY consolesChanged)
	// The console whose saves are shown (the one in use unless another was
	// picked), and whether its FTP answers.
	Q_PROPERTY(QString source READ source NOTIFY consolesChanged)
	Q_PROPERTY(QString sourceName READ sourceName NOTIFY consolesChanged)
	Q_PROPERTY(bool online READ online NOTIFY consolesChanged)
	// That console's users: [{ user, name, psidFolder, linked, accountLabel }].
	Q_PROPERTY(QVariantList users READ users NOTIFY savesChanged)
	// The console saves are about to be sent to, as read for it:
	// { address, name, type, ready, ok, error, users: [as above] }.
	Q_PROPERTY(QVariantMap target READ target NOTIFY targetChanged)

public:
	explicit SavesController(AppController *app, QObject *parent = nullptr);
	~SavesController() override;

	QVariantList saves() const { return saves_; }
	bool busy() const { return busy_; }
	QString status() const { return status_; }
	double progress() const { return progress_; }
	bool scanned() const { return scanned_; }
	QString vaultFolder() const;
	QUrl vaultUrl() const;
	QVariantList consoles() const;
	QString source() const;
	QString sourceName() const;
	bool online() const;
	QVariantList users() const { return users_; }
	QVariantMap target() const { return target_; }

	// Reads the console (when its FTP answers) and the vault.
	Q_INVOKABLE void refresh();
	// Shows another console's saves (its address).
	Q_INVOKABLE void setSource(const QString &address);
	// Backs up the given saves (keys) to the PC; with none, every save on
	// the console whose backup is missing or out of date.
	Q_INVOKABLE void backup(const QStringList &keys);
	// Sends the latest backup of each to the console at `address` (the one
	// shown when empty): to its user with the save's PSID, so a PS4 game's
	// save goes from a PS4 to a PS5 of the same account and back. One the
	// console does not list also gets its row in the console's list of
	// saves (a copy of which is kept on this PC first).
	Q_INVOKABLE void send(const QStringList &keys, const QString &address);
	// Reads the users of the console at `address` into `target`.
	Q_INVOKABLE void inspectTarget(const QString &address);
	Q_INVOKABLE void removeFromVault(const QStringList &keys);
	// Console user folder → Account ID (base64) kept in the app.
	Q_INVOKABLE void linkAccount(const QString &user, const QString &accountId);
	// Console user folder → the PSID (16 hex digits) the vault keeps its
	// saves under, for a user of another console.
	Q_INVOKABLE void linkPsid(const QString &user, const QString &psidFolder);
	Q_INVOKABLE void setVaultFolder(const QString &folder);
	Q_INVOKABLE void openVaultFolder() const;

signals:
	void savesChanged();
	void busyChanged();
	void vaultFolderChanged();
	void consolesChanged();
	void targetChanged();
	// The outcome of an action, for a notice.
	void finished(const QString &message, bool error);

private:
	struct Console
	{
		QString address;
		QString name;
		QString type; // "ps4", "ps5" or ""
		FtpClient::Config config;
	};
	using Job = std::function<QString(SaveRemote &, SaveVault &, std::vector<SaveInfo> &, bool *error)>;
	void run(const QString &what, Job job);
	void publish(const std::vector<SaveInfo> &list, SaveVault &vault, SaveRemote *remote);
	std::vector<SaveInfo> pick(const QStringList &keys) const;
	Console consoleAt(const QString &address) const;
	// Console user folder → PSID folder, from the links to Account IDs.
	std::map<std::string, std::string> links() const;
	// For console users no link names: the PSID their console's own list of
	// saves gives, kept as their link from then on.
	std::map<std::string, std::string> learnOwners(SaveRemote &remote, std::vector<ConsoleUser> &users,
		const QString &type, const QString &scratch);
	QVariantList userItems(const std::vector<ConsoleUser> &users) const;
	// Adds rows to the console user's list of saves; how many changed, or -1
	// with the reason.
	int addToConsoleList(SaveRemote &remote, SaveVault &vault, const std::string &user,
		const std::vector<SaveDbEntry> &entries, QString *problem);
	void setProgress(const QString &status, double progress);

	AppController *app_;
	QVariantList saves_;
	QVariantList users_;
	QVariantMap target_;
	std::vector<SaveInfo> current_;
	mutable std::mutex mutex_;
	bool busy_ = false;
	bool scanned_ = false;
	QString status_;
	double progress_ = 0.0;
	// The console picked to be shown; empty: the one in use.
	QString source_;
	std::thread worker_;
	// Reading the users of the console saves are to be sent to.
	std::thread inspector_;
	bool inspecting_ = false;
	QString nextInspection_;
};

} // namespace orbislink
