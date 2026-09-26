// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/common/log.h"
#include "orbislink/settings/settings_store.h"
#include "test_support.h"

#include <cstdio>
#include <sstream>

using namespace orbislink;

ORBISLINK_TEST(settings_round_trip)
{
	Settings settings;
	settings.consoleName = "Living room PS4";
	settings.consoleAddress = "192.168.1.42";
	settings.defaultMode = TransferMode::FtpUpload;
	settings.ftpUploadDirectory = "/mnt/usb0/pkg/";
	settings.httpPort = 9000;
	settings.restrictToConsoleIp = false;
	settings.ftpMaxConnections = 2;
	settings.ftpAdvancedMode = true;
	settings.language = "en";

	bool ok = false;
	const Settings restored = Settings::fromJson(settings.toJson(), &ok);
	CHECK(ok);
	CHECK_EQ(restored.consoleName, std::string("Living room PS4"));
	CHECK_EQ(restored.consoleAddress, std::string("192.168.1.42"));
	CHECK(restored.defaultMode == TransferMode::FtpUpload);
	CHECK_EQ(restored.ftpUploadDirectory, std::string("/mnt/usb0/pkg/"));
	CHECK_EQ(restored.httpPort, static_cast<uint16_t>(9000));
	CHECK(!restored.restrictToConsoleIp);
	CHECK_EQ(restored.ftpMaxConnections, 2);
	CHECK(restored.ftpAdvancedMode);
	CHECK_EQ(restored.language, std::string("en"));
}

ORBISLINK_TEST(defaults_follow_the_specification)
{
	const Settings settings;
	CHECK_EQ(settings.ftpPort, static_cast<uint16_t>(2121));
	CHECK_EQ(settings.installerPort, static_cast<uint16_t>(12800));
	CHECK_EQ(settings.httpPort, static_cast<uint16_t>(8765));
	CHECK(settings.defaultMode == TransferMode::DirectInstall);
	CHECK_EQ(settings.ftpUploadDirectory, std::string("/data/pkg/"));
	CHECK(settings.restrictToConsoleIp);
	CHECK(!settings.installAfterUpload);
	CHECK(!settings.ftpAdvancedMode);
	CHECK(!settings.debugLogging);
	CHECK(settings.checkForUpdates);
	CHECK_EQ(settings.ftpMaxConnections, 1);
	// Remote Play
	CHECK_EQ(settings.streamResolution, 720);
	CHECK_EQ(settings.streamFps, 60);
	CHECK_EQ(settings.streamBitrateKbps, 0);
	CHECK(settings.streamHardwareDecode);
	CHECK(!settings.streamFullscreenOnConnect);
	CHECK(settings.streamRumble);
	CHECK_EQ(settings.theme, std::string("dark"));
	CHECK_EQ(settings.language, std::string("en"));
}

ORBISLINK_TEST(out_of_range_stream_settings_are_corrected)
{
	// A hand-edited file cannot ask for what the console does not know.
	bool ok = false;
	const Settings settings = Settings::fromJson(
		R"({"stream_resolution":999,"stream_fps":144,"stream_bitrate_kbps":-5,"theme":"neon"})",
		&ok);
	CHECK(ok);
	CHECK_EQ(settings.streamResolution, 720);
	CHECK_EQ(settings.streamFps, 60);
	CHECK_EQ(settings.streamBitrateKbps, 0);
	CHECK_EQ(settings.theme, std::string("dark"));
}

ORBISLINK_TEST(allowed_stream_settings_pass)
{
	bool ok = false;
	const Settings settings = Settings::fromJson(
		R"({"stream_resolution":1080,"stream_fps":30,"stream_bitrate_kbps":15000,"theme":"vidro"})",
		&ok);
	CHECK(ok);
	CHECK_EQ(settings.streamResolution, 1080);
	CHECK_EQ(settings.streamFps, 30);
	CHECK_EQ(settings.streamBitrateKbps, 15000);
	CHECK_EQ(settings.theme, std::string("glass"));
}

ORBISLINK_TEST(invalid_json_does_not_break_the_settings)
{
	bool ok = true;
	const Settings settings = Settings::fromJson("{ garbage", &ok);
	CHECK(!ok);
	CHECK_EQ(settings.ftpPort, static_cast<uint16_t>(2121));
}

ORBISLINK_TEST(limits_ftp_connections_to_two)
{
	const Settings settings = Settings::fromJson(R"({"ftp_max_connections": 9})");
	CHECK_EQ(settings.ftpMaxConnections, 2);
}

ORBISLINK_TEST(save_and_read_from_disk)
{
	const std::string path = ".orbislink-test-settings.json";
	Settings settings;
	settings.consoleAddress = "10.0.0.5";
	SettingsStore store(path);
	CHECK(store.save(settings));

	Settings loaded;
	CHECK(store.load(&loaded));
	CHECK_EQ(loaded.consoleAddress, std::string("10.0.0.5"));
	std::remove(path.c_str());
}

