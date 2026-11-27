// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/qt/lifeline.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <map>
#include <thread>
#include <vector>

namespace orbislink {

class AppController;
class FtpClient;

// The trophy vault for QML (`trophies`): a PS4 user's trophies, read over
// FTP from the console's own list of them (/user/home/<user>/trophy/db/
// trophy_local.db, SQLite, as Apollo Save Tool reads it), with each game's
// icon; and a copy of a game's trophy files kept on this PC. Only read and
// copied: nothing is unlocked or written on the console. One action at a
// time, on a thread of its own, with its own FTP connection.
class TrophiesController : public QObject
{
	Q_OBJECT
	// The games with trophies: [{ id, npId, title, description, total,
	// unlocked, progress, platinum, platinumTotal, gold, goldTotal, silver,
	// silverTotal, bronze, bronzeTotal, icon, backedUp, backupDate }].
	Q_PROPERTY(QVariantList titles READ titles NOTIFY titlesChanged)
	// The console's users: [{ folder, name }]; `user` the one shown.
	Q_PROPERTY(QVariantList users READ users NOTIFY titlesChanged)
	Q_PROPERTY(QString user READ user NOTIFY titlesChanged)
	Q_PROPERTY(bool scanned READ scanned NOTIFY titlesChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(QString status READ status NOTIFY busyChanged)
	Q_PROPERTY(double progress READ progress NOTIFY busyChanged)
	Q_PROPERTY(QString folder READ folder CONSTANT)
	// The console in use is a PS5: its trophies are kept another way, not
	// read yet.
	Q_PROPERTY(bool ps5 READ ps5 NOTIFY titlesChanged)
	// Each trophy's picture fetched so far: "<npId>/<trophy id>" → file URL.
	Q_PROPERTY(QVariantMap art READ art NOTIFY artChanged)

public:
	explicit TrophiesController(AppController *app, QObject *parent = nullptr);
	~TrophiesController() override;

	QVariantList titles() const { return titles_; }
	QVariantList users() const { return users_; }
	QVariantMap art() const { return art_; }
	QString user() const { return user_; }
	bool scanned() const { return scanned_; }
	bool busy() const { return busy_; }
	QString status() const { return status_; }
	double progress() const { return progress_; }
	// Where the copies go on this PC (Documents/OrbisLink trophies).
	QString folder() const;
	bool ps5() const;

	// Reads the console's users and the chosen one's trophies again.
	Q_INVOKABLE void refresh();
	// Shows another user's trophies (its folder).
	Q_INVOKABLE void setUser(const QString &folder);
	// One game's trophies: [{ id, group, title, description, grade
	// ("platinum", "gold", "silver", "bronze"), unlocked, hidden, date (day
	// and time), day, time, when (ms), rarity (-1 when not known) }].
	Q_INVOKABLE QVariantList trophiesOf(int titleId) const;
	// Copies the trophy files of these games (their ids; every game when
	// empty) and the user's list of trophies to this PC.
	Q_INVOKABLE void backup(const QList<int> &titleIds);
	Q_INVOKABLE void openFolder() const;
	// Fetches the pictures of one game's trophies (once; kept on this PC).
	Q_INVOKABLE void loadArt(int titleId);
	// The same game for every user of the console who has it: [{ user, name,
	// progress, unlocked, total, platinum…, earned: [trophy ids] }].
	Q_INVOKABLE QVariantList compareOf(const QString &npId) const;

signals:
	void titlesChanged();
	void busyChanged();
	void artChanged();
	// The outcome of an action, for a notice.
	void finished(const QString &message, bool error);

private:
	using Job = std::function<QString(FtpClient &ftp, bool *error)>;
	void run(const QString &what, Job job, bool read);
	void setProgress(const QString &status, double fraction);
	QString userFolderOnPc(const QString &user) const;

	AppController *app_;
	LifelinePtr lifeline_ = std::make_shared<Lifeline>(this);
	QVariantList titles_;
	QVariantList users_;
	QString user_;
	// Each game's trophies, by its id in the console's list.
	std::map<int, QVariantList> trophies_;
	QVariantMap art_;
	// Every user's games and trophies, for comparing: user → npId → game
	// (with its "trophies").
	std::map<QString, QVariantMap> everyone_;
	QString address_;
	bool scanned_ = false;
	bool busy_ = false;
	QString status_;
	double progress_ = 0.0;
	std::thread worker_;
	std::atomic<bool> stopping_ { false };
};

} // namespace orbislink
