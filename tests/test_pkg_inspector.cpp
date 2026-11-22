// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/pkg/pkg_inspector.h"
#include "test_fixtures.h"
#include "test_support.h"

#include <fstream>
#include <sstream>

using namespace orbislink;
using namespace orbislink_test;

ORBISLINK_TEST(reads_metadata_of_a_valid_pkg)
{
	PkgOptions options;
	const std::string path = writeTempFile("game.pkg", buildPkg(options));

	PkgInspector inspector;
	const PkgInfo info = inspector.inspect(path);
	CHECK(info.valid);
	CHECK_EQ(info.title, std::string("Test Game"));
	CHECK_EQ(info.titleId, std::string("CUSA12345"));
	CHECK_EQ(info.contentId, std::string("UP0001-CUSA12345_00-ORBISLINKTEST001"));
	CHECK_EQ(info.category, std::string("gd"));
	CHECK_EQ(info.appVersion, std::string("01.00"));
	CHECK(info.kind == PkgCategory::Game);
	CHECK(!info.isPatch);
	CHECK(!info.iconPng.empty());
	CHECK_EQ(info.iconPng[0], static_cast<uint8_t>(0x89));
	CHECK_EQ(info.declaredSize, static_cast<uint64_t>(info.fileSize));

	removeTempFile(path);
}

ORBISLINK_TEST(tells_patch_apart_by_header_flags)
{
	PkgOptions options;
	options.contentFlags = 0x00100000; // FIRST_PATCH
	options.sfoEntries = { { "CATEGORY", "gp" }, { "TITLE", "Test Game" },
		{ "TITLE_ID", "CUSA12345" }, { "APP_VER", "01.02" } };
	const std::string path = writeTempFile("patch.pkg", buildPkg(options));

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(info.valid);
	CHECK(info.isPatch);
	CHECK(info.kind == PkgCategory::Patch);
	CHECK_EQ(pkgCategoryInstallOrder(info.kind), 1);

	removeTempFile(path);
}

ORBISLINK_TEST(recognises_dlc_by_content_type)
{
	PkgOptions options;
	options.contentType = 0x1B; // AC
	options.sfoEntries = { { "CATEGORY", "ac" }, { "TITLE", "Extra pack" },
		{ "TITLE_ID", "CUSA12345" } };
	const std::string path = writeTempFile("dlc.pkg", buildPkg(options));

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(info.kind == PkgCategory::Dlc);
	CHECK_EQ(pkgCategoryInstallOrder(info.kind), 3);
	CHECK_EQ(std::string(pkgCategoryLabel(info.kind)), std::string("DLC"));

	removeTempFile(path);
}

ORBISLINK_TEST(tells_official_packages_from_fake_ones)
{
	PkgOptions options;
	const std::string fake = writeTempFile("fake.pkg", buildPkg(options));
	CHECK(!PkgInspector().inspect(fake).official);

	// Sony's own: 0x81 or 0x83 in the type field's top half.
	for(uint32_t type : { 0x81000000u, 0x83000001u })
	{
		options.pkgType = type;
		const std::string path = writeTempFile("official.pkg", buildPkg(options));
		CHECK(PkgInspector().inspect(path).official);
	}
	// An official delta patch says so in its content flags instead.
	options.pkgType = 0x80000000u;
	options.contentType = 0x1E;
	options.contentFlags = 0x43000000u;
	const std::string delta = writeTempFile("delta.pkg", buildPkg(options));
	CHECK(PkgInspector().inspect(delta).official);
	std::remove(fake.c_str());
	std::remove(delta.c_str());
	std::remove(".orbislink-test-official.pkg");
}

ORBISLINK_TEST(reads_the_firmware_a_package_needs)
{
	PkgOptions options;
	options.sfoInts = { { "SYSTEM_VER", 0x05058000u } };
	std::string path = writeTempFile("fw.pkg", buildPkg(options));
	PkgInfo info = PkgInspector().inspect(path);
	CHECK_EQ(info.requiredFirmware, static_cast<uint16_t>(0x0505));
	CHECK_EQ(firmwareText(info.requiredFirmware), std::string("5.05"));

	// PUBTOOLINFO's SDK version wins over SYSTEM_VER.
	options.sfoEntries.push_back({ "PUBTOOLINFO", "c_date=20210812,sdk_ver=09008001,st_type=digital50" });
	path = writeTempFile("fw.pkg", buildPkg(options));
	info = PkgInspector().inspect(path);
	CHECK_EQ(firmwareText(info.requiredFirmware), std::string("9.00"));

	// Without either, nothing is known.
	PkgOptions bare;
	path = writeTempFile("fw.pkg", buildPkg(bare));
	CHECK_EQ(PkgInspector().inspect(path).requiredFirmware, static_cast<uint16_t>(0));
	std::remove(path.c_str());
}

