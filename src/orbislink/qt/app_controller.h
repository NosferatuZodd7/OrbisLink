// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "orbislink/console/console_manager.h"
#include "orbislink/ftp/ftp_client.h"
#include "orbislink/http/local_http_server.h"
#include "orbislink/installer/rpi_client.h"
#include "orbislink/net/console_locator.h"
#include "orbislink/qt/ftp_model.h"
#include "orbislink/qt/lifeline.h"
#include "orbislink/qt/queue_model.h"
#include "orbislink/queue/install_queue.h"
#include "orbislink/settings/settings_store.h"
#include "orbislink/update/update_checker.h"

#include <QHash>
#include <QObject>
#include <QSet>
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
	// A PS5: a package already on it installs by its path there.
	Q_PROPERTY(bool installsFromConsole READ installsFromConsole NOTIFY settingsChanged)
	// How opaque a page shown over Remote Play is (overlayMinOpacity to 1).
	Q_PROPERTY(double overlayOpacity READ overlayOpacity NOTIFY settingsChanged)
	Q_PROPERTY(double overlayMinOpacity READ overlayMinOpacity CONSTANT)
	// The console's firmware as Remote Play discovery reports it: "9.00",
	// and as a number to compare with a package's (0x0900; 0 unknown).
	Q_PROPERTY(QString consoleFirmware READ consoleFirmware NOTIFY consoleFirmwareChanged)
	Q_PROPERTY(bool canUseFtp READ canUseFtp NOTIFY statusChanged)
	Q_PROPERTY(bool queuePaused READ queuePaused NOTIFY queueStateChanged)
	Q_PROPERTY(QString pauseReason READ pauseReason NOTIFY queueStateChanged)
	// The queue waits for Remote Package Installer to be opened on the console.
	Q_PROPERTY(bool waitingForInstaller READ waitingForInstaller NOTIFY queueStateChanged)
	Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
	Q_PROPERTY(QString httpServerAddress READ httpServerAddress NOTIFY statusChanged)
	Q_PROPERTY(QString ftpPath READ ftpPath NOTIFY ftpPathChanged)
	Q_PROPERTY(bool ftpBusy READ ftpBusy NOTIFY ftpBusyChanged)
	// Why the folder on screen could not be listed (empty when it was): an
	// empty folder and one that could not be opened are not the same.
	Q_PROPERTY(QString ftpListError READ ftpListError NOTIFY ftpListErrorChanged)
	// What is being deleted on the console from the file list: their paths,
	// each row showing its own progress.
	Q_PROPERTY(QStringList ftpDeleting READ ftpDeleting NOTIFY ftpDeletingChanged)
	// The folder on screen: { folders, files, bytes (its files' sizes), free
	// (the room left on its disk, -1 when the server does not say) }.
	Q_PROPERTY(QVariantMap ftpSpace READ ftpSpace NOTIFY ftpSpaceChanged)
	// The FTP uploads not finished yet, for the file list to show them where
	// they are going: [{name, directory, percent, sending}].
	Q_PROPERTY(QVariantList ftpUploads READ ftpUploads NOTIFY ftpUploadsChanged)
	// For the consoles not in use: address → whether their FTP port answered
	// the last time it was tried (see probeFtp). The console in use has
	// ftpState instead.
	Q_PROPERTY(QVariantMap ftpReachable READ ftpReachable NOTIFY ftpReachableChanged)
	// Which homebrew enabler each console with FTP runs: "GoldHEN",
	// "etaHEN" or "HEN" (some other), by address.
	Q_PROPERTY(QVariantMap jailbreaks READ jailbreaks NOTIFY ftpReachableChanged)
	// The version of that enabler, by address, when its FTP greeting gives
	// one ("2.4b18"); none otherwise.
	Q_PROPERTY(QVariantMap henVersions READ henVersions NOTIFY ftpReachableChanged)
	// Where each local file stands in the queue, by its path (normalised
	// with transferKey): { stage: "sending" | "installing" | "sent" |
	// "installed" | "error" | "cancelled", percent, message }.
	Q_PROPERTY(QVariantMap transfers READ transfers NOTIFY transfersChanged)
	Q_PROPERTY(bool downloadActive READ downloadActive NOTIFY downloadChanged)
	Q_PROPERTY(QString downloadName READ downloadName NOTIFY downloadChanged)
	Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadChanged)
	// The transfer is a move between two disks of the console, through this PC.
	Q_PROPERTY(bool downloadMoving READ downloadMoving NOTIFY downloadChanged)
	// The transfer under way is a copy on the console (not a move).
	Q_PROPERTY(bool downloadCopying READ downloadCopying NOTIFY downloadChanged)
	// Settings → Advanced mode for FTP (protected areas, server commands).
	Q_PROPERTY(bool ftpAdvancedMode READ ftpAdvancedMode NOTIFY settingsChanged)
	Q_PROPERTY(QStringList ftpShortcuts READ ftpShortcuts NOTIFY ftpShortcutsChanged)
	// Remote Play picture: "fit" (as the console sends it), "4:3" or "fill".
	Q_PROPERTY(QString streamAspect READ streamAspect WRITE setStreamAspect NOTIFY streamAspectChanged)
	// Two Remote Play sessions at once: "side", "stacked" or "pip". Kept.
	Q_PROPERTY(QString streamLayout READ streamLayout WRITE setStreamLayout NOTIFY streamLayoutChanged)
	// Personalisation: the colours changed on top of the theme, and the
	// saved looks ({ name, theme, colors }).
	Q_PROPERTY(QVariantMap themeColors READ themeColors NOTIFY themeColorsChanged)
	Q_PROPERTY(QVariantList themePresets READ themePresets NOTIFY themePresetsChanged)
	// A custom look is in use (colours on top of the theme), and which
	// saved look it is, if it is one.
	Q_PROPERTY(bool customLook READ customLook NOTIFY themeColorsChanged)
	Q_PROPERTY(QString activePreset READ activePreset NOTIFY themeColorsChanged)
	// A picture of one's own behind the glass: the blurred version the
	// window shows (a file URL; empty: the room the app comes with), how out
	// of focus it is, whether the colours come from it, what it is made of
	// ({ average, primary, secondary }), and the light moving over it.
	Q_PROPERTY(QString backgroundUrl READ backgroundUrl NOTIFY backgroundChanged)
	Q_PROPERTY(int backgroundBlur READ backgroundBlur NOTIFY backgroundChanged)
	Q_PROPERTY(bool backgroundPalette READ backgroundPalette NOTIFY backgroundChanged)
	Q_PROPERTY(QVariantMap backgroundTones READ backgroundTones NOTIFY backgroundChanged)
	Q_PROPERTY(bool backgroundMotion READ backgroundMotion NOTIFY backgroundChanged)
	Q_PROPERTY(orbislink::QueueModel *queue READ queue CONSTANT)
	Q_PROPERTY(orbislink::FtpModel *files READ files CONSTANT)
	Q_PROPERTY(QString version READ version CONSTANT)
	// True when the application was built with Remote Play.

	// Updates: "idle", "checking", "available", "downloading",
	// "ready", "up-to-date" or "error".
	Q_PROPERTY(QString updateState READ updateState NOTIFY updateChanged)
	Q_PROPERTY(QString updateMessage READ updateMessage NOTIFY updateChanged)
	Q_PROPERTY(QString updateVersion READ updateVersion NOTIFY updateChanged)
	Q_PROPERTY(QString updateNotes READ updateNotes NOTIFY updateChanged)
	Q_PROPERTY(QString updatePageUrl READ updatePageUrl NOTIFY updateChanged)
	Q_PROPERTY(bool updateCanInstall READ updateCanInstall NOTIFY updateChanged)
	Q_PROPERTY(double updateProgress READ updateProgress NOTIFY updateChanged)
	// On the testing channel: a stable version newer than the one in use
	// (empty when there is none), offered apart from the test builds.
	Q_PROPERTY(QString stableUpdateVersion READ stableUpdateVersion NOTIFY updateChanged)

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
	// What to do when the console's installer or FTP is not running.
	QString installerHelpKind() const;
	QString installerHelpTitle() const;
	QString installerHelpText() const;
	QString ftpHelpTitle() const;
	QString ftpHelpText() const;
	QString installerHint() const;
	bool canInstallDirectly() const;
	bool installsFromConsole() const { return activeIsPs5(); }
	double overlayOpacity() const { return settings_.overlayOpacity; }
	// Below this a page over the picture can no longer be read.
	static double overlayMinOpacity() { return Settings::kOverlayMinOpacity; }
	// Kept once the slider is let go (not at each step of a drag).
	Q_INVOKABLE void setOverlayOpacity(double opacity);
	QString consoleFirmware() const;
	bool canUseFtp() const;
	// The console in use is a PS5 (by the type that was stored).
	bool activeIsPs5() const;
	// The Files tab's pinned folders of the console in use: a PS4's or a PS5's.
	std::vector<std::string> &pinnedFolders();
	const std::vector<std::string> &pinnedFolders() const;
	// The FTP port of the console in use (the PS4 one or the PS5 one).
	uint16_t activeFtpPort() const;
	// What the ports asked for FTP said the last time it was looked for
	// beyond the set port, one line each, for the diagnostics.
	QString ftpSearchReport() const;
	// The Account ID of the console in use, or the last accepted one if it
	// does not have one yet.
	std::string activeAccountId() const;
	bool queuePaused() const;
	QString pauseReason() const;
	bool waitingForInstaller() const;
	QString statusMessage() const { return statusMessage_; }
	QString httpServerAddress() const;
	QString ftpPath() const { return ftpPath_; }
	bool ftpBusy() const { return ftpBusy_; }
	QString ftpListError() const { return ftpListError_; }
	QStringList ftpDeleting() const { return ftpDeleting_; }
	QVariantMap ftpSpace() const { return ftpSpace_; }
	QVariantList ftpUploads() const { return ftpUploads_; }
	QVariantMap ftpReachable() const { return ftpReachable_; }
	QVariantMap jailbreaks() const { return jailbreaks_; }
	QVariantMap henVersions() const { return henVersions_; }
	QVariantMap transfers() const { return transfers_; }
	static QString transferKey(const QString &localPath);
	bool downloadActive() const { return downloadActive_; }
	QString downloadName() const { return downloadName_; }
	double downloadProgress() const { return downloadProgress_; }
	bool downloadMoving() const { return downloadMoving_; }
	bool downloadCopying() const { return downloadCopying_; }
	QStringList ftpShortcuts() const;
	QString streamAspect() const { return QString::fromStdString(settings_.streamAspect); }
	QString streamLayout() const { return QString::fromStdString(settings_.streamLayout); }
	void setStreamLayout(const QString &layout);
	void setStreamAspect(const QString &aspect);
	// Remote Play's picture as chosen during a session (the stream bar): kept
	// in the settings for the next one. The stream controller asks the
	// console itself (StreamController::changeQuality).
	Q_INVOKABLE void setStreamQuality(int resolution, int fps);
	QVariantMap themeColors() const;
	QVariantList themePresets() const;
	// An empty colour puts the theme's own back.
	Q_INVOKABLE void setThemeColor(const QString &key, const QString &color);
	Q_INVOKABLE void resetThemeColors();
	// Saves the theme and colours in use under `name` (replacing one with
	// the same name); applying one brings both back.
	Q_INVOKABLE void saveThemePreset(const QString &name);
	Q_INVOKABLE void applyThemePreset(const QString &name);
	Q_INVOKABLE void deleteThemePreset(const QString &name);
	bool customLook() const { return !settings_.themeColors.empty(); }
	QString backgroundUrl() const;
	int backgroundBlur() const { return settings_.backgroundBlur; }
	bool backgroundPalette() const { return settings_.backgroundPalette; }
	QVariantMap backgroundTones() const;
	bool backgroundMotion() const { return settings_.backgroundMotion; }
	// The picture chosen (a file URL or path) goes behind the glass. False,
	// with a notice, when it cannot be read.
	Q_INVOKABLE bool chooseBackground(const QString &file);
	// Back to the room the app comes with.
	Q_INVOKABLE void clearBackground();
	Q_INVOKABLE void setBackgroundBlur(int strength);
	Q_INVOKABLE void setBackgroundPalette(bool fromPicture);
	Q_INVOKABLE void setBackgroundMotion(bool moving);
	QString activePreset() const { return QString::fromStdString(settings_.activePreset); }
	// The "custom" button: the chosen saved look, or the colours set aside.
	// False when there is none yet (the Personalisation page should open).
	Q_INVOKABLE bool applyCustomLook();
	QueueModel *queue() { return &queueModel_; }
	FtpModel *files() { return &ftpModel_; }
	QString version() const;
	// The settings in effect, for whoever needs all of them (the
	// Remote Play controller).
	const Settings &settings() const { return settings_; }
	// How to reach the console's FTP right now, for whoever needs a
	// connection of its own (the save vault).
	FtpClient::Config ftpClientConfig() const;
	// The same for any saved console (its address): the one in use as
	// above, another on its own address and its kind's FTP port.
	FtpClient::Config ftpClientConfigFor(const std::string &address) const;
	// Whether that console's FTP answers: the services say for the one in
	// use, the last probe for the others.
	bool ftpAnswers(const std::string &address) const;
	// Changes settings that need nothing rebuilt (folders remembered by
	// other parts of the app) and saves them.
	void updateSettings(const std::function<void(Settings &)> &change);
	// Stores the PSN Account ID, so it does not have to be typed every time.
	// The account a console accepted (or was given on its card): kept with
	// that console (the one at `address`; the one in use when empty) and as
	// the last used.
	Q_INVOKABLE void rememberAccountId(const QString &address, const QString &accountId);

	// Dropping files: mode 0 = direct install, 1 = FTP upload — into the
	// folder open in the Files tab, which is what the drop zone says.
	Q_INVOKABLE void dropUrls(const QList<QUrl> &urls, int mode);
	// `directory`: where an FTP upload goes (empty: the upload folder).
	Q_INVOKABLE void addPaths(const QStringList &paths, int mode, const QString &directory = QString());
	// Installs a .pkg that is already on the console (a PS5), by its path.
	Q_INVOKABLE void installFromConsole(const QString &remotePath);
	// The answer to uploadConflicts: one {index, action, name} per file that
	// was already on the console; action is "overwrite", "rename" (to name)
	// or "skip". The other files of the drop go as they were.
	Q_INVOKABLE void resolveUploadConflicts(const QVariantList &decisions);
	Q_INVOKABLE void checkServicesNow() { checkServices(true); }
	// The services checked now; `search` also looks for the console at once
	// when it is not where it was saved (a check asked for by hand), else the
	// search keeps its own pace (a queue waiting for the installer asks every
	// few seconds, which must not search the network every few seconds).
	void checkServices(bool search);
	// The Remote Play state comes from chiaki (StreamController), not from
	// the periodic check: this is how it reaches the status bar indicator.
	Q_INVOKABLE void reportRemotePlayState(const QString &state, const QString &detail);
	// The system version discovery reported for a console ("09000000");
	// empty when it did not answer. Only the console in use's counts.
	void reportConsoleFirmware(const QString &address, const QString &systemVersion);
	// The consoles that took a direct Remote Play connection without
	// answering the search, and noting one more.
	QStringList directConsoles() const;
	void rememberDirectConsole(const QString &address);
	// What to know before a package goes to the console in use: an
	// official package, or one that needs a newer firmware than this PS4's
	// (`category` as the package says it: "gd", "gp"…). Empty when there is
	// nothing to say.
	Q_INVOKABLE QString packageWarning(bool official, int requiredFirmware, const QString &category,
		bool ps5Package = false) const;
	// What starts a PS5 fake package on the console in use, by its firmware:
	// kstuff up to 11.60, the Titan Core payload on 12.00–13.60, nothing
	// public from 14.00.
	Q_INVOKABLE QString ps5StartHint() const;
	// The last time Titan Core was asked about an install: whether it
	// answered, its version and what it said (for the diagnostics).
	QString titanCoreReport() const { return titanCoreReport_; }
	// A Remote Play session on screen (StreamController says so): work that
	// would load the console's network (an automatic save backup, a console
	// switch) waits for it to end.
	bool streaming() const { return streaming_; }
	void setStreaming(bool on) { streaming_ = on; }
	// The consoles Remote Play sessions are on (none, one or two): they can
	// be others than the one in use. Their networks are the busy ones.
	QStringList streamingAddresses() const { return streamingAddresses_; }
	void setStreamingAddresses(const QStringList &addresses) { streamingAddresses_ = addresses; }
	// A session streams from this console (not just any).
	bool streamingOn(const QString &address) const
	{
		return streaming_ && !address.isEmpty() && streamingAddresses_.contains(address);
	}
	// Something in the install queue under way or waiting.
	bool queueBusy() const;
	// Work elsewhere (a conversion or a packing) whose result is sent to the
	// console in use once it is made: a console switch waits for it too.
	void setSendsPending(std::function<bool()> check) { sendsPending_ = std::move(check); }
	bool sendsPending() const { return sendsPending_ && sendsPending_(); }
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
	// Every finished task (installed, sent, failed, cancelled) off the panel.
	Q_INVOKABLE void clearFinishedTasks();
	// Folders pinned at the top of the Files tab (ftpShortcuts).
	Q_INVOKABLE bool isFtpPinned(const QString &path) const;
	Q_INVOKABLE void pinFtpFolder(const QString &path);
	Q_INVOKABLE void unpinFtpFolder(const QString &path);
	// Drops the finished tasks (sent, installed, failed, cancelled) of one
	// file: a conversion card that goes away takes its queue tasks along.
	void removeFinishedTasksOf(const QString &localPath);
	// A converted game: over FTP into `directory` and, with `install`,
	// installed once there and the copy on the console deleted after it.
	void sendTo(const QString &pkg, const QString &directory, bool install);
	Q_INVOKABLE void moveTaskUp(const QString &id);
	Q_INVOKABLE void moveTaskDown(const QString &id);
	Q_INVOKABLE void pauseQueue();
	Q_INVOKABLE void resumeQueue();

	Q_INVOKABLE void ftpNavigate(const QString &path);
	Q_INVOKABLE void ftpRefresh();
	Q_INVOKABLE void ftpUp();
	// Files, or folders with everything in them, one after the other:
	// [{ path, isDirectory }]; each row shows it going.
	Q_INVOKABLE void ftpDeleteMany(const QVariantList &items);
	Q_INVOKABLE void ftpMakeDirectory(const QString &name);
	Q_INVOKABLE void ftpRename(const QString &path, const QString &newName);
	// The folders inside `path` on the console, for the folder picker,
	// without touching the file list; the answer comes in ftpFoldersListed.
	Q_INVOKABLE void ftpListFolders(const QString &path);
	// Creates a folder (full path) on the console; the answer comes in
	// ftpFolderCreated.
	Q_INVOKABLE void ftpCreateFolder(const QString &path);
	// Starts dragging a row of the FTP list (localUrl: its copy on the PC,
	// if there is one).
	Q_INVOKABLE void startFtpDrag(const QString &remotePath, const QString &localUrl);
	// Moves a file or folder to another folder of the console.
	Q_INVOKABLE void ftpMove(const QString &path, const QString &destinationDir);
	// Several, one after the other.
	Q_INVOKABLE void ftpMoveMany(const QStringList &paths, const QString &destinationDir);
	// The same, copied: the original stays. Through this PC (the consoles'
	// servers have no copy command); into the same folder it gets a new
	// name ("name copy").
	Q_INVOKABLE void ftpCopyMany(const QStringList &paths, const QString &destinationDir);
	// SITE CHMOD on each of [{ path, isDirectory }] — `mode` octal ("755",
	// "777") — and, when asked, on everything inside the folders.
	Q_INVOKABLE void ftpChmod(const QVariantList &items, const QString &mode, bool everythingInside);
	// A small text file of the console (up to 1 MB) to read and change:
	// ftpTextLoaded brings it, ftpTextSaved says it went back.
	Q_INVOKABLE void ftpLoadText(const QString &path);
	Q_INVOKABLE void ftpSaveText(const QString &path, const QString &text);
	// A command of the console's FTP server itself (Advanced mode only):
	// "MTRW" (ftpsrv: /system writable) or "KILL" (ftpsrv: stops itself).
	Q_INVOKABLE void ftpServerCommand(const QString &command);
	bool ftpAdvancedMode() const { return settings_.ftpAdvancedMode; }
	// A file of the console, to the local cache that feeds dragging out.
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
	// FTP is looked for on the other usual ports too (9090 only with `ps5`).
	Q_INVOKABLE void probeConsole(const QString &address, int ftpPort, int installerPort, bool ps5 = false);

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
	// Leaves the test builds for the stable channel (saved) and offers the
	// newer stable version found by the last check, as any update is.
	Q_INVOKABLE void switchToStableUpdate();
	QString stableUpdateVersion() const { return stableUpdateVersion_; }
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
	// What stops another console being made the one in use now ("" when
	// nothing): transfers or a conversion under way would be cut, or what
	// they make sent to the other one. A Remote Play session is not a
	// reason: it keeps its own console.
	Q_INVOKABLE QString consoleSwitchBlock() const;
	// Adds a console to the list and, with `select`, switches to it. If the
	// address is already there, it is just selected.
	// `type` is "ps4", "ps5" or empty (unknown).
	// `startMode` is "remoteplay" or "ftp" (see ConsoleEntry::startMode).
	Q_INVOKABLE void addConsole(const QString &name, const QString &address,
		const QString &type = QString(), bool select = true,
		const QString &startMode = QString());
	// Chooses what a click on the console's card starts: "remoteplay" or "ftp".
	Q_INVOKABLE void setConsoleStartMode(const QString &address, const QString &startMode);
	// The console's own Remote Play PIN: 8 digits (the pairing PIN it shows,
	// filled in when registering), 4 (the passcode it asks for when
	// connecting), or empty for none (then the account's is used). Returns
	// false, with the reason in the status bar, when it is neither.
	Q_INVOKABLE bool setConsolePin(const QString &address, const QString &pin);
	// Opens the file browser on the console in use: the panel shows the
	// files, in the upload folder, freshly listed.
	Q_INVOKABLE void openFiles();
	// Tries the FTP port of these consoles (each on its own port: PS4 or
	// PS5), in the background; the answers land in ftpReachable.
	Q_INVOKABLE void probeFtp(const QStringList &addresses);
	// Remembers the type of a console that answered, to show it even when
	// it is off. Only saves if it changed.
	Q_INVOKABLE void rememberConsoleType(const QString &address, bool ps5,
		const QString &hostId = QString());
	// A saved console that stopped answering at its address may only have
	// been given another one by the router. It is looked for on the network
	// by who it is (net/console_locator.h); found elsewhere, its address is
	// changed and a notice says so. `urgent` skips the wait between searches;
	// `done` gets each address asked about and where that console is now
	// (empty: not found anywhere else).
	using LocateDone = std::function<void(const QString &oldAddress, const QString &newAddress)>;
	void findMovedConsoles(const QStringList &addresses, bool urgent, LocateDone done = {});
	// Remote Play's search of the network (when Remote Play is built in): the
	// consoles that answered it, with their host-ids.
	void setNetworkSearch(std::function<std::vector<Sighting>()> search) { networkSearch_ = std::move(search); }
	// No looking around the network while a session streams.
	void setStreamBusy(bool busy) { streamBusy_ = busy; }
	// Changes the name and IP of a saved console (the one in use or any
	// other). Returns false, with the reason in the status bar, when the
	// new IP is empty or already belongs to another console.
	Q_INVOKABLE bool updateConsole(const QString &oldAddress, const QString &name,
		const QString &address);
	// Saves an Account ID (base64) under a name, with its own Remote Play PIN
	// (digits, or empty for none). With `oldAccountId` it edits that entry,
	// and the consoles using it follow. Returns false, with the reason in
	// the status bar, when the ID is empty or already saved, or the PIN is
	// not 4 or 8 digits.
	Q_INVOKABLE bool saveAccount(const QString &oldAccountId, const QString &label,
		const QString &accountId, const QString &remotePlayPin = QString());
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
	void savePadBindings(const std::map<std::string, std::string> &bindings);
	Q_INVOKABLE void saveUpdateSettings(bool checkForUpdates, const QString &repository,
		const QString &channel);

