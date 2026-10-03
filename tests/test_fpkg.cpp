// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The fake PKG builder. The expected digest below is of a package that came
// out byte for byte the same as LibOrbisPkg's PkgTool for the same files
// (volume_ts "2020-01-01 00:00:00", c_date "2020-01-01"); if a change moves
// it, compare with PkgTool again before updating it.
#include "orbislink/fpkg/classic_converter.h"
#include "orbislink/fpkg/disc_scanner.h"
#include "orbislink/fpkg/fself.h"
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

// What PS-Classics-fPKG-Builder keeps under Tools/PS4: stand-ins, not
// Sony's files.
fs::path makeEmulatorBundle()
{
	const fs::path dir = fs::temp_directory_path() / "orbislink-test-emus";
	fs::remove_all(dir);
	const fs::path ps4 = dir / "Tools/PS4";
	for(const char *emu : {"Jak v2", "Rogue v1"})
	{
		writeFile(ps4 / "emus" / emu / "eboot.bin", pattern(1000, 5));
		writeFile(ps4 / "emus" / emu / "ps2-emu-compiler.self", pattern(2000, 6));
		writeFile(ps4 / "emus" / emu / "sce_sys/param.sfo", pattern(100, 7));
		writeFile(ps4 / "emus" / emu / "sce_sys/icon0.png", pattern(300, 8));
		const std::string cfg = "--host-audio=1\n--ps2-title-id=SCUS-97124\n--gs-uprender=2x2\n";
		writeFile(ps4 / "emus" / emu / "config-emu-ps4.txt", Bytes(cfg.begin(), cfg.end()));
	}
	writeFile(ps4 / "emus/ps1hd/eboot.bin", pattern(900, 9));
	writeFile(ps4 / "emus/ps1hd/sce_sys/icon0.png", pattern(300, 10));
	writeFile(ps4 / "emus/psphd/eboot.bin", pattern(900, 11));
	writeFile(ps4 / "lua_include/common.lua", pattern(50, 12));
	const std::string ids = "SLUS20946;A Real Title\r\nSCES01420;Other\r\n";
	writeFile(dir / "Tools/ps2ids.txt", Bytes(ids.begin(), ids.end()));
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

ORBISLINK_TEST(finds_the_emulators_in_a_bundle)
{
	const fs::path dir = makeEmulatorBundle();
	const EmulatorInfo emus = findEmulators(dir.u8string());
	CHECK(emus.hasPs2());
	CHECK(emus.hasPs1());
	CHECK_EQ(emus.ps2Name, std::string("Jak v2"));
	CHECK(emus.ps1Dir.find("ps1hd") != std::string::npos);
	CHECK(emus.luaInclude.find("lua_include") != std::string::npos);
	CHECK_EQ(lookupTitle(emus.titleDatabase, "SLUS20946"), std::string("A Real Title"));
	// Pointing straight at one emulator works too.
	const EmulatorInfo one = findEmulators((dir / "Tools/PS4/emus/Rogue v1").u8string());
	CHECK_EQ(one.ps2Name, std::string("Rogue v1"));
	CHECK(!one.hasPs1());
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
	options.icon = pattern(400, 13);
	PkgRequest request;
	ClassicResult result;
	std::string error;
	CHECK(prepareClassic(disc, findEmulators(emuDir.u8string()), options, &request, &result, &error));
	CHECK_EQ(result.contentId, std::string("UP9000-SLUS20946_00-SLUS209460000001"));
	CHECK_EQ(result.title, std::string("A Real Title"));
	CHECK(result.pkgPath.find("A-Real-Title_SLUS20946.pkg") != std::string::npos);
	CHECK_EQ(packageFileName("Jogo: Ação & Aventura!", "SLES12345"), std::string("Jogo-A-o-Aventura_SLES12345.pkg"));
	const auto t = targets(request);
	CHECK(std::find(t.begin(), t.end(), "eboot.bin") != t.end());
	CHECK(std::find(t.begin(), t.end(), "lua_include/common.lua") != t.end());
	CHECK_EQ(t.back(), std::string("image/disc01.iso"));
	CHECK_EQ(std::count(t.begin(), t.end(), "sce_sys/param.sfo"), 1);
	CHECK_EQ(std::count(t.begin(), t.end(), "sce_sys/icon0.png"), 1);
	CHECK_EQ(source(request, "sce_sys/icon0.png")->data.size(), size_t(400));
	const std::string cfg = text(source(request, "config-emu-ps4.txt"));
	CHECK(cfg.find("--ps2-title-id=SLUS-20946\n") != std::string::npos);
	CHECK(cfg.find("--max-disc-num=1") != std::string::npos);
	CHECK(cfg.find("SCUS-97124") == std::string::npos);
	ParamSfo sfo;
	CHECK(sfo.parse(source(request, "sce_sys/param.sfo")->data));
	CHECK_EQ(sfo.find("TITLE_ID")->text, std::string("SLUS20946"));
	CHECK_EQ(sfo.find("CATEGORY")->text, std::string("gd"));

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
	CHECK_EQ(result.title, std::string("Old Game"));
	CHECK(text(source(request, "config-title.txt")).find("--image=\"data/disc1.bin\"") != std::string::npos);
	CHECK(text(source(request, "data/disc1.cue")).find("FILE \"disc1.bin\" BINARY") != std::string::npos);
	CHECK(source(request, "data/disc1.bin")->sourcePath.find("Old Game (Europe).bin") != std::string::npos);
	// The PS2 emulator's files stay out.
	const auto t = targets(request);
	CHECK(std::find(t.begin(), t.end(), "ps2-emu-compiler.self") == t.end());

	// Without the emulator, a clear refusal.
	EmulatorInfo none;
	CHECK(!prepareClassic(disc, none, options, &request, &result, &error));
	CHECK(error.find("PS1 emulator") != std::string::npos);
	fs::remove_all(discs);
	fs::remove_all(emuDir);
}

namespace {

// A small PS4-like executable: a code segment and a dynlib data segment.
Bytes makeElf()
{
	Bytes elf = pattern(0x6100, 21);
	std::memset(elf.data(), 0, 0x40 + 2 * 0x38);
	const uint8_t ident[] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 9};
	std::memcpy(elf.data(), ident, sizeof ident);
	auto put = [&](size_t at, uint64_t v, int n) {
		for(int i = 0; i < n; ++i)
			elf[at + i] = static_cast<uint8_t>(v >> (8 * i));
	};
	put(0x10, 0xFE10, 2); // ET_SCE_DYNEXEC
	put(0x12, 0x3E, 2);   // x86-64
	put(0x14, 1, 4);
	put(0x20, 0x40, 8);   // program headers
	put(0x34, 0x40, 2);
	put(0x36, 0x38, 2);
	put(0x38, 2, 2);
	const uint64_t segs[2][3] = {{1, 0x1000, 0x5000}, {0x61000000, 0x6000, 0x100}};
	for(int i = 0; i < 2; ++i)
	{
		const size_t ph = 0x40 + i * 0x38;
		put(ph, segs[i][0], 4);
		put(ph + 4, 5, 4);
		put(ph + 0x08, segs[i][1], 8);
		put(ph + 0x20, segs[i][2], 8);
		put(ph + 0x28, segs[i][2], 8);
		put(ph + 0x30, 0x4000, 8);
	}
	return elf;
}

} // namespace

