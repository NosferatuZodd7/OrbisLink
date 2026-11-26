// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A game downloaded as an archive: its volumes told apart by name and found
// beside each other, and what 7-Zip says about it (as 7-Zip 23 prints it)
// read into what is in it, whether it needs a password and what is wrong.
#include "orbislink/library/archive_set.h"
#include "test_support.h"

#include <filesystem>
#include <fstream>

using namespace orbislink;
using namespace orbislink::library;
namespace fs = std::filesystem;

namespace {

struct TempFolder
{
	fs::path path;
	explicit TempFolder(const std::string &name) : path(fs::current_path() / fs::u8path(".orbislink-test-" + name))
	{
		fs::remove_all(path);
		fs::create_directories(path);
	}
	~TempFolder() { fs::remove_all(path); }
	std::string file(const std::string &name, size_t bytes = 10) const
	{
		const fs::path where = path / fs::u8path(name);
		std::ofstream(where, std::ios::binary) << std::string(bytes, 'x');
		return where.u8string();
	}
};

} // namespace

ORBISLINK_TEST(reads_volume_names)
{
	ArchiveVolume v;
	CHECK(parseArchiveVolume("Game.part1.rar", &v));
	CHECK_EQ(v.base, std::string("Game"));
	CHECK_EQ(v.index, 0);
	CHECK_EQ(v.scheme, std::string("part"));
	CHECK(parseArchiveVolume("Game.PART03.RAR", &v));
	CHECK_EQ(v.index, 2);
	CHECK(parseArchiveVolume("My Game v1.00.7z.002", &v));
	CHECK_EQ(v.base, std::string("My Game v1.00"));
	CHECK_EQ(v.index, 1);
	CHECK_EQ(v.scheme, std::string("split"));
	CHECK_EQ(v.ext, std::string(".7z"));
	CHECK(parseArchiveVolume("Game.zip.001", &v));
	CHECK_EQ(v.ext, std::string(".zip"));
	CHECK(parseArchiveVolume("Game.r00", &v));
	CHECK_EQ(v.index, 1);
	CHECK_EQ(v.scheme, std::string("old"));
	CHECK(parseArchiveVolume("Game.rar", &v));
	CHECK_EQ(v.scheme, std::string("single"));
	CHECK(parseArchiveVolume("Game.zip", &v));
	CHECK(parseArchiveVolume("Game.7z", &v));
	// Not archives, or not their volumes.
	CHECK(!parseArchiveVolume("Game.pkg", nullptr));
	CHECK(!parseArchiveVolume("Game.001", nullptr));
	CHECK(!parseArchiveVolume("Game.iso.001", nullptr));
	CHECK(!parseArchiveVolume("Game.part0.rar", nullptr));
	CHECK(!parseArchiveVolume(".7z", nullptr));
	CHECK(!parseArchiveVolume("Game.7z.000", nullptr));
}

ORBISLINK_TEST(finds_the_volumes_in_order)
{
	TempFolder folder("archive-volumes");
	const std::string first = folder.file("Game.part1.rar", 100);
	folder.file("Game.part3.rar", 50);
	folder.file("Game.part2.rar", 100);
	folder.file("Other.part1.rar", 7);
	const ArchiveVolumes set = findArchiveVolumes(first);
	CHECK_EQ(set.paths.size(), static_cast<size_t>(3));
	CHECK_EQ(set.totalSize, static_cast<int64_t>(250));
	CHECK_EQ(set.missingIndex, -1);
	CHECK(fs::u8path(set.paths[1]).filename().u8string() == "Game.part2.rar");
}

ORBISLINK_TEST(tells_the_volume_missing)
{
	TempFolder folder("archive-missing");
	const std::string first = folder.file("Game.7z.001");
	folder.file("Game.7z.002");
	folder.file("Game.7z.004");
	const ArchiveVolumes set = findArchiveVolumes(first);
	CHECK_EQ(set.missingIndex, 2);
	CHECK_EQ(set.missingName, std::string("Game.7z.003"));

	const std::string rar = folder.file("Old.rar");
	folder.file("Old.r00");
	folder.file("Old.r02");
	const ArchiveVolumes old = findArchiveVolumes(rar);
	CHECK_EQ(old.scheme, std::string("old"));
	CHECK_EQ(old.paths.size(), static_cast<size_t>(3));
	CHECK_EQ(old.missingName, std::string("Old.r01"));

	const std::string parts = folder.file("P.part1.rar");
	folder.file("P.part3.rar");
	CHECK_EQ(findArchiveVolumes(parts).missingName, std::string("P.part2.rar"));
}

