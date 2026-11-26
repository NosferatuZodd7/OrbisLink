// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A package split into parts (<name>_0.pkg, _1.pkg…) found, checked and
// joined back: the result has to be the original package byte for byte,
// and anything wrong with the parts has to stop the join before a file that
// looks finished is written.
#include "orbislink/pkg/pkg_inspector.h"
#include "orbislink/pkg/pkg_parts.h"
#include "test_fixtures.h"
#include "test_support.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace orbislink;
using namespace orbislink_test;
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
	std::string file(const std::string &name) const { return (path / fs::u8path(name)).u8string(); }
};

// A synthetic package of about `bytes`, its header saying its real size;
// the bytes after the metadata are a pattern, so a part out of place or
// repeated changes the result.
std::vector<uint8_t> package(size_t bytes)
{
	std::vector<uint8_t> pkg = buildPkg(PkgOptions());
	const size_t start = pkg.size();
	pkg.resize(std::max(bytes, start + 16));
	for(size_t i = start; i < pkg.size(); ++i)
		pkg[i] = static_cast<uint8_t>((i * 2654435761u) >> 13);
	putBE64(pkg, 0x430, static_cast<uint64_t>(pkg.size()));
	return pkg;
}

void writeFile(const std::string &path, const uint8_t *data, size_t size)
{
	std::ofstream out(fs::u8path(path), std::ios::binary | std::ios::trunc);
	out.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
}

