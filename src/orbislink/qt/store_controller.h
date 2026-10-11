// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/ftp/ftp_client.h"
#include "orbislink/store/store_catalog.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace orbislink {

class AppController;

// The PS5 homebrew store for QML (`store`): the catalog of homebrew.page,
// used only once its signature checks out; each app's details; installing
// an app on a jailbroken PS5 over FTP — its folder goes where ShadowMountPlus
// finds it, which puts it on the home screen — updating it, removing it.
// One install or removal at a time, on a thread of its own.
class StoreController : public QObject
{
	Q_OBJECT
	// [{ titleId, name, kind, author, version, size, updated, icon, sandbox,
	//    available, installable, installed, installedVersion, update }]
	Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
	// Reading the catalog, and why it could not be (empty when it was).
	Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
	Q_PROPERTY(QString error READ error NOTIFY stateChanged)
	// The catalog shown is the last one kept: the site did not answer.
	Q_PROPERTY(bool offline READ offline NOTIFY appsChanged)
	// The app whose details were asked for, once read:
	// { titleId, ready, error, description, license, sourceRepo, page,
	//   releaseNotes, prerelease, safetyKnown, safetyRoutes, safetyNetwork,
	//   safetyHelpers, … and the fields of apps }.
	Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
	// The app being installed or removed, the stage and how far.
	Q_PROPERTY(QString working READ working NOTIFY stateChanged)
	Q_PROPERTY(QString stage READ stage NOTIFY progressChanged)
	Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
	// While bytes move (the download, the copy to the PS5): the speed,
	// steadied over the last seconds ("3.2 MB/s", "" until it is known),
	// and how much of how much ("12.3 MB / 26.0 MB").
	Q_PROPERTY(QString speedText READ speedText NOTIFY progressChanged)
	Q_PROPERTY(QString amountText READ amountText NOTIFY progressChanged)
	// The PS5s apps can go to: [{ address, name, ftp, target }], and the one
	// they go to.
	Q_PROPERTY(QVariantList consoles READ consoles NOTIFY consolesChanged)
	Q_PROPERTY(QString target READ target NOTIFY consolesChanged)
	Q_PROPERTY(QString targetName READ targetName NOTIFY consolesChanged)
	Q_PROPERTY(bool canInstall READ canInstall NOTIFY consolesChanged)
	// Why an installed app is or is not on the PS5's home screen, from
	// ShadowMountPlus's own log: { titleId, busy, verdict ("registered",
	// "settling", "bad-metadata", "failed", "gave-up", "duplicate",
	// "not-seen", "no-log", "no-ftp"), code, lines, rescanned }.
	Q_PROPERTY(QVariantMap homeCheck READ homeCheck NOTIFY homeCheckChanged)
	// The installs and removals of this session, for the queue panel, the
	// latest first: [{ id, titleId, name, icon, kind ("install"/"remove"),
	// state ("working"/"done"/"error"/"cancelled"), stageText, percent,
	// speedText, amountText, etaText, message }].
	Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
	// The catalog's address of the user's own ("" for the built-in ones),
	// and the site the catalog in use came from.
	Q_PROPERTY(QString catalogAddress READ catalogAddress NOTIFY catalogAddressChanged)
	Q_PROPERTY(QString catalogSource READ catalogSource NOTIFY appsChanged)
	// "On this PC": the app archives (.zip) in a folder of this PC —
	// [{ path, file, name, titleId, version, sizeText, installed, update,
	//    problem }] — and that folder.
	Q_PROPERTY(QVariantList localApps READ localApps NOTIFY localChanged)
	Q_PROPERTY(QString localFolder READ localFolder NOTIFY localChanged)
	Q_PROPERTY(bool localScanning READ localScanning NOTIFY localChanged)

public:
	explicit StoreController(AppController *app, QObject *parent = nullptr);
	~StoreController() override;

	QVariantList apps() const { return apps_; }
	bool loading() const { return loading_; }
	QString error() const { return error_; }
	bool offline() const { return offline_; }
	QVariantMap detail() const { return detail_; }
	QString working() const { return working_; }
	QString stage() const { return stage_; }
	double progress() const { return progress_; }
	QString speedText() const { return speedText_; }
	QString amountText() const { return amountText_; }
	QVariantList consoles() const;
	QString target() const;
	QString targetName() const;
	bool canInstall() const;
	QVariantMap homeCheck() const { return homeCheck_; }
	QVariantList jobs() const { return jobs_; }
	QString catalogAddress() const;
	QString catalogSource() const { return catalogSource_; }
	QVariantList localApps() const { return localApps_; }
	QString localFolder() const;
	bool localScanning() const { return localScanning_; }

