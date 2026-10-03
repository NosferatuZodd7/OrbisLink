// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/ftp/ftp_client.h"
#include "orbislink/saves/save_vault.h"

#include "test_fixtures.h"
#include "test_support.h"

#include <filesystem>
#include <fstream>
#include <map>

using namespace orbislink;
using orbislink_test::buildSfo;
namespace fs = std::filesystem;

namespace {

// The "console": a folder whose paths are the console's.
class FolderRemote : public SaveRemote
{
public:
	explicit FolderRemote(fs::path root) : root_(std::move(root)) {}

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
			const auto stamp = written_.find(item.path);
			item.modified = stamp != written_.end() ? stamp->second : "Oct 03 18:42";
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
		// Like a console: what is written gets the time it was written.
		written_[remote] = "Oct 04 0" + std::to_string(++writes_ % 10) + ":00";
		std::error_code failure;
		fs::copy_file(local, at(remote), fs::copy_options::overwrite_existing, failure);
		if(failure && error)
			*error = failure.message();
		return !failure;
	}
	bool makeDirectory(const std::string &dir) override
	{
		// A real FTP server refuses this, and the client retries it: slow.
		if(fs::exists(at(dir)))
			++redundantMakeDirectory;
		std::error_code ignored;
		return fs::create_directory(at(dir), ignored);
	}
	int redundantMakeDirectory = 0;
	bool removeFile(const std::string &path, std::string *) override
	{
		std::error_code ignored;
		return fs::remove(at(path), ignored);
	}
	bool removeDirectory(const std::string &path) override
	{
		std::error_code ignored;
		return fs::remove(at(path), ignored);
	}

private:
	fs::path root_;
	std::map<std::string, std::string> written_;
	int writes_ = 0;
};

void writeBytes(const fs::path &path, size_t size, char fill)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << std::string(size, fill);
}

void writeData(const fs::path &path, const std::vector<uint8_t> &data)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(data.data()),
		static_cast<std::streamsize>(data.size()));
}

// A console with two saves of one game on one account.
fs::path makeConsole(const fs::path &root)
{
	const fs::path home = root / "user/home/1eb71bbd";
	for(const std::string dir : { "SAVE0", "SAVE1" })
	{
		writeBytes(home / "savedata/CUSA00001" / ("sdimg_" + dir), 4096, 'x');
		writeBytes(home / "savedata/CUSA00001" / (dir + ".bin"), 96, 'k');
		writeData(home / "savedata_meta/user/CUSA00001" / dir / "param.sfo",
			buildSfo({ { "MAINTITLE", "Orbis Racing" }, { "SUBTITLE", "Slot " + dir },
				{ "DETAIL", "Chapter 5 - 34%" } }));
		writeBytes(home / "savedata_meta/user/CUSA00001" / dir / "icon0.png", 64, 'p');
	}
	writeBytes(root / "user/appmeta/CUSA00001/icon0.png", 128, 'g');
	return home;
}

const SaveInfo *find(const std::vector<SaveInfo> &saves, const std::string &dir)
{
	for(const SaveInfo &save : saves)
		if(save.dir == dir)
			return &save;
	return nullptr;
}

struct Fixture
{
	fs::path base;
	fs::path consoleRoot;
	fs::path home;
	Fixture()
	{
		base = fs::temp_directory_path() / ("orbislink-vault-" + std::to_string(std::rand()));
		fs::remove_all(base);
		consoleRoot = base / "console";
		home = makeConsole(consoleRoot);
	}
	~Fixture() { std::error_code ignored; fs::remove_all(base, ignored); }
};

} // namespace

