// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/ftp/ftp_client.h"

#include <QObject>
#include <QSet>
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
	//    group ("games": games and apps, "extras": patches, add-ons and
	//    themes, "payloads", "other": what cannot be used as it is),
	//    title, titleId, version, platform ("ps4"/"ps5"/"ps1"/"ps2"/""),
	//    category, icon (a data: URL or ""), installed,
	//    action ("install"/"convert"/"mount"/"run"/""), note }]
	Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
	// What the library's buttons set going that the installs panel shows
	// (installs and conversions have their own cards there):
	// [{ id, kind ("mount"/"run"), name, icon, console, state
	//    ("working"/"done"/"error"), stageText, message }]
	Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
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
	QVariantList jobs() const { return jobs_; }
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
	// A disc: converted, its package put back beside it and, when asked,
	// installed.
	Q_INVOKABLE void convert(const QString &path, bool install);
	// Deletes a file from the library.
	Q_INVOKABLE void remove(const QString &path);
	Q_INVOKABLE void removeJob(const QString &id);
	Q_INVOKABLE void clearFinishedJobs();

signals:
	void itemsChanged();
	void jobsChanged();
	void busyChanged();
	void finished(const QString &message, bool error);

private:
	// The item at that path (empty when there is none).
	QVariantMap itemAt(const QString &path) const;
	// A job on the action thread; refreshes after when asked. With a `kind`,
	// it has a card in the installs panel.
	void runAction(const QString &what, std::function<QString(FtpClient &ftp, bool *error)> job, bool rescan,
		const QVariantMap &item = QVariantMap(), const QString &kind = QString());
	// The name of the console in use, for the cards.
	QString consoleName() const;
	void updateJob(const QString &id, const QVariantMap &fields);
	// The queue tasks that had finished (sent or installed) when the
	// library was last read: one finishing after that changes what it shows.
	QSet<QString> finishedTasks() const;

	AppController *app_;
	GamesController *games_;
	QVariantList items_;
	QVariantList folders_;
	QVariantList jobs_;
	int nextJob_ = 1;
	QSet<QString> finishedSeen_;
	bool scanning_ = false;
	bool scanned_ = false;
	bool busy_ = false;
	QString status_;
	std::atomic<bool> cancel_ { false };
	std::thread scanner_;
	std::thread worker_;
};

} // namespace orbislink