ORBISLINK_TEST(logs_carry_no_sensitive_data)
{
	// §8/§9: no Account ID or registration keys in the exported logs.
	const std::string redacted =
		redactSensitive(R"({"psn_account_id":"1234567890","rp_key":"abcdef","title":"Game"})");
	CHECK(redacted.find("1234567890") == std::string::npos);
	CHECK(redacted.find("abcdef") == std::string::npos);
	CHECK(redacted.find("[REDACTED]") != std::string::npos);
	CHECK(redacted.find("Game") != std::string::npos);

	const std::string plain = redactSensitive("account_id=AABBCCDD and password=secret");
	CHECK(plain.find("AABBCCDD") == std::string::npos);
	CHECK(plain.find("secret") == std::string::npos);
}

ORBISLINK_TEST(wizard_only_appears_the_first_time)
{
	// By default the wizard must appear; after running once, never
	// again — and that has to survive saving and reloading.
	Settings settings;
	CHECK(!settings.firstRunDone);

	settings.firstRunDone = true;
	bool ok = false;
	const Settings reloaded = Settings::fromJson(settings.toJson(), &ok);
	CHECK(ok);
	CHECK(reloaded.firstRunDone);

	// An old file, without the field, still asks for the wizard.
	const Settings old = Settings::fromJson(R"({"console_address":"10.0.0.5"})");
	CHECK(!old.firstRunDone);
}


// If the project moves to another repository, whoever has the old version
// has the old repository stored in the settings, and the new app cannot
// keep looking for versions there.
ORBISLINK_TEST(update_repository_follows_the_build)
{
	const std::string fresh = "new/OrbisLink";
	const std::string old = "old/repo";
	// File from before the rule: it does not say what the default was.
	CHECK_EQ(resolveUpdateRepository(old, nullptr, fresh), fresh);
	// Stored equal to the default at the time: nobody chose it.
	CHECK_EQ(resolveUpdateRepository(old, &old, fresh), fresh);
	// Written by hand (different from the default at the time): it stays.
	const std::string other = "other/copy";
	CHECK_EQ(resolveUpdateRepository(other, &old, fresh), other);
	// Empty is never valid.
	CHECK_EQ(resolveUpdateRepository("", &old, fresh), fresh);
	// Local build, without a repository: nothing to replace it with.
	CHECK_EQ(resolveUpdateRepository(old, nullptr, ""), old);
}

ORBISLINK_TEST(keyboard_keys_are_stored)
{
	Settings settings;
	settings.keyboardBindings["cross"] = 32;
	settings.keyboardBindings["ps"] = 80;
	const Settings loaded = Settings::fromJson(settings.toJson());
	CHECK_EQ(loaded.keyboardBindings.size(), static_cast<size_t>(2));
	CHECK_EQ(loaded.keyboardBindings.at("cross"), 32);
	// With nothing stored, it stays empty: the default map applies.
	CHECK(Settings::fromJson("{}").keyboardBindings.empty());
}

ORBISLINK_TEST(console_list)
{
	// Settings from before the list: the console in use becomes the first.
	const Settings oldOnes =
		Settings::fromJson(R"({"console_name":"Living room","console_address":"10.0.0.5"})");
	CHECK_EQ(oldOnes.consoles.size(), static_cast<size_t>(1));
	CHECK_EQ(oldOnes.consoles[0].address, std::string("10.0.0.5"));
	CHECK_EQ(oldOnes.consoles[0].name, std::string("Living room"));

	// Two consoles, round trip, with no duplicates or empty addresses.
	Settings settings;
	settings.consoleName = "Living room";
	settings.consoleAddress = "10.0.0.5";
	settings.consoles = { { "Living room", "10.0.0.5" }, { "Bedroom", "10.0.0.9" },
		{ "Repeated", "10.0.0.9" }, { "Empty", " " } };
	const Settings loaded = Settings::fromJson(settings.toJson());
	CHECK_EQ(loaded.consoles.size(), static_cast<size_t>(2));
	CHECK_EQ(loaded.consoles[1].name, std::string("Bedroom"));

	// With no console set, the list stays empty.
	CHECK(Settings::fromJson("{}").consoles.empty());

	// The type is stored; a value other than ps4/ps5 counts as
	// unknown.
	Settings withType;
	withType.consoles = { { "Bedroom", "10.0.0.9", "ps5" } };
	CHECK_EQ(Settings::fromJson(withType.toJson()).consoles[0].type, std::string("ps5"));
	const Settings odd = Settings::fromJson(
		R"({"consoles":[{"name":"X","address":"10.0.0.7","type":"xbox"}]})");
	CHECK(odd.consoles[0].type.empty());

	// Each console keeps its own Account ID: the PS4 and PS5 of the same
	// account may need it with the bytes in different orders.
	Settings two;
	two.consoles = { { "PS4", "10.0.0.3", "ps4", "CAcGBQQDAgE=" },
		{ "PS5", "10.0.0.4", "ps5", "AQIDBAUGBwg=" } };
	const Settings reloaded = Settings::fromJson(two.toJson());
	CHECK_EQ(reloaded.consoles[0].accountId, std::string("CAcGBQQDAgE="));
	CHECK_EQ(reloaded.consoles[1].accountId, std::string("AQIDBAUGBwg="));
	// Without an Account ID nothing is stored, and reading returns empty.
	CHECK(Settings::fromJson(withType.toJson()).consoles[0].accountId.empty());
}

TEST_MAIN()