ORBISLINK_TEST(scan_backup_change_delete_restore)
{
	Fixture f;
	FolderRemote console(f.consoleRoot);
	SaveVault vault((f.base / "vault").string());

	std::string error;
	std::vector<SaveInfo> saves = vault.scan(console, &error);
	CHECK_EQ(saves.size(), static_cast<size_t>(2));
	const SaveInfo *save0 = find(saves, "SAVE0");
	CHECK(save0 != nullptr);
	CHECK_EQ(save0->gameTitle, std::string("Orbis Racing"));
	CHECK_EQ(save0->saveTitle, std::string("Slot SAVE0"));
	CHECK_EQ(save0->detail, std::string("Chapter 5 - 34%"));
	CHECK(save0->sync() == SaveSync::ConsoleOnly);
	CHECK_EQ(save0->consoleFiles.size(), static_cast<size_t>(4));
	CHECK(!save0->iconPath.empty());
	CHECK(!vault.gameIcon(&console, "CUSA00001").empty());

	// Backed up: the same on both sides.
	SaveInfo copy = *save0;
	CHECK(vault.backup(console, copy, &error));
	saves = vault.scan(console, &error);
	CHECK(find(saves, "SAVE0")->sync() == SaveSync::Same);
	CHECK_EQ(find(saves, "SAVE0")->versions, 1);
	CHECK(find(saves, "SAVE1")->sync() == SaveSync::ConsoleOnly);

	// The game wrote to it: changed since the backup.
	writeBytes(f.home / "savedata/CUSA00001/sdimg_SAVE0", 8192, 'y');
	saves = vault.scan(console, &error);
	CHECK(find(saves, "SAVE0")->sync() == SaveSync::Changed);

	// The console lost it: only the vault has it now…
	CHECK(vault.removeFromConsole(console, *find(saves, "SAVE0"), &error));
	CHECK(!fs::exists(f.home / "savedata/CUSA00001/sdimg_SAVE0"));
	saves = vault.scan(console, &error);
	const SaveInfo *lost = find(saves, "SAVE0");
	CHECK(lost != nullptr);
	CHECK(lost->sync() == SaveSync::VaultOnly);
	CHECK_EQ(lost->gameTitle, std::string("Orbis Racing"));

	// …and it goes back as it was backed up.
	CHECK(vault.restore(console, *lost, &error));
	CHECK_EQ(console.redundantMakeDirectory, 0);
	CHECK_EQ(fs::file_size(f.home / "savedata/CUSA00001/sdimg_SAVE0"), static_cast<uintmax_t>(4096));
	CHECK(fs::exists(f.home / "savedata_meta/user/CUSA00001/SAVE0/param.sfo"));
	saves = vault.scan(console, &error);
	CHECK(find(saves, "SAVE0")->sync() == SaveSync::Same);

	// Out of the vault: back to "never backed up".
	CHECK(vault.removeFromVault(*find(saves, "SAVE0"), &error));
	saves = vault.scan(console, &error);
	CHECK(find(saves, "SAVE0")->sync() == SaveSync::ConsoleOnly);
	CHECK(vault.vaultSaves().empty());
}

ORBISLINK_TEST(only_the_last_backups_are_kept)
{
	Fixture f;
	FolderRemote console(f.consoleRoot);
	SaveVault vault((f.base / "vault").string());
	std::string error;
	SaveInfo save = *find(vault.scan(console, &error), "SAVE1");
	for(int i = 0; i < SaveVault::kVersionsKept + 2; ++i)
		CHECK(vault.backup(console, save, &error));
	const std::vector<SaveInfo> kept = vault.vaultSaves();
	CHECK_EQ(kept.size(), static_cast<size_t>(1));
	CHECK_EQ(kept[0].versions, SaveVault::kVersionsKept);
}

ORBISLINK_TEST(vault_files_cannot_point_outside_their_folder)
{
	Fixture f;
	FolderRemote console(f.consoleRoot);
	SaveVault vault((f.base / "vault").string());
	std::string error;
	SaveInfo save = *find(vault.scan(console, &error), "SAVE0");
	CHECK(vault.backup(console, save, &error));

	// A hand-edited vault.json naming a file outside the save.
	const fs::path folder = f.base / "vault/1eb71bbd/CUSA00001/SAVE0";
	const fs::path version = fs::directory_iterator(folder)->path();
	std::ofstream(version / "vault.json", std::ios::binary | std::ios::trunc)
		<< R"({"files":[{"relative":"savedata/../../../escape","size":1},)"
		   R"({"relative":"savedata/sdimg_SAVE0","size":4096}]})";
	const std::vector<SaveInfo> saves = vault.vaultSaves();
	CHECK_EQ(saves.size(), static_cast<size_t>(1));
	CHECK_EQ(saves[0].vaultFiles.size(), static_cast<size_t>(1));
	CHECK_EQ(saves[0].vaultFiles[0].relative, std::string("savedata/sdimg_SAVE0"));
}

TEST_MAIN()