ORBISLINK_TEST(one_file_is_one_volume)
{
	TempFolder folder("archive-single");
	const std::string zip = folder.file("Game.zip", 30);
	folder.file("Game.7z", 30);
	const ArchiveVolumes set = findArchiveVolumes(zip);
	CHECK_EQ(set.paths.size(), static_cast<size_t>(1));
	CHECK_EQ(set.totalSize, static_cast<int64_t>(30));
	const std::string rar = folder.file("Alone.rar", 5);
	CHECK_EQ(findArchiveVolumes(rar).scheme, std::string("single"));
}

ORBISLINK_TEST(reads_a_listing)
{
	const std::string output = "Path = Demo Game.pkg\nFolder = -\nSize = 40016311\nPacked Size = 39014160\n"
							   "Encrypted = -\nMethod = v5:m0:LZ\n\nPath = src\nFolder = +\nSize = 0\n\n";
	const ArchiveListing listing = parseSevenZipListing(output, 0);
	CHECK(listing.ok);
	CHECK_EQ(listing.entries.size(), static_cast<size_t>(2));
	CHECK_EQ(listing.entries[0].path, std::string("Demo Game.pkg"));
	CHECK_EQ(listing.entries[0].size, static_cast<int64_t>(40016311));
	CHECK(!listing.entries[0].folder);
	CHECK(listing.entries[1].folder);
	CHECK(!contentsEncrypted(listing));
	CHECK(listing.error.empty());
}

ORBISLINK_TEST(tells_a_password_is_needed)
{
	// Its contents encrypted: listed, each entry marked.
	const ArchiveListing contents = parseSevenZipListing("Path = Demo Game.pkg\r\nSize = 40016311\r\nEncrypted = +\r\n", 0);
	CHECK(contents.ok);
	CHECK(contentsEncrypted(contents));
	// Its names encrypted too: nothing listed.
	const std::string headers = "ERROR: game.7z.001\nCannot open encrypted archive. Wrong password?\n\nERRORS:\nHeaders Error\n";
	const ArchiveListing locked = parseSevenZipListing(headers, 2);
	CHECK(!locked.ok);
	CHECK(locked.needsPassword);
	CHECK(locked.entries.empty());
	CHECK_EQ(locked.error, std::string("The archive's password is wrong or missing."));
	CHECK_EQ(sevenZipProblem("ERROR: Wrong password : Demo Game.pkg\n", 2),
		std::string("The archive's password is wrong or missing."));
}

ORBISLINK_TEST(tells_what_went_wrong)
{
	CHECK_EQ(sevenZipProblem("ERRORS:\nMissing volume : game.part3.rar\n\nERROR: Data Error : Demo Game.pkg\n", 2),
		std::string("A volume of the archive is missing: game.part3.rar"));
	const ArchiveListing missing = parseSevenZipListing("ERRORS:\nMissing volume : game.part3.rar\n", 2);
	CHECK_EQ(missing.missingVolume, std::string("game.part3.rar"));
	CHECK(sevenZipProblem("ERROR: Unsupported Method : Demo Game.pkg\n", 2).find("7-zip.org") != std::string::npos);
	CHECK(parseSevenZipListing("ERROR: Unsupported Method : x\n", 2).unsupported);
	CHECK(sevenZipProblem("ERROR: x.zip\nCannot open the file as archive\n", 2).find("not an archive") != std::string::npos);
	CHECK(sevenZipProblem("ERROR: CRC Failed : Demo Game.pkg\n", 2).find("damaged") != std::string::npos);
	CHECK(sevenZipProblem("E_FAIL\n", 8).find("exit code 8") != std::string::npos);
	CHECK(sevenZipProblem("Everything is Ok\n", 0).empty());
}

TEST_MAIN()
