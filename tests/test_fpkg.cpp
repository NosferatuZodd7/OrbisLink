// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The fake PKG builder. The expected digest below is of a package that came
// out byte for byte the same as LibOrbisPkg's PkgTool for the same files
// (volume_ts "2020-01-01 00:00:00", c_date "2020-01-01"); if a change moves
// it, compare with PkgTool again before updating it.
#include "orbislink/fpkg/classic_converter.h"
#include "orbislink/fpkg/classics_assets.h"
#include "orbislink/fpkg/disc_scanner.h"
#include "orbislink/fpkg/param_sfo.h"
#include "orbislink/fpkg/pkg_builder.h"
#include "test_support.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace orbislink::fpkg;
using namespace orbislink_test;

namespace {

namespace fs = std::filesystem;

const char *kContentId = "UP9000-SLUS20946_00-SLUS209460000001";

Bytes pattern(size_t size, uint32_t seed)
{
	Bytes out(size);
	uint32_t x = seed;
	for(auto &b : out)
	{
		x = x * 1664525u + 1013904223u;
		b = static_cast<uint8_t>(x >> 24);
	}
	return out;
}

void writeFile(const fs::path &path, const Bytes &data)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(data.data()),
		static_cast<std::streamsize>(data.size()));
}

std::string hex(const Digest &d)
{
	static const char *digits = "0123456789abcdef";
	std::string s;
	for(uint8_t b : d)
	{
		s += digits[b >> 4];
		s += digits[b & 15];
	}
	return s;
}

Digest fileSha(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	Sha256 hash;
	Bytes buffer(1 << 16);
	while(in)
	{
		in.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
		hash.update(buffer.data(), static_cast<size_t>(in.gcount()));
	}
	return hash.finish();
}

// The project: an emulator-like layout with a disc image of 3 MiB and a bit.
fs::path makeProject()
{
	const char *keep = std::getenv("ORBISLINK_FPKG_FIXTURE");
	const fs::path dir = keep ? fs::path(keep) : fs::temp_directory_path() / "orbislink-test-fpkg";
	fs::remove_all(dir);
	ParamSfo sfo;
	sfo.setInteger("APP_TYPE", 1);
	sfo.setString("APP_VER", "01.00", 8);
	sfo.setInteger("ATTRIBUTE", 0);
	sfo.setString("CATEGORY", "gd", 4);
	sfo.setString("CONTENT_ID", kContentId, 48);
	sfo.setString("FORMAT", "obs", 4);
	sfo.setString("TITLE", "Test Game", 128);
	sfo.setString("TITLE_ID", "SLUS20946", 12);
	sfo.setString("VERSION", "01.00", 8);
	sfo.setInteger("SYSTEM_VER", 0);
	writeFile(dir / "proj/sce_sys/param.sfo", sfo.serialize());
	writeFile(dir / "proj/sce_sys/icon0.png", pattern(5000, 1));
	writeFile(dir / "proj/eboot.bin", pattern(70000, 2));
	writeFile(dir / "proj/config-emu-ps4.txt", Bytes({'-', '-', 'x', '\n'}));
	writeFile(dir / "proj/lua_include/a.lua", pattern(700, 3));
	writeFile(dir / "proj/image/disc01.iso", pattern(3 * 1024 * 1024 + 12345, 4));
	return dir;
}

void cleanUp(const fs::path &dir)
{
	// Kept when asked for, to compare with other tools.
	if(!std::getenv("ORBISLINK_FPKG_FIXTURE"))
		fs::remove_all(dir);
}

PkgRequest makeRequest(const fs::path &dir)
{
	PkgRequest request;
	request.contentId = kContentId;
	request.volumeTime = 1577836800; // 2020-01-01 00:00:00 UTC
	request.creationDate = "20200101";
	for(const char *f : {"config-emu-ps4.txt", "eboot.bin", "lua_include/a.lua", "sce_sys/icon0.png",
			"sce_sys/param.sfo", "image/disc01.iso"})
	{
		PkgSource source;
		source.targetPath = f;
		source.sourcePath = (dir / "proj" / f).u8string();
		request.files.push_back(source);
	}
	return request;
}

