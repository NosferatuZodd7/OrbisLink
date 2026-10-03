// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/saves/save_vault.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace orbislink {

class AppController;

// The save vault for QML (`saves`): the console's saves next to the ones
// kept on this PC, and backing up, putting back and deleting them.
// One action at a time, on a thread of its own, with its own FTP connection.
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

	// Reads the console (when its FTP answers) and the vault.
	Q_INVOKABLE void refresh();
	// Backs up the given saves (keys); with none, every save on the console
	// whose backup is missing or out of date.
	Q_INVOKABLE void backup(const QStringList &keys);
	// Puts the latest backup of each back on the console.
	Q_INVOKABLE void restore(const QStringList &keys);
	Q_INVOKABLE void removeFromConsole(const QStringList &keys);
	Q_INVOKABLE void removeFromVault(const QStringList &keys);
	Q_INVOKABLE void setVaultFolder(const QString &folder);
	Q_INVOKABLE void openVaultFolder() const;

signals:
	void savesChanged();
	void busyChanged();
	void vaultFolderChanged();
	// The outcome of an action, for a notice.
	void finished(const QString &message, bool error);

private:
	using Job = std::function<QString(SaveRemote &, SaveVault &, std::vector<SaveInfo> &, bool *error)>;
	void run(const QString &what, Job job);
	void publish(const std::vector<SaveInfo> &list, SaveVault &vault, SaveRemote *remote);
	std::vector<SaveInfo> pick(const QStringList &keys) const;
	void setProgress(const QString &status, double progress);

	AppController *app_;
	QVariantList saves_;
	std::vector<SaveInfo> current_;
	mutable std::mutex mutex_;
	bool busy_ = false;
	bool scanned_ = false;
	QString status_;
	double progress_ = 0.0;
	std::thread worker_;
};

} // namespace orbislink
