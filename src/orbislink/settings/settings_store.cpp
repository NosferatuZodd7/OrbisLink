// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/settings/settings_store.h"

#include "orbislink/common/json.h"
#include "orbislink/common/log.h"
#include "orbislink/common/util.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#ifdef _WIN32
#	include <direct.h>
#endif

namespace orbislink {

namespace {

// "#RRGGBB", as the colour picker writes it.
bool isThemeColor(const std::string &value)
{
	if(value.size() != 7 || value[0] != '#')
		return false;
	for(size_t i = 1; i < value.size(); ++i)
		if(!std::isxdigit(static_cast<unsigned char>(value[i])))
			return false;
	return true;
}

} // namespace

const char *transferModeName(TransferMode mode)
{
	return mode == TransferMode::FtpUpload ? "ftp" : mode == TransferMode::ConsoleInstall ? "console" : "direct";
}

TransferMode transferModeFromName(const std::string &name, TransferMode fallback)
{
	if(name == "ftp")
		return TransferMode::FtpUpload;
	if(name == "direct")
		return TransferMode::DirectInstall;
	if(name == "console")
		return TransferMode::ConsoleInstall;
	return fallback;
}

std::string Settings::toJson() const
{
	Json root = Json::makeObject();
	root.set("console_name", Json::fromString(consoleName));
	root.set("console_address", Json::fromString(consoleAddress));
	Json items = Json::makeArray();
	for(const ConsoleEntry &console : consoles)
	{
		Json input = Json::makeObject();
		input.set("name", Json::fromString(console.name));
		input.set("address", Json::fromString(console.address));
		input.set("type", Json::fromString(console.type));
		if(!console.accountId.empty())
			input.set("account_id", Json::fromString(console.accountId));
		if(!console.hostId.empty())
			input.set("host_id", Json::fromString(console.hostId));
		if(console.startMode == "ftp")
			input.set("start_mode", Json::fromString(console.startMode));
		items.push(input);
	}
	root.set("consoles", items);
	root.set("ftp_port", Json::fromInt(ftpPort));
	root.set("ftp_port_ps5", Json::fromInt(ftpPortPs5));
	root.set("installer_port", Json::fromInt(installerPort));
	root.set("default_mode", Json::fromString(transferModeName(defaultMode)));
	root.set("ftp_upload_directory", Json::fromString(ftpUploadDirectory));
	Json pinned = Json::makeArray();
	for(const std::string &folder : ftpPinnedFolders)
		pinned.push(Json::fromString(folder));
	root.set("ftp_pinned_folders", pinned);
	Json pinnedPs5 = Json::makeArray();
	for(const std::string &folder : ftpPinnedFoldersPs5)
		pinnedPs5.push(Json::fromString(folder));
	root.set("ftp_pinned_folders_ps5", pinnedPs5);
	root.set("check_already_installed", Json::fromBool(checkAlreadyInstalled));
	root.set("install_after_upload", Json::fromBool(installAfterUpload));
	root.set("delete_from_console_after_install", Json::fromBool(deleteFromConsoleAfterInstall));
	root.set("http_port", Json::fromInt(httpPort));
	root.set("http_bind_address", Json::fromString(httpBindAddress));
	root.set("restrict_to_console_ip", Json::fromBool(restrictToConsoleIp));
	root.set("firewall_notice_shown", Json::fromBool(firewallNoticeShown));
	root.set("ftp_max_connections", Json::fromInt(ftpMaxConnections));
	root.set("ftp_advanced_mode", Json::fromBool(ftpAdvancedMode));
	root.set("stream_resolution", Json::fromInt(streamResolution));
	root.set("stream_fps", Json::fromInt(streamFps));
	root.set("stream_bitrate_kbps", Json::fromInt(streamBitrateKbps));
	root.set("stream_hardware_decode", Json::fromBool(streamHardwareDecode));
	root.set("stream_fullscreen_on_connect", Json::fromBool(streamFullscreenOnConnect));
	root.set("stream_aspect", Json::fromString(streamAspect));
	root.set("stream_rumble", Json::fromBool(streamRumble));
	root.set("stream_touchpad_from_mouse", Json::fromBool(streamTouchpadFromMouse));
	root.set("stream_account_id", Json::fromString(streamAccountId));
	Json savedAccounts = Json::makeArray();
	for(const SavedAccount &account : accounts)
	{
		Json input = Json::makeObject();
		input.set("label", Json::fromString(account.label));
		input.set("account_id", Json::fromString(account.accountId));
		input.set("remote_play_pin", Json::fromString(account.remotePlayPin));
		savedAccounts.push(std::move(input));
	}
	root.set("accounts", savedAccounts);
	Json keys = Json::makeObject();
	for(const auto &pair : keyboardBindings)
		keys.set(pair.first, Json::fromInt(pair.second));
	root.set("keyboard_bindings", keys);
	Json pad = Json::makeObject();
	for(const auto &pair : padBindings)
		pad.set(pair.first, Json::fromString(pair.second));
	root.set("pad_bindings", pad);
	root.set("theme", Json::fromString(theme));
	Json colors = Json::makeObject();
	for(const auto &pair : themeColors)
		colors.set(pair.first, Json::fromString(pair.second));
	root.set("theme_colors", colors);
	Json presets = Json::makeArray();
	for(const ThemePreset &preset : themePresets)
	{
		Json item = Json::makeObject();
		item.set("name", Json::fromString(preset.name));
		item.set("theme", Json::fromString(preset.theme));
		Json presetColors = Json::makeObject();
		for(const auto &pair : preset.colors)
			presetColors.set(pair.first, Json::fromString(pair.second));
		item.set("colors", presetColors);
		presets.push(std::move(item));
	}
	root.set("theme_presets", presets);
	root.set("theme_active_preset", Json::fromString(activePreset));
	Json lastColors = Json::makeObject();
	for(const auto &pair : lastCustomColors)
		lastColors.set(pair.first, Json::fromString(pair.second));
	root.set("theme_last_custom", lastColors);
	root.set("theme_last_custom_base", Json::fromString(lastCustomTheme));
	root.set("language", Json::fromString(language));
	root.set("debug_logging", Json::fromBool(debugLogging));
	root.set("check_for_updates", Json::fromBool(checkForUpdates));
	root.set("first_run_done", Json::fromBool(firstRunDone));
	root.set("games_folder", Json::fromString(gamesFolder));
	root.set("convert_output_folder", Json::fromString(convertOutputFolder));
	root.set("install_storage", Json::fromString(installStorage));
	root.set("save_vault_folder", Json::fromString(saveVaultFolder));
	Json places = Json::makeArray();
	for(const LibraryLocation &place : libraryLocations)
	{
		Json item = Json::makeObject();
		item.set("id", Json::fromString(place.id));
		item.set("name", Json::fromString(place.name));
		item.set("path", Json::fromString(place.path));
		item.set("where", Json::fromString(place.where));
		item.set("favorite", Json::fromBool(place.favorite));
		item.set("in_search", Json::fromBool(place.inSearch));
		places.push(std::move(item));
	}
	root.set("library_locations", places);
	root.set("library_locations_seeded", Json::fromBool(libraryLocationsSeeded));
	Json links = Json::makeObject();
	for(const auto &pair : saveAccountLinks)
		links.set(pair.first, Json::fromString(pair.second));
	root.set("save_account_links", links);
	root.set("update_repository", Json::fromString(updateRepository));
	root.set("update_repository_default", Json::fromString(ORBISLINK_REPOSITORY_STRING));
	root.set("update_channel", Json::fromString(updateChannel));
	return root.dump();
}

Settings Settings::fromJson(const std::string &text, bool *ok)
{
	Settings settings;
	std::string error;
	const Json root = Json::parse(text, &error);
	if(!root.isObject())
	{
		if(ok)
			*ok = false;
		return settings;
	}

	settings.consoleName = root["console_name"].toString(settings.consoleName);
	settings.consoleAddress = root["console_address"].toString(settings.consoleAddress);
	if(root["consoles"].isArray())
	{
		for(const Json &input : root["consoles"].items())
		{
			if(!input.isObject())
				continue;
			std::string kind = input["type"].toString();
			if(kind != "ps4" && kind != "ps5")
				kind.clear();
			ConsoleEntry entry;
			entry.name = input["name"].toString();
			entry.address = input["address"].toString();
			entry.type = kind;
			entry.accountId = input["account_id"].toString();
			entry.hostId = input["host_id"].toString();
			entry.startMode = input["start_mode"].toString() == "ftp" ? "ftp" : "";
			settings.consoles.push_back(entry);
		}
	}
	normaliseConsoles(settings);
	settings.ftpPort = static_cast<uint16_t>(root["ftp_port"].toInt(settings.ftpPort));
	settings.ftpPortPs5 = static_cast<uint16_t>(root["ftp_port_ps5"].toInt(settings.ftpPortPs5));
	settings.installerPort = static_cast<uint16_t>(root["installer_port"].toInt(settings.installerPort));
	settings.defaultMode = transferModeFromName(root["default_mode"].toString(), settings.defaultMode);
	settings.ftpUploadDirectory = root["ftp_upload_directory"].toString(settings.ftpUploadDirectory);
	// Missing: the defaults. Present, even empty: what the person left.
	if(root["ftp_pinned_folders"].isArray())
	{
		settings.ftpPinnedFolders.clear();
		for(const Json &folder : root["ftp_pinned_folders"].items())
			if(folder.isString() && !folder.toString().empty())
				settings.ftpPinnedFolders.push_back(folder.toString());
	}
	if(root["ftp_pinned_folders_ps5"].isArray())
	{
		settings.ftpPinnedFoldersPs5.clear();
		for(const Json &folder : root["ftp_pinned_folders_ps5"].items())
			if(folder.isString() && !folder.toString().empty())
				settings.ftpPinnedFoldersPs5.push_back(folder.toString());
	}
	settings.checkAlreadyInstalled =
		root["check_already_installed"].toLooseBool(settings.checkAlreadyInstalled);
	settings.installAfterUpload = root["install_after_upload"].toLooseBool(settings.installAfterUpload);
	settings.deleteFromConsoleAfterInstall =
		root["delete_from_console_after_install"].toLooseBool(settings.deleteFromConsoleAfterInstall);
	settings.httpPort = static_cast<uint16_t>(root["http_port"].toInt(settings.httpPort));
	settings.httpBindAddress = root["http_bind_address"].toString(settings.httpBindAddress);
	settings.restrictToConsoleIp = root["restrict_to_console_ip"].toLooseBool(settings.restrictToConsoleIp);
	settings.firewallNoticeShown = root["firewall_notice_shown"].toLooseBool(settings.firewallNoticeShown);
	settings.ftpMaxConnections = static_cast<int>(root["ftp_max_connections"].toInt(settings.ftpMaxConnections));
	settings.ftpAdvancedMode = root["ftp_advanced_mode"].toLooseBool(settings.ftpAdvancedMode);
	settings.streamResolution =
		static_cast<int>(root["stream_resolution"].toInt(settings.streamResolution));
	settings.streamFps = static_cast<int>(root["stream_fps"].toInt(settings.streamFps));
	settings.streamBitrateKbps =
		static_cast<int>(root["stream_bitrate_kbps"].toInt(settings.streamBitrateKbps));
	settings.streamHardwareDecode =
		root["stream_hardware_decode"].toLooseBool(settings.streamHardwareDecode);
	settings.streamFullscreenOnConnect =
		root["stream_fullscreen_on_connect"].toLooseBool(settings.streamFullscreenOnConnect);
	{
		const std::string aspect = root["stream_aspect"].toString(settings.streamAspect);
		if(aspect == "fit" || aspect == "4:3" || aspect == "fill")
			settings.streamAspect = aspect;
	}
	settings.streamRumble = root["stream_rumble"].toLooseBool(settings.streamRumble);
	settings.streamTouchpadFromMouse =
		root["stream_touchpad_from_mouse"].toLooseBool(settings.streamTouchpadFromMouse);
	settings.streamAccountId = root["stream_account_id"].toString(settings.streamAccountId);
	if(root["accounts"].isArray())
	{
		for(const Json &input : root["accounts"].items())
		{
			SavedAccount account;
			account.label = input["label"].toString();
			account.accountId = input["account_id"].toString();
			account.remotePlayPin = input["remote_play_pin"].toString();
			settings.accounts.push_back(account);
		}
	}
	normaliseAccounts(settings);
	if(root["keyboard_bindings"].isObject())
	{
		for(const auto &pair : root["keyboard_bindings"].members())
		{
			if(pair.second.isNumber())
				settings.keyboardBindings[pair.first] = static_cast<int>(pair.second.toInt());
		}
	}
	if(root["pad_bindings"].isObject())
	{
		for(const auto &pair : root["pad_bindings"].members())
			if(pair.second.isString())
				settings.padBindings[pair.first] = pair.second.toString();
	}
	settings.theme = root["theme"].toString(settings.theme);
	auto readColors = [](const Json &object) {
		std::map<std::string, std::string> colors;
		if(object.isObject())
			for(const auto &pair : object.members())
				if(pair.second.isString() && isThemeColor(pair.second.toString()))
					colors[pair.first] = pair.second.toString();
		return colors;
	};
	settings.themeColors = readColors(root["theme_colors"]);
	settings.lastCustomColors = readColors(root["theme_last_custom"]);
	settings.lastCustomTheme = root["theme_last_custom_base"].toString();
	settings.activePreset = root["theme_active_preset"].toString();
	if(root["theme_presets"].isArray())
	{
		for(const Json &item : root["theme_presets"].items())
		{
			if(!item.isObject() || item["name"].toString().empty())
				continue;
			ThemePreset preset;
			preset.name = item["name"].toString();
			preset.theme = item["theme"].toString("dark");
			preset.colors = readColors(item["colors"]);
			settings.themePresets.push_back(std::move(preset));
		}
	}
	settings.language = root["language"].toString(settings.language);
	// English is the default for everyone; "auto" (following the system) was
	// the old default and becomes English too.
	if(settings.language != "pt_PT")
		settings.language = "en";
	settings.debugLogging = root["debug_logging"].toLooseBool(settings.debugLogging);
	settings.checkForUpdates = root["check_for_updates"].toLooseBool(settings.checkForUpdates);
	settings.firstRunDone = root["first_run_done"].toLooseBool(settings.firstRunDone);
	settings.gamesFolder = root["games_folder"].toString(settings.gamesFolder);
	settings.convertOutputFolder = root["convert_output_folder"].toString(settings.convertOutputFolder);
	settings.installStorage = root["install_storage"].toString(settings.installStorage);
	if(settings.installStorage != "usb" && settings.installStorage != "ext")
		settings.installStorage = "internal";
	settings.saveVaultFolder = root["save_vault_folder"].toString(settings.saveVaultFolder);
	if(root["library_locations"].isArray())
		for(const Json &input : root["library_locations"].items())
		{
			LibraryLocation place;
			place.id = input["id"].toString();
			place.name = input["name"].toString();
			place.path = input["path"].toString();
			place.where = input["where"].toString("pc") == "console" ? "console" : "pc";
			place.favorite = input["favorite"].toLooseBool(false);
			place.inSearch = input["in_search"].toLooseBool(true);
			if(!place.id.empty() && !place.path.empty())
				settings.libraryLocations.push_back(place);
		}
	settings.libraryLocationsSeeded = root["library_locations_seeded"].toLooseBool(settings.libraryLocationsSeeded);
	if(root["save_account_links"].isObject())
		for(const auto &pair : root["save_account_links"].members())
			if(pair.second.isString())
				settings.saveAccountLinks[pair.first] = pair.second.toString();
	{
		const std::string stored = root["update_repository"].toString(settings.updateRepository);
		const bool hasDefault = root["update_repository_default"].isString();
		const std::string savedDefault = root["update_repository_default"].toString();
		settings.updateRepository = resolveUpdateRepository(stored,
			hasDefault ? &savedDefault : nullptr, ORBISLINK_REPOSITORY_STRING);
	}
	settings.updateChannel = root["update_channel"].toString(settings.updateChannel);

	if(settings.ftpMaxConnections < 1)
		settings.ftpMaxConnections = 1;
	if(settings.ftpMaxConnections > 2)
		settings.ftpMaxConnections = 2;
	// A hand-edited file cannot ask for what the console does not know:
	// chiaki only accepts these four resolution presets and two frame rates.
	if(settings.streamResolution != 360 && settings.streamResolution != 540
		&& settings.streamResolution != 720 && settings.streamResolution != 1080)
		settings.streamResolution = 720;
	if(settings.streamFps != 30 && settings.streamFps != 60)
		settings.streamFps = 60;
	if(settings.streamBitrateKbps < 0)
		settings.streamBitrateKbps = 0;
	// "escuro"/"vidro"/"claro" are the names older versions saved.
	if(settings.theme == "escuro")
		settings.theme = "dark";
	else if(settings.theme == "vidro")
		settings.theme = "glass";
	else if(settings.theme == "claro")
		settings.theme = "light";
	if(settings.theme != "dark" && settings.theme != "glass" && settings.theme != "light")
		settings.theme = "dark";
	// "estavel"/"testes" are the names older versions saved.
	if(settings.updateChannel == "testes")
		settings.updateChannel = "testing";
	if(settings.updateChannel != "stable" && settings.updateChannel != "testing")
		settings.updateChannel = "stable";
	if(ok)
		*ok = true;
	return settings;
}

void normaliseConsoles(Settings &settings)
{
	std::vector<ConsoleEntry> clean;
	auto alreadyHas = [&clean](const std::string &address) {
		for(const ConsoleEntry &c : clean)
			if(c.address == address)
				return true;
		return false;
	};
	for(const ConsoleEntry &console : settings.consoles)
	{
		const std::string address = trim(console.address);
		if(address.empty() || alreadyHas(address))
			continue;
		ConsoleEntry copy = console;
		copy.address = address;
		clean.push_back(copy);
	}
	const std::string current = trim(settings.consoleAddress);
	if(!current.empty())
	{
		bool found = false;
		for(ConsoleEntry &c : clean)
		{
			if(c.address == current)
			{
				// The name of the console in use is the one in the settings:
				// that is the one edited in the settings dialog.
				c.name = settings.consoleName;
				found = true;
			}
		}
		if(!found)
		{
			ConsoleEntry entry;
			entry.name = settings.consoleName;
			entry.address = current;
			clean.insert(clean.begin(), entry);
		}
	}
	settings.consoles = clean;
}

void normaliseAccounts(Settings &settings)
{
	std::vector<SavedAccount> clean;
	auto find = [&clean](const std::string &id) -> SavedAccount * {
		for(SavedAccount &account : clean)
			if(account.accountId == id)
				return &account;
		return nullptr;
	};
	auto add = [&](const std::string &rawId, const std::string &label, const std::string &pin) {
		const std::string id = trim(rawId);
		if(id.empty() || find(id))
			return;
		SavedAccount account;
		account.accountId = id;
		account.label = trim(label).empty() ? "PSN account " + std::to_string(clean.size() + 1) : trim(label);
		account.remotePlayPin = trim(pin);
		clean.push_back(account);
	};
	for(const SavedAccount &account : settings.accounts)
		add(account.accountId, account.label, account.remotePlayPin);
	for(const ConsoleEntry &console : settings.consoles)
		add(console.accountId, console.name, std::string());
	add(settings.streamAccountId, std::string(), std::string());
	settings.accounts = clean;
}

std::string resolveUpdateRepository(const std::string &stored, const std::string *storedDefault,
	const std::string &compiledDefault)
{
	// A local build knows of no repository: it has nothing to replace
	// it with, so the stored value stays.
	if(compiledDefault.empty())
		return stored;
	if(!storedDefault || stored == *storedDefault || stored.empty())
		return compiledDefault;
	return stored;
}

SettingsStore::SettingsStore(std::string path) : path_(std::move(path)) {}

bool SettingsStore::load(Settings *settings) const
{
	std::ifstream file(path_, std::ios::binary);
	if(!file)
		return false;
	std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	bool ok = false;
	const Settings parsed = Settings::fromJson(text, &ok);
	if(!ok)
	{
		logWarning("Unreadable settings in " + path_ + "; using the defaults.");
		return false;
	}
	if(settings)
		*settings = parsed;
	return true;
}

bool SettingsStore::save(const Settings &settings) const
{
	const size_t slash = path_.find_last_of("/\\");
	if(slash != std::string::npos)
		ensureDirectory(path_.substr(0, slash));
	std::ofstream file(path_, std::ios::binary | std::ios::trunc);
	if(!file)
	{
		logError("Could not save the settings to " + path_);
		return false;
	}
	file << settings.toJson();
	return file.good();
}

std::string SettingsStore::defaultDirectory()
{
#ifdef _WIN32
	const char *appData = std::getenv("APPDATA");
	if(appData && *appData)
		return joinPath(appData, "OrbisLink");
	return "OrbisLink";
#else
	const char *xdg = std::getenv("XDG_CONFIG_HOME");
	if(xdg && *xdg)
		return joinPath(xdg, "orbislink");
	const char *home = std::getenv("HOME");
	if(home && *home)
		return joinPath(joinPath(home, ".config"), "orbislink");
	return ".orbislink";
#endif
}

std::string SettingsStore::defaultSettingsPath()
{
	return joinPath(defaultDirectory(), "settings.json");
}

std::string SettingsStore::defaultQueuePath() { return joinPath(defaultDirectory(), "queue.json"); }

std::string SettingsStore::defaultLogPath() { return joinPath(defaultDirectory(), "orbislink.log"); }

bool SettingsStore::ensureDirectory(const std::string &path)
{
	if(path.empty() || directoryExists(path))
		return true;
	const size_t slash = path.find_last_of("/\\");
	if(slash != std::string::npos && slash > 0)
		ensureDirectory(path.substr(0, slash));
#ifdef _WIN32
	return _mkdir(path.c_str()) == 0 || directoryExists(path);
#else
	return mkdir(path.c_str(), 0755) == 0 || directoryExists(path);
#endif
}

} // namespace orbislink
