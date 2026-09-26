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
	const std::string path = writeTempFile("jogo.pkg", buildPkg(options));

	PkgInspector inspector;
	const PkgInfo info = inspector.inspect(path);
	CHECK(info.valid);
	CHECK_EQ(info.title, std::string("Jogo de Teste"));
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
	options.sfoEntries = { { "CATEGORY", "gp" }, { "TITLE", "Jogo de Teste" },
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
	options.sfoEntries = { { "CATEGORY", "ac" }, { "TITLE", "Pacote extra" },
		{ "TITLE_ID", "CUSA12345" } };
	const std::string path = writeTempFile("dlc.pkg", buildPkg(options));

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(info.kind == PkgCategory::Dlc);
	CHECK_EQ(pkgCategoryInstallOrder(info.kind), 3);
	CHECK_EQ(std::string(pkgCategoryLabel(info.kind)), std::string("DLC"));

	removeTempFile(path);
}

ORBISLINK_TEST(rejects_invalid_magic)
{
	PkgOptions options;
	options.validMagic = false;
	const std::string path = writeTempFile("naoepkg.pkg", buildPkg(options));

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(!info.valid);
	CHECK_EQ(info.error, std::string("Not a valid PS4 pkg."));
	CHECK(!PkgInspector::hasPkgMagic(path));

	removeTempFile(path);
}

ORBISLINK_TEST(rejects_too_small_file)
{
	std::vector<uint8_t> tiny(100, 0);
	tiny[0] = 0x7F;
	tiny[1] = 'C';
	tiny[2] = 'N';
	tiny[3] = 'T';
	const std::string path = writeTempFile("pequeno.pkg", tiny);

	const PkgInfo info = PkgInspector().inspect(path);
	CHECK(!info.valid);
	CHECK(info.error.find("too small") != std::string::npos);

	removeTempFile(path);
}

ORBISLINK_TEST(missing_file)
{
	const PkgInfo info = PkgInspector().inspect("nao-existe-mesmo.pkg");
	CHECK(!info.valid);
	CHECK(!info.error.empty());
}

ORBISLINK_TEST(pkg_without_param_sfo_is_still_valid)
{
	PkgOptions options;
	options.includeSfo = false;
	const std::string path = writeTempFile("semsfo.pkg", buildPkg(options));

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
	const std::string path = writeTempFile("corrompido.pkg", data);

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
	const std::string path = ".orbislink-test-grande.pkg";
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
		std::cout << "        (aviso: ficheiro esparso não criado; teste ignorado)\n";
		removeTempFile(path);
		return;
	}
	CHECK(info.valid);
	CHECK(info.fileSize > 4ll * 1024 * 1024 * 1024);
	CHECK_EQ(info.titleId, std::string("CUSA12345"));

	removeTempFile(path);
}

TEST_MAIN()
