// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/ftp/ftp_client.h"
#include "test_support.h"

#include <algorithm>
#include <sstream>

using namespace orbislink;

ORBISLINK_TEST(reads_unix_style_listing)
{
	const std::string listing =
		"drwxr-xr-x   2 root  root      4096 Jan  1 00:00 pkg\r\n"
		"-rw-r--r--   1 root  root  5368709120 Mar 15 12:34 big game.pkg\r\n"
		"-rw-r--r--   1 root  root       512 Mar 15 12:35 note.txt\r\n";
	const auto entries = FtpClient::parseListing(listing, "/data");
	CHECK_EQ(entries.size(), static_cast<size_t>(3));
	// Folders first, then files in alphabetical order.
	CHECK_EQ(entries[0].name, std::string("pkg"));
	CHECK(entries[0].isDirectory);
	CHECK_EQ(entries[0].path, std::string("/data/pkg"));
	CHECK_EQ(entries[1].name, std::string("big game.pkg")); // name with spaces
	CHECK_EQ(entries[1].size, 5368709120ll);                   // > 4 GB
	CHECK(!entries[1].isDirectory);
	CHECK_EQ(entries[2].name, std::string("note.txt"));
	CHECK_EQ(entries[2].path, std::string("/data/note.txt"));
}

ORBISLINK_TEST(ignores_dot_and_dot_dot)
{
	const std::string listing =
		"drwxr-xr-x 2 root root 4096 Jan 1 00:00 .\n"
		"drwxr-xr-x 2 root root 4096 Jan 1 00:00 ..\n"
		"-rw-r--r-- 1 root root  10 Jan 1 00:00 a.bin\n";
	const auto entries = FtpClient::parseListing(listing, "/");
	CHECK_EQ(entries.size(), static_cast<size_t>(1));
	CHECK_EQ(entries[0].name, std::string("a.bin"));
	CHECK_EQ(entries[0].path, std::string("/a.bin"));
}

ORBISLINK_TEST(reads_symbolic_links)
{
	const std::string listing = "lrwxrwxrwx 1 root root 7 Jan 1 00:00 shortcut -> /data/pkg\n";
	const auto entries = FtpClient::parseListing(listing, "/mnt");
	CHECK_EQ(entries.size(), static_cast<size_t>(1));
	CHECK(entries[0].isSymlink);
	CHECK_EQ(entries[0].name, std::string("shortcut"));
}

ORBISLINK_TEST(reads_ms_dos_style_listing)
{
	const std::string listing =
		"01-15-24  10:22AM       <DIR>          pkg\r\n"
		"01-15-24  10:23AM             1048576 game.pkg\r\n";
	const auto entries = FtpClient::parseListing(listing, "/data/");
	CHECK_EQ(entries.size(), static_cast<size_t>(2));
	CHECK(entries[0].isDirectory);
	CHECK_EQ(entries[1].size, 1048576);
}

ORBISLINK_TEST(unknown_line_is_not_lost)
{
	const auto entries = FtpClient::parseListing("something-odd\n", "/data");
	CHECK_EQ(entries.size(), static_cast<size_t>(1));
	CHECK_EQ(entries[0].name, std::string("something-odd"));
}

ORBISLINK_TEST(protected_system_areas)
{
	CHECK(FtpClient::isProtectedPath("/system"));
	CHECK(FtpClient::isProtectedPath("/system/priv_data"));
	CHECK(FtpClient::isProtectedPath("/system_ex/app"));
	CHECK(FtpClient::isProtectedPath("/preinst/x"));
	CHECK(!FtpClient::isProtectedPath("/data/pkg"));
	CHECK(!FtpClient::isProtectedPath("/mnt/usb0"));

	FtpClient::Config config;
	config.host = "192.168.1.10";
	FtpClient client(config);
	CHECK(!client.isWriteAllowed("/system/x"));
	CHECK(client.isWriteAllowed("/data/pkg/x.pkg"));

	config.advancedMode = true;
	client.setConfig(config);
	CHECK(client.isWriteAllowed("/system/x"));
}

ORBISLINK_TEST(writes_correct_ftp_urls)
{
	FtpClient::Config config;
	config.host = "192.168.1.10";
	config.port = 2121;
	FtpClient client(config);
	CHECK_EQ(client.urlFor("/data/pkg/game test.pkg"),
		std::string("ftp://192.168.1.10:2121/data/pkg/game%20test.pkg"));
}

ORBISLINK_TEST(suggested_shortcuts)
{
	const auto shortcuts = FtpClient::shortcutPaths();
	CHECK(std::find(shortcuts.begin(), shortcuts.end(), "/data/pkg/") != shortcuts.end());
	CHECK(std::find(shortcuts.begin(), shortcuts.end(), "/mnt/usb0/") != shortcuts.end());
}

ORBISLINK_TEST(machine_listing_reads_types_sizes_times_and_modes)
{
	// 2026-11-17 22:13:00 UTC, and "now" an hour later.
	const int64_t now = 1794953580 + 3600;
	const std::string listing =
		"type=cdir;modify=20261117221300; .\r\n"
		"type=pdir;modify=20261117221300; ..\r\n"
		"type=dir;size=4096;modify=20261117221300;UNIX.mode=0755; homebrew\r\n"
		"type=file;size=1048576;modify=20261117221300;UNIX.mode=0644; Game Name.pkg\r\n"
		"type=file;size=12;modify=20200101000000; old.txt\r\n"
		"type=OS.unix=slink:/data;modify=20261117221300; link\r\n"
		"not an mlsd line\r\n";
	const auto entries = FtpClient::parseMachineListing(listing, "/data", now);
	CHECK_EQ(entries.size(), size_t(4));
	CHECK_EQ(entries[0].name, std::string("homebrew"));
	CHECK(entries[0].isDirectory);
	CHECK_EQ(entries[0].permissions, std::string("drwxr-xr-x"));
	CHECK_EQ(entries[0].path, std::string("/data/homebrew"));
	// Files after folders, by name; the space in the name kept.
	CHECK_EQ(entries[1].name, std::string("Game Name.pkg"));
	CHECK_EQ(entries[1].size, int64_t(1048576));
	CHECK_EQ(entries[1].permissions, std::string("-rw-r--r--"));
	// Written as LIST writes it: recent with the time, old with the year.
	CHECK_EQ(entries[1].modified, std::string("Nov 17 22:13"));
	CHECK_EQ(entries[2].name, std::string("link"));
	CHECK(entries[2].isSymlink);
	CHECK_EQ(entries[3].name, std::string("old.txt"));
	CHECK_EQ(entries[3].modified, std::string("Jan 01 2020"));
}

TEST_MAIN()