std::vector<uint8_t> readFile(const std::string &path)
{
	std::ifstream in(fs::u8path(path), std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Cuts `pkg` into parts of `sizes` (the last part takes the rest), named
// <base>_<n>.pkg in `folder`; returns their paths.
std::vector<std::string> split(const TempFolder &folder, const std::string &base, const std::vector<uint8_t> &pkg,
	const std::vector<size_t> &sizes)
{
	std::vector<std::string> paths;
	size_t offset = 0;
	for(size_t n = 0; offset < pkg.size(); ++n)
	{
		const size_t size = n < sizes.size() ? std::min(sizes[n], pkg.size() - offset) : pkg.size() - offset;
		paths.push_back(folder.file(base + "_" + std::to_string(n) + ".pkg"));
		writeFile(paths.back(), pkg.data() + offset, size);
		offset += size;
	}
	return paths;
}

const std::string kBase = "UP0001-CUSA12345_00-ORBISLINKTEST001";

} // namespace

ORBISLINK_TEST(reads_part_names)
{
	std::string base;
	int index = -1;
	CHECK(parsePkgPartName("Game_0.pkg", &base, &index));
	CHECK_EQ(base, std::string("Game"));
	CHECK_EQ(index, 0);
	CHECK(parsePkgPartName("UP0001-CUSA12345_00-X_12.PKG", &base, &index));
	CHECK_EQ(base, std::string("UP0001-CUSA12345_00-X"));
	CHECK_EQ(index, 12);
	CHECK(parsePkgPartName("Jogo de Teste – ação [v1]_3.pkg", &base, &index));
	CHECK_EQ(index, 3);
	CHECK(!parsePkgPartName("Game.pkg", &base, &index));
	CHECK(!parsePkgPartName("Game_1.part.pkg", &base, &index));
	CHECK(!parsePkgPartName("Game_x.pkg", &base, &index));
	CHECK(!parsePkgPartName("Game_-1.pkg", &base, &index));
	CHECK(!parsePkgPartName("Game_12345.pkg", &base, &index));
	CHECK(!parsePkgPartName("_0.pkg", &base, &index));
	CHECK(!parsePkgPartName("Game_0.iso", &base, &index));
}

ORBISLINK_TEST(joins_parts_back_into_the_same_bytes)
{
	TempFolder folder("parts-valid");
	const std::vector<uint8_t> pkg = package(3 * 1024 * 1024 + 4567);
	const auto parts = split(folder, kBase, pkg, { 600000, 900000, 700000 });
	CHECK_EQ(parts.size(), static_cast<size_t>(4));

	const PkgPartSet set = planPkgParts(parts[0]);
	CHECK(set.ok);
	CHECK_EQ(set.parts.size(), static_cast<size_t>(4));
	CHECK_EQ(set.declaredSize, static_cast<uint64_t>(pkg.size()));
	CHECK_EQ(set.totalSize, static_cast<int64_t>(pkg.size()));
	CHECK_EQ(joinedPkgName(set), kBase + ".pkg");

	uint64_t last = 0;
	uint64_t reportedTotal = 0;
	const std::string output = folder.file(joinedPkgName(set));
	std::string error;
	CHECK(joinPkgParts(set, output, [&](uint64_t done, uint64_t total) {
		CHECK(done >= last);
		last = done;
		reportedTotal = total;
		return true;
	}, &error));
	CHECK_EQ(error, std::string());
	CHECK_EQ(last, static_cast<uint64_t>(pkg.size()));
	CHECK_EQ(reportedTotal, static_cast<uint64_t>(pkg.size()));
	CHECK(readFile(output) == pkg);
	CHECK(!fs::exists(fs::u8path(output + ".part")));
	// The joined package reads as one, its size as its header says.
	const PkgInfo info = PkgInspector().inspect(output);
	CHECK(info.valid);
	CHECK_EQ(info.declaredSize, static_cast<uint64_t>(info.fileSize));
	// The parts are left as they were.
	for(const std::string &part : parts)
		CHECK(fs::exists(fs::u8path(part)));
}

ORBISLINK_TEST(orders_parts_by_number_past_nine)
{
	TempFolder folder("parts-many");
	const std::vector<uint8_t> pkg = package(1200000);
	std::vector<size_t> sizes(24, 50000);
	sizes[0] = 0x3000;
	const auto parts = split(folder, kBase, pkg, sizes);
	CHECK(parts.size() > 11);
	const PkgPartSet set = planPkgParts(parts[0]);
	CHECK(set.ok);
	CHECK_EQ(set.parts.size(), parts.size());
	for(size_t i = 0; i < parts.size(); ++i)
		CHECK_EQ(fs::u8path(set.parts[i]).filename().u8string(), kBase + "_" + std::to_string(i) + ".pkg");
	const std::string output = folder.file("out.pkg");
	std::string error;
	CHECK(joinPkgParts(set, output, nullptr, &error));
	CHECK(readFile(output) == pkg);
}

ORBISLINK_TEST(takes_parts_of_very_different_sizes_and_upper_case_extensions)
{
	TempFolder folder("parts-sizes");
	const std::vector<uint8_t> pkg = package(300000);
	const auto parts = split(folder, kBase, pkg, { 0x2400, 1, 64 });
	fs::rename(fs::u8path(parts[2]), fs::u8path(folder.file(kBase + "_2.PKG")));
	const PkgPartSet set = planPkgParts(parts[0]);
	CHECK(set.ok);
	CHECK_EQ(set.sizes[1], static_cast<int64_t>(1));
	const std::string output = folder.file("out.pkg");
	std::string error;
	CHECK(joinPkgParts(set, output, nullptr, &error));
	CHECK(readFile(output) == pkg);
}

ORBISLINK_TEST(handles_spaces_accents_and_brackets)
{
	TempFolder folder("parts pasta com espaços & [acentuação]");
	const std::string base = "Jogo de Teste – ação & [v1.00]";
	const std::vector<uint8_t> pkg = package(200000);
	const auto parts = split(folder, base, pkg, { 50000, 50000 });
	const PkgPartSet set = planPkgParts(parts[0]);
	CHECK(set.ok);
	const std::string output = folder.file(joinedPkgName(set));
	std::string error;
	CHECK(joinPkgParts(set, output, nullptr, &error));
	CHECK(readFile(output) == pkg);
}

ORBISLINK_TEST(a_missing_part_stops_it)
{
	TempFolder folder("parts-missing");
	const std::vector<uint8_t> pkg = package(400000);
	const auto parts = split(folder, kBase, pkg, { 100000, 100000, 100000 });
	fs::remove(fs::u8path(parts[2]));
	const PkgPartSet set = planPkgParts(parts[0]);
	CHECK(!set.ok);
	CHECK_EQ(set.missingIndex, 2);
	const std::string output = folder.file("out.pkg");
	std::string error;
	CHECK(!joinPkgParts(set, output, nullptr, &error));
	CHECK(!error.empty());
	CHECK(!fs::exists(fs::u8path(output)));
	CHECK(!fs::exists(fs::u8path(output + ".part")));
}

ORBISLINK_TEST(a_last_part_missing_is_seen_by_the_size)
{
	TempFolder folder("parts-last-missing");
	const std::vector<uint8_t> pkg = package(400000);
	const auto parts = split(folder, kBase, pkg, { 100000, 100000, 100000 });
	fs::remove(fs::u8path(parts.back()));
	const PkgPartSet set = planPkgParts(parts[0]);
	CHECK(!set.ok);
	CHECK_EQ(set.missingIndex, static_cast<int>(parts.size() - 1));
}

ORBISLINK_TEST(a_part_named_twice_stops_it)
{
	TempFolder folder("parts-duplicate");
	const std::vector<uint8_t> pkg = package(400000);
	const auto parts = split(folder, kBase, pkg, { 100000, 100000, 100000 });
	fs::copy_file(fs::u8path(parts[1]), fs::u8path(folder.file(kBase + "_01.pkg")));
	const PkgPartSet set = planPkgParts(parts[0]);
	CHECK(!set.ok);
	CHECK_EQ(set.duplicateIndex, 1);
}

ORBISLINK_TEST(a_part_cut_short_or_too_long_stops_it)
{
	TempFolder folder("parts-sizes-wrong");
	const std::vector<uint8_t> pkg = package(400000);
	auto parts = split(folder, kBase, pkg, { 100000, 100000, 100000 });
	fs::resize_file(fs::u8path(parts[1]), 50000);
	PkgPartSet set = planPkgParts(parts[0]);
	CHECK(!set.ok);
	CHECK(set.error.find("smaller") != std::string::npos);

	fs::resize_file(fs::u8path(parts[1]), 100001);
	set = planPkgParts(parts[0]);
	CHECK(!set.ok);
	CHECK(set.error.find("bigger") != std::string::npos);
}

ORBISLINK_TEST(a_part_that_is_not_its_own_is_left_out_by_name)
{
	TempFolder folder("parts-other-names");
	const std::vector<uint8_t> pkg = package(300000);
	const auto parts = split(folder, kBase, pkg, { 100000, 100000 });
	// Another package's parts, and files that are not parts, beside them.
	split(folder, "UP0001-CUSA99999_00-SOMETHINGELSE000", package(120000), { 60000 });
	writeFile(folder.file(kBase + "_1.part.pkg"), pkg.data(), 10);
	writeFile(folder.file("notes.txt"), pkg.data(), 10);
	const PkgPartSet set = planPkgParts(parts[0]);
	CHECK(set.ok);
	CHECK_EQ(set.parts.size(), parts.size());
}

ORBISLINK_TEST(a_damaged_first_part_or_a_lone_package_is_not_joined)
{
	TempFolder folder("parts-damaged");
	std::vector<uint8_t> pkg = package(300000);
	auto parts = split(folder, kBase, pkg, { 100000, 100000 });
	writeFile(parts[0], reinterpret_cast<const uint8_t *>("\0\0\0\0"), 4);
	CHECK(!planPkgParts(parts[0]).ok);
	CHECK(!planPkgParts(parts[1]).ok); // not a first part

	TempFolder lone("parts-lone");
	const std::string single = lone.file(kBase + "_0.pkg");
	writeFile(single, pkg.data(), pkg.size());
	const PkgPartSet set = planPkgParts(single);
	CHECK(!set.ok);
}

ORBISLINK_TEST(a_later_part_starting_like_a_header_is_still_a_part)
{
	TempFolder folder("parts-magic-inside");
	std::vector<uint8_t> pkg = package(300000);
	// The bytes where part 1 begins happen to read "\x7FCNT".
	pkg[100000] = 0x7F;
	pkg[100001] = 'C';
	pkg[100002] = 'N';
	pkg[100003] = 'T';
	const auto parts = split(folder, kBase, pkg, { 100000, 100000 });
	const PkgPartSet set = planPkgParts(parts[0]);
	CHECK(set.ok);
	const std::string output = folder.file("out.pkg");
	std::string error;
	CHECK(joinPkgParts(set, output, nullptr, &error));
	CHECK(readFile(output) == pkg);
}

ORBISLINK_TEST(an_existing_file_is_never_overwritten)
{
	TempFolder folder("parts-existing");
	const std::vector<uint8_t> pkg = package(300000);
	const auto parts = split(folder, kBase, pkg, { 100000, 100000 });
	const PkgPartSet set = planPkgParts(parts[0]);
	const std::string output = folder.file(joinedPkgName(set));
	const std::string kept = "what was there";
	writeFile(output, reinterpret_cast<const uint8_t *>(kept.data()), kept.size());
	std::string error;
	CHECK(!joinPkgParts(set, output, nullptr, &error));
	CHECK(!error.empty());
	const std::vector<uint8_t> after = readFile(output);
	CHECK(std::string(after.begin(), after.end()) == kept);
}

ORBISLINK_TEST(cancelling_leaves_nothing_behind)
{
	TempFolder folder("parts-cancel");
	const std::vector<uint8_t> pkg = package(12 * 1024 * 1024);
	const auto parts = split(folder, kBase, pkg, { 5 * 1024 * 1024, 5 * 1024 * 1024 });
	const PkgPartSet set = planPkgParts(parts[0]);
	CHECK(set.ok);
	const std::string output = folder.file("out.pkg");
	std::string error;
	bool cancelled = false;
	CHECK(!joinPkgParts(set, output, [](uint64_t done, uint64_t) { return done < (6u << 20); }, &error, &cancelled));
	CHECK(cancelled);
	CHECK(!fs::exists(fs::u8path(output)));
	CHECK(!fs::exists(fs::u8path(output + ".part")));
}

ORBISLINK_TEST(a_part_changed_after_the_check_stops_it_cleanly)
{
	TempFolder folder("parts-changed");
	const std::vector<uint8_t> pkg = package(400000);
	const auto parts = split(folder, kBase, pkg, { 100000, 100000, 100000 });
	const PkgPartSet set = planPkgParts(parts[0]);
	CHECK(set.ok);
	// Gone (or unreadable) between the check and the join.
	fs::remove(fs::u8path(parts[2]));
	const std::string output = folder.file("out.pkg");
	std::string error;
	CHECK(!joinPkgParts(set, output, nullptr, &error));
	CHECK(!error.empty());
	CHECK(!fs::exists(fs::u8path(output)));
	CHECK(!fs::exists(fs::u8path(output + ".part")));
}

// A PS5 package as a download comes: an image after its "\x7FFIH" header,
// then the metadata container the header points at (0x58).
std::vector<uint8_t> ps5Package(size_t imageBytes, size_t *containerOffset)
{
	std::vector<uint8_t> pkg(0x1000, 0);
	pkg[0] = 0x7F;
	pkg[1] = 'F';
	pkg[2] = 'I';
	pkg[3] = 'H';
	pkg.resize(0x1000 + imageBytes);
	for(size_t i = 0x1000; i < pkg.size(); ++i)
		pkg[i] = static_cast<uint8_t>((i * 40503u) >> 7);
	*containerOffset = pkg.size();
	for(size_t i = 0; i < 8; ++i)
		pkg[0x58 + i] = static_cast<uint8_t>((static_cast<uint64_t>(*containerOffset) >> (8 * i)) & 0xFF);
	PkgOptions options;
	options.contentId = "UP9999-PPSA77777_00-ORBISLINKPS5SPLT";
	options.includeSfo = false;
	options.paramJson = R"({"titleId":"PPSA77777","contentId":"UP9999-PPSA77777_00-ORBISLINKPS5SPLT",)"
						R"("contentVersion":"01.000.000","localizedParameters":{"defaultLanguage":"en-US",)"
						R"("en-US":{"titleName":"PS5 Split Demo"}}})";
	const std::vector<uint8_t> container = buildPkg(options);
	pkg.insert(pkg.end(), container.begin(), container.end());
	return pkg;
}

ORBISLINK_TEST(reads_the_container_part_name)
{
	std::string base;
	int index = -1;
	CHECK(parsePkgPartName("UP9999-PPSA77777_00-X_sc.pkg", &base, &index));
	CHECK_EQ(base, std::string("UP9999-PPSA77777_00-X"));
	CHECK_EQ(index, kPkgContainerPart);
	CHECK(parsePkgPartName("Game_SC.PKG", &base, &index));
	CHECK_EQ(index, kPkgContainerPart);
	CHECK(!parsePkgPartName("Game_scx.pkg", &base, &index));
}

ORBISLINK_TEST(finds_a_ps5_package_in_parts_with_its_container_last)
{
	TempFolder folder("parts-ps5");
	size_t containerOffset = 0;
	const std::vector<uint8_t> pkg = ps5Package(400000, &containerOffset);
	const std::string base = "UP9999-PPSA77777_00-ORBISLINKPS5SPLT";
	writeFile(folder.file(base + "_0.pkg"), pkg.data(), 150000);
	writeFile(folder.file(base + "_1.pkg"), pkg.data() + 150000, containerOffset - 150000);
	writeFile(folder.file(base + "_sc.pkg"), pkg.data() + containerOffset, pkg.size() - containerOffset);

	const PkgPartSet set = planPkgParts(folder.file(base + "_0.pkg"));
	CHECK(set.ok);
	CHECK(set.ps5);
	CHECK_EQ(set.parts.size(), static_cast<size_t>(3));
	CHECK_EQ(fs::u8path(set.parts.back()).filename().u8string(), base + "_sc.pkg");
	CHECK_EQ(set.totalSize, static_cast<int64_t>(pkg.size()));
	CHECK_EQ(set.contentId, std::string("UP9999-PPSA77777_00-ORBISLINKPS5SPLT"));
	CHECK_EQ(set.title, std::string("PS5 Split Demo"));
	// Read across its parts it is the package; its first part alone is not.
	const PkgInfo whole = inspectPkgParts(set);
	CHECK(whole.valid);
	CHECK(whole.ps5);
	CHECK(!PkgInspector().inspect(set.parts.front()).valid);
	// Joined by Packwright, not here: nothing is written.
	std::string error;
	CHECK(!joinPkgParts(set, folder.file("out.pkg"), nullptr, &error));
	CHECK(error.find("Packwright") != std::string::npos);
	CHECK(!fs::exists(fs::u8path(folder.file("out.pkg"))));

	// A numbered part missing before the container is still seen.
	fs::remove(fs::u8path(folder.file(base + "_1.pkg")));
	const PkgPartSet missing = planPkgParts(folder.file(base + "_0.pkg"));
	CHECK(!missing.ok);
	CHECK_EQ(missing.missingIndex, 1);
}

ORBISLINK_TEST(a_container_part_beside_a_ps4_package_is_not_guessed)
{
	TempFolder folder("parts-ps4-sc");
	const std::vector<uint8_t> pkg = package(300000);
	const auto parts = split(folder, kBase, pkg, { 100000, 100000 });
	writeFile(folder.file(kBase + "_sc.pkg"), pkg.data(), 1000);
	const PkgPartSet set = planPkgParts(parts[0]);
	CHECK(!set.ok);
	CHECK(set.error.find("_sc") != std::string::npos);
}

TEST_MAIN()