ORBISLINK_TEST(firmware_versions_read_and_compare)
{
	CHECK_EQ(firmwareText(0x1100), std::string("11.00"));
	CHECK_EQ(firmwareText(0x0950), std::string("9.50"));
	CHECK_EQ(firmwareText(0), std::string());
	CHECK_EQ(firmwareFromSystemVer(0x11008001u), static_cast<uint16_t>(0x1100));
	CHECK_EQ(firmwareFromSystemVer(0x0672), static_cast<uint16_t>(0x0672));
	// Remote Play discovery's "system-version".
	CHECK_EQ(firmwareFromDiscovery("09000000"), static_cast<uint16_t>(0x0900));
	CHECK_EQ(firmwareFromDiscovery("11008001"), static_cast<uint16_t>(0x1100));
	CHECK_EQ(firmwareFromDiscovery(""), static_cast<uint16_t>(0));
	CHECK_EQ(firmwareFromDiscovery("9.00"), static_cast<uint16_t>(0));
	// As BCD, newer is bigger.
	CHECK(firmwareFromSystemVer(0x09508000u) > firmwareFromDiscovery("09000000"));
	CHECK(firmwareFromSystemVer(0x11008000u) > firmwareFromSystemVer(0x09508000u));
}

ORBISLINK_TEST(reads_a_ps5_fake_package_and_its_param_json)
{
	PkgOptions options;
	options.contentId = "UP9000-PPSA01234_00-ORBISLINKPS50001";
	options.includeSfo = false;
	options.paramJson = R"({"titleId":"PPSA01234","contentId":"UP9000-PPSA01234_00-ORBISLINKPS50001",)"
						R"("contentVersion":"01.002.000","requiredSystemSoftwareVersion":"0x0960000000000000",)"
						R"("localizedParameters":{"defaultLanguage":"en-US","en-US":{"titleName":"Test PS5 Game"}}})";
	const std::string path = writeTempFile("ps5.pkg", buildPs5Pkg(options, false));
	const PkgInfo info = PkgInspector().inspect(path);
	removeTempFile(path);
	CHECK(info.valid);
	CHECK(info.ps5);
	CHECK(!info.official);
	CHECK(!info.backport);
	CHECK_EQ(info.titleId, std::string("PPSA01234"));
	CHECK_EQ(info.title, std::string("Test PS5 Game"));
	CHECK_EQ(info.appVersion, std::string("01.002.000"));
	CHECK_EQ(info.requiredFirmware, uint16_t(0x0960));
	CHECK(info.kind == PkgCategory::Game);
	CHECK(!info.iconPng.empty());
}

ORBISLINK_TEST(tells_an_official_ps5_package_and_a_backport)
{
	PkgOptions options;
	options.contentId = "UP9000-PPSA01234_00-ORBISLINKPS50001";
	options.includeSfo = false;
	const std::string official = writeTempFile("ps5-official.pkg", buildPs5Pkg(options, true));
	const PkgInfo store = PkgInspector().inspect(official);
	removeTempFile(official);
	CHECK(store.ps5);
	CHECK(store.official);

	const std::string backport = writeTempFile("ps5-backport.pkg", buildPs5Pkg(options, false, true));
	const PkgInfo lih = PkgInspector().inspect(backport);
	removeTempFile(backport);
	CHECK(lih.valid);
	CHECK(lih.ps5);
	CHECK(lih.backport);
	CHECK(!lih.official);
	// No param.json: the title ID comes from the content ID.
	CHECK_EQ(lih.titleId, std::string("PPSA01234"));

	// An encrypted store package: no readable container inside.
	std::vector<uint8_t> retail(0x3000, 0);
	retail[0] = 0x7F;
	retail[1] = 'F';
	retail[2] = 'I';
	retail[3] = 'H';
	retail[5] = 0x80;
	const std::string sealed = writeTempFile("ps5-retail.pkg", retail);
	const PkgInfo closed = PkgInspector().inspect(sealed);
	removeTempFile(sealed);
	CHECK(!closed.valid);
	CHECK(closed.ps5);
	CHECK(closed.official);
}