	// Reads the catalog again, and what the PS5 has installed.
	Q_INVOKABLE void refresh();
	Q_INVOKABLE void showDetails(const QString &titleId);
	// Downloads the app, checks it against the catalog's SHA-256 and puts its
	// folder on the PS5 (replacing the one there, which is kept aside).
	Q_INVOKABLE void install(const QString &titleId);
	// Takes the app away from the PS5 (its saves stay).
	Q_INVOKABLE void uninstall(const QString &titleId);
	Q_INVOKABLE void cancel();
	// The PS5 apps go to (its address).
	Q_INVOKABLE void setTarget(const QString &address);
	// Reads ShadowMountPlus's log on the PS5 for this app (homeCheck), and
	// asks it to look again, retrying what it gave up on.
	Q_INVOKABLE void checkHomeScreen(const QString &titleId);
	Q_INVOKABLE void removeJob(const QString &id);
	Q_INVOKABLE void clearFinishedJobs();

	// A mirror of the catalog: "" puts the built-in addresses back. False
	// (and nothing changes) when it is not an http(s) address.
	Q_INVOKABLE bool setCatalogAddress(const QString &address);
	Q_INVOKABLE QString defaultCatalogAddress() const;
	// "On this PC": the folder read again, another folder, and an archive of
	// this PC put on the PS5 the way the catalog's are (no checksum to check
	// it against: it is the user's own file).
	Q_INVOKABLE void scanLocal();
	Q_INVOKABLE void setLocalFolder(const QUrl &folder);
	Q_INVOKABLE QUrl localFolderUrl() const;
	// `path`: a path of this PC, or a file: URL.
	Q_INVOKABLE void installLocal(const QString &path);

signals:
	void appsChanged();
	void stateChanged();
	void detailChanged();
	void progressChanged();
	void consolesChanged();
	void homeCheckChanged();
	void jobsChanged();
	void catalogAddressChanged();
	void localChanged();
	// The outcome of an install or a removal, for a notice.
	void finished(const QString &message, bool error);

private:
	struct Installed
	{
		std::string contentVersion;
	};
	void publish();
	void readInstalled(const std::string &address);
	void startWork(const QString &titleId, const QString &what, const QString &kind, std::function<void()> work);
	// The job under way takes these fields.
	void updateJob(const QVariantMap &fields);
	// From the worker: the stage and its bytes (total 0 when it has none).
	void setProgress(const QString &stage, int64_t done, int64_t total);
	void resetProgress();
	std::string cacheDir() const;
	// On the worker: the app folder of `zip` put on the PS5 and
	// ShadowMountPlus told; then finishInstall.
	void putOnConsole(const std::string &id, const QString &name, const std::string &zip, bool keepZip,
		const std::string &address, const QString &console, const FtpClient::Config &config, const std::string &cache,
		const std::string &version);
	void finishInstall(const std::string &id, const QString &message, bool error, const std::string &version);


	AppController *app_;
	std::unique_ptr<store::StoreCatalog> catalog_;
	// The catalog is read, details are asked for and apps installed on
	// different threads: one at a time touches it.
	mutable std::mutex catalogMutex_;
	std::vector<store::StoreApp> list_;
	std::map<std::string, std::string> icons_;
	std::map<std::string, Installed> installed_;
	bool installedKnown_ = false;
	QVariantList apps_;
	bool loading_ = false;
	QString error_;
	bool offline_ = false;
	QVariantMap detail_;
	QString working_;
	QString stage_;
	double progress_ = 0.0;
	QString speedText_;
	QString amountText_;
	// The speed meter, on the worker only: recent (time, bytes) samples of
	// the current stage, and when the window last heard.
	QString meterStage_;
	std::deque<std::pair<int64_t, int64_t>> samples_;
	int64_t lastPostMs_ = 0;
	QString target_;
	std::atomic<bool> cancel_ { false };
	std::atomic<bool> stopIcons_ { false };
	std::thread loader_;
	std::thread detailer_;
	std::thread worker_;
	QVariantMap homeCheck_;
	std::thread checker_;
	QVariantList jobs_;
	QString jobId_;
	QString catalogSource_;
	QVariantList localApps_;
	bool localScanning_ = false;
	std::thread localScanner_;
};

} // namespace orbislink