ORBISLINK_TEST(a_plain_elf_becomes_a_fake_self)
{
	const Bytes elf = makeElf();
	CHECK(isPlainElf(elf.data(), elf.size()));
	std::string error;
	const Bytes self = makeFself(elf, &error);
	CHECK_EQ(error, std::string());
	CHECK(!isPlainElf(self.data(), self.size()));
	CHECK(self.size() > 0x20 && self[0] == 0x4F && self[1] == 0x15 && self[2] == 0x3D && self[3] == 0x1D);
	CHECK_EQ(int(self[24]), 4); // two entries per loaded segment
	// The same bytes as OpenOrbis' create-fself (make_fself.py's defaults).
	CHECK_EQ(hex(sha256(self)), std::string(EXPECTED_FSELF_SHA));
	CHECK(makeFself(pattern(100, 1), &error).empty());
}

ORBISLINK_TEST(a_dump_folder_gives_only_the_emulator)
{
	// A GoldHEN dump used as it is, with the packages made from it next to it.
	const fs::path dir = fs::temp_directory_path() / "orbislink-test-dump";
	fs::remove_all(dir);
	writeFile(dir / "eboot.bin", makeElf());
	writeFile(dir / "eboot.fself", Bytes());
	writeFile(dir / "ps2-emu-compiler.self", pattern(3000, 30));
	writeFile(dir / "PS20220WD20050620.crack", pattern(1000, 31));
	writeFile(dir / "sce_module/libc.prx", makeElf());
	writeFile(dir / "sce_sys/keystone", pattern(96, 32));
	writeFile(dir / "sce_sys/license.dat", pattern(1024, 33));
	writeFile(dir / "sce_sys/playgo-chunk.dat", pattern(1024, 34));
	writeFile(dir / "sce_sys/icon0.png", pattern(500, 35));
	writeFile(dir / "docs/readme.txt", pattern(50, 36));
	writeFile(dir / "image/disc01.iso", pattern(4096, 37));
	writeFile(dir / "Old-Game_SLUS20905.pkg", pattern(4096, 38));
	writeFile(dir / "SLUS20905.gp4", pattern(400, 39));
	writeFile(dir / "lua_include/common.lua", pattern(70, 40));
	writeFile(dir / "games/Game (USA).iso", makeDisc("BOOT2 = cdrom0:\\SLUS_209.05;1\r\n", false));
	const DiscInfo disc = inspectDisc((dir / "games/Game (USA).iso").u8string());
	ClassicOptions options;
	options.outputDir = dir.u8string();
	options.now = 1577836800;
	PkgRequest request;
	ClassicResult result;
	std::string error;
	CHECK(prepareClassic(disc, findEmulators(dir.u8string()), options, &request, &result, &error));
	CHECK_EQ(error, std::string());
	auto t = targets(request);
	std::sort(t.begin(), t.end());
	const std::vector<std::string> expected = {"PS20220WD20050620.crack", "config-emu-ps4.txt", "eboot.bin",
		"image/disc01.iso", "lua_include/common.lua", "ps2-emu-compiler.self", "sce_module/libc.prx",
		"sce_sys/icon0.png", "sce_sys/param.sfo"};
	CHECK(t == expected);
	// The decrypted executables go in fake-signed.
	const PkgSource *eboot = source(request, "eboot.bin");
	CHECK(eboot->sourcePath.empty() && eboot->data.size() > 4 && eboot->data[0] == 0x4F);
	CHECK(source(request, "sce_module/libc.prx")->data[0] == 0x4F);
	CHECK(source(request, "image/disc01.iso")->sourcePath.find("Game (USA).iso") != std::string::npos);
	fs::remove_all(dir);
}

TEST_MAIN()
