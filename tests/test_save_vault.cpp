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
class TestConsole : public SaveRemote
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
			const auto stamp = written_.find(item.path);
			item.modified = stamp != written_.end() ? stamp->second : "Oct 03 18:42";
			entries->push_back(item);
		}
		return true;
	}
	bool upload(const std::string &local, const std::string &remote, std::string *error) override
	{
		// Like a console: what is written gets the time it was written.
		written_[remote] = "Oct 04 0" + std::to_string(++writes_ % 10) + ":00";
		std::error_code failure;
		fs::copy_file(local, at(remote), fs::copy_options::overwrite_existing, failure);
		if(failure && error)
			*error = failure.message();
		// A transfer cut short that still says "done".
		if(truncateUploads && !failure)
			fs::resize_file(at(remote), fs::file_size(at(remote)) / 2);
		return !failure;
	}
	bool truncateUploads = false;
	// A file the console's game wrote, at that time.
	void touch(const std::string &remote, const std::string &when) { written_[remote] = when; }
	bool makeDirectory(const std::string &dir) override
	{
		// A real FTP server refuses this, and the client retries it: slow.
		if(fs::exists(at(dir)))
			++redundantMakeDirectory;
		std::error_code ignored;
		return fs::create_directory(at(dir), ignored);
	}
	int redundantMakeDirectory = 0;
	bool truncateDownloads = false;
	bool download(const std::string &remote, const std::string &local, std::string *error) override
	{
		std::error_code failure;
		fs::copy_file(at(remote), local, fs::copy_options::overwrite_existing, failure);
		if(failure && error)
			*error = failure.message();
		// A transfer cut short that still says "done".
		if(truncateDownloads && !failure)
			fs::resize_file(local, fs::file_size(local) / 2);
		return !failure;
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

void writePng(const fs::path &path)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << std::string("\x89PNG\r\n\x1a\n", 8) << std::string(56, 'p');
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
				{ "DETAIL", "Chapter 5 - 34%" }, { "ACCOUNT_ID", "ABCDEFGH" } }));
		writePng(home / "savedata_meta/user/CUSA00001" / dir / "icon0.png");
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

// The console's ACCOUNT_ID "ABCDEFGH" as a PSID folder: its bytes the
// other way round.
const char *kPsid = "4847464544434241";

ORBISLINK_TEST(scan_backup_change_delete)
{
	Fixture f;
	TestConsole console(f.consoleRoot);
	const fs::path root = f.base / "vault";
	SaveVault vault(root.string());

	std::string error;
	std::vector<SaveInfo> saves = vault.scan(console, &error);
	CHECK_EQ(saves.size(), static_cast<size_t>(2));
	const SaveInfo *save0 = find(saves, "SAVE0");
	CHECK(save0 != nullptr);
	CHECK_EQ(save0->gameTitle, std::string("Orbis Racing"));
	CHECK_EQ(save0->saveTitle, std::string("Slot SAVE0"));
	CHECK_EQ(save0->detail, std::string("Chapter 5 - 34%"));
	CHECK(save0->sync() == SaveSync::ConsoleOnly);
	// The account it belongs to, as the bytes are stored, and as the PS4
	// names its folder.
	CHECK_EQ(save0->accountId, std::string("4142434445464748"));
	CHECK_EQ(save0->psid, std::string(kPsid));
	CHECK(!save0->iconPath.empty());
	CHECK(!vault.gameIcon(&console, "CUSA00001").empty());

	// Backed up as the PS4 copies saves to USB: PS4/SAVEDATA/<PSID>/<game>.
	SaveInfo copy = *save0;
	CHECK(vault.backup(console, copy, &error));
	const fs::path kept = root / "PS4/SAVEDATA" / kPsid / "CUSA00001";
	CHECK_EQ(fs::file_size(kept / "SAVE0"), static_cast<uintmax_t>(4096));
	CHECK_EQ(fs::file_size(kept / "SAVE0.bin"), static_cast<uintmax_t>(96));
	CHECK(fs::exists(root / ".vault" / kPsid / "CUSA00001/SAVE0/info.json"));
	CHECK(fs::exists(root / ".vault" / kPsid / "CUSA00001/SAVE0/icon0.png"));
	saves = vault.scan(console, &error);
	CHECK(find(saves, "SAVE0")->sync() == SaveSync::Same);
	CHECK_EQ(find(saves, "SAVE0")->versions, 1);
	CHECK(find(saves, "SAVE1")->sync() == SaveSync::ConsoleOnly);

	// The game wrote to it: changed since the backup.
	writeBytes(f.home / "savedata/CUSA00001/sdimg_SAVE0", 8192, 'y');
	saves = vault.scan(console, &error);
	CHECK(find(saves, "SAVE0")->sync() == SaveSync::Changed);

	// The console lost it: the vault still has it, names and all.
	fs::remove(f.home / "savedata/CUSA00001/sdimg_SAVE0");
	fs::remove(f.home / "savedata/CUSA00001/SAVE0.bin");
	fs::remove_all(f.home / "savedata_meta/user/CUSA00001/SAVE0");
	saves = vault.scan(console, &error);
	const SaveInfo *lost = find(saves, "SAVE0");
	CHECK(lost != nullptr);
	CHECK(lost->sync() == SaveSync::VaultOnly);
	CHECK_EQ(lost->account, std::string("1eb71bbd"));
	CHECK_EQ(lost->gameTitle, std::string("Orbis Racing"));
	CHECK_EQ(lost->saveTitle, std::string("Slot SAVE0"));

	// Out of the vault: nothing of it left.
	CHECK(vault.removeFromVault(*lost, &error));
	CHECK(vault.vaultSaves().empty());
	CHECK(!fs::exists(root / "PS4/SAVEDATA" / kPsid));
	CHECK(!fs::exists(root / ".vault" / kPsid));
}