ORBISLINK_TEST(rejects_invalid_magic)
{
	PkgOptions options;
	options.validMagic = false;
	const std::string path = writeTempFile("notapkg.pkg", buildPkg(options));

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(!info.valid);
	CHECK_EQ(info.error, std::string("Not a valid PS4 or PS5 package."));
	CHECK(!PkgInspector::hasPkgMagic(path));

	removeTempFile(path);
}

ORBISLINK_TEST(names_the_older_consoles_packages)
{
	// "\x7FPKG" and the platform at offset 4: 1 for the PS3, 2 for the PSP
	// and the PS Vita.
	for(const uint8_t platform : { uint8_t(1), uint8_t(2) })
	{
		std::vector<uint8_t> bytes(8192, 0);
		bytes[0] = 0x7F;
		bytes[1] = 'P';
		bytes[2] = 'K';
		bytes[3] = 'G';
		bytes[5] = platform;
		const std::string path = writeTempFile("older.pkg", bytes);
		const PkgInfo info = PkgInspector().inspect(path);
		CHECK(!info.valid);
		CHECK_EQ(info.error, std::string(platform == 1 ? "A PS3 package, not a PS4 one."
			: "A PSP or PS Vita package, not a PS4 one."));
		removeTempFile(path);
	}
}

ORBISLINK_TEST(rejects_too_small_file)
{
	std::vector<uint8_t> tiny(100, 0);
	tiny[0] = 0x7F;
	tiny[1] = 'C';
	tiny[2] = 'N';
	tiny[3] = 'T';
	const std::string path = writeTempFile("small.pkg", tiny);

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(!info.valid);
	CHECK(info.error.find("too small") != std::string::npos);

	removeTempFile(path);
}

ORBISLINK_TEST(missing_file)
{
	const PkgInfo info = PkgInspector().inspect("really-missing.pkg");
	CHECK(!info.valid);
	CHECK(!info.error.empty());
}

ORBISLINK_TEST(pkg_without_param_sfo_is_still_valid)
{
	PkgOptions options;
	options.includeSfo = false;
	const std::string path = writeTempFile("nosfo.pkg", buildPkg(options));

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(info.valid);
	CHECK(info.title.empty());
	// The TITLE_ID comes from the header's content id even without PARAM.SFO.
	CHECK_EQ(info.titleId, std::string("CUSA12345"));
	CHECK_EQ(info.displayTitle(), std::string("CUSA12345"));

	removeTempFile(path);
}

ORBISLINK_TEST(out_of_bounds_entry_table)
{
	PkgOptions options;
	auto data = buildPkg(options);
	putBE32(data, 0x10, 100000); // entry_count impossible for this file
	const std::string path = writeTempFile("corrupted.pkg", data);

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(!info.valid);
	CHECK(info.error.find("entry table") != std::string::npos);

	removeTempFile(path);
}

ORBISLINK_TEST(file_over_4gb_is_read_by_offsets)
{
	// Sparse ~5 GB file: checks that sizes use 64 bits and that only the
	// needed offsets are read (the test runs in seconds).
	PkgOptions options;
	const auto data = buildPkg(options);
	const std::string path = ".orbislink-test-big.pkg";
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write(reinterpret_cast<const char *>(data.data()),
			static_cast<std::streamsize>(data.size()));
		const int64_t target = 5ll * 1024 * 1024 * 1024;
		file.seekp(static_cast<std::streamoff>(target - 1), std::ios::beg);
		const char zero = 0;
		file.write(&zero, 1);
	}

	const PkgInfo info = PkgInspector().inspect(path);
	if(info.fileSize < 4ll * 1024 * 1024 * 1024)
	{
		// File system without sparse file support: does not fail the test.
		std::cout << "        (warning: sparse file not created; test skipped)\n";
		removeTempFile(path);
		return;
	}
	CHECK(info.valid);
	CHECK(info.fileSize > 4ll * 1024 * 1024 * 1024);
	CHECK_EQ(info.titleId, std::string("CUSA12345"));

	removeTempFile(path);
}

TEST_MAIN()
