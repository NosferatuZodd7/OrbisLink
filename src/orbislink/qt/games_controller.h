// SPDX-License-Identifier: AGPL-3.0-or-later
//
// "PS1/PS2 Games": the discs in a folder of the PC, sent to the console as
// they are or turned into PS4 packages and installed.
//
// Conversions run one at a time on a thread of their own and show up in the
// queue panel while they happen; a package meant for installing then goes
// into the ordinary install queue. Like easy-ps2-fpkg, nothing has to be
// provided: the emulator files are downloaded the first time they are needed
// and kept (ORBISLINK_CLASSICS_ASSETS points at a folder that already has
// them).
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
#include <functional>
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
	Q_PROPERTY(QString outputFolder READ outputFolder NOTIFY foldersChanged)
	// Where on the console a game goes to be installed ("internal", "usb",
	// "ext"), its folder there, and which drives the console has:
	// { checked, usb, ext } (checked: false until it was asked).
	Q_PROPERTY(QString storage READ storage NOTIFY foldersChanged)
	Q_PROPERTY(QString storageFolder READ storageFolder NOTIFY foldersChanged)
	Q_PROPERTY(QVariantMap drives READ drives NOTIFY drivesChanged)
	// The emulator files: "missing" (downloaded with the first conversion),
	// "downloading", "unpacking", "ready" or "error" (assetsMessage says why).
	Q_PROPERTY(QString assetsState READ assetsState NOTIFY assetsChanged)
	Q_PROPERTY(double assetsPercent READ assetsPercent NOTIFY assetsChanged)
	Q_PROPERTY(QString assetsMessage READ assetsMessage NOTIFY assetsChanged)
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
	QString outputFolder() const;
	QString storage() const;
	QString storageFolder() const;
	QVariantMap drives() const { return drives_; }
	// The folder a game goes to on that storage.
	static QString folderOn(const QString &storage);
	QString assetsState() const { return assetsState_; }
	double assetsPercent() const { return assetsPercent_; }
	QString assetsMessage() const { return assetsMessage_; }
	bool scanning() const { return scanning_; }
	QString scanStatus() const { return scanStatus_; }
	QVariantList games() const { return games_; }
	QVariantList conversions() const { return conversions_; }
	bool converting() const;
	QVariantMap progress() const { return progress_; }

	// Folders come as paths or file:// URLs (what FolderDialog gives).
	Q_INVOKABLE void setGamesFolder(const QString &folder);
	Q_INVOKABLE void setOutputFolder(const QString &folder);
	Q_INVOKABLE void setStorage(const QString &storage);
	Q_INVOKABLE QString folderFor(const QString &storage) const { return folderOn(storage); }
	// Asks the console in use over FTP which drives it has (drives).
	Q_INVOKABLE void checkDrives();
	Q_INVOKABLE void rescan();
	// Gets the emulator files now instead of with the first conversion.
	Q_INVOKABLE void downloadAssets();

	// `paths` are the games' "path" values. `install` sends each package to
	// the console over FTP once it is built and then installs it; otherwise
	// it stays in the output folder. `titles` (optional, same order)
	// overrides the names.
	// `reuse`: a package already in the output folder is sent as it is.
	Q_INVOKABLE void convert(const QStringList &paths, bool install, const QStringList &titles = {},
		bool reuse = false);
	// The package made earlier for this disc under this name, or empty.
	Q_INVOKABLE QString existingPackage(const QString &path, const QString &title) const;
	// The disc files themselves, over FTP, to the folder the file list is in.
	Q_INVOKABLE void sendToConsole(const QStringList &paths);
	Q_INVOKABLE void cancelConversion(const QString &id);
	Q_INVOKABLE void clearFinishedConversions();
	// Takes a finished conversion's card away.
	Q_INVOKABLE void removeConversion(const QString &id);
	Q_INVOKABLE void openOutputFolder() const;
	// A folder as FolderDialog wants it (empty: the user's documents).
	Q_INVOKABLE QUrl folderUrl(const QString &path) const;
	Q_INVOKABLE QString packageNameFor(const QString &path, const QString &title) const;

signals:
	void foldersChanged();
	void scanChanged();
	void conversionsChanged();
	void progressChanged();
	void assetsChanged();
	void drivesChanged();

private:
	struct Job;
	QString toLocalPath(const QString &folder) const;
	QString assetsFolder() const;
	// Downloads and unpacks the emulator files unless they are there; one
	// at a time, from any thread. `progress` gets "download"/"unpack" and a
	// percentage; returning false cancels.
	bool ensureAssets(const std::function<bool(const QString &, double)> &progress, QString *error);
	void setAssets(const QString &state, double percent, const QString &message = {});
	QVariantMap gameByPath(const QString &path) const;
	void workerLoop();
	void runJob(const std::shared_ptr<Job> &job);
	void publish();
	void updateProgress();

	AppController *app_;
	QString assetsState_;
	double assetsPercent_ = 0;
	QString assetsMessage_;
	std::mutex assetsMutex_;
	bool scanning_ = false;
	QString scanStatus_;
	QVariantList games_;
	quint64 scanRun_ = 0;

	QVariantList conversions_;
	QVariantMap progress_;
	// The package each disc became (kept after the conversion rows go).
	QMap<QString, QString> packageOf_;
	mutable std::mutex mutex_;
	std::deque<std::shared_ptr<Job>> jobs_;
	std::condition_variable wakeup_;
	std::atomic<bool> stopping_ { false };
	std::atomic<bool> publishPending_ { false };
	std::thread worker_;
	std::thread drivesThread_;
	QVariantMap drives_;
	int nextId_ = 1;
};

} // namespace orbislink
