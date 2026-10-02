// SPDX-License-Identifier: AGPL-3.0-or-later
//
// "PS1/PS2 Games": the discs in a folder of the PC, sent to the console as
// they are or turned into PS4 packages and installed.
//
// Conversions run one at a time on a thread of their own and show up in the
// queue panel while they happen; a package meant for installing then goes
// into the ordinary install queue.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QMap>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace orbislink {

class AppController;

class GamesController : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool available READ available CONSTANT)
	Q_PROPERTY(QString gamesFolder READ gamesFolder NOTIFY foldersChanged)
	Q_PROPERTY(QString emulatorFolder READ emulatorFolder NOTIFY foldersChanged)
	Q_PROPERTY(QString outputFolder READ outputFolder NOTIFY foldersChanged)
	Q_PROPERTY(bool ps1Emulator READ ps1Emulator NOTIFY foldersChanged)
	Q_PROPERTY(bool ps2Emulator READ ps2Emulator NOTIFY foldersChanged)
	Q_PROPERTY(QString ps2EmulatorName READ ps2EmulatorName NOTIFY foldersChanged)
	Q_PROPERTY(bool scanning READ scanning NOTIFY scanChanged)
	Q_PROPERTY(QString scanStatus READ scanStatus NOTIFY scanChanged)
	Q_PROPERTY(QVariantList games READ games NOTIFY scanChanged)
	Q_PROPERTY(QVariantList conversions READ conversions NOTIFY conversionsChanged)
	Q_PROPERTY(bool converting READ converting NOTIFY conversionsChanged)
	// Where each game stands, by its "path": { stage, percent, message },
	// stage being "waiting", "converting", "converted", "sending", "sent",
	// "installing", "installed" or "error". Games with nothing going on
	// are not in it.
	Q_PROPERTY(QVariantMap progress READ progress NOTIFY progressChanged)

public:
	explicit GamesController(AppController *app, QObject *parent = nullptr);
	~GamesController() override;

	bool available() const;
	QString gamesFolder() const;
	QString emulatorFolder() const;
	QString outputFolder() const;
	bool ps1Emulator() const { return ps1Emulator_; }
	bool ps2Emulator() const { return ps2Emulator_; }
	QString ps2EmulatorName() const { return ps2EmulatorName_; }
	bool scanning() const { return scanning_; }
	QString scanStatus() const { return scanStatus_; }
	QVariantList games() const { return games_; }
	QVariantList conversions() const { return conversions_; }
	bool converting() const;
	QVariantMap progress() const { return progress_; }

	// Folders come as paths or file:// URLs (what FolderDialog gives).
	Q_INVOKABLE void setGamesFolder(const QString &folder);
	Q_INVOKABLE void setEmulatorFolder(const QString &folder);
	Q_INVOKABLE void setOutputFolder(const QString &folder);
	Q_INVOKABLE void rescan();

	// `paths` are the games' "path" values. `install` sends each package to
	// the console over FTP once it is built and then installs it; otherwise
	// it stays in the output folder. `titles` (optional, same order)
	// overrides the names.
	Q_INVOKABLE void convert(const QStringList &paths, bool install, const QStringList &titles = {});
	// The disc files themselves, over FTP, to the folder the file list is in.
	Q_INVOKABLE void sendToConsole(const QStringList &paths);
	Q_INVOKABLE void cancelConversion(const QString &id);
	Q_INVOKABLE void clearFinishedConversions();
	Q_INVOKABLE void openOutputFolder() const;
	// A folder as FolderDialog wants it (empty: the user's documents).
	Q_INVOKABLE QUrl folderUrl(const QString &path) const;
	Q_INVOKABLE QString packageNameFor(const QString &path, const QString &title) const;

signals:
	void foldersChanged();
	void scanChanged();
	void conversionsChanged();
	void progressChanged();

private:
	struct Job;
	QString toLocalPath(const QString &folder) const;
	void refreshEmulators();
	QVariantMap gameByPath(const QString &path) const;
	void workerLoop();
	void runJob(const std::shared_ptr<Job> &job);
	void publish();
	void updateProgress();

	AppController *app_;
	bool ps1Emulator_ = false;
	bool ps2Emulator_ = false;
	QString ps2EmulatorName_;
	bool scanning_ = false;
	QString scanStatus_;
	QVariantList games_;
	quint64 scanRun_ = 0;

	QVariantList conversions_;
	QVariantMap progress_;
	// The package each disc became (kept after the conversion rows go).
	QMap<QString, QString> packageOf_;
	// Packages on their way over FTP that get installed once they land
	// (the flag: their upload was seen under way).
	QMap<QString, bool> installAfterSend_;
	mutable std::mutex mutex_;
	std::deque<std::shared_ptr<Job>> jobs_;
	std::condition_variable wakeup_;
	std::atomic<bool> stopping_ { false };
	std::atomic<bool> publishPending_ { false };
	std::thread worker_;
	int nextId_ = 1;
};

} // namespace orbislink
