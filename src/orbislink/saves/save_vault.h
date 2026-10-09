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
};

// The row the PS4 keeps for a save in its list of saves
// (/system_data/savedata/<user>/db/user/savedata.db, table "savedata"): a
// save whose files are there but has no row is not shown by the console.
// What Apollo Save Tool writes when it creates a save.
struct SaveDbEntry
{
	std::string titleId;
	std::string dir;
	std::string mainTitle;
	std::string subTitle;
	std::string detail;
	int64_t blocks = 0;     // of 32 KiB: the image's size / 32768
	int64_t accountId = 0;  // the owner's PSID, as a number
	uint32_t userId = 0;    // the console user's folder, as a number
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
	VaultOnly,   // in the vault, gone from the console
};

// A save of a game, on the console, in the vault, or both.
//
// PS4 layout: /user/home/<user>/savedata/<TITLE_ID>/ holds "sdimg_<dir>"
// (the encrypted image, with the save's own param.sfo inside) and
// "<dir>.bin" (its key), plus the system's backup copy of both as
// "sce_bu_<dir>"; /user/home/<user>/savedata_meta/user/<TITLE_ID>/ holds
// entries named after each save (its listing data and icon, where the
// server lets them be read).
struct SaveInfo
{
	std::string account; // the console user's folder, "1a2b3c4d"; "" when not known
	std::string titleId; // "CUSA00001"
	std::string dir;     // "SAVEDATA00"
	std::string gameTitle;
	std::string saveTitle;
	std::string detail;  // what the game wrote about it: chapter, level…
	// The PSN account it belongs to: param.sfo's ACCOUNT_ID, its 8 bytes in
	// hex as stored (little-endian, like the Account ID of Remote Play).
	std::string accountId;
	// The owner's PSID as the PS4 names its folders of saves (16 hex digits,
	// the number written the usual way): from the console user's link to an
	// Account ID, else from the save's ACCOUNT_ID. "" while not known.
	std::string psid;
	bool onConsole = false;
	bool inVault = false;
	std::vector<SaveFile> consoleFiles;
	std::vector<SaveFile> vaultFiles; // as they were at the latest backup
	std::string backedUpAt;           // "2026-10-03 18:42"
	int versions = 0;                 // backups kept
	std::string iconPath;             // local copy of the save's icon, if any

	std::string key() const { return (account.empty() ? psid : account) + "/" + titleId + "/" + dir; }
	int64_t consoleBytes() const;
	int64_t vaultBytes() const;
	SaveSync sync() const;
};

// The vault: a folder on this PC with every save backed up, laid out as
// the PS4 lays out the saves it copies to a USB drive.
//
// <root>/PS4/SAVEDATA/<PSID>/<TITLE_ID>/<dir>      the latest backup's image
// <root>/PS4/SAVEDATA/<PSID>/<TITLE_ID>/<dir>.bin  and its key
// <root>/.vault/<PSID>/<TITLE_ID>/<dir>/info.json  names, dates, sizes, user
// <root>/.vault/<PSID>/<TITLE_ID>/<dir>/<YYYYMMDD-HHMMSS>/  earlier backups
// <root>/.cache/                                   game icons and names
//
// Backing up only reads the console. Putting a save back writes its image
// and key where the console keeps them (never while its game runs); a save
// the console no longer lists also needs its row in the console's list of
// saves (see SaveDbEntry), which the caller adds.
class SaveVault
{
public:
	using Progress = std::function<void(const std::string &what, double fraction)>;
	static constexpr int kVersionsKept = 5;

	explicit SaveVault(std::string root);
	const std::string &root() const { return root_; }

	// Console user folder → PSID (16 hex digits): whose saves are whose.
	void setLinks(std::map<std::string, std::string> psidOfUser);

	// The vault alone, from the PC.
	std::vector<SaveInfo> vaultSaves();
	// The console's saves merged with the vault's.
	std::vector<SaveInfo> scan(SaveRemote &remote, std::string *error, const Progress &progress = {});

	// Copies the save's image and key from the console into the vault (the
	// backup before goes with the earlier ones; beyond kVersionsKept, the
	// oldest go). Needs the save's PSID. The info is updated.
	bool backup(SaveRemote &remote, SaveInfo &save, std::string *error, const Progress &progress = {});
	// Puts the latest backup's image and key back on the console, in the
	// folder of `account` (the console user it belongs to; the save's own
	// when empty), and checks that both arrived whole.
	bool restore(SaveRemote &remote, const SaveInfo &save, const std::string &account, std::string *error,
		const Progress &progress = {});
	// The row for the console's list of saves, for a save just put back.
	SaveDbEntry dbEntry(const SaveInfo &save, const std::string &account) const;
	static std::string saveDbPath(const std::string &account);
	bool removeFromVault(const SaveInfo &save, std::string *error);

	// The game's own name (from its param.sfo, read once), or "".
	std::string gameTitle(SaveRemote *remote, const std::string &titleId);
	// Local copy of a game's icon (downloaded once), or "" if there is none.
	std::string gameIcon(SaveRemote *remote, const std::string &titleId);

	static std::string consoleSaveDir(const std::string &account, const std::string &titleId);
	// The game's folder in savedata_meta; each save's entries in it are
	// named after it (files, or folders on some systems).
	static std::string consoleMetaRoot(const std::string &account, const std::string &titleId);
	// ACCOUNT_ID as stored (little-endian hex) → the PSID folder name.
	static std::string psidOfAccountId(const std::string &storedHex);

private:
	std::string psidFor(const SaveInfo &save) const;
	std::string userOf(const std::string &psid) const;
	std::string imageDir(const std::string &psid, const std::string &titleId) const;
	std::string infoDir(const std::string &psid, const std::string &titleId, const std::string &dir) const;
	void readVault(std::map<std::string, SaveInfo> &out);
	void readLegacy(std::map<std::string, SaveInfo> &out) const;
	void migrateLegacy();
	void prune(const std::string &infoFolder) const;

	std::string root_;
	std::map<std::string, std::string> links_;
};

} // namespace orbislink