// A minimal ISO 9660 disc with SYSTEM.CNF in its root, in 2048-byte
// sectors or as a raw 2352-byte mode 2 image.
Bytes makeDisc(const std::string &systemCnf, bool raw, const char *systemId = "PLAYSTATION")
{
	std::vector<Bytes> sectors(20, Bytes(2048, 0));
	Bytes &pvd = sectors[16];
	pvd[0] = 1;
	std::memcpy(pvd.data() + 1, "CD001", 5);
	pvd[6] = 1;
	std::memset(pvd.data() + 8, ' ', 32);
	std::memcpy(pvd.data() + 8, systemId, std::strlen(systemId));
	auto record = [](uint8_t *at, uint32_t extent, uint32_t size, const std::string &name, bool dir) {
		const uint8_t len = static_cast<uint8_t>(33 + name.size() + (name.size() % 2 == 0 ? 1 : 0));
		at[0] = len;
		for(int i = 0; i < 4; ++i)
		{
			at[2 + i] = static_cast<uint8_t>(extent >> (8 * i));
			at[10 + i] = static_cast<uint8_t>(size >> (8 * i));
		}
		at[25] = dir ? 2 : 0;
		at[32] = static_cast<uint8_t>(name.size());
		std::memcpy(at + 33, name.data(), name.size());
		return len;
	};
	record(pvd.data() + 156, 18, 2048, std::string(1, '\0'), true);
	sectors[17][0] = 255;
	std::memcpy(sectors[17].data() + 1, "CD001", 5);
	uint8_t *dir = sectors[18].data();
	size_t at = 0;
	at += record(dir + at, 18, 2048, std::string(1, '\0'), true);
	at += record(dir + at, 18, 2048, std::string(1, '\1'), true);
	if(!systemCnf.empty())
	{
		record(dir + at, 19, static_cast<uint32_t>(systemCnf.size()), "SYSTEM.CNF;1", false);
		std::memcpy(sectors[19].data(), systemCnf.data(), systemCnf.size());
	}
	Bytes out;
	for(const Bytes &s : sectors)
	{
		if(raw)
		{
			Bytes header(24, 0);
			header[15] = 2; // mode 2
			out.insert(out.end(), header.begin(), header.end());
			out.insert(out.end(), s.begin(), s.end());
			out.insert(out.end(), 280, 0);
		}
		else
			out.insert(out.end(), s.begin(), s.end());
	}
	return out;
}

} // namespace

ORBISLINK_TEST(sfo_round_trips_and_sorts)
{
	ParamSfo sfo;
	sfo.setString("TITLE", "Jogo com acentuação", 128);
	sfo.setInteger("APP_TYPE", 1);
	const Bytes bytes = sfo.serialize();
	ParamSfo back;
	CHECK(back.parse(bytes));
	CHECK(back.find("TITLE") != nullptr);
	CHECK_EQ(back.find("TITLE")->text, std::string("Jogo com acentuação"));
	CHECK_EQ(back.find("APP_TYPE")->number, 1);
	CHECK_EQ(back.serialize().size(), sfo.fileSize());
}

ORBISLINK_TEST(sfo_cuts_long_titles_on_a_character)
{
	ParamSfo sfo;
	sfo.setString("TITLE", std::string(10, 'a') + "ção", 12);
	CHECK_EQ(sfo.find("TITLE")->text, std::string("aaaaaaaaaa"));
}

ORBISLINK_TEST(builds_the_same_package_as_pkgtool)
{
	const fs::path dir = makeProject();
	const fs::path out = dir / "out.pkg";
	std::string error;
	uint64_t lastImage = 0;
	const bool ok = buildFakePkg(makeRequest(dir), out.u8string(),
		[&](const std::string &stage, uint64_t done, uint64_t) {
			if(stage == "image")
				lastImage = done;
			return true;
		},
		&error);
	CHECK_EQ(error, std::string());
	CHECK(ok);
	CHECK(lastImage > 3u * 1024 * 1024);
	CHECK_EQ(fs::file_size(out), uintmax_t(6619136));
	CHECK_EQ(hex(fileSha(out)), std::string(EXPECTED_SHA));
	cleanUp(dir);
}

