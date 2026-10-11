// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The games page's shelf: a real folder of this PC read and told apart —
// what is shown, what each thing is, where its picture comes from — a
// folder's preview, the cache that keeps what was found, and the dates of
// the console's FTP listings.

#include "orbislink/library/shelf.h"
#include "test_fixtures.h"
#include "test_support.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace orbislink;
using namespace orbislink::library;
using namespace orbislink_test;
namespace fs = std::filesystem;

namespace {

// A folder that goes away with the test.
struct TempFolder
{
	fs::path path;
	explicit TempFolder(const std::string &name) : path(fs::current_path() / (".orbislink-test-" + name))
	{
		fs::remove_all(path);
		fs::create_directories(path);
	}
	~TempFolder() { fs::remove_all(path); }
	std::string file(const std::string &name, const std::vector<uint8_t> &bytes) const
	{
		const fs::path target = path / name;
		fs::create_directories(target.parent_path());
		std::ofstream out(target, std::ios::binary);
		out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		return target.u8string();
	}
	std::string text(const std::string &name, const std::string &content) const
	{
		return file(name, std::vector<uint8_t>(content.begin(), content.end()));
	}
};

const std::vector<uint8_t> kPng = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x01 };

std::vector<uint8_t> package(const std::string &titleId, const std::string &title, const std::string &category)
{
	PkgOptions options;
	options.contentId = "UP0001-" + titleId + "_00-ORBISLINKSHELF01";
	options.sfoEntries = { { "APP_VER", "01.02" }, { "CATEGORY", category }, { "CONTENT_ID", options.contentId },
		{ "TITLE", title }, { "TITLE_ID", titleId }, { "VERSION", "01.00" } };
	if(category == "gp")
		options.contentFlags = 0x00100000;
	return buildPkg(options);
}

// The converter, as far as the shelf asks it: a cue names its track, and a
// disc whose name says "PS2" is one.
DiscProbe fakeProbe()
{
	DiscProbe probe;
	probe.cueTrack = [](const std::string &text) {
		const size_t open = text.find('"');
		const size_t close = text.find('"', open + 1);
		return open == std::string::npos || close == std::string::npos ? std::string()
																	   : text.substr(open + 1, close - open - 1);
	};
	probe.inspect = [](const std::string &name, int64_t, const std::function<bool(uint64_t, uint8_t *, size_t)> &read) {
		DiscId id;
		uint8_t first = 0;
		if(!read(0, &first, 1) || first != 'D')
			return id;
		id.platform = name.find("PS1") != std::string::npos ? "ps1" : "ps2";
		id.serial = "SLUS-20946";
		id.titleId = "SLUS20946";
		id.title = "From The Name";
		return id;
	};
	probe.knownTitle = [](const std::string &, const std::string &titleId) {
		return titleId == "SLUS20946" ? std::string("Neon Drift") : std::string();
	};
	return probe;
}

std::map<std::string, ShelfItem> byName(const std::vector<ShelfItem> &items)
{
	std::map<std::string, ShelfItem> out;
	for(const ShelfItem &item : items)
		out[item.name] = item;
	return out;
}

} // namespace

