// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/console/console_manager.h"
#include "orbislink/ftp/ftp_client.h"
#include "orbislink/http/local_http_server.h"
#include "orbislink/installer/rpi_client.h"
#include "orbislink/qt/ftp_model.h"
#include "orbislink/qt/queue_model.h"
#include "orbislink/queue/install_queue.h"
#include "orbislink/settings/settings_store.h"

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace orbislink {

// Bridge between the core (its own threads) and QML (the UI thread).
// The core does not know Qt: this is where listeners are forwarded to
// signals, always marshalled to the UI thread.
class AppController : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QString consoleName READ consoleName NOTIFY settingsChanged)
	Q_PROPERTY(QString consoleAddress READ consoleAddress NOTIFY settingsChanged)
	// The saved consoles: [{ name, address, active, type, hostId }].
	Q_PROPERTY(QVariantList consoles READ consoles NOTIFY settingsChanged)
	// The saved PSN Account IDs: [{ label, accountId, usedBy }], accountId in
	// base64 and usedBy the names of the consoles that use it.
	Q_PROPERTY(QVariantList accounts READ accounts NOTIFY settingsChanged)
	Q_PROPERTY(QString remotePlayState READ remotePlayState NOTIFY statusChanged)
	Q_PROPERTY(QString remotePlayHint READ remotePlayHint NOTIFY statusChanged)
	Q_PROPERTY(QString ftpState READ ftpState NOTIFY statusChanged)
	Q_PROPERTY(QString ftpHint READ ftpHint NOTIFY statusChanged)
	Q_PROPERTY(QString installerState READ installerState NOTIFY statusChanged)
	Q_PROPERTY(QString installerHint READ installerHint NOTIFY statusChanged)
	Q_PROPERTY(bool canInstallDirectly READ canInstallDirectly NOTIFY statusChanged)
	Q_PROPERTY(bool canUseFtp READ canUseFtp NOTIFY statusChanged)
	Q_PROPERTY(bool queuePaused READ queuePaused NOTIFY queueStateChanged)
	Q_PROPERTY(QString pauseReason READ pauseReason NOTIFY queueStateChanged)
	Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
	Q_PROPERTY(QString httpServerAddress READ httpServerAddress NOTIFY statusChanged)
	Q_PROPERTY(QString ftpPath READ ftpPath NOTIFY ftpPathChanged)
	Q_PROPERTY(bool ftpBusy READ ftpBusy NOTIFY ftpBusyChanged)
	// The FTP uploads not finished yet, for the file list to show them where
	// they are going: [{name, directory, percent, sending}].
	Q_PROPERTY(QVariantList ftpUploads READ ftpUploads NOTIFY ftpUploadsChanged)
	Q_PROPERTY(bool downloadActive READ downloadActive NOTIFY downloadChanged)
	Q_PROPERTY(QString downloadName READ downloadName NOTIFY downloadChanged)
	Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadChanged)
	Q_PROPERTY(QStringList ftpShortcuts READ ftpShortcuts CONSTANT)
	Q_PROPERTY(orbislink::QueueModel *queue READ queue CONSTANT)
	Q_PROPERTY(orbislink::FtpModel *files READ files CONSTANT)
	Q_PROPERTY(QString version READ version CONSTANT)
	// True when the application was built with Remote Play.
	Q_PROPERTY(bool streamAvailable READ streamAvailable CONSTANT)

	// Updates: "idle", "checking", "available", "downloading",
	// "ready", "up-to-date" or "error".
	Q_PROPERTY(QString updateState READ updateState NOTIFY updateChanged)
	Q_PROPERTY(QString updateMessage READ updateMessage NOTIFY updateChanged)
	Q_PROPERTY(QString updateVersion READ updateVersion NOTIFY updateChanged)
	Q_PROPERTY(QString updateNotes READ updateNotes NOTIFY updateChanged)
	Q_PROPERTY(QString updatePageUrl READ updatePageUrl NOTIFY updateChanged)
	Q_PROPERTY(bool updateCanInstall READ updateCanInstall NOTIFY updateChanged)
	Q_PROPERTY(double updateProgress READ updateProgress NOTIFY updateChanged)

