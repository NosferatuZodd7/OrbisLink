// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The fake PKG builder. The expected digest below is of a package that came
// out byte for byte the same as LibOrbisPkg's PkgTool for the same files
// (volume_ts "2020-01-01 00:00:00", c_date "2020-01-01"); if a change moves
// it, compare with PkgTool again before updating it.
#include "orbislink/fpkg/param_sfo.h"
#include "orbislink/fpkg/pkg_builder.h"
#include "test_support.h"

#include <cstdlib>
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

TEST_MAIN()
