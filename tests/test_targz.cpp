// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The .tar.gz reader that unpacks the PS2 emulator files. The fixtures were
// made with Python:
//
//   gzip.compress(text, compresslevel=0 / 9, mtime=0)
//   tarfile (GNU_FORMAT / PAX_FORMAT) with a path longer than 100 characters
#include "orbislink/fpkg/fpkg_crypto.h"
#include "orbislink/fpkg/targz.h"
#include "targz_fixtures.h"
#include "test_support.h"

#include <filesystem>
#include <fstream>
#include <map>

using namespace orbislink::fpkg;
using namespace orbislink_test;

namespace {

namespace fs = std::filesystem;

const char *kTextSha = "b819dccf5ffae0cb34fea677d60659833989410288936a6860e0f7c4064d5606";

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

std::string inflateAll(const uint8_t *data, size_t size, std::string *error)
{
	std::string out;
	if(!gunzip(data, size, [&](const uint8_t *p, size_t n) {
			out.append(reinterpret_cast<const char *>(p), n);
			return true;
		}, error))
		return {};
	return out;
}

std::string readFile(const fs::path &p)
{
	std::ifstream in(p, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Unpacks a fixture and returns path → contents of what came out.
std::map<std::string, std::string> unpack(const uint8_t *data, size_t size, const char *name)
{
	const fs::path dir = fs::temp_directory_path() / name;
	fs::remove_all(dir);
	fs::create_directories(dir);
	const fs::path archive = dir / "a.tar.gz";
	std::ofstream(archive, std::ios::binary).write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
	std::map<std::string, std::string> seen;
	std::string error;
	const bool ok = extractTarGz(archive.u8string(),
		[&](const std::string &path, uint64_t) { return (dir / "out" / fs::u8path(path)).u8string(); },
		nullptr, &error);
	CHECK(ok);
	CHECK_EQ(error, std::string());
	for(const auto &e : fs::recursive_directory_iterator(dir / "out"))
		if(e.is_regular_file())
			seen[fs::relative(e.path(), dir / "out").generic_u8string()] = readFile(e.path());
	fs::remove_all(dir);
	return seen;
}

} // namespace

ORBISLINK_TEST(inflates_stored_and_compressed_blocks)
{
	std::string error;
	const std::string stored = inflateAll(kGzStored, sizeof kGzStored, &error);
	CHECK_EQ(error, std::string());
	CHECK_EQ(hex(sha256(stored.data(), stored.size())), std::string(kTextSha));
	const std::string best = inflateAll(kGzBest, sizeof kGzBest, &error);
	CHECK_EQ(error, std::string());
	CHECK_EQ(best, stored);
	CHECK(sizeof kGzBest < sizeof kGzStored / 2);
}

ORBISLINK_TEST(refuses_damaged_gzip)
{
	std::string error;
	std::vector<uint8_t> bad(kGzBest, kGzBest + sizeof kGzBest);
	bad[bad.size() / 2] ^= 0x55;
	inflateAll(bad.data(), bad.size(), &error);
	CHECK(!error.empty());
	const uint8_t notGzip[] = {'h', 'e', 'l', 'l', 'o'};
	inflateAll(notGzip, sizeof notGzip, &error);
	CHECK_EQ(error, std::string("not a gzip file"));
	inflateAll(kGzBest, sizeof kGzBest - 10, &error);
	CHECK(!error.empty());
}

ORBISLINK_TEST(unpacks_gnu_and_pax_archives)
{
	const std::string longName = "Tools/" + [] {
		std::string s;
		for(int i = 0; i < 5; ++i)
			s += "a-very-long-folder-name-";
		return s;
	}() + "/file.txt";
	for(const auto &seen : {unpack(kTarGnu, sizeof kTarGnu, "orbislink-test-tar-gnu"),
			unpack(kTarPax, sizeof kTarPax, "orbislink-test-tar-pax")})
	{
		CHECK_EQ(seen.size(), size_t(3));
		std::string self;
		for(int i = 0; i < 10; ++i)
			self += "SELF";
		CHECK_EQ(seen.at("Tools/PS4/emus/Jak v2/eboot.bin"), self);
		CHECK_EQ(seen.at(longName), std::string("long name"));
		CHECK_EQ(seen.at("Tools/ps2ids.txt"), std::string("SLUS20946;A Real Title\n"));
	}
}

ORBISLINK_TEST(leaves_out_what_is_not_wanted)
{
	const fs::path dir = fs::temp_directory_path() / "orbislink-test-tar-pick";
	fs::remove_all(dir);
	fs::create_directories(dir);
	const fs::path archive = dir / "a.tar.gz";
	std::ofstream(archive, std::ios::binary).write(reinterpret_cast<const char *>(kTarPax), sizeof kTarPax);
	std::string error;
	int asked = 0;
	CHECK(extractTarGz(archive.u8string(),
		[&](const std::string &path, uint64_t) {
			++asked;
			return path == "Tools/ps2ids.txt" ? (dir / "ids.txt").u8string() : std::string();
		},
		nullptr, &error));
	CHECK_EQ(asked, 3);
	CHECK_EQ(readFile(dir / "ids.txt"), std::string("SLUS20946;A Real Title\n"));
	CHECK(!fs::exists(dir / "Tools"));
	fs::remove_all(dir);
}

TEST_MAIN()