ORBISLINK_TEST(reads_a_folder_of_this_pc_and_tells_everything_apart)
{
	TempFolder folder("shelf-read");
	folder.file("Demo Game.pkg", package("CUSA00001", "Demo Game", "gd"));
	folder.file("Demo Update.pkg", package("CUSA00001", "Demo Game", "gp"));
	folder.file("Pictured.pkg", package("CUSA00002", "Pictured Game", "gd"));
	folder.file("Pictured.jpg", kPng);
	folder.file("broken.pkg", std::vector<uint8_t>(4096, 0x11));
	folder.text("Neon Drift (USA).cue", "FILE \"Neon Drift (USA).bin\" BINARY\n");
	folder.file("Neon Drift (USA).bin", std::vector<uint8_t>(64ll * 1024 * 1024 / 1024, 'D'));
	folder.file("Orbis Racing.iso", std::vector<uint8_t>(2048, 'D'));
	folder.file("Not A Disc.iso", std::vector<uint8_t>(2048, 'X'));
	folder.text("App/sce_sys/param.json",
		R"({"titleId":"PPSA09999","contentVersion":"01.000","localizedParameters":{"defaultLanguage":"en-US","en-US":{"titleName":"Fake App"}}})");
	folder.file("App/sce_sys/icon0.png", kPng);
	folder.text("Plain folder/readme.txt", "hello");
	folder.file("hello.elf", std::vector<uint8_t>(512, 0x7F));
	folder.file("backup.zip", std::vector<uint8_t>(64, 0x50));
	folder.text("notes.txt", "not for the shelf");
	folder.file("cover.png", kPng);
	folder.text(".hidden.pkg", "x");

	LocalSource source;
	std::vector<ShelfEntry> entries;
	std::string error;
	CHECK(source.list(folder.path.u8string(), &entries, &error));
	const auto items = byName(readShelf(source, entries, fakeProbe()));

	// Text, pictures, hidden files and the cue's track are not shown apart.
	CHECK(!items.count("notes.txt"));
	CHECK(!items.count("cover.png"));
	CHECK(!items.count("Pictured.jpg"));
	CHECK(!items.count(".hidden.pkg"));
	CHECK(!items.count("Neon Drift (USA).bin"));
	CHECK_EQ(items.size(), static_cast<size_t>(11));

	const ShelfItem &game = items.at("Demo Game.pkg");
	CHECK_EQ(game.title, std::string("Demo Game"));
	CHECK_EQ(game.titleId, std::string("CUSA00001"));
	CHECK_EQ(game.group, std::string("games"));
	CHECK_EQ(game.pictureFrom, std::string("metadata"));
	CHECK(!game.picture.empty());
	CHECK_EQ(game.ext, std::string(".pkg"));

	CHECK_EQ(items.at("Demo Update.pkg").group, std::string("extras"));
	// An image named after it beats its own icon.
	CHECK_EQ(items.at("Pictured.pkg").pictureFrom, std::string("folder"));
	CHECK(items.at("Pictured.pkg").pictureFile.find("Pictured.jpg") != std::string::npos);

	CHECK(!items.at("broken.pkg").readable);
	CHECK_EQ(items.at("broken.pkg").group, std::string("other"));

	const ShelfItem &cue = items.at("Neon Drift (USA).cue");
	CHECK_EQ(cue.platform, std::string("ps2"));
	CHECK_EQ(cue.title, std::string("Neon Drift"));
	CHECK_EQ(cue.serial, std::string("SLUS-20946"));
	CHECK(cue.trackPath.find("Neon Drift (USA).bin") != std::string::npos);
	CHECK_EQ(cue.group, std::string("games"));
	CHECK_EQ(items.at("Orbis Racing.iso").group, std::string("games"));
	CHECK_EQ(items.at("Not A Disc.iso").group, std::string("other"));

	const ShelfItem &app = items.at("App");
	CHECK(app.appFolder);
	CHECK_EQ(app.group, std::string("games"));
	CHECK_EQ(app.title, std::string("Fake App"));
	CHECK_EQ(app.platform, std::string("ps5"));
	CHECK_EQ(app.pictureFrom, std::string("metadata"));
	CHECK(app.pictureFile.find("icon0.png") != std::string::npos);

	CHECK(!items.at("Plain folder").appFolder);
	CHECK_EQ(items.at("Plain folder").group, std::string("folders"));
	CHECK_EQ(items.at("hello.elf").group, std::string("payloads"));
	CHECK_EQ(items.at("backup.zip").group, std::string("other"));

	// A folder that is not there says so.
	CHECK(!source.list((folder.path / "missing").u8string(), &entries, &error));
	CHECK(!error.empty());
}

