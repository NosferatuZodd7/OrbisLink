// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The console library: what each thing in OrbisLinkFPKG is, where
// ShadowMountPlus mounts from on the same drive, and a package's header read
// in a few pieces, as it is over FTP.

#include "orbislink/library/console_library.h"
#include "orbislink/pkg/pkg_inspector.h"
#include "test_fixtures.h"
#include "test_support.h"

#include <cstring>
#include <string>
#include <vector>

using namespace orbislink;
using namespace orbislink::library;
using namespace orbislink_test;

ORBISLINK_TEST(tells_each_kind_apart)
{
	CHECK(kindOf("Game.pkg", false, 1000) == Kind::Package);
	CHECK(kindOf("Game (Europe).ISO", false, 1000) == Kind::Disc);
	CHECK(kindOf("Game.cue", false, 200) == Kind::Disc);
	CHECK(kindOf("Game (Track 1).bin", false, 600ll * 1024 * 1024) == Kind::Disc);
	CHECK(kindOf("payload.bin", false, 300 * 1024) == Kind::Payload);
	CHECK(kindOf("kstuff.elf", false, 100) == Kind::Payload);
	CHECK(kindOf("PPSA01234.ffpkg", false, 100) == Kind::Image);
	CHECK(kindOf("Game.exfat", false, 100) == Kind::Image);
	CHECK(kindOf("Game.zip", false, 100) == Kind::Archive);
	CHECK(kindOf("notes.txt", false, 100) == Kind::Other);
	CHECK(kindOf("PPSA01234-app", true, 0) == Kind::Folder);
	CHECK_EQ(std::string(kindName(Kind::Image)), std::string("image"));
}

ORBISLINK_TEST(finds_title_ids_in_names)
{
	CHECK_EQ(titleIdInName("PPSA01234-app.ffpkg"), std::string("PPSA01234"));
	CHECK_EQ(titleIdInName("Game_CUSA12345_v1.pkg"), std::string("CUSA12345"));
	CHECK_EQ(titleIdInName("SLUS_209.46.Game.iso"), std::string("SLUS20946"));
	CHECK_EQ(titleIdInName("Disney-Pixar-Game_SLES54733.pkg"), std::string("SLES54733"));
	CHECK_EQ(titleIdInName("sles-54733 game.iso"), std::string("SLES54733"));
	// Four letters and five digits that are no title ID stay out.
	CHECK(titleIdInName("PART12345.iso").empty());
	CHECK(titleIdInName("CUSA123456789").empty());
	CHECK(titleIdInName("Plain Game (Europe).iso").empty());
}

ORBISLINK_TEST(knows_the_drives_and_where_shadowmount_looks)
{
	CHECK_EQ(driveOf("/data/OrbisLinkFPKG/a.pkg"), std::string("internal"));
	CHECK_EQ(driveOf("/mnt/usb1/OrbisLinkFPKG/a.pkg"), std::string("usb"));
	CHECK_EQ(driveOf("/mnt/ext0/OrbisLinkFPKG/a.pkg"), std::string("ext"));
	CHECK_EQ(mountFolderFor("/data/OrbisLinkFPKG/a.ffpkg"), std::string("/data/homebrew"));
	CHECK_EQ(mountFolderFor("/mnt/usb3/OrbisLinkFPKG/a.ffpkg"), std::string("/mnt/usb3/homebrew"));
	CHECK_EQ(mountFolderFor("/mnt/ext1/OrbisLinkFPKG/b"), std::string("/mnt/ext1/homebrew"));
	CHECK(mountFolderFor("/mnt/usbx/a").empty());
	CHECK(mountFolderFor("/user/app/a").empty());

	const std::vector<std::string> folders = libraryFolders();
	CHECK_EQ(folders.front(), std::string("/data/OrbisLinkFPKG"));
	CHECK_EQ(folders.size(), static_cast<size_t>(11));
}

ORBISLINK_TEST(reads_what_an_app_says_about_itself)
{
	const AppParams ps5 = readParamJson(R"({ "titleId": "PPSA09999", "contentId": "UP0000-PPSA09999_00-FAKEAPP000000000",
		"contentVersion": "01.002.000",
		"localizedParameters": { "defaultLanguage": "pt-PT", "en-US": { "titleName": "Fake App" },
			"pt-PT": { "titleName": "App Falsa" } } })");
	CHECK(ps5.ok);
	CHECK_EQ(ps5.titleId, std::string("PPSA09999"));
	CHECK_EQ(ps5.title, std::string("App Falsa"));
	CHECK_EQ(ps5.version, std::string("01.002.000"));
	CHECK(!readParamJson("not json").ok);

	const AppParams ps4 = readParamSfo(buildSfo({ { "TITLE", "Fake Game" }, { "TITLE_ID", "CUSA09999" },
		{ "APP_VER", "01.05" } }));
	CHECK(ps4.ok);
	CHECK_EQ(ps4.title, std::string("Fake Game"));
	CHECK_EQ(ps4.titleId, std::string("CUSA09999"));
	CHECK_EQ(ps4.version, std::string("01.05"));
}

ORBISLINK_TEST(reads_a_package_header_in_a_few_pieces)
{
	PkgOptions options;
	options.contentId = "UP0001-CUSA07777_00-ORBISLINKREMOTE1";
	options.sfoEntries = { { "CATEGORY", "gd" }, { "TITLE", "Remote Game" }, { "TITLE_ID", "CUSA07777" },
		{ "APP_VER", "01.00" } };
	const std::vector<uint8_t> pkg = buildPkg(options);

	// As FTP would: whole blocks, each asked for once.
	BlockReader reader([&pkg](int64_t offset, size_t length, std::vector<uint8_t> *bytes) {
		bytes->assign(pkg.begin() + offset, pkg.begin() + offset + static_cast<int64_t>(length));
		return true;
	}, static_cast<int64_t>(pkg.size()), 4096);
	const PkgInfo info = PkgInspector().inspect("Remote Game.pkg", static_cast<int64_t>(pkg.size()),
		[&reader](int64_t offset, void *buffer, size_t size) { return reader.read(offset, buffer, size); });
	CHECK(info.valid);
	CHECK_EQ(info.title, std::string("Remote Game"));
	CHECK_EQ(info.titleId, std::string("CUSA07777"));
	CHECK(reader.fetches() <= pkg.size() / 4096 + 1);

	// Reads cross block edges whole.
	std::vector<uint8_t> middle(6000);
	CHECK(reader.read(1000, middle.data(), middle.size()));
	CHECK(std::memcmp(middle.data(), pkg.data() + 1000, middle.size()) == 0);
	CHECK(!reader.read(static_cast<int64_t>(pkg.size()) - 10, middle.data(), 20));
}

TEST_MAIN()