// A save the console lost goes back as it was backed up, to its own user,
// whole, and then reads as the same save again.
ORBISLINK_TEST(a_lost_save_goes_back_whole)
{
	Fixture f;
	TestConsole console(f.consoleRoot);
	SaveVault vault((f.base / "vault").string());
	std::string error;
	SaveInfo save = *find(vault.scan(console, &error), "SAVE0");
	CHECK(vault.backup(console, save, &error));

	// Lost: files, game folder and all.
	fs::remove_all(f.home / "savedata/CUSA00001");
	fs::remove_all(f.home / "savedata_meta/user/CUSA00001/SAVE0");
	std::vector<SaveInfo> saves = vault.scan(console, &error);
	const SaveInfo *lost = find(saves, "SAVE0");
	CHECK(lost->sync() == SaveSync::VaultOnly);

	CHECK(vault.restore(console, *lost, std::string(), &error));
	CHECK_EQ(console.redundantMakeDirectory, 0);
	CHECK_EQ(fs::file_size(f.home / "savedata/CUSA00001/sdimg_SAVE0"), static_cast<uintmax_t>(4096));
	CHECK_EQ(fs::file_size(f.home / "savedata/CUSA00001/SAVE0.bin"), static_cast<uintmax_t>(96));
	saves = vault.scan(console, &error);
	CHECK(find(saves, "SAVE0")->sync() == SaveSync::Same);

	// The row the console's list of saves needs for it.
	const SaveDbEntry row = vault.dbEntry(*find(saves, "SAVE0"), std::string());
	CHECK_EQ(row.titleId, std::string("CUSA00001"));
	CHECK_EQ(row.dir, std::string("SAVE0"));
	CHECK_EQ(row.mainTitle, std::string("Orbis Racing"));
	CHECK_EQ(row.subTitle, std::string("Slot SAVE0"));
	CHECK_EQ(row.userId, static_cast<uint32_t>(0x1eb71bbd));
	CHECK_EQ(row.accountId, static_cast<int64_t>(0x4847464544434241LL));
	CHECK_EQ(SaveVault::saveDbPath("1eb71bbd"), std::string("/system_data/savedata/1eb71bbd/db/user/savedata.db"));
}

// What does not arrive whole is an error; a user the console does not have
// is refused before anything is written.
ORBISLINK_TEST(putting_back_checks_the_user_and_the_sizes)
{
	Fixture f;
	TestConsole console(f.consoleRoot);
	SaveVault vault((f.base / "vault").string());
	std::string error;
	SaveInfo save = *find(vault.scan(console, &error), "SAVE1");
	CHECK(vault.backup(console, save, &error));
	save = *find(vault.scan(console, &error), "SAVE1");

	CHECK(!vault.restore(console, save, "deadbeef", &error));
	CHECK(error.find("deadbeef") != std::string::npos);

	console.truncateUploads = true;
	error.clear();
	CHECK(!vault.restore(console, save, std::string(), &error));
	CHECK(error.find("whole") != std::string::npos);
}