public:
	explicit AppController(QObject *parent = nullptr);
	~AppController() override;

	QString consoleName() const;
	QString consoleAddress() const;
	QVariantList consoles() const;
	QVariantList accounts() const;
	QString remotePlayState() const;
	QString remotePlayHint() const;
	QString ftpState() const;
	QString ftpHint() const;
	QString installerState() const;
	QString installerHint() const;
	bool canInstallDirectly() const;
	bool canUseFtp() const;
	// The console in use is a PS5 (by the type that was stored).
	bool activeIsPs5() const;
	// The FTP port of the console in use (the PS4 one or the PS5 one).
	uint16_t activeFtpPort() const;
	// The Account ID of the console in use, or the last accepted one if it
	// does not have one yet.
	std::string activeAccountId() const;
	bool queuePaused() const;
	QString pauseReason() const;
	QString statusMessage() const { return statusMessage_; }
	QString httpServerAddress() const;
	QString ftpPath() const { return ftpPath_; }
	bool ftpBusy() const { return ftpBusy_; }
	QVariantList ftpUploads() const { return ftpUploads_; }
	bool downloadActive() const { return downloadActive_; }
	QString downloadName() const { return downloadName_; }
	double downloadProgress() const { return downloadProgress_; }
	QStringList ftpShortcuts() const;
	QueueModel *queue() { return &queueModel_; }
	FtpModel *files() { return &ftpModel_; }
	QString version() const;
	// The settings in effect, for whoever needs all of them (the
	// Remote Play controller).
	const Settings &settings() const { return settings_; }
	// Stores the PSN Account ID, so it does not have to be typed every time.
	Q_INVOKABLE void rememberAccountId(const QString &accountId);
	bool streamAvailable() const
	{
#ifdef ORBISLINK_HAS_STREAM
		return true;
#else
		return false;
#endif
	}

	// Dropping files: mode 0 = direct install, 1 = FTP upload.
	Q_INVOKABLE void dropUrls(const QList<QUrl> &urls, int mode);
	Q_INVOKABLE void addPaths(const QStringList &paths, int mode);
	// The answer to uploadConflicts: one {index, action, name} per file that
	// was already on the console; action is "overwrite", "rename" (to name)
	// or "skip". The other files of the drop go as they were.
	Q_INVOKABLE void resolveUploadConflicts(const QVariantList &decisions);
	Q_INVOKABLE void checkServicesNow();
	// The Remote Play state comes from chiaki (StreamController), not from
	// the periodic check: this is how it reaches the status bar indicator.
	Q_INVOKABLE void reportRemotePlayState(const QString &state, const QString &detail);
	// QML reports here every time a drag enters, leaves or is dropped.
	//
	// Without counters there is no telling Windows not delivering the event
	// (an elevated application, for example) apart from us refusing it. With
	// them, zero drags seen is an answer, not a guess.
	Q_INVOKABLE void noteDrag(const QString &eventName, bool withFiles);
	QString dragSummary() const;
	// Diagnostics need to ask Remote Play how the audio path is doing, but
	// AppController does not know StreamController (there is not always
	// one). It keeps only the question, not who answers it.
	void setAudioProbe(std::function<QString()> probe);
	QString audioProbe() const;
	void setVideoProbe(std::function<QString()> probe);
	QString videoProbe() const;
	Q_INVOKABLE void cancelTask(const QString &id);
	Q_INVOKABLE void retryTask(const QString &id);
	Q_INVOKABLE void removeTask(const QString &id);
	Q_INVOKABLE void moveTaskUp(const QString &id);
	Q_INVOKABLE void moveTaskDown(const QString &id);
	Q_INVOKABLE void pauseQueue();
	Q_INVOKABLE void resumeQueue();

	Q_INVOKABLE void ftpNavigate(const QString &path);
	Q_INVOKABLE void ftpRefresh();
	Q_INVOKABLE void ftpUp();
	Q_INVOKABLE void ftpDelete(const QString &path, bool isDirectory);
	Q_INVOKABLE void ftpMakeDirectory(const QString &name);
	Q_INVOKABLE void ftpRename(const QString &path, const QString &newName);

	// Bring from the console to the PC (§5.5). Empty `destination` = default
	// folder (desktop).
	Q_INVOKABLE void ftpDownload(const QString &remotePath, const QString &name,
		const QString &destinationDir);
	// The same, but for the local cache that feeds dragging out.
	Q_INVOKABLE void ftpPrepareForDrag(const QString &remotePath, const QString &name, qint64 size);
	// Local URL of the file if it is already cached with the right size, otherwise empty.
	Q_INVOKABLE QString cachedFileUrl(const QString &remotePath, qint64 size) const;
	Q_INVOKABLE void cancelDownload();
	Q_INVOKABLE QString defaultDownloadDirectory() const;
	Q_INVOKABLE void openLocalFolder(const QString &path) const;
	Q_INVOKABLE void copyToClipboard(const QString &text) const;
	Q_INVOKABLE void setFtpUploadDirectory(const QString &path);

	// One-off check of an address not saved yet: the settings dialog calls
	// this while the IP is typed, without touching the settings in effect.
	Q_INVOKABLE void probeConsole(const QString &address, int ftpPort, int installerPort);

	// Diagnostics: the live log in the window, and the report in a file
	// that can be attached to a message.
	Q_INVOKABLE QStringList recentLog(int lines = 300) const;
	Q_INVOKABLE QString diagnosticsReport() const;
	Q_INVOKABLE QString exportDiagnostics(const QString &directory = QString());
	Q_INVOKABLE void copyDiagnosticsToClipboard();
	Q_INVOKABLE QString logFilePath() const;
	Q_INVOKABLE void setStreamVerbose(bool verbose);
	Q_INVOKABLE bool streamVerbose() const { return streamVerbose_; }

	// Updates. Checking and downloading happen off the interface thread;
	// the state arrives through updateChanged().
	Q_INVOKABLE void checkForUpdatesNow(bool silentWhenUpToDate = false);
	// Downloads the installer, confirms the SHA-256 when one is published,
	// and runs it. On systems without a published installer, opens the page.
	Q_INVOKABLE void installUpdate();
	Q_INVOKABLE void openUpdatePage() const;
	Q_INVOKABLE void dismissUpdate();
	// Only for screenshots (--demo-update): fills in the fields from a
	// sample reply, through the same code path the real reply
	// follows.
	Q_INVOKABLE void loadDemoUpdate();
	QString updateState() const { return updateState_; }
	QString updateMessage() const { return updateMessage_; }
	QString updateVersion() const { return updateVersion_; }
	QString updateNotes() const { return updateNotes_; }
	QString updatePageUrl() const { return updatePageUrl_; }
	bool updateCanInstall() const { return !updateAssetUrl_.isEmpty(); }
	double updateProgress() const { return updateProgress_; }

	Q_INVOKABLE QVariantMap settingsMap() const;
	Q_INVOKABLE void applySettings(const QVariantMap &values);
	// Switches to the console with this address (it must be in the list).
	Q_INVOKABLE void selectConsole(const QString &address);
	// Adds a console to the list and, with `select`, switches to it. If the
	// address is already there, it is just selected.
	// `type` is "ps4", "ps5" or empty (unknown).
	Q_INVOKABLE void addConsole(const QString &name, const QString &address,
		const QString &type = QString(), bool select = true);
	// Remembers the type of a console that answered, to show it even when
	// it is off. Only saves if it changed.
	Q_INVOKABLE void rememberConsoleType(const QString &address, bool ps5,
		const QString &hostId = QString());
	// Changes the name and IP of a saved console (the one in use or any
	// other). Returns false, with the reason in the status bar, when the
	// new IP is empty or already belongs to another console.
	Q_INVOKABLE bool updateConsole(const QString &oldAddress, const QString &name,
		const QString &address);
	// Saves an Account ID (base64) under a name. With `oldAccountId` it edits
	// that entry, and the consoles using it follow. Returns false, with the
	// reason in the status bar, when the ID is empty or already saved.
	Q_INVOKABLE bool saveAccount(const QString &oldAccountId, const QString &label,
		const QString &accountId);
	// Removes a saved Account ID; the consoles using it are left without one.
	Q_INVOKABLE void removeAccount(const QString &accountId);
	// Chooses which saved Account ID a console registers with ("" for none).
	Q_INVOKABLE void setConsoleAccount(const QString &address, const QString &accountId);
	// Removes a console from the list. If it is the one in use, the next
	// one takes its place; the last console cannot be removed.
	Q_INVOKABLE void removeConsole(const QString &address);
	// Saves only the theme. applySettings rebuilds all the services (queue,
	// HTTP server, console manager) — changing theme in the middle of an
	// install would stop the transfer.
	Q_INVOKABLE void setTheme(const QString &theme);
	// Saves only what the update check needs, for the same reason:
	// pressing "Check now" must not touch anything else.
	// Saves the keyboard-as-controller keys (action → key), without
	// rebuilding the services.
	void saveKeyBindings(const std::map<std::string, int> &bindings);
	Q_INVOKABLE void saveUpdateSettings(bool checkForUpdates, const QString &repository,
		const QString &channel);