ORBISLINK_TEST(cancelling_leaves_no_file)
{
	const fs::path dir = makeProject();
	const fs::path out = dir / "out.pkg";
	std::string error;
	const bool ok = buildFakePkg(makeRequest(dir), out.u8string(),
		[](const std::string &stage, uint64_t, uint64_t) { return stage != "image"; }, &error);
	CHECK(!ok);
	CHECK_EQ(error, std::string("cancelled"));
	CHECK(!fs::exists(out));
	cleanUp(dir);
}

ORBISLINK_TEST(refuses_a_package_without_param_sfo)
{
	const fs::path dir = makeProject();
	PkgRequest request = makeRequest(dir);
	request.files.erase(request.files.begin() + 4);
	std::string error;
	CHECK(!buildFakePkg(request, (dir / "out.pkg").u8string(), {}, &error));
	CHECK(error.find("param.sfo") != std::string::npos);
	cleanUp(dir);
}

ORBISLINK_TEST(serials_and_titles)
{
	CHECK_EQ(normaliseSerial("SLUS_209.46;1"), std::string("SLUS-20946"));
	CHECK_EQ(normaliseSerial("SCES-50490"), std::string("SCES-50490"));
	CHECK_EQ(normaliseSerial("readme.txt"), std::string());
	CHECK_EQ(regionOfSerial("SLES-12345"), std::string("Europe"));
	CHECK_EQ(regionOfSerial("SLPM-12345"), std::string("Japan"));
	int disc = 0;
	CHECK_EQ(titleFromFileName("Final_Game (USA) (Disc 2) [v1.1].bin", &disc), std::string("Final Game"));
	CHECK_EQ(disc, 2);
	CHECK_EQ(titleFromFileName("SLUS_209.05.Disney-Pixar The Incredibles.iso"), std::string("Disney-Pixar The Incredibles"));
	CHECK_EQ(titleFromFileName("SCES-50490 Some Game.iso"), std::string("Some Game"));
}

ORBISLINK_TEST(scans_ps1_and_ps2_discs)
{
	const fs::path dir = fs::temp_directory_path() / "orbislink-test-discs";
	fs::remove_all(dir);
	writeFile(dir / "Some PS2 Game (USA).iso",
		makeDisc("BOOT2 = cdrom0:\\SLUS_209.46;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n", false));
	writeFile(dir / "psx/A PS1 Game (Europe).bin", makeDisc("BOOT = cdrom:\\SCES_014.20;1\r\nTCB = 4\r\n", true));
	const std::string cue = "FILE \"A PS1 Game (Europe).bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";
	writeFile(dir / "psx/A PS1 Game (Europe).cue", Bytes(cue.begin(), cue.end()));
	writeFile(dir / "notes.iso", Bytes(40000, 7));

	const auto discs = scanFolder(dir.u8string());
	CHECK_EQ(discs.size(), size_t(2));
	CHECK_EQ(discs[0].title, std::string("A PS1 Game"));
	CHECK_EQ(discs[0].platform, std::string("ps1"));
	CHECK_EQ(discs[0].serial, std::string("SCES-01420"));
	CHECK_EQ(discs[0].region, std::string("Europe"));
	CHECK_EQ(discs[0].format, std::string("bin"));
	CHECK(discs[0].listedPath.find(".cue") != std::string::npos);
	CHECK_EQ(discs[1].title, std::string("Some PS2 Game"));
	CHECK_EQ(discs[1].platform, std::string("ps2"));
	CHECK_EQ(discs[1].titleId, std::string("SLUS20946"));
	CHECK_EQ(discs[1].region, std::string("USA"));
	fs::remove_all(dir);
}