// The image's size in 32 KiB blocks is what the list of saves records.
ORBISLINK_TEST(the_save_list_row_counts_blocks_of_32_kib)
{
	Fixture f;
	writeBytes(f.home / "savedata/CUSA00007/sdimg_BIG", 98304, 'b');
	writeBytes(f.home / "savedata/CUSA00007/BIG.bin", 96, 'k');
	TestConsole console(f.consoleRoot);
	SaveVault vault((f.base / "vault").string());
	vault.setLinks({ { "1eb71bbd", "1c020a82bb40e5fe" } });
	std::string error;
	SaveInfo save = *find(vault.scan(console, &error), "BIG");
	CHECK(vault.backup(console, save, &error));
	const SaveDbEntry row = vault.dbEntry(*find(vault.vaultSaves(), "BIG"), "1eb71bbd");
	CHECK_EQ(row.blocks, static_cast<int64_t>(3));
	CHECK_EQ(row.accountId, static_cast<int64_t>(0x1c020a82bb40e5feLL));
}

// The layout seen on a real console: the PS4's own backup copy of each
// save ("sce_bu_"), and savedata_meta entries that are files, not folders.
// No ACCOUNT_ID to be read: the console user's link gives the PSID.
ORBISLINK_TEST(real_layout_with_system_backups_and_a_linked_user)
{
	Fixture f;
	// A user with only this save: nothing says whose it is.
	const fs::path home = f.consoleRoot / "user/home/2c4d6e8f";
	const fs::path data = home / "savedata/CUSA00009";
	writeBytes(data / "sdimg_RDR2SAVE0.SAV", 2048, 'a');
	writeBytes(data / "RDR2SAVE0.SAV.bin", 96, 'b');
	writeBytes(data / "sdimg_sce_bu_RDR2SAVE0.SAV", 2048, 'c');
	writeBytes(data / "sce_bu_RDR2SAVE0.SAV.bin", 96, 'd');
	writeData(home / "savedata_meta/user/CUSA00009/RDR2SAVE0.SAV",
		buildSfo({ { "MAINTITLE", "Red Dead" }, { "SUBTITLE", "Chapter 2" } }));
	writeBytes(home / "savedata_meta/user/CUSA00009/OTHERSAVE", 300, 'o');
	writeData(f.consoleRoot / "user/appmeta/CUSA00009/param.sfo", buildSfo({ { "TITLE", "Red Dead Redemption 2" } }));

	TestConsole console(f.consoleRoot);
	const fs::path root = f.base / "vault";
	SaveVault vault(root.string());
	std::string error;
	auto rdr = [&](const std::vector<SaveInfo> &saves) {
		const SaveInfo *found = nullptr;
		int count = 0;
		for(const SaveInfo &s : saves)
			if(s.titleId == "CUSA00009")
			{
				++count;
				found = &s;
			}
		CHECK_EQ(count, 1); // one save, not two: the system's copy goes with it
		return found;
	};
	std::vector<SaveInfo> saves = vault.scan(console, &error);
	SaveInfo save = *rdr(saves);
	CHECK_EQ(save.dir, std::string("RDR2SAVE0.SAV"));
	CHECK_EQ(save.saveTitle, std::string("Chapter 2"));
	CHECK(save.psid.empty());
	// Whose it is is not known: not backed up, and said why.
	CHECK(!vault.backup(console, save, &error));
	CHECK(error.find("link") != std::string::npos);

	vault.setLinks({ { "2c4d6e8f", "1C020A82BB40E5FE" } });
	save = *rdr(vault.scan(console, &error));
	CHECK_EQ(save.psid, std::string("1c020a82bb40e5fe"));
	CHECK(vault.backup(console, save, &error));
	// The image and its key, named as on a PS4's USB drive; not the
	// system's copies.
	const fs::path kept = root / "PS4/SAVEDATA/1c020a82bb40e5fe/CUSA00009";
	int files = 0;
	for(const auto &entry : fs::directory_iterator(kept))
	{
		++files;
		CHECK(entry.path().filename() == "RDR2SAVE0.SAV" || entry.path().filename() == "RDR2SAVE0.SAV.bin");
	}
	CHECK_EQ(files, 2);
	CHECK(rdr(vault.scan(console, &error))->sync() == SaveSync::Same);
	// The console's files are as they were.
	CHECK_EQ(fs::file_size(data / "sdimg_RDR2SAVE0.SAV"), static_cast<uintmax_t>(2048));

	// Its own image gone, only the system's copies left: the save is
	// missing from the console, and the vault's copy is what is left.
	fs::remove(data / "sdimg_RDR2SAVE0.SAV");
	saves = vault.scan(console, &error);
	const SaveInfo *broken = rdr(saves);
	CHECK(broken->sync() == SaveSync::VaultOnly);
	CHECK_EQ(broken->account, std::string("2c4d6e8f"));
}

