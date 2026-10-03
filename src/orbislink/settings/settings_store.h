// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <vector>
#include <string>

namespace orbislink {

// Transfer mode chosen in the DropOverlay (§5.7).
enum class TransferMode { DirectInstall, FtpUpload };

const char *transferModeName(TransferMode mode);
TransferMode transferModeFromName(const std::string &name, TransferMode fallback);

// All OrbisLink settings that do not belong to chiaki-ng.
// Stream settings (resolution, fps, bitrate) are still managed
// by chiaki-ng and are not duplicated here.
// A saved look: the base theme and the colours changed on top of it.
struct ThemePreset
{
	std::string name;
	std::string theme;                         // "dark", "glass" or "light"
	std::map<std::string, std::string> colors; // key → "#RRGGBB"
};

// A console saved in the list.
struct ConsoleEntry
{
	std::string name;
	std::string address;
	// "ps4", "ps5", or empty while the console has never answered.
	std::string type;
	// The Account ID this console accepted at registration, in base64. Each
	// console keeps its own: a PS4 whose account was activated by hand may
	// have the bytes in a different order from the PS5 of the same account.
	std::string accountId;
	// The host-id the console reported in discovery (its MAC). It is what
	// ties the entry to its Remote Play registration on this PC; empty until
	// the console has answered once.
	std::string hostId;
	// What a click on its card starts: "remoteplay" (Remote Play, with FTP
	// and the installer alongside) or "ftp" (only the file browser, as an
	// alternative to FileZilla). Empty means "remoteplay".
	std::string startMode;
};

// A PSN Account ID saved under a name of the user's choosing, so it can be
// picked for any console instead of being typed again.
struct SavedAccount
{
	std::string label;
	// In base64, the only form Remote Play accepts.
	std::string accountId;
};

struct Settings
{
	// Console: the one in use (FTP, the installer and Remote Play talk to
	// it) and the list of added consoles, which also contains it.
	std::string consoleName = "PS4";
	std::string consoleAddress;
	std::vector<ConsoleEntry> consoles;
	uint16_t ftpPort = 2121;
	// A jailbroken PS5 (etaHEN) has FTP on another port; it is stored
	// separately so people with both consoles do not keep swapping the port.
	uint16_t ftpPortPs5 = 1337;
	uint16_t installerPort = 12800;

	// Installation
	TransferMode defaultMode = TransferMode::DirectInstall;
	std::string ftpUploadDirectory = "/data/pkg/";
	// The folders pinned at the top of the Files tab, in order.
	std::vector<std::string> ftpPinnedFolders { "/data/", "/data/pkg/", "/data/GoldHEN/", "/user/app/",
		"/mnt/usb0/" };
	bool checkAlreadyInstalled = true;
	bool installAfterUpload = false;
	// After uploading via FTP and installing, delete the copy on the
	// console: only meaningful with installAfterUpload enabled.
	bool deleteFromConsoleAfterInstall = false;

	// Servidor HTTP local
	uint16_t httpPort = 8765;
	std::string httpBindAddress;       // empty = choose by the console's subnet
	bool restrictToConsoleIp = true;
	bool firewallNoticeShown = false;  // aviso da firewall do Windows (§5.3)

	// FTP
	int ftpMaxConnections = 1;
	bool ftpAdvancedMode = false;

	// Remote Play
	int streamResolution = 720;      // 360, 540, 720 or 1080
	int streamFps = 60;              // 30 or 60
	int streamBitrateKbps = 0;       // 0 = whatever chiaki's preset sets
	bool streamHardwareDecode = true;
	bool streamFullscreenOnConnect = false;
	// How the picture fills the window: "fit" (as sent), "4:3" or "fill".
	std::string streamAspect = "fit";
	bool streamRumble = true;
	bool streamTouchpadFromMouse = true;
	std::string streamAccountId;     // the last accepted Account ID, in base64 (for new consoles)
	// The saved Account IDs. Each console's accountId is one of these.
	std::vector<SavedAccount> accounts;
	// Keyboard as controller: action → key (Qt::Key). Only what was changed;
	// the rest keep their default key.
	std::map<std::string, int> keyboardBindings;
	// Physical controller button (SDL name) → action, only what was changed.
	std::map<std::string, std::string> padBindings;

	// Application
	std::string theme = "dark";      // "dark", "glass" or "light"
	// Colours changed on top of the theme (accent, hen, ok, warn, error,
	// background, panel, text → "#RRGGBB"), and the saved looks.
	std::map<std::string, std::string> themeColors;
	std::vector<ThemePreset> themePresets;
	std::string language = "en";    // "en" or "pt_PT"
	bool debugLogging = false;
	// "Updates over the internet". On by default: with a public repository
	// and a build per push, an app that never asks falls behind without
	// anyone noticing. It can still be turned off (§9) — when off, the app
	// never goes online for updates — and it never installs without
	// asking.
	bool checkForUpdates = true;
	// Where to look for releases, as "owner/name". It is a setting and not
	// a constant in the code: a copy of the project in another repository
	// points the app at itself without recompiling.
	// See ORBISLINK_REPOSITORY in CMakeLists.txt: CI fills it in.
	std::string updateRepository = ORBISLINK_REPOSITORY_STRING;
	// "stable" only sees final releases; "testing" also sees the
	// prereleases that CI publishes for every build.
	std::string updateChannel = "stable";
	// The first-run wizard only appears once; after that it is opened
	// from the settings.
	bool firstRunDone = false;

	// PS1/PS2 games: where the discs are and where converted packages go.
	std::string gamesFolder;
	std::string convertOutputFolder;

	std::string toJson() const;
	static Settings fromJson(const std::string &text, bool *ok = nullptr);
};

// The update repository that results from reading the settings.
//
// The stored repository is the default of the build that stored it. If the
// project moves to another repository, that would leave the new app looking
// for versions in the old one. So the default at the time is stored too
// ("storedDefault"): if the stored value equals it, nobody chose it and the
// one from this build applies; if it differs, it was written by hand and
// stays. Files without storedDefault (nullptr) cannot tell the difference,
// and this build's value applies.
// Makes sure the console in use is in the list (at the front, if it is not)
// and that the list has no empty or repeated addresses. This is what carries
// over settings from before the list existed, which only had one console.
void normaliseConsoles(Settings &settings);

// Makes sure every Account ID a console uses, and the last accepted one,
// is in the saved list, with no empty or repeated entries. Settings from
// before the list existed get one entry per ID, named after its console.
void normaliseAccounts(Settings &settings);

std::string resolveUpdateRepository(const std::string &stored, const std::string *storedDefault,
	const std::string &compiledDefault);

// JSON persistence in the application's data folder.
class SettingsStore
{
public:
	explicit SettingsStore(std::string path);

	const std::string &path() const { return path_; }
	bool load(Settings *settings) const;
	bool save(const Settings &settings) const;

	// %APPDATA%/OrbisLink no Windows, ~/.config/orbislink no resto.
	static std::string defaultDirectory();
	static std::string defaultSettingsPath();
	static std::string defaultQueuePath();
	static std::string defaultLogPath();
	static bool ensureDirectory(const std::string &path);

private:
	std::string path_;
};

} // namespace orbislink
