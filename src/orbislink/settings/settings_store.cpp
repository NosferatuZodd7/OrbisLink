// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/settings/settings_store.h"

#include "orbislink/common/json.h"
#include "orbislink/common/log.h"
#include "orbislink/common/util.h"

#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#ifdef _WIN32
#	include <direct.h>
#endif

namespace orbislink {

const char *transferModeName(TransferMode mode)
{
	return mode == TransferMode::FtpUpload ? "ftp" : "direct";
}

TransferMode transferModeFromName(const std::string &name, TransferMode fallback)
{
	if(name == "ftp")
		return TransferMode::FtpUpload;
	if(name == "direct")
		return TransferMode::DirectInstall;
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
		items.push(input);
	}
	root.set("consoles", items);
	root.set("ftp_port", Json::fromInt(ftpPort));
	root.set("ftp_port_ps5", Json::fromInt(ftpPortPs5));
	root.set("installer_port", Json::fromInt(installerPort));
	root.set("default_mode", Json::fromString(transferModeName(defaultMode)));
	root.set("ftp_upload_directory", Json::fromString(ftpUploadDirectory));
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
	root.set("stream_rumble", Json::fromBool(streamRumble));
	root.set("stream_touchpad_from_mouse", Json::fromBool(streamTouchpadFromMouse));
	root.set("stream_account_id", Json::fromString(streamAccountId));
	Json keys = Json::makeObject();
	for(const auto &pair : keyboardBindings)
		keys.set(pair.first, Json::fromInt(pair.second));
	root.set("keyboard_bindings", keys);
	root.set("theme", Json::fromString(theme));
	root.set("language", Json::fromString(language));
	root.set("debug_logging", Json::fromBool(debugLogging));
	root.set("check_for_updates", Json::fromBool(checkForUpdates));
	root.set("first_run_done", Json::fromBool(firstRunDone));
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
			settings.consoles.push_back(entry);
		}
	}
	normaliseConsoles(settings);
	settings.ftpPort = static_cast<uint16_t>(root["ftp_port"].toInt(settings.ftpPort));
	settings.ftpPortPs5 = static_cast<uint16_t>(root["ftp_port_ps5"].toInt(settings.ftpPortPs5));
	settings.installerPort = static_cast<uint16_t>(root["installer_port"].toInt(settings.installerPort));
	settings.defaultMode = transferModeFromName(root["default_mode"].toString(), settings.defaultMode);
	settings.ftpUploadDirectory = root["ftp_upload_directory"].toString(settings.ftpUploadDirectory);
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
	settings.streamRumble = root["stream_rumble"].toLooseBool(settings.streamRumble);
	settings.streamTouchpadFromMouse =
		root["stream_touchpad_from_mouse"].toLooseBool(settings.streamTouchpadFromMouse);
	settings.streamAccountId = root["stream_account_id"].toString(settings.streamAccountId);
	if(root["keyboard_bindings"].isObject())
	{
		for(const auto &pair : root["keyboard_bindings"].members())
		{
			if(pair.second.isNumber())
				settings.keyboardBindings[pair.first] = static_cast<int>(pair.second.toInt());
		}
	}
	settings.theme = root["theme"].toString(settings.theme);
	settings.language = root["language"].toString(settings.language);
	// English is the default for everyone; "auto" (following the system) was
	// the old default and becomes English too.
	if(settings.language != "pt_PT")
		settings.language = "en";
	settings.debugLogging = root["debug_logging"].toLooseBool(settings.debugLogging);
	settings.checkForUpdates = root["check_for_updates"].toLooseBool(settings.checkForUpdates);
	settings.firstRunDone = root["first_run_done"].toLooseBool(settings.firstRunDone);
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
