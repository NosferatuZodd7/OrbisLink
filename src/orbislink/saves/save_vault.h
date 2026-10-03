// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace orbislink {

struct FtpEntry;

// What the vault needs from the console: an FTP server with the console's
// file system (GoldHEN, etaHEN…). An interface so the tests can use a folder.
class SaveRemote
{
public:
	virtual ~SaveRemote() = default;
	// Empty and true for a folder that is not there.
	virtual bool list(const std::string &dir, std::vector<FtpEntry> *entries, std::string *error) = 0;
	virtual bool download(const std::string &remote, const std::string &local, std::string *error) = 0;
	virtual bool upload(const std::string &local, const std::string &remote, std::string *error) = 0;
	virtual bool makeDirectory(const std::string &dir) = 0;
	virtual bool removeFile(const std::string &path, std::string *error) = 0;
	virtual bool removeDirectory(const std::string &path) = 0;
};

// One file of a save, as the console lists it. `relative` is
// "savedata/<file>" (the encrypted image and its key) or "meta/<file>"
// (param.sfo, icon0.png…).
struct SaveFile
{
	std::string relative;
	int64_t size = 0;
	std::string modified;
	bool operator==(const SaveFile &o) const
	{
		return relative == o.relative && size == o.size && modified == o.modified;
	}
};

enum class SaveSync
{
	Same,        // the console's save is the one in the vault
	Changed,     // both have it, but the console's changed since the backup
	ConsoleOnly, // never backed up
	VaultOnly,   // in the vault, gone from the console: can be put back
};

// A save of a game, on the console, in the vault, or both.
//
// PS4 layout: /user/home/<account>/savedata/<TITLE_ID>/ holds
// "sdimg_<dir>" (the encrypted image, with the save's own param.sfo inside)
// and "<dir>.bin" (its key), plus the system's backup copy of both as
// "sce_bu_<dir>"; /user/home/<account>/savedata_meta/user/<TITLE_ID>/ holds
// entries named after each save (its listing data and icon, where there
// are any). They are copied as they are: a save only works on the console
// and account it came from, and the PS4 keeps a database of its saves
// (/system_data/savedata) that copying files does not touch — so a save is
// only put back over one the console still lists.
struct SaveInfo
{
	std::string account; // "1eb71bbd"
	std::string titleId; // "CUSA00001"
	std::string dir;     // "SAVEDATA00"
	std::string gameTitle;
	std::string saveTitle;
	std::string detail;  // what the game wrote about it: chapter, level…
	// The PSN account it belongs to: param.sfo's ACCOUNT_ID, its 8 bytes in
	// hex as stored (little-endian, like the Account ID of Remote Play).
	std::string accountId;
	bool onConsole = false;
	bool inVault = false;
	std::vector<SaveFile> consoleFiles;
	std::vector<SaveFile> vaultFiles; // as they were at the latest backup
	std::string backedUpAt;           // "2026-10-03 18:42"
	int versions = 0;                 // backups kept
	std::string iconPath;             // local copy of the save's icon, if any

	std::string key() const { return account + "/" + titleId + "/" + dir; }
	int64_t consoleBytes() const;
	int64_t vaultBytes() const;
	SaveSync sync() const;
};

// The vault: a folder on this PC with every save backed up, by account,
// game and save, each with the last few backups.
//
// <root>/<account>/<TITLE_ID>/<dir>/<YYYYMMDD-HHMMSS>/
//     savedata/…   meta/…   vault.json
// <root>/.cache/   game icons and the console's param.sfo/icons
class SaveVault
{
public:
	using Progress = std::function<void(const std::string &what, double fraction)>;
	static constexpr int kVersionsKept = 5;

	explicit SaveVault(std::string root);
	const std::string &root() const { return root_; }

	// The vault alone, from the PC.
	std::vector<SaveInfo> vaultSaves() const;
	// The console's saves merged with the vault's.
	std::vector<SaveInfo> scan(SaveRemote &remote, std::string *error, const Progress &progress = {});

	// Copies the save from the console into a new backup (older ones beyond
	// kVersionsKept go). The info is updated.
	bool backup(SaveRemote &remote, SaveInfo &save, std::string *error, const Progress &progress = {});
	// Puts the latest backup back on the console, as it was.
	bool restore(SaveRemote &remote, const SaveInfo &save, std::string *error, const Progress &progress = {});
	// No "delete from the console": the PS4 lists its saves in a database
	// of its own, and taking the files away over FTP leaves it with an entry
	// it reports as corrupted. Saves are deleted on the PS4 itself.
	bool removeFromVault(const SaveInfo &save, std::string *error);

	// The game's own name (from its param.sfo, read once), or "".
	std::string gameTitle(SaveRemote *remote, const std::string &titleId);
	// Local copy of a game's icon (downloaded once), or "" if there is none.
	std::string gameIcon(SaveRemote *remote, const std::string &titleId);

	static std::string consoleSaveDir(const std::string &account, const std::string &titleId);
	// The game's folder in savedata_meta; each save's entries in it are
	// named after it (files, or folders on some systems).
	static std::string consoleMetaRoot(const std::string &account, const std::string &titleId);

private:
	std::string saveFolder(const SaveInfo &save) const;
	std::string latestVersion(const SaveInfo &save) const;
	void readVault(std::map<std::string, SaveInfo> &out) const;
	void prune(const SaveInfo &save) const;

	std::string root_;
};

} // namespace orbislink
