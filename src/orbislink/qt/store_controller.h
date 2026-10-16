// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/ftp/ftp_client.h"
#include "orbislink/settings/settings_store.h"
#include "orbislink/store/hb_store.h"
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

// The homebrew store for QML (`store`): the apps of every store turned on,
// shown together — homebrew.page's catalog (used only once its signature
// checks out), whose apps are folders a jailbroken PS5 gets over FTP where
// ShadowMountPlus finds them; and the Homebrew Store databases (PKG-Zone and
// those the user adds), whose apps are packages, downloaded to this PC and
// installed through the queue on the console in use, a PS4 or a PS5. An app
// in more than one store is one card with each store's tag; which store it
// comes from is the user's to pick. One install or removal at a time, on a
// thread of its own.
class StoreController : public QObject
{
	Q_OBJECT
	// [{ titleId, name, kind, author, version, size, updated, icon, sandbox,
	//    available, installable, installed, installedVersion, update,
	//    description, stores: [names], sources: [{ store (its address),
	//    storeName, format ("folder", "pkg"), version, size, firmware,
	//    available, installable, note }] }]
	Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
	// [{ url, name, kind, enabled, builtIn, count, error, offline }]
	Q_PROPERTY(QVariantList stores READ stores NOTIFY storesChanged)
	// A store being looked at before it is added.
	Q_PROPERTY(bool addingStore READ addingStore NOTIFY storesChanged)
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
	// The consoles apps can go to: [{ address, name, type, ftp, target }],
	// and the one they go to — the console in use; picking another makes
	// it the one in use. Its kind ("ps4", "ps5", "" while unknown).
	Q_PROPERTY(QVariantList consoles READ consoles NOTIFY consolesChanged)
	Q_PROPERTY(QString target READ target NOTIFY consolesChanged)
	Q_PROPERTY(QString targetName READ targetName NOTIFY consolesChanged)
	Q_PROPERTY(QString targetType READ targetType NOTIFY consolesChanged)
	// An app folder can go now (a PS5 whose FTP answers); a package can go
	// (to the queue, which waits for the console's installer).
	Q_PROPERTY(bool canInstall READ canInstall NOTIFY consolesChanged)
	Q_PROPERTY(bool canInstallPackages READ canInstallPackages NOTIFY consolesChanged)
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
	QVariantList stores() const;
	bool addingStore() const { return addingStore_; }
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
	QString targetType() const;
	bool canInstall() const;
	bool canInstallPackages() const;
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
	// From `storeUrl` ("": the one store that can), an app folder is
	// downloaded, checked against the catalog's SHA-256 and put on the PS5
	// (replacing the one there, which is kept aside); a package is
	// downloaded to this PC, checked against its MD5 when the store has
	// one, and queued to install on the console in use.
	Q_INVOKABLE void install(const QString &titleId, const QString &storeUrl = QString());
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

	// A store of the user's: the address is looked at first (storeAdded says
	// how it went); a Homebrew Store's is added, a copy of homebrew.page's
	// catalog becomes its mirror. False when it is not an http(s) address.
	Q_INVOKABLE bool addStore(const QString &address, const QString &name = QString());
	// Only the user's own go; the built-in ones can be turned off.
	Q_INVOKABLE void removeStore(const QString &url);
	Q_INVOKABLE void setStoreEnabled(const QString &url, bool enabled);
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
	void storesChanged();
	// The outcome of an install or a removal, for a notice.
	void finished(const QString &message, bool error);
	// Whether the store asked for was added, and what was found there.
	void storeAdded(bool ok, const QString &message);

private:
	struct Installed
	{
		std::string contentVersion;
	};
	// A Homebrew Store as last read: its apps, and why it could not be read
	// (shown from the copy kept, then).
	struct HbStore
	{
		StoreSource source;
		std::vector<store::HbStoreApp> apps;
		bool offline = false;
		QString error;
	};
	// On a worker: the store's database, fetched again when the store's MD5
	// says it changed, else the copy kept.
	static HbStore loadHbStore(const StoreSource &source, const std::string &cacheDir,
		const std::atomic<bool> *cancel);
	static std::string hbStoreDir(const std::string &cacheDir, const std::string &base);
	// Downloads a store's package to this PC and queues it to install.
	void installPackage(const QString &titleId, const StoreSource &source, const store::HbStoreApp &app);
	// What can or cannot go from a store to the console apps go to.
	QVariantMap sourceFor(const StoreSource &source, const store::StoreApp *folder,
		const store::HbStoreApp *package) const;
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
	std::vector<HbStore> hbStores_;
	// By title ID: homebrew.page's icon, else a store's.
	std::map<std::string, std::string> icons_;
	std::map<std::string, Installed> installed_;
	// What the console has under /user/app, with the version of those a
	// store has (as the console says it, "01.08").
	std::map<std::string, std::string> installedPackages_;
	bool installedKnown_ = false;
	bool addingStore_ = false;
	std::thread adder_;
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