// The same PSN account on a PS4 and a PS5: a save backed up from one goes to
// the other's user with that PSID, and each console's copy is known to be
// the latest backup or an earlier one.
ORBISLINK_TEST(a_save_goes_to_another_console_of_the_same_psid)
{
	Fixture f;
	TestConsole ps4(f.consoleRoot);
	const fs::path ps5Root = f.base / "ps5";
	const fs::path ps5Home = ps5Root / "user/home/5a6b7c8d";
	fs::create_directories(ps5Home);
	fs::create_directories(ps5Root / "user/home/6c7d8e9f");
	TestConsole ps5(ps5Root);
	SaveVault vault((f.base / "vault").string());
	vault.setLinks({ { "5a6b7c8d", kPsid }, { "6c7d8e9f", "1c020a82bb40e5fe" } });
	std::string error;
	SaveInfo save = *find(vault.scan(ps4, &error), "SAVE0");
	CHECK(vault.backup(ps4, save, &error));

	// On the PS5, it is not there yet: it would go to the user with its PSID.
	std::vector<SaveInfo> saves = vault.scan(ps5, &error);
	const SaveInfo *there = find(saves, "SAVE0");
	CHECK(there->sync() == SaveSync::VaultOnly);
	CHECK_EQ(there->account, std::string("5a6b7c8d"));
	CHECK(vault.restore(ps5, *there, there->account, &error));
	CHECK_EQ(fs::file_size(ps5Home / "savedata/CUSA00001/sdimg_SAVE0"), static_cast<uintmax_t>(4096));
	CHECK(!fs::exists(ps5Root / "user/home/6c7d8e9f/savedata"));
	// One save on both, the latest backup on both.
	saves = vault.scan(ps5, &error);
	CHECK(find(saves, "SAVE0")->sync() == SaveSync::Same);
	CHECK_EQ(find(saves, "SAVE0")->key(), std::string(kPsid) + "/CUSA00001/SAVE0");
	CHECK(find(vault.scan(ps4, &error), "SAVE0")->sync() == SaveSync::Same);

	// Played on the PS5, backed up from there: the PS4's copy is the earlier one.
	writeBytes(ps5Home / "savedata/CUSA00001/sdimg_SAVE0", 4096, 'z');
	ps5.touch("/user/home/5a6b7c8d/savedata/CUSA00001/sdimg_SAVE0", "Oct 05 21:30");
	saves = vault.scan(ps5, &error);
	save = *find(saves, "SAVE0");
	CHECK(save.sync() == SaveSync::Changed);
	CHECK(vault.backup(ps5, save, &error));
	saves = vault.scan(ps4, &error);
	save = *find(saves, "SAVE0");
	CHECK(save.sync() == SaveSync::Older);
	CHECK_EQ(save.versions, 2);
	// Sent there too: the latest everywhere.
	CHECK(vault.restore(ps4, save, save.account, &error));
	CHECK(find(vault.scan(ps4, &error), "SAVE0")->sync() == SaveSync::Same);
	CHECK(find(vault.scan(ps5, &error), "SAVE0")->sync() == SaveSync::Same);
	std::ifstream in(f.home / "savedata/CUSA00001/sdimg_SAVE0", std::ios::binary);
	CHECK_EQ(static_cast<char>(in.get()), 'z');

	// The PS5's own save of another game, of that name, size and minute: not
	// the PC's copy, which was never there.
	writeBytes(f.home / "savedata/CUSA00002/sdimg_SAVE9", 4096, 'a');
	writeBytes(f.home / "savedata/CUSA00002/SAVE9.bin", 96, 'b');
	save = *find(vault.scan(ps4, &error), "SAVE9");
	CHECK(vault.backup(ps4, save, &error));
	writeBytes(ps5Home / "savedata/CUSA00002/sdimg_SAVE9", 4096, 'c');
	writeBytes(ps5Home / "savedata/CUSA00002/SAVE9.bin", 96, 'd');
	CHECK(find(vault.scan(ps5, &error), "SAVE9")->sync() == SaveSync::Changed);
	CHECK(find(vault.scan(ps4, &error), "SAVE9")->sync() == SaveSync::Same);

	// A user of another PSID is never given it.
	vault.setLinks({ { "6c7d8e9f", "1c020a82bb40e5fe" } });
	fs::remove_all(ps5Home);
	saves = vault.scan(ps5, &error);
	CHECK(find(saves, "SAVE0")->account.empty());
}