namespace {

// The emulator files as they are after the download (emus/, lua_include/,
// the title lists): stand-ins, not Sony's files.
fs::path makeEmulatorBundle()
{
	const fs::path dir = fs::temp_directory_path() / "orbislink-test-emus";
	fs::remove_all(dir);
	ParamSfo sfo;
	sfo.setInteger("APP_TYPE", 1);
	sfo.setString("APP_VER", "01.00", 8);
	sfo.setString("CATEGORY", "gd", 4);
	sfo.setString("CONTENT_ID", "UP9000-CRST00001_00-TEST123450000001", 48);
	sfo.setInteger("REMOTE_PLAY_KEY_ASSIGN", 1);
	sfo.setString("SERVICE_ID_ADDCONT_ADD_1", "", 20);
	sfo.setString("TITLE", "Something Else", 128);
	sfo.setString("TITLE_ID", "CRST00001", 12);
	for(const char *emu : {"Jak v2", "Rogue v1"})
	{
		writeFile(dir / "emus" / emu / "eboot.bin", pattern(1000, 5));
		writeFile(dir / "emus" / emu / "ps2-emu-compiler.self", pattern(2000, 6));
		writeFile(dir / "emus" / emu / "sce_sys/param.sfo", sfo.serialize());
		writeFile(dir / "emus" / emu / "sce_sys/icon0.png", pattern(300, 8));
		writeFile(dir / "emus" / emu / "sce_sys/pic1.png", pattern(400, 13));
		writeFile(dir / "emus" / emu / "docs/revision.h", pattern(30, 14));
		writeFile(dir / "emus" / emu / "lua_include/common.lua", pattern(40, 15));
		writeFile(dir / "emus" / emu / "lua_include/own.lua", pattern(20, 16));
		const std::string cfg = "--host-audio=1\r\n--ps2-title-id=SLES-00000\r\n--gs-uprender=2x2\r\n";
		writeFile(dir / "emus" / emu / "config-emu-ps4.txt", Bytes(cfg.begin(), cfg.end()));
	}
	writeFile(dir / "emus/ps1hd/eboot.bin", pattern(900, 9));
	writeFile(dir / "emus/ps1hd/sce_sys/icon0.png", pattern(300, 10));
	writeFile(dir / "emus/ps1hd/sce_sys/param.sfo", pattern(100, 17));
	writeFile(dir / "emus/ps1hd/config-title.txt", Bytes(5, 'x'));
	writeFile(dir / "emus/psphd/eboot.bin", pattern(900, 11));
	writeFile(dir / "lua_include/common.lua", pattern(50, 12));
	const std::string ids = "SLUS20946;\"Real Title, A\"\r\nSCES01420;Other\r\n";
	writeFile(dir / "ps2ids.txt", Bytes(ids.begin(), ids.end()));
	const std::string ps1 = "SCES-01420;Old Game From The List\r\n";
	writeFile(dir / "ps1ids.txt", Bytes(ps1.begin(), ps1.end()));
	return dir;
}

std::vector<std::string> targets(const PkgRequest &r)
{
	std::vector<std::string> out;
	for(const auto &f : r.files)
		out.push_back(f.targetPath);
	return out;
}

const PkgSource *source(const PkgRequest &r, const std::string &target)
{
	for(const auto &f : r.files)
		if(f.targetPath == target)
			return &f;
	return nullptr;
}

std::string text(const PkgSource *s)
{
	return s ? std::string(s->data.begin(), s->data.end()) : std::string();
}

} // namespace

ORBISLINK_TEST(keeps_only_the_emulator_files_of_the_download)
{
	// easy-ps2-fpkg's MapAssetPath, plus the PS1 title list.
	const std::string top = "PS Classics fPKG Builder v1/";
	CHECK_EQ(classicsAssetPath(top + "Tools/PS4/emus/Jak v2/eboot.bin"), std::string("emus/Jak v2/eboot.bin"));
	CHECK_EQ(classicsAssetPath(top + "Tools/PS4/lua_include/utils.lua"), std::string("lua_include/utils.lua"));
	CHECK_EQ(classicsAssetPath(top + "Tools/PS4/ps2-configs/widescreen.dat"), std::string("ps2-configs/widescreen.dat"));
	CHECK_EQ(classicsAssetPath(top + "Tools/ps2ids.txt"), std::string("ps2ids.txt"));
	CHECK_EQ(classicsAssetPath(top + "Tools/ps1ids.txt"), std::string("ps1ids.txt"));
	CHECK_EQ(classicsAssetPath(top + "Tools/PS4/gengp4_app.exe"), std::string());
	CHECK_EQ(classicsAssetPath(top + "Avalonia.Base.dll"), std::string());
	CHECK_EQ(classicsAssetPath(top + "Tools/PS4/emus/../../../evil"), std::string());
	CHECK(std::string(kClassicsAssetsUrl).find("SvenGDK/PS-Classics-fPKG-Builder/releases/download/v1/") != std::string::npos);
}