signals:
	void settingsChanged();
	// A new log line, already masked. The diagnostics window connects to
	// this to show what happens in real time.
	void logLine(const QString &level, const QString &text);
	void statusChanged();
	void queueStateChanged();
	void statusMessageChanged();
	void ftpPathChanged();
	void ftpBusyChanged();
	void ftpUploadsChanged();
	// Some of the files dropped for FTP already exist in the upload folder:
	// [{index, name, localSize, remoteSize, suggestion}]. The window asks
	// what to do and answers with resolveUploadConflicts.
	void uploadConflicts(const QVariantList &conflicts);
	// Files were queued: the panel shows where they can be followed —
	// "queue" for installs, "files" for FTP uploads.
	void showPanel(const QString &which);
	void notify(const QString &title, const QString &message, bool error);
	void consoleProbed(const QString &address, bool ftpOk, bool installerOk,
		const QString &detail);
	void downloadChanged();
	// Emitted when a file is ready in the local cache: FtpBrowser can then
	// drag it out of the window.
	void dragFileReady(const QString &remotePath, const QString &localUrl);
	void updateChanged();
	// Emitted when there is a new version and the check was not silent:
	// the window opens the dialog from here.
	void updateAvailable(const QString &version);

private:
	void rebuildBackends();
	// Says why a click did nothing, instead of swallowing it silently.
	// Returns false when the operation cannot go ahead.
	bool ftpReady(const QString &operation);
	void refreshQueueModel();
	void setStatusMessage(const QString &message);
	void setFtpBusy(bool busy);
	static QStringList collectPkgFiles(const QStringList &paths);
	void registerIcons(const QStringList &paths, const QStringList &taskIds);
	void enqueueFiles(const QStringList &files, TransferMode mode,
		const std::vector<std::string> &remoteNames = {});
	// Lists the upload folder and, for the files already there, asks first.
	void checkUploadConflicts(const QStringList &files);
	void refreshFtpListing(bool announce);
	QString uploadDirectory() const;

	Settings settings_;
	SettingsStore store_;
	ConsoleStatus status_;

	std::unique_ptr<ConsoleManager> console_;
	std::unique_ptr<LocalHttpServer> httpServer_;
	std::unique_ptr<RpiClient> installer_;
	std::unique_ptr<FtpClient> ftp_;
	std::unique_ptr<InstallQueue> queue_;

	QueueModel queueModel_;
	FtpModel ftpModel_;
	QString statusMessage_;
	QString ftpPath_ = QStringLiteral("/data/pkg/");
	bool ftpBusy_ = false;
	QVariantList ftpUploads_;
	// The FTP drop waiting for an answer about the names already taken.
	QStringList pendingUploads_;
	// Only the most recent check matters: earlier ones are discarded when
	// they arrive, so the result never contradicts what is on screen.
	std::atomic<uint64_t> probeGeneration_ { 0 };
	bool streamVerbose_ = false;
	int dragsSeen_ = 0;
	int dragsDropped_ = 0;
	int dragsRefused_ = 0;
	std::function<QString()> audioProbe_;
	std::function<QString()> videoProbe_;
	// The last Remote Play state StreamController reported, to restore it
	// after the services are rebuilt.
	QString lastRemotePlayState_;
	QString lastRemotePlayDetail_;

	void startDownload(const QString &remotePath, const QString &name, const QString &localPath,
		bool forDrag);
	static QString dragCacheDirectory();
	static QString cachePathFor(const QString &remotePath);
	static QString uniqueLocalPath(const QString &wanted);

	bool downloadActive_ = false;
	QString downloadName_;
	double downloadProgress_ = 0.0;
	std::atomic<bool> downloadCancel_ { false };

	void setUpdateState(const QString &state, const QString &message);
	QString updateState_ = QStringLiteral("idle");
	QString updateMessage_;
	QString updateVersion_;
	QString updateNotes_;
	QString updatePageUrl_;
	QString updateAssetUrl_;
	QString updateAssetName_;
	QString updateAssetSha256_;
	QString updateAssetSha256Url_;
	int64_t updateAssetSize_ = 0;
	double updateProgress_ = 0.0;
	std::atomic<bool> updateBusy_ { false };
};

} // namespace orbislink