// A save that does not say whose it is, in the folder of a user whose other
// saves do: that user's. The console's users, with their names.
ORBISLINK_TEST(a_users_psid_comes_from_its_other_saves)
{
	Fixture f;
	writeBytes(f.home / "savedata/CUSA00009/sdimg_QUIET", 2048, 'a');
	writeBytes(f.home / "savedata/CUSA00009/QUIET.bin", 96, 'b');
	fs::create_directories(f.consoleRoot / "user/home/2c4d6e8f");
	std::ofstream(f.home / "username.dat", std::ios::binary) << std::string("Player One") << std::string(6, '\0');
	TestConsole console(f.consoleRoot);
	SaveVault vault((f.base / "vault").string());
	std::string error;
	const std::vector<SaveInfo> saves = vault.scan(console, &error);
	CHECK_EQ(find(saves, "QUIET")->psid, std::string(kPsid));
	CHECK_EQ(vault.users().size(), static_cast<size_t>(2));
	for(const ConsoleUser &user : vault.users())
	{
		if(user.folder == "1eb71bbd")
		{
			CHECK_EQ(user.name, std::string("Player One"));
			CHECK_EQ(user.psid, std::string(kPsid));
			CHECK(!user.linked);
		}
		else
		{
			CHECK(user.name.empty());
			CHECK(user.psid.empty());
		}
	}
}

// A file that does not arrive whole is an error, and the backup there was
// stays as it was.
ORBISLINK_TEST(a_short_download_is_reported)
{
	Fixture f;
	TestConsole console(f.consoleRoot);
	const fs::path root = f.base / "vault";
	SaveVault vault(root.string());
	std::string error;
	SaveInfo save = *find(vault.scan(console, &error), "SAVE0");
	CHECK(vault.backup(console, save, &error));
	writeBytes(f.home / "savedata/CUSA00001/sdimg_SAVE0", 8192, 'y');
	console.truncateDownloads = true;
	error.clear();
	save = *find(vault.scan(console, &error), "SAVE0");
	CHECK(!vault.backup(console, save, &error));
	CHECK(error.find("Incomplete") != std::string::npos);
	CHECK_EQ(fs::file_size(root / "PS4/SAVEDATA" / kPsid / "CUSA00001/SAVE0"), static_cast<uintmax_t>(4096));
	CHECK_EQ(find(vault.vaultSaves(), "SAVE0")->versions, 1);
}

ORBISLINK_TEST(only_the_last_backups_are_kept)
{
	Fixture f;
	TestConsole console(f.consoleRoot);
	const fs::path root = f.base / "vault";
	SaveVault vault(root.string());
	std::string error;
	SaveInfo save = *find(vault.scan(console, &error), "SAVE1");
	for(int i = 0; i < SaveVault::kVersionsKept + 2; ++i)
		CHECK(vault.backup(console, save, &error));
	const std::vector<SaveInfo> kept = vault.vaultSaves();
	CHECK_EQ(kept.size(), static_cast<size_t>(1));
	CHECK_EQ(kept[0].versions, SaveVault::kVersionsKept);
	// The earlier ones, each with the image and its key.
	int earlier = 0;
	for(const auto &entry : fs::directory_iterator(root / ".vault" / kPsid / "CUSA00001/SAVE1"))
		if(entry.is_directory())
		{
			++earlier;
			CHECK(fs::exists(entry.path() / "SAVE1"));
			CHECK(fs::exists(entry.path() / "SAVE1.bin"));
		}
	CHECK_EQ(earlier, SaveVault::kVersionsKept - 1);
}

