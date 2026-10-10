// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/ftp/ftp_client.h"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <atomic>
#include <thread>

namespace orbislink {

class AppController;
class GamesController;

// The console library for QML (`consoleLibrary`): what is in the
// OrbisLinkFPKG folders of the console in use — its memory, its USB drives,
// its extended storage — read over FTP and told apart (see
// library/console_library.h), each with the one thing that makes it
// playable: install a package, convert a PS1/PS2 disc, hand a PS5 image or
// app folder to ShadowMountPlus, run a payload.
class LibraryController : public QObject
{
	Q_OBJECT
	// [{ path, name, folder, drive ("internal"/"usb"/"ext"), size, sizeText,
	//    kind ("package"/"disc"/"image"/"folder"/"payload"/"archive"),
	//    title, titleId, version, platform ("ps4"/"ps5"/"ps1"/"ps2"/""),
	//    category, icon (a data: URL or ""), installed,
	//    action ("install"/"convert"/"mount"/"run"/""), note }]
	Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
	// The library folders the console has: [{ path, drive }].
	Q_PROPERTY(QVariantList folders READ folders NOTIFY itemsChanged)
	Q_PROPERTY(bool scanning READ scanning NOTIFY itemsChanged)
	Q_PROPERTY(bool scanned READ scanned NOTIFY itemsChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(QString status READ status NOTIFY itemsChanged)
	// What the console in use is, as the actions depend on it.
	Q_PROPERTY(bool ps5 READ ps5 NOTIFY itemsChanged)

public:
	LibraryController(AppController *app, GamesController *games, QObject *parent = nullptr);
	~LibraryController() override;

	QVariantList items() const { return items_; }
	QVariantList folders() const { return folders_; }
	bool scanning() const { return scanning_; }
	bool scanned() const { return scanned_; }
	bool busy() const { return busy_; }
	QString status() const { return status_; }
	bool ps5() const;

	// Reads the folders again.
	Q_INVOKABLE void refresh();
	// Does the item's action.
	Q_INVOKABLE void act(const QString &path);
	// Deletes a file from the library.
	Q_INVOKABLE void remove(const QString &path);

signals:
	void itemsChanged();
	void busyChanged();
	void finished(const QString &message, bool error);

private:
	// The item at that path (empty when there is none).
	QVariantMap itemAt(const QString &path) const;
	// A job on the action thread; refreshes after when asked.
	void runAction(const QString &what, std::function<QString(FtpClient &ftp, bool *error)> job, bool rescan);

	AppController *app_;
	GamesController *games_;
	QVariantList items_;
	QVariantList folders_;
	bool scanning_ = false;
	bool scanned_ = false;
	bool busy_ = false;
	QString status_;
	std::atomic<bool> cancel_ { false };
	std::thread scanner_;
	std::thread worker_;
};

} // namespace orbislink