ORBISLINK_TEST(finds_the_emulators_in_a_bundle)
{
	const fs::path dir = makeEmulatorBundle();
	CHECK(hasClassicsAssets(dir.u8string()));
	CHECK(!hasClassicsAssets((dir / "nothing").u8string()));
	const EmulatorInfo emus = findEmulators(dir.u8string());
	CHECK(emus.hasPs2());
	CHECK(emus.hasPs1());
	CHECK_EQ(emus.ps2Name, std::string("Jak v2"));
	CHECK(emus.ps1Dir.find("ps1hd") != std::string::npos);
	CHECK_EQ(fs::u8path(emus.luaInclude), dir / "lua_include");
	// Quotes around a name with a comma go; PS1 serials have a dash.
	CHECK_EQ(lookupTitle(emus.titleDatabase, "SLUS20946"), std::string("Real Title, A"));
	CHECK_EQ(lookupTitle(emus.ps1TitleDatabase, "SCES01420"), std::string("Old Game From The List"));
	CHECK(!findEmulators((dir / "nothing").u8string()).hasPs2());
	fs::remove_all(dir);
}

ORBISLINK_TEST(a_ps2_disc_becomes_a_ps2_classic)
{
	const fs::path emuDir = makeEmulatorBundle();
	const fs::path discs = fs::temp_directory_path() / "orbislink-test-ps2";
	fs::remove_all(discs);
	writeFile(discs / "Game (USA).iso", makeDisc("BOOT2 = cdrom0:\\SLUS_209.46;1\r\n", false));
	const DiscInfo disc = inspectDisc((discs / "Game (USA).iso").u8string());
	ClassicOptions options;
	options.outputDir = (discs / "out").u8string();
	options.now = 1577836800;
	PkgRequest request;
	ClassicResult result;
	std::string error;
	CHECK(prepareClassic(disc, findEmulators(emuDir.u8string()), options, &request, &result, &error));
	CHECK_EQ(result.contentId, std::string("UP9000-SLUS20946_00-SLUS209460000001"));
	CHECK_EQ(result.title, std::string("Real Title, A"));
	CHECK(result.pkgPath.find("Real-Title-A_SLUS20946.pkg") != std::string::npos);
	CHECK_EQ(packageFileName("Jogo: Ação & Aventura!", "SLES12345"), std::string("Jogo-A-o-Aventura_SLES12345.pkg"));

	// easy-ps2-fpkg's project: the emulator folder as it is (docs too), the
	// shared lua_include over its own, the disc last.
	const auto t = targets(request);
	for(const char *f : {"eboot.bin", "ps2-emu-compiler.self", "docs/revision.h", "lua_include/own.lua",
			"sce_sys/icon0.png", "sce_sys/pic1.png", "config-emu-ps4.txt"})
		CHECK(std::find(t.begin(), t.end(), f) != t.end());
	CHECK_EQ(source(request, "lua_include/common.lua")->sourcePath, (emuDir / "lua_include/common.lua").u8string());
	CHECK_EQ(source(request, "sce_sys/icon0.png")->sourcePath, (emuDir / "emus/Jak v2/sce_sys/icon0.png").u8string());
	CHECK_EQ(t.back(), std::string("image/disc01.iso"));
	CHECK_EQ(std::count(t.begin(), t.end(), "sce_sys/param.sfo"), 1);

	// The emulator's config, with the serial and one disc.
	CHECK_EQ(text(source(request, "config-emu-ps4.txt")),
		std::string("--host-audio=1\n--ps2-title-id=SLUS-20946\n--gs-uprender=2x2\n--max-disc-num=1\n"));

	// The emulator's param.sfo, with only the game's IDs and name changed.
	ParamSfo sfo;
	CHECK(sfo.parse(source(request, "sce_sys/param.sfo")->data));
	CHECK_EQ(sfo.find("CONTENT_ID")->text, std::string("UP9000-SLUS20946_00-SLUS209460000001"));
	CHECK_EQ(sfo.find("TITLE_ID")->text, std::string("SLUS20946"));
	CHECK_EQ(sfo.find("TITLE")->text, std::string("Real Title, A"));
	CHECK_EQ(sfo.find("REMOTE_PLAY_KEY_ASSIGN")->number, 1);
	CHECK(sfo.find("SERVICE_ID_ADDCONT_ADD_1") != nullptr);

	// A cover replaces the emulator's art: icon0, and pic1 + pic0.
	options.icon = pattern(400, 18);
	options.background = pattern(500, 19);
	options.title = "My Name";
	CHECK(prepareClassic(disc, findEmulators(emuDir.u8string()), options, &request, &result, &error));
	CHECK_EQ(result.title, std::string("My Name"));
	CHECK_EQ(source(request, "sce_sys/icon0.png")->data.size(), size_t(400));
	CHECK_EQ(source(request, "sce_sys/pic1.png")->data.size(), size_t(500));
	CHECK_EQ(source(request, "sce_sys/pic0.png")->data.size(), size_t(500));
	const auto withArt = targets(request);
	CHECK_EQ(std::count(withArt.begin(), withArt.end(), "sce_sys/icon0.png"), 1);

	// And it builds.
	CHECK(convertClassic(disc, findEmulators(emuDir.u8string()), options, {}, &result, &error));
	CHECK_EQ(error, std::string());
	CHECK(fs::file_size(fs::u8path(result.pkgPath)) > 0);
	fs::remove_all(discs);
	fs::remove_all(emuDir);
}

