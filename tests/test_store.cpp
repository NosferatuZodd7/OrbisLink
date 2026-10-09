// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The homebrew store: an app is only installed from a catalog whose
// signature and hashes check out, its ZIP is unpacked whole or not at all,
// and the folder lands where ShadowMountPlus looks, open to everyone.

#include "orbislink/ftp/ftp_client.h"
#include "orbislink/store/store_catalog.h"
#include "orbislink/store/store_installer.h"
#include "orbislink/store/zip_reader.h"

#include "store_fixtures.h"
#include "test_support.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

using namespace orbislink;
using namespace orbislink::store;
namespace fs = std::filesystem;

namespace {

fs::path scratch()
{
	const fs::path dir = fs::temp_directory_path() / ("orbislink-store-" + std::to_string(std::rand()));
	fs::remove_all(dir);
	fs::create_directories(dir);
	return dir;
}

std::string writeBytes(const fs::path &path, const unsigned char *data, size_t size)
{
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
	return path.string();
}

std::string readAll(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// What the test ZIP's eboot.bin holds.
std::string expectedEboot()
{
	std::string out;
	for(int i = 0; i < 4000; ++i)
		out += "ELF-test-content-";
	for(int r = 0; r < 20; ++r)
		for(int b = 0; b < 256; ++b)
			out += static_cast<char>(b);
	return out;
}

// The "console": a folder whose paths are the console's. Files come out of
// LIST as rw-r--r--, as some FTP servers write them, until chmod'ed.
class TestConsole : public StoreRemote
{
public:
	explicit TestConsole(fs::path root) : root_(std::move(root)) {}
	fs::path at(const std::string &path) const { return root_ / path.substr(1); }

	bool list(const std::string &dir, std::vector<FtpEntry> *entries, std::string *) override
	{
		entries->clear();
		std::error_code error;
		for(const auto &entry : fs::directory_iterator(at(dir), error))
		{
			FtpEntry item;
			item.name = entry.path().filename().string();
			item.path = dir + "/" + item.name;
			item.isDirectory = entry.is_directory();
			item.size = item.isDirectory ? 0 : static_cast<int64_t>(entry.file_size());
			const bool open = opened_.count(item.path) > 0;
			item.rawLine = std::string(item.isDirectory ? "d" : "-") + (open ? "rwxrwxrwx" : "rwxr-xr-x")
				+ " 1 root wheel 0 Oct 08 12:00 " + item.name;
			entries->push_back(item);
		}
		return true;
	}
	bool download(const std::string &remote, const std::string &local, std::string *error) override
	{
		std::error_code failure;
		fs::copy_file(at(remote), local, fs::copy_options::overwrite_existing, failure);
		if(failure && error)
			*error = failure.message();
		return !failure;
	}
	bool upload(const std::string &local, const std::string &remote, std::string *error) override
	{
		std::error_code failure;
		fs::copy_file(local, at(remote), fs::copy_options::overwrite_existing, failure);
		if(failure && error)
			*error = failure.message();
		++uploads;
		return !failure;
	}
	bool makeDirectory(const std::string &dir) override
	{
		std::error_code ignored;
		return fs::create_directory(at(dir), ignored);
	}
	bool rename(const std::string &from, const std::string &to, std::string *error) override
	{
		std::error_code failure;
		fs::rename(at(from), at(to), failure);
		if(failure && error)
			*error = failure.message();
		return !failure;
	}
	bool removeFile(const std::string &path, std::string *) override
	{
		std::error_code ignored;
		return fs::remove(at(path), ignored);
	}
	bool removeDirectory(const std::string &path, std::string *) override
	{
		std::error_code ignored;
		return fs::remove(at(path), ignored);
	}
	bool chmod(const std::string &path, const std::string &, std::string *) override
	{
		if(!chmodWorks)
			return false;
		opened_.insert(path);
		++chmods;
		return true;
	}

	bool chmodWorks = true;
	int chmods = 0;
	int uploads = 0;

private:
	fs::path root_;
	std::set<std::string> opened_;
};

} // namespace

ORBISLINK_TEST(a_zip_is_read_and_unpacked_whole)
{
	const fs::path dir = scratch();
	const std::string zip = writeBytes(dir / "app.zip", store_fixtures::kAppZip, sizeof(store_fixtures::kAppZip));
	std::vector<ZipEntry> entries;
	std::string error;
	CHECK(readZipDirectory(zip, &entries, &error));
	CHECK_EQ(entries.size(), size_t(5));
	CHECK_EQ(entries[0].name, std::string("PPSA99999/"));
	CHECK(entries[0].directory);
	CHECK_EQ(entries[1].name, std::string("PPSA99999/eboot.bin"));
	CHECK_EQ(entries[1].method, uint16_t(8));
	CHECK_EQ(entries[3].method, uint16_t(0));

	// Deflated and stored, both checked against their CRC.
	CHECK(extractZipEntry(zip, entries[1], (dir / "eboot.bin").string(), nullptr, &error));
	CHECK(readAll(dir / "eboot.bin") == expectedEboot());
	CHECK(extractZipEntry(zip, entries[3], (dir / "icon0.png").string(), nullptr, &error));
	CHECK_EQ(readAll(dir / "icon0.png").size(), size_t(108));

	// A damaged byte is caught.
	std::string bytes = readAll(zip);
	bytes[static_cast<size_t>(entries[3].localHeaderOffset) + 30 + 26 + 20] ^= 0x55;
	std::ofstream(dir / "bad.zip", std::ios::binary) << bytes;
	CHECK(!extractZipEntry((dir / "bad.zip").string(), entries[3], (dir / "x").string(), nullptr, &error));
	CHECK(error.find("checksum") != std::string::npos);
	fs::remove_all(dir);
}

ORBISLINK_TEST(a_zip_with_a_path_outside_its_folder_is_refused)
{
	const fs::path dir = scratch();
	const std::string zip = writeBytes(dir / "evil.zip", store_fixtures::kEvilZip, sizeof(store_fixtures::kEvilZip));
	std::vector<ZipEntry> entries;
	std::string error;
	CHECK(!readZipDirectory(zip, &entries, &error));
	CHECK(error.find("leads elsewhere") != std::string::npos);
	std::ofstream(dir / "text.zip") << "not a zip at all";
	CHECK(!readZipDirectory((dir / "text.zip").string(), &entries, &error));
	fs::remove_all(dir);
}

ORBISLINK_TEST(the_catalog_is_only_trusted_with_its_signature)
{
	PublicKey testKey {};
	std::copy(std::begin(store_fixtures::kTestPublicKey), std::end(store_fixtures::kTestPublicKey), testKey.begin());
	const std::string manifest = store_fixtures::kManifest;
	const std::string signature(reinterpret_cast<const char *>(store_fixtures::kManifestSignature),
		sizeof(store_fixtures::kManifestSignature));
	CHECK(verifySignature(manifest, signature, { testKey }));
	// Not by the catalog's own keys, and not a changed byte, nor a short signature.
	CHECK(!verifySignature(manifest, signature, catalogKeys()));
	std::string changed = manifest;
	changed[changed.size() - 3] = '9';
	CHECK(!verifySignature(changed, signature, { testKey }));
	CHECK(!verifySignature(manifest, signature.substr(0, 63), { testKey }));
	// Either of two keys will do.
	CHECK(verifySignature(manifest, signature, { catalogKeys()[0], testKey }));

	CatalogManifest parsed;
	std::string error;
	CHECK(parseManifest(manifest, &parsed, &error));
	CHECK_EQ(parsed.sequence, int64_t(72));
	CHECK_EQ(parsed.files.at("index.json"), std::string("00"));
	CHECK_EQ(catalogKeys().size(), size_t(2));
}

ORBISLINK_TEST(catalog_files_are_read_as_documented)
{
	const std::string index = R"({"schema":3,"generated":"2026-10-02T18:26:14Z","count":2,"apps":[
		{"titleid":"PPSA99002","status":"available","name":"ProsperoLight","kind":"app","author":"Someone",
		 "version":"01.000.070","content_version":"01.000.070","format":"zip","size":36383357,
		 "released":"2026-10-01T01:50:46Z","updated":"2026-10-01T04:17:55Z",
		 "icon_small":"https://homebrew.page/api/v1/icons/PPSA99002-256.png?v=9f2c","icon_hash":"9f2c4d7e1a6b3c58",
		 "sandbox":"leaves"},
		{"titleid":"PPSA99420","status":"coming_soon","name":"Later","kind":"game","author":"Other",
		 "version":null,"content_version":null,"format":null,"size":null,"icon_small":null,"icon_hash":null},
		{"titleid":"../../etc","status":"available","name":"Bad"}]})";
	std::vector<StoreApp> apps;
	std::string error;
	CHECK(parseIndex(index, &apps, &error));
	CHECK_EQ(apps.size(), size_t(2));
	CHECK(apps[0].installable());
	CHECK_EQ(apps[0].size, int64_t(36383357));
	CHECK_EQ(apps[0].sandbox, std::string("leaves"));
	CHECK(!apps[1].available());
	CHECK(!apps[1].installable());
	CHECK_EQ(apps[1].size, int64_t(-1));

	const std::string app = R"({"schema":3,"titleid":"PPSA99039","name":"EVO Player","kind":"app",
		"description":"Media player","license":"GPL-3.0","author":"Dev","version":"0.10.0",
		"source_repo":"https://github.com/x/y","artifact_url":"https://github.com/x/y/releases/download/v1/a.zip",
		"sha256":"A2B14616A2662A5E8401AAD9012BA1C0D3DF68211E5B70039EEDFB9A0845A0BE","status":"available",
		"content_version":"01.000.001","format":"zip","size":21561344,"release_notes":"A Real Application\n- one",
		"release_notes_truncated":true,"prerelease":false,
		"safety":{"sandbox":"leaves","routes":["service"],"helpers":1,"helpers_unapproved":0,"network":true,
		          "build":"developer","build_workflow":null},
		"page":"https://homebrew.page/app/PPSA99039/"})";
	StoreAppDetail detail;
	CHECK(parseAppDetail(app, &detail, &error));
	CHECK_EQ(detail.license, std::string("GPL-3.0"));
	CHECK_EQ(detail.sha256, std::string("a2b14616a2662a5e8401aad9012ba1c0d3df68211e5b70039eedfb9a0845a0be"));
	CHECK(detail.safetyKnown);
	CHECK_EQ(detail.safetyRoutes.size(), size_t(1));
	CHECK_EQ(detail.safetyHelpers, 1);
	CHECK(detail.safetyNetwork);
	CHECK(detail.releaseNotesTruncated);
	CHECK(detail.installable());

	CHECK(compareContentVersions("01.000.070", "01.000.060") > 0);
	CHECK_EQ(compareContentVersions("01.000.070", "01.000.070"), 0);
	CHECK(compareContentVersions("01.000.070", "01.000.080") < 0);
	CHECK_EQ(compareContentVersions("0.10.0", "01.000.080"), kNotComparable);
	CHECK_EQ(compareContentVersions("", "01.000.080"), kNotComparable);
}