signals:
	void ftpShortcutsChanged();
	void streamAspectChanged();
	void streamLayoutChanged();
	void themeColorsChanged();
	void themePresetsChanged();
	void backgroundChanged();
	void settingsChanged();
	// A new log line, already masked. The diagnostics window connects to
	// this to show what happens in real time.
	void logLine(const QString &level, const QString &text);
	void statusChanged();
	void consoleFirmwareChanged();
	void queueStateChanged();
	void statusMessageChanged();
	void ftpPathChanged();
	void ftpFoldersListed(const QString &path, const QStringList &folders, const QString &error);
	void ftpTextLoaded(const QString &path, const QString &text, const QString &error);
	void ftpTextSaved(const QString &path, const QString &error);
	void ftpFolderCreated(const QString &path, const QString &error);
	void ftpBusyChanged();
	void ftpListErrorChanged();
	void ftpDeletingChanged();
	void ftpSpaceChanged();
	void ftpUploadsChanged();
	void ftpReachableChanged();
	void transfersChanged();
	// Some of the files dropped for FTP already exist in the upload folder:
	// [{index, name, localSize, remoteSize, suggestion}]. The window asks
	// what to do and answers with resolveUploadConflicts.
	void uploadConflicts(const QVariantList &conflicts);
	// Files were queued: the panel shows where they can be followed —
	// "queue" for installs, "files" for FTP uploads.
	void showPanel(const QString &which);
	void notify(const QString &title, const QString &message, bool error);
	// A long job came to its end (an install or upload, a conversion, a copy
	// to this PC, a store install, a download): with the window in the
	// background, a system notification says so.
	void jobFinished(const QString &title, const QString &message, bool error);
	// A feature needs the installer ("installer" on a PS5, "rpi" on a PS4)
	// or FTP ("ftp") and it is not running: what to do, for a window to show.
	void serviceHelp(const QString &kind, const QString &title, const QString &text);
	// `ftpPort` is where FTP answered (0 when it did not): another port
	// than the one asked for when FTP turned up there.
	void consoleProbed(const QString &address, bool ftpOk, bool installerOk,
		const QString &detail, int ftpPort);
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
	// Makes (once) the blurred version of the chosen picture for the blur
	// in use, and forgets the others.
	void prepareBackdrop();
	QString backdropPath_;
	// Says why a click did nothing, instead of swallowing it silently.
	// Returns false when the operation cannot go ahead.
	bool ftpReady(const QString &operation);
	void resumeWhenInstallerIsBack();
	// FTP answered on another port than the console's: that port is kept
	// in the settings and used from now on.
	void adoptFtpPort(uint16_t port);
	void refreshQueueModel();
	void setStatusMessage(const QString &message);
	void setFtpBusy(bool busy);
	static QStringList collectPkgFiles(const QStringList &paths);
	// Reads each queued package's icon, and what to know about it (its
	// firmware, an official package), on a worker thread.
	void readPackageFacts(const QStringList &paths, const QStringList &taskIds);
	// Folders dropped to go over FTP: every file in them, with their tree,
	// under `baseDir` (the folder open in the file list, or the upload folder).
	void enqueueFolderUploads(const QStringList &folders, const QString &baseDir);
	void enqueueFiles(const QStringList &files, TransferMode mode,
		const std::vector<std::string> &remoteNames = {}, const QString &directory = QString());
	// Lists the folder the files go to and, for those already there, asks first.
	void checkUploadConflicts(const QStringList &files, const QString &directory);
	// Which enabler the console runs, and its version when `greeting` (what
	// its FTP said on connecting) gives one.
	static std::string detectJailbreak(const std::string &address, uint16_t port,
		const std::string &greeting, std::string *version);
	void refreshFtpListing(bool announce);
	QString uploadDirectory() const;

	Settings settings_;
	SettingsStore store_;
	ConsoleStatus status_;

	// Shared with the threads that use them: a thread still talking to the
	// console keeps its client alive when the services are rebuilt.
	std::shared_ptr<ConsoleManager> console_;
	std::unique_ptr<LocalHttpServer> httpServer_;
	std::unique_ptr<RpiClient> installer_;
	std::shared_ptr<FtpClient> ftp_;
	std::unique_ptr<InstallQueue> queue_;

	QueueModel queueModel_;
	FtpModel ftpModel_;
	QString statusMessage_;
	QString ftpPath_ = QStringLiteral("/data/pkg/");
	bool ftpBusy_ = false;
	QString ftpListError_;
	void setFtpListError(const QString &error)
	{
		if(ftpListError_ == error)
			return;
		ftpListError_ = error;
		emit ftpListErrorChanged();
	}
	QStringList ftpDeleting_;
	// Asked to list while a listing was under way.
	bool ftpRelist_ = false;
	QVariantMap ftpSpace_;
	// The room left on each disk of the console (its first folders, "/data",
	// "/mnt/usb0"), and when it was asked: not asked at every listing.
	QHash<QString, QPair<qint64, qint64>> ftpFree_;
	void updateFtpSpace(const std::vector<FtpEntry> &entries);
	bool installerRecheckPending_ = false;
	QVariantList ftpUploads_;
	// The uploads whose start the file list was read again for.
	QSet<QString> uploadsListed_;
	QVariantMap ftpReachable_;
	QVariantMap jailbreaks_;
	QVariantMap henVersions_;
	QVariantMap transfers_;
	// The console whose files the list shows: when the console in use
	// changes, the previous one's listing must not stay on screen.
	std::string listedConsole_;
	bool probingFtp_ = false;
	// Following consoles that change address (findMovedConsoles).
	void checkConsoleAddresses(const QStringList &only, bool urgent, LocateDone done);
	void relocateConsole(const std::string &from, const std::string &to);
	void learnConsoleMac(const std::string &address, const std::string &mac);
	struct LocateWait
	{
		int64_t nextMs = 0;
		int64_t delayMs = 0;
	};
	// Per saved address: when a console that does not answer is looked for
	// again. The wait grows (2, 5, 12 … up to 30 minutes), so a console that
	// is simply off does not have the network searched every minute.
	std::map<std::string, LocateWait> locateWaits_;
	bool locating_ = false;
	bool streamBusy_ = false;
	QStringList streamingAddresses_;
	std::vector<std::pair<QStringList, LocateDone>> locateWaiters_;
	std::function<std::vector<Sighting>()> networkSearch_;
	// The FTP drop waiting for an answer about the names already taken.
	QStringList pendingUploads_;
	QString pendingUploadDirectory_;
	// The queue was waiting for the installer at its last change.
	bool wasWaiting_ = false;
	// Only the most recent check matters: earlier ones are discarded when
	// they arrive, so the result never contradicts what is on screen.
	std::atomic<uint64_t> probeGeneration_ { 0 };
	// Which ConsoleManager is the current one: what an earlier one says (a
	// check still under way when the console was switched) speaks of
	// another console and is not taken.
	uint64_t consoleGeneration_ = 0;
	bool streamVerbose_ = false;
	int dragsSeen_ = 0;
	int dragsDropped_ = 0;
	int dragsRefused_ = 0;
	std::function<QString()> audioProbe_;
	std::function<QString()> videoProbe_;
	// The last Remote Play state StreamController reported, to restore it
	// after the services are rebuilt.
	QString lastRemotePlayState_;
	uint16_t consoleFirmware_ = 0;
	// What each queued package said about itself, to say again what to know
	// about it once the console's firmware is known (or another console).
	struct PackageFacts
	{
		bool official = false;
		int requiredFirmware = 0;
		QString category;
		QString title;
		QString warning; // the last one said
		bool ps5 = false; // a PS5 package
	};
	// A PS5 package installed on the console in use: Titan Core, when it
	// runs there (port 8097), is asked to look for it and its progress
	// preparing it to start is shown on the job's card; without it, what
	// starts it on this firmware.
	void followPs5Install(const QString &taskId, const QString &titleId);
	// A PS5 game image copied where ShadowMountPlus looks: it is asked to
	// look now (its API, else started again); its card says what happened.
	void handImageToShadowMount(const QString &taskId, const QString &remotePath);
	// A line under a job's card (and its conversion card's), on the UI thread.
	void setTaskNote(const QString &taskId, const QString &text);
	QSet<QString> ps5Followed_;
	QString titanCoreReport_;
	bool streaming_ = false;
	std::function<bool()> sendsPending_;
	QHash<QString, PackageFacts> packageFacts_;
	void refreshPackageWarnings();
	QString lastRemotePlayDetail_;

	void startDownload(const QString &remotePath, const QString &name, const QString &localPath,
		bool forDrag);
	static QString dragCacheDirectory();
	static QString cachePathFor(const QString &remotePath);
	static QString uniqueLocalPath(const QString &wanted);

	bool downloadActive_ = false;
	QString downloadName_;
	double downloadProgress_ = 0.0;
	bool downloadMoving_ = false;
	bool downloadCopying_ = false;
	// Move or copy on the console (ftpMoveMany / ftpCopyMany).
	void transferOnConsole(const QStringList &paths, const QString &destinationDir, bool copy);
	// Each download's own flag, shared with its thread.
	std::shared_ptr<std::atomic<bool>> downloadCancel_ = std::make_shared<std::atomic<bool>>(false);

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
	QString stableUpdateVersion_;
	ReleaseInfo stableRelease_;
	// For the threads of its own that answer after it may be gone.
	LifelinePtr lifeline_ = std::make_shared<Lifeline>(this);
};

} // namespace orbislink