ORBISLINK_TEST(a_ps1_disc_becomes_a_ps1_classic)
{
	const fs::path emuDir = makeEmulatorBundle();
	const fs::path discs = fs::temp_directory_path() / "orbislink-test-ps1";
	fs::remove_all(discs);
	writeFile(discs / "Old Game (Europe).bin", makeDisc("BOOT = cdrom:\\SCES_014.20;1\r\n", true));
	const std::string cue = "FILE \"Old Game (Europe).bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";
	writeFile(discs / "Old Game (Europe).cue", Bytes(cue.begin(), cue.end()));
	const DiscInfo disc = inspectDisc((discs / "Old Game (Europe).cue").u8string());
	ClassicOptions options;
	options.outputDir = (discs / "out").u8string();
	options.now = 1577836800;
	PkgRequest request;
	ClassicResult result;
	std::string error;
	CHECK(prepareClassic(disc, findEmulators(emuDir.u8string()), options, &request, &result, &error));
	CHECK_EQ(result.contentId, std::string("UP9000-SCES01420_00-SCES01420PS1FPKG"));
	CHECK_EQ(result.title, std::string("Old Game From The List"));
	CHECK_EQ(text(source(request, "config-title.txt")),
		std::string("--ps4-trophies=0\n--ps5-uds=0\n--trophies=0\n--image=\"data/disc1.bin\"\n"));
	CHECK(text(source(request, "data/disc1.cue")).find("FILE \"disc1.bin\" BINARY") != std::string::npos);
	CHECK(source(request, "data/disc1.bin")->sourcePath.find("Old Game (Europe).bin") != std::string::npos);
	ParamSfo sfo;
	CHECK(sfo.parse(source(request, "sce_sys/param.sfo")->data));
	CHECK_EQ(sfo.find("TITLE_ID")->text, std::string("SCES01420"));
	// The PS2 emulator's files stay out.
	const auto t = targets(request);
	CHECK(std::find(t.begin(), t.end(), "ps2-emu-compiler.self") == t.end());
	CHECK_EQ(std::count(t.begin(), t.end(), "config-title.txt"), 1);

	// Without the emulator, a clear refusal.
	EmulatorInfo none;
	CHECK(!prepareClassic(disc, none, options, &request, &result, &error));
	CHECK(error.find("PS1 emulator") != std::string::npos);
	fs::remove_all(discs);
	fs::remove_all(emuDir);
}

TEST_MAIN()