ORBISLINK_TEST(an_app_goes_into_place_whole_and_open_to_everyone)
{
	const fs::path dir = scratch();
	const fs::path consoleRoot = dir / "console";
	fs::create_directories(consoleRoot / "data");
	const std::string zip = writeBytes(dir / "app.zip", store_fixtures::kAppZip, sizeof(store_fixtures::kAppZip));
	TestConsole console(consoleRoot);
	StoreInstaller installer;
	std::string error;
	InstallReport report;

	// Not the title the archive says it is.
	CHECK(!installer.install(console, zip, "PPSA99998", (dir / "work").string(), nullptr, &error, &report));
	CHECK(error.find("PPSA99999") != std::string::npos);
	CHECK(!fs::exists(consoleRoot / "data/homebrew/PPSA99998"));

	std::vector<std::string> stages;
	CHECK(installer.install(console, zip, "PPSA99999", (dir / "work").string(),
		[&stages](const std::string &stage, double) {
			if(stages.empty() || stages.back() != stage)
				stages.push_back(stage);
			return true;
		},
		&error, &report));
	CHECK(report.permissionsSet);
	CHECK(!report.replaced);
	const fs::path app = consoleRoot / "data/homebrew/PPSA99999";
	CHECK(readAll(app / "eboot.bin") == expectedEboot());
	CHECK(fs::exists(app / "sce_sys/param.json"));
	CHECK(fs::exists(app / "data/readme.txt"));
	CHECK(!fs::exists(consoleRoot / "data/orbislink/staging/PPSA99999"));
	// Every folder and file opened: the app folder, its 2 subfolders and 4 files.
	CHECK_EQ(console.chmods, 7);
	CHECK_EQ(stages.front(), std::string("unpack"));
	CHECK_EQ(stages.back(), std::string("finish"));

	std::vector<InstalledApp> installed = installer.installed(console, (dir / "work").string(), &error);
	CHECK_EQ(installed.size(), size_t(1));
	CHECK_EQ(installed[0].titleId, std::string("PPSA99999"));
	CHECK_EQ(installed[0].contentVersion, std::string("01.000.020"));

	// An update keeps the copy it replaces, and what the user put in the
	// app's folder (what the archive does not have) goes on into the new
	// one, as copying the new version over the old would keep it; what the
	// archive has is the new version's.
	std::ofstream(app / "mine.txt") << "user file";
	fs::create_directories(app / "games/doom");
	std::ofstream(app / "games/doom/doom2.wad") << "user game";
	std::ofstream(app / "data/settings.ini") << "user settings";
	std::ofstream(app / "data/readme.txt") << "changed by the user";
	CHECK(installer.install(console, zip, "PPSA99999", (dir / "work").string(), nullptr, &error, &report));
	CHECK(report.replaced);
	CHECK(report.left.empty());
	CHECK_EQ(report.carried.size(), size_t(3));
	CHECK(fs::exists(app / "mine.txt"));
	CHECK(fs::exists(app / "games/doom/doom2.wad"));
	CHECK(fs::exists(app / "data/settings.ini"));
	CHECK(readAll(app / "data/readme.txt") != "changed by the user");
	CHECK(fs::exists(app / "eboot.bin"));
	CHECK(fs::exists(consoleRoot / "data/orbislink/previous/PPSA99999/eboot.bin"));
	CHECK(!fs::exists(consoleRoot / "data/orbislink/previous/PPSA99999/mine.txt"));

	// A server that cannot set permissions: installed, with a word on it.
	console.chmodWorks = false;
	TestConsole fresh(dir / "console2");
	fs::create_directories(dir / "console2/data");
	fresh.chmodWorks = false;
	CHECK(installer.install(fresh, zip, "PPSA99999", (dir / "work").string(), nullptr, &error, &report));
	CHECK(!report.permissionsSet);

	// Uninstalled: the folder goes, its saves are not touched.
	CHECK(installer.uninstall(console, "PPSA99999", &error));
	CHECK(!fs::exists(app));
	CHECK(installer.uninstall(console, "PPSA99999", &error));
	CHECK(!installer.uninstall(console, "../etc", &error));
	fs::remove_all(dir);
}

TEST_MAIN()