ORBISLINK_TEST(previews_a_folder_with_the_first_pictures_in_it)
{
	TempFolder folder("shelf-preview");
	folder.file("a.pkg", package("CUSA00011", "A", "gd"));
	folder.file("b.pkg", package("CUSA00012", "B", "gd"));
	folder.file("c.zip", std::vector<uint8_t>(32, 1));
	folder.text("sub/readme.txt", "x");

	LocalSource source;
	const FolderPreview preview = previewFolder(source, folder.path.u8string(), fakeProbe(), 3, 16);
	CHECK_EQ(preview.count, 4);
	// The two games, then the folder inside, as a tile of its own.
	CHECK_EQ(preview.pictures.size(), static_cast<size_t>(3));
	CHECK(!preview.pictures[0].picture.empty());
	CHECK(!preview.pictures[1].picture.empty());
	CHECK(preview.pictures[2].directory);
	CHECK_EQ(preview.pictures[2].title, std::string("sub"));

	// Short of room, the folder keeps its place over a game.
	const FolderPreview capped = previewFolder(source, folder.path.u8string(), fakeProbe(), 2, 16);
	CHECK_EQ(capped.pictures.size(), static_cast<size_t>(2));
	CHECK(!capped.pictures[0].directory);
	CHECK(capped.pictures[1].directory);

	// Without folders inside, only games.
	fs::remove_all(folder.path / "sub");
	const FolderPreview games = previewFolder(source, folder.path.u8string(), fakeProbe(), 3, 16);
	CHECK_EQ(games.pictures.size(), static_cast<size_t>(2));
	CHECK(!games.pictures[1].directory);
}

ORBISLINK_TEST(keeps_what_was_found_across_runs)
{
	TempFolder folder("shelf-cache");
	const std::string cacheDir = (folder.path / "cache").u8string();
	ShelfItem item;
	item.path = "/data/pkg/Demo Game.pkg";
	item.name = "Demo Game.pkg";
	item.size = 1234;
	item.modified = 1700000000;
	item.kind = Kind::Package;
	item.group = "games";
	item.title = "Demo Game";
	item.titleId = "CUSA00001";
	item.picture = kPng;
	item.pictureExt = ".png";
	item.pictureFrom = "metadata";
	const std::string key = ShelfCache::keyOf("console", item);
	{
		ShelfCache cache(cacheDir);
		cache.store(key, &item);
		CHECK(item.picture.empty());
		CHECK(fs::exists(fs::u8path(item.pictureFile)));
		// The game's picture serves its other copies.
		CHECK_EQ(cache.pictureFor("cusa00001"), item.pictureFile);
		cache.save();
	}
	ShelfCache again(cacheDir);
	ShelfItem found;
	CHECK(again.find(key, &found));
	CHECK_EQ(found.title, std::string("Demo Game"));
	CHECK(found.kind == Kind::Package);
	CHECK_EQ(found.pictureFile, item.pictureFile);
	CHECK_EQ(again.pictureFor("CUSA00001"), item.pictureFile);
	// A changed file is a new key.
	item.size = 99;
	CHECK(!again.find(ShelfCache::keyOf("console", item), &found));

	FolderPreview preview;
	preview.count = 3;
	ShelfItem picture;
	picture.title = "Pic";
	picture.picture = kPng;
	picture.pictureExt = ".png";
	preview.pictures.push_back(picture);
	again.storePreview("folder|/x|1", &preview);
	again.save();
	ShelfCache third(cacheDir);
	FolderPreview kept;
	CHECK(third.findPreview("folder|/x|1", &kept));
	CHECK_EQ(kept.count, 3);
	CHECK_EQ(kept.pictures.size(), static_cast<size_t>(1));
	CHECK(fs::exists(fs::u8path(kept.pictures[0].pictureFile)));
}

ORBISLINK_TEST(reads_the_dates_of_an_ftp_listing)
{
	// 2026-10-11 12:00 UTC.
	const int64_t now = 1791720000;
	CHECK_EQ(ftpListingTime("Oct 10 20:17", now), static_cast<int64_t>(1791663420));
	// A date ahead of now is last year's.
	CHECK_EQ(ftpListingTime("Dec 24 08:00", now), static_cast<int64_t>(1766563200));
	CHECK_EQ(ftpListingTime("Jan  2  2024", now), static_cast<int64_t>(1704153600));
	CHECK_EQ(ftpListingTime("", now), static_cast<int64_t>(0));
	CHECK_EQ(ftpListingTime("Foo 10 2024", now), static_cast<int64_t>(0));
}

TEST_MAIN()
