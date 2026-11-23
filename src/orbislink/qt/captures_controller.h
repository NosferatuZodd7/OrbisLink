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
#include <memory>
#include <thread>

namespace orbislink {

class AppController;
class FtpClient;

// The console's captures (screenshots and video clips) for QML
// (`captures`): read over FTP from where the console keeps them
// (/user/av_contents: on a PS4 a folder per game under photo and video; on a
// PS5 only the console's own previews can be read, see the .cpp), shown with
// small pictures made on this PC, copied to this PC or (on a PS4) deleted
// from the console. One action at a time, on a thread of its own, with its
// own FTP connection.
class CapturesController : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QVariantList captures READ captures NOTIFY capturesChanged)
	// Each capture's small picture made on this PC (its path → a file URL),
	// apart from the list so the grid is not rebuilt as they come.
	Q_PROPERTY(QVariantMap thumbs READ thumbs NOTIFY thumbsChanged)
	Q_PROPERTY(bool scanned READ scanned NOTIFY capturesChanged)
	// Only the small pictures are being made: Copy and Delete stop them.
	Q_PROPERTY(bool thumbnailing READ thumbnailing NOTIFY thumbsChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(QString status READ status NOTIFY busyChanged)
	Q_PROPERTY(double progress READ progress NOTIFY busyChanged)
	Q_PROPERTY(QString where READ where CONSTANT)
	Q_PROPERTY(QString folder READ folder CONSTANT)
	// The console in use is a PS5: what is listed are its previews, and
	// nothing is deleted.
	Q_PROPERTY(bool ps5 READ ps5 NOTIFY consoleChanged)

public:
	explicit CapturesController(AppController *app, QObject *parent = nullptr);
	~CapturesController() override;

	QVariantList captures() const { return captures_; }
	QVariantMap thumbs() const { return thumbs_; }
	bool scanned() const { return scanned_; }
	bool thumbnailing() const { return thumbnailing_; }
	bool busy() const { return busy_; }
	QString status() const { return status_; }
	double progress() const { return progress_; }
	// Where on the console they are looked for.
	QString where() const;
	// Where on this PC they are copied to (Pictures/OrbisLink captures).
	QString folder() const;
	bool ps5() const;

	// Lists the console's captures again (and makes the missing pictures).
	Q_INVOKABLE void refresh();
	// Copies them to this PC, each into its game's folder (all of them when
	// `paths` is empty); one already there with the same size is skipped.
	Q_INVOKABLE void download(const QStringList &paths);
	// Deletes them from the console, then lists again. Not on a PS5: the
	// preview listed is not the original.
	Q_INVOKABLE void remove(const QStringList &paths);
	Q_INVOKABLE void openFolder() const;

signals:
	void capturesChanged();
	void thumbsChanged();
	void busyChanged();
	void consoleChanged();
	// The outcome of an action, for a notice.
	void finished(const QString &message, bool error);

private:
	using Job = std::function<QString(FtpClient &ftp, bool *error)>;
	// Runs `job` (may be empty) then, with `list`, reads the captures again.
	void run(const QString &what, Job job, bool list);
	void setProgress(const QString &status, double fraction);

	AppController *app_;
	LifelinePtr lifeline_ = std::make_shared<Lifeline>(this);
	QVariantList captures_;
	QVariantMap thumbs_;
	// The console the list is of, and its type then: another one in use (or
	// the type first known) empties it.
	QString listedAddress_;
	bool listedPs5_ = false;
	// Making the small pictures, which a click on Copy or Delete stops.
	bool thumbnailing_ = false;
	std::atomic<bool> thumbStop_ { false };
	bool scanned_ = false;
	bool busy_ = false;
	QString status_;
	double progress_ = 0.0;
	std::thread worker_;
	std::atomic<bool> stopping_ { false };
};

} // namespace orbislink
