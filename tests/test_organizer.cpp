// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Putting a folder's packages in order: the names a pattern gives, the
// install-order numbers, a folder per game, copies of the same package left
// alone, names no system refuses, and the renaming itself on a real folder.

#include "orbislink/library/pkg_organizer.h"
#include "test_fixtures.h"
#include "test_support.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace orbislink;
using namespace orbislink::library;
using namespace orbislink_test;
namespace fs = std::filesystem;

namespace {

struct TempFolder
{
	fs::path path;
	explicit TempFolder(const std::string &name) : path(fs::current_path() / (".orbislink-test-" + name))
	{
		fs::remove_all(path);
		fs::create_directories(path);
	}
	~TempFolder() { fs::remove_all(path); }
};

// A package of `kind` ("gd", "gp", "ac") with what it says about itself.
std::vector<uint8_t> package(const std::string &contentId, const std::string &title, const std::string &kind,
	const std::string &version)
{
	PkgOptions options;
	options.contentId = contentId;
	options.contentType = kind == "ac" ? 0x1B : 0x1A;
	options.contentFlags = kind == "gp" ? 0x00100000 : 0;
	const std::string titleId = contentId.substr(7, 9);
	options.sfoEntries = {
		{ "APP_VER", version },
		{ "CATEGORY", kind },
		{ "CONTENT_ID", contentId },
		{ "TITLE", title },
		{ "TITLE_ID", titleId },
		{ "VERSION", version },
	};
	return buildPkg(options);
}

void write(const fs::path &file, const std::vector<uint8_t> &bytes)
{
	fs::create_directories(file.parent_path());
	std::ofstream out(file, std::ios::binary);
	out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

const OrganizeStep *stepFor(const std::vector<OrganizeStep> &plan, const fs::path &from)
{
	for(const OrganizeStep &step : plan)
		if(fs::u8path(step.from) == from)
			return &step;
	return nullptr;
}

} // namespace

ORBISLINK_TEST(patterns_name_packages_from_what_they_say)
{
	PkgInfo info;
	info.title = "Bloodborne";
	info.titleId = "CUSA00207";
	info.contentId = "EP9000-CUSA00207_00-BLOODBORNE0000EU";
	info.appVersion = "01.09";
	info.kind = PkgCategory::Patch;
	info.requiredFirmware = 0x0450;
	CHECK_EQ(expandPattern(organizePatterns()[0], info), std::string("Bloodborne [CUSA00207] [Update] [v01.09]"));
	CHECK_EQ(expandPattern("{TITLE} ({REGION}) v{VERSION}", info), std::string("Bloodborne (EUR) v01.09"));
	CHECK_EQ(expandPattern("{CONTENT_ID}", info), std::string("EP9000-CUSA00207_00-BLOODBORNE0000EU"));
	CHECK_EQ(expandPattern("{TITLE} FW{FIRMWARE} {TYPE}", info), std::string("Bloodborne FW4.50 Fake"));
	// What a package does not say leaves no empty brackets behind.
	info.appVersion.clear();
	info.version.clear();
	CHECK_EQ(expandPattern(organizePatterns()[0], info), std::string("Bloodborne [CUSA00207] [Update]"));
	CHECK_EQ(expandPattern("{TITLE} ({REGION}) v{VERSION}", info), std::string("Bloodborne (EUR)"));
	CHECK_EQ(regionOf("UP0001-CUSA00001_00-X"), std::string("USA"));
	CHECK_EQ(regionOf("JP0001-CUSA00001_00-X"), std::string("JPN"));
	CHECK_EQ(regionOf(""), std::string());
}

ORBISLINK_TEST(names_every_system_takes)
{
	CHECK_EQ(safeFileName("Marvel's Spider-Man: Miles Morales"), std::string("Marvel's Spider-Man - Miles Morales"));
	CHECK_EQ(safeFileName("What? <Really> \"yes\" | no / \\ *"), std::string("What Really yes no"));
	CHECK_EQ(safeFileName("Ends with dots..."), std::string("Ends with dots"));
	CHECK_EQ(safeFileName("CON"), std::string("CON_"));
	// Cut on a character, never in the middle of one.
	std::string longName;
	for(int i = 0; i < 150; ++i)
		longName += "é"; // two bytes each
	const std::string cut = safeFileName(longName);
	CHECK(cut.size() <= 200);
	CHECK_EQ(cut.size() % 2, size_t(0));
}

ORBISLINK_TEST(plans_install_order_folders_and_copies)
{
	TempFolder dir("organize-plan");
	const fs::path game = dir.path / "bb.pkg";
	const fs::path patch = dir.path / "sub" / "bb-patch.pkg";
	const fs::path dlc = dir.path / "oldhunters.pkg";
	const fs::path copy = dir.path / "copy of bb.pkg";
	write(game, package("EP9000-CUSA00207_00-BLOODBORNE0000EU", "Bloodborne", "gd", "01.00"));
	write(patch, package("EP9000-CUSA00207_00-BLOODBORNE0000EU", "Bloodborne", "gp", "01.09"));
	write(dlc, package("EP9000-CUSA00207_00-BLOODBORNEDLC001", "Bloodborne The Old Hunters", "ac", "01.00"));
	write(copy, package("EP9000-CUSA00207_00-BLOODBORNE0000EU", "Bloodborne", "gd", "01.00"));
	write(dir.path / "notes.txt", { 'h', 'i' });

	OrganizeOptions options;
	options.installOrder = true;
	options.folderPerGame = true;
	const std::vector<OrganizeStep> plan = planOrganize(dir.path.u8string(), options);
	CHECK_EQ(plan.size(), size_t(4)); // the .txt is not a package

	const fs::path folder = dir.path / "Bloodborne (CUSA00207)";
	const OrganizeStep *g = stepFor(plan, game);
	const OrganizeStep *p = stepFor(plan, patch);
	const OrganizeStep *d = stepFor(plan, dlc);
	const OrganizeStep *c = stepFor(plan, copy);
	CHECK(g && p && d && c);
	// One of the two copies of the game is the copy (whichever comes second by name).
	const OrganizeStep *kept = g->duplicate ? c : g;
	const OrganizeStep *other = g->duplicate ? g : c;
	CHECK(!kept->duplicate);
	CHECK(other->duplicate);
	CHECK(!other->changes());
	CHECK_EQ(other->sameAs, kept->from);
	CHECK(fs::u8path(kept->to) == folder / "00 - Bloodborne [CUSA00207] [Game] [v01.00].pkg");
	CHECK(fs::u8path(p->to) == folder / "01 - Bloodborne [CUSA00207] [Update] [v01.09].pkg");
	// The add-on goes in its game's folder, under its own name.
	CHECK(fs::u8path(d->to) == folder / "02 - Bloodborne The Old Hunters [CUSA00207] [DLC] [v01.00].pkg");
}

ORBISLINK_TEST(two_packages_never_get_one_name)
{
	TempFolder dir("organize-clash");
	// Not the same package (another content ID), but the same name.
	write(dir.path / "a.pkg", package("UP0001-CUSA00001_00-HOMEBREWAPP00001", "Homebrew", "gd", "01.00"));
	write(dir.path / "b.pkg", package("UP0001-CUSA00001_00-HOMEBREWAPP00002", "Homebrew", "gd", "01.00"));
	// And a file already called what one of them would be.
	write(dir.path / "Homebrew [CUSA00001] [Game] [v01.00].pkg", { 'x' });
	OrganizeOptions options;
	const std::vector<OrganizeStep> plan = planOrganize(dir.path.u8string(), options);
	std::vector<std::string> targets;
	for(const OrganizeStep &step : plan)
		if(step.changes())
			targets.push_back(fs::u8path(step.to).filename().u8string());
	CHECK_EQ(targets.size(), size_t(2));
	CHECK(targets[0] != targets[1]);
	for(const std::string &name : targets)
		CHECK(name != "Homebrew [CUSA00001] [Game] [v01.00].pkg");
}

ORBISLINK_TEST(applies_the_plan_and_tidies_up)
{
	TempFolder dir("organize-apply");
	const fs::path patch = dir.path / "old" / "deep" / "patch.pkg";
	write(patch, package("EP9000-CUSA00207_00-BLOODBORNE0000EU", "Bloodborne", "gp", "01.09"));
	write(dir.path / "game.pkg", package("EP9000-CUSA00207_00-BLOODBORNE0000EU", "Bloodborne", "gd", "01.00"));
	OrganizeOptions options;
	options.folderPerGame = true;
	const std::vector<OrganizeStep> plan = planOrganize(dir.path.u8string(), options);
	int calls = 0;
	const OrganizeResult result = applyOrganize(dir.path.u8string(), plan, [&calls](int, int) { ++calls; });
	CHECK_EQ(result.moved, 2);
	CHECK(result.failed.empty());
	CHECK_EQ(calls, 2);
	const fs::path folder = dir.path / "Bloodborne (CUSA00207)";
	CHECK(fs::exists(folder / "Bloodborne [CUSA00207] [Game] [v01.00].pkg"));
	CHECK(fs::exists(folder / "Bloodborne [CUSA00207] [Update] [v01.09].pkg"));
	// The folders the update left behind, empty, are gone; the folder
	// organised stays.
	CHECK(!fs::exists(dir.path / "old"));
	CHECK(fs::exists(dir.path));

	// Organised already: nothing changes the second time.
	const std::vector<OrganizeStep> again = planOrganize(dir.path.u8string(), options);
	for(const OrganizeStep &step : again)
		CHECK(!step.changes());
}

TEST_MAIN()