// Saves copied in by hand from a PS4's USB drive are in the vault too.
ORBISLINK_TEST(saves_copied_from_a_usb_drive_are_read)
{
	Fixture f;
	const fs::path root = f.base / "vault";
	writeBytes(root / "PS4/SAVEDATA/1c020a82bb40e5fe/CUSA36843/PREBOOT.SAV", 3000, 'p');
	writeBytes(root / "PS4/SAVEDATA/1c020a82bb40e5fe/CUSA36843/PREBOOT.SAV.bin", 96, 'k');
	// Not a save: no key next to it.
	writeBytes(root / "PS4/SAVEDATA/1c020a82bb40e5fe/CUSA36843/notes.txt", 10, 'n');
	SaveVault vault(root.string());
	vault.setLinks({ { "1eb71bbd", "1c020a82bb40e5fe" } });
	const std::vector<SaveInfo> saves = vault.vaultSaves();
	CHECK_EQ(saves.size(), static_cast<size_t>(1));
	CHECK_EQ(saves[0].dir, std::string("PREBOOT.SAV"));
	CHECK_EQ(saves[0].account, std::string("1eb71bbd"));
	CHECK_EQ(saves[0].vaultBytes(), static_cast<int64_t>(3000 + 96));

	// Linked to the console user, it is the same save as the console's.
	TestConsole console(f.consoleRoot);
	writeBytes(f.home / "savedata/CUSA36843/sdimg_PREBOOT.SAV", 3000, 'p');
	writeBytes(f.home / "savedata/CUSA36843/PREBOOT.SAV.bin", 96, 'k');
	std::string error;
	const SaveInfo *both = find(vault.scan(console, &error), "PREBOOT.SAV");
	CHECK(both != nullptr);
	CHECK(both->sync() == SaveSync::Same);
}

// Backups made before the PS4 layout move over, every version, once their
// PSID is known.
ORBISLINK_TEST(earlier_backups_move_to_the_ps4_layout)
{
	Fixture f;
	const fs::path root = f.base / "vault";
	const fs::path folder = root / "1eb71bbd/CUSA00001/SAVE0";
	for(const std::string stamp : { "20261001-100000", "20261002-100000" })
	{
		writeBytes(folder / stamp / "savedata/sdimg_SAVE0", stamp == "20261001-100000" ? 1000 : 2000, 'x');
		writeBytes(folder / stamp / "savedata/SAVE0.bin", 96, 'k');
		writeBytes(folder / stamp / "savedata/sdimg_sce_bu_SAVE0", 10, 's');
		writePng(folder / stamp / "meta/icon0.png");
		std::ofstream(folder / stamp / "vault.json", std::ios::binary)
			<< R"({"account":"1eb71bbd","title_id":"CUSA00001","dir":"SAVE0","game_title":"Orbis Racing",)"
			   R"("save_title":"Career","account_id":"","backed_up_at":"2026-10-02 10:00","files":[)"
			   R"({"relative":"savedata/sdimg_SAVE0","size":2000,"modified":"Oct 02 10:00"},)"
			   R"({"relative":"savedata/SAVE0.bin","size":96,"modified":"Oct 02 10:00"}]})";
	}
	SaveVault vault(root.string());
	// Whose they are is not known yet: shown as they are, left in place.
	std::vector<SaveInfo> saves = vault.vaultSaves();
	CHECK_EQ(saves.size(), static_cast<size_t>(1));
	CHECK_EQ(saves[0].versions, 2);
	CHECK(fs::exists(folder));

	vault.setLinks({ { "1eb71bbd", "1c020a82bb40e5fe" } });
	saves = vault.vaultSaves();
	CHECK_EQ(saves.size(), static_cast<size_t>(1));
	CHECK_EQ(saves[0].psid, std::string("1c020a82bb40e5fe"));
	CHECK_EQ(saves[0].account, std::string("1eb71bbd"));
	CHECK_EQ(saves[0].saveTitle, std::string("Career"));
	CHECK_EQ(saves[0].versions, 2);
	CHECK(!saves[0].iconPath.empty());
	CHECK_EQ(fs::file_size(root / "PS4/SAVEDATA/1c020a82bb40e5fe/CUSA00001/SAVE0"), static_cast<uintmax_t>(2000));
	CHECK_EQ(fs::file_size(root / ".vault/1c020a82bb40e5fe/CUSA00001/SAVE0/20261001-100000/SAVE0"),
		static_cast<uintmax_t>(1000));
	CHECK(!fs::exists(root / "1eb71bbd"));
}

TEST_MAIN()
