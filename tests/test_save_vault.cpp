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
			item.modified = "Oct 03 18:42";
			entries->push_back(item);
		}
		return true;
	}
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

// The layout seen on a real console: the PS4's own backup copy of each
// save ("sce_bu_"), and savedata_meta entries that are files, not folders.
// No ACCOUNT_ID to be read: the console user's link gives the PSID.
ORBISLINK_TEST(real_layout_with_system_backups_and_a_linked_user)
{
	Fixture f;
	const fs::path data = f.home / "savedata/CUSA00009";
	writeBytes(data / "sdimg_RDR2SAVE0.SAV", 2048, 'a');
	writeBytes(data / "RDR2SAVE0.SAV.bin", 96, 'b');
	writeBytes(data / "sdimg_sce_bu_RDR2SAVE0.SAV", 2048, 'c');
	writeBytes(data / "sce_bu_RDR2SAVE0.SAV.bin", 96, 'd');
	writeData(f.home / "savedata_meta/user/CUSA00009/RDR2SAVE0.SAV",
		buildSfo({ { "MAINTITLE", "Red Dead" }, { "SUBTITLE", "Chapter 2" } }));
	writeBytes(f.home / "savedata_meta/user/CUSA00009/OTHERSAVE", 300, 'o');
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

	vault.setLinks({ { "1eb71bbd", "1C020A82BB40E5FE" } });
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
