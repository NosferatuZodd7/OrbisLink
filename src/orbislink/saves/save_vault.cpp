// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/saves/save_vault.h"

#include "orbislink/common/json.h"
#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/ftp/ftp_client.h"
#include "orbislink/pkg/sfo_parser.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

namespace fs = std::filesystem;

namespace orbislink {

namespace {

// An account folder under /user/home: eight hex digits.
bool isAccountId(const std::string &name)
{
	if(name.size() != 8)
		return false;
	return std::all_of(name.begin(), name.end(),
		[](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

// The date as LIST gives it comes in two shapes: "Oct 03 18:42" for recent
// files and "Oct 03 2025" for old ones; the same file can change shape when
// it ages. Two dates in different shapes do not count as a change.
bool sameModified(const std::string &a, const std::string &b)
{
	if(a.empty() || b.empty())
		return true;
	const bool aTime = a.find(':') != std::string::npos;
	const bool bTime = b.find(':') != std::string::npos;
	return aTime != bTime || a == b;
}

std::string localStamp(const char *format)
{
	const std::time_t now = std::time(nullptr);
	std::tm parts {};
#ifdef _WIN32
	localtime_s(&parts, &now);
#else
	localtime_r(&now, &parts);
#endif
	char text[32] = {};
	std::strftime(text, sizeof(text), format, &parts);
	return text;
}

bool readFile(const std::string &path, std::vector<uint8_t> *data)
{
	std::ifstream in(fs::u8path(path), std::ios::binary);
	if(!in)
		return false;
	data->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return true;
}

void ensureDir(const std::string &path)
{
	std::error_code ignored;
	fs::create_directories(fs::u8path(path), ignored);
}

// The names a save's param.sfo gives it.
void readSfo(const std::string &path, SaveInfo &save)
{
	std::vector<uint8_t> data;
	Sfo sfo;
	if(!readFile(path, &data) || !sfo.parse(data))
		return;
	const std::string title = sfo.stringValue("MAINTITLE", sfo.stringValue("TITLE"));
	if(!title.empty())
		save.gameTitle = title;
	const std::string subtitle = sfo.stringValue("SUBTITLE");
	if(!subtitle.empty())
		save.saveTitle = subtitle;
	const std::string detail = sfo.stringValue("DETAIL");
	if(!detail.empty())
		save.detail = detail;
	if(const SfoEntry *account = sfo.find("ACCOUNT_ID"))
	{
		if(account->value.size() >= 8)
		{
			static const char *digits = "0123456789abcdef";
			std::string hex;
			for(size_t i = 0; i < 8; ++i)
			{
				hex += digits[account->value[i] >> 4];
				hex += digits[account->value[i] & 0x0F];
			}
			// All zeros: a save made offline, with no account.
			if(hex != std::string(16, '0'))
				save.accountId = hex;
		}
	}
}

Json filesToJson(const std::vector<SaveFile> &files)
{
	Json list = Json::makeArray();
	for(const SaveFile &file : files)
	{
		Json item = Json::makeObject();
		item.set("relative", Json::fromString(file.relative));
		item.set("size", Json::fromInt(file.size));
		item.set("modified", Json::fromString(file.modified));
		list.push(std::move(item));
	}
	return list;
}

// "savedata/x" → the console path of that file; "meta/x" (or "meta/x/y")
// → under the game's folder in savedata_meta.
std::string consolePathOf(const SaveInfo &save, const std::string &relative)
{
	if(startsWith(relative, "savedata/"))
		return SaveVault::consoleSaveDir(save.account, save.titleId) + "/" + relative.substr(9);
	return SaveVault::consoleMetaRoot(save.account, save.titleId) + "/" + relative.substr(5);
}

// Only plain names: a file name from the console or a vault.json must not
// lead outside its folder. "savedata/<file>", "meta/<file>" or
// "meta/<folder>/<file>".
bool safeRelative(const std::string &relative)
{
	std::string rest;
	size_t depth = 0;
	if(startsWith(relative, "savedata/"))
	{
		rest = relative.substr(9);
		depth = 1;
	}
	else if(startsWith(relative, "meta/"))
	{
		rest = relative.substr(5);
		depth = 2;
	}
	else
		return false;
	if(rest.find('\\') != std::string::npos)
		return false;
	const std::vector<std::string> parts = split(rest, '/', true);
	if(parts.empty() || parts.size() > depth)
		return false;
	for(const std::string &part : parts)
		if(part.empty() || part == "." || part == "..")
			return false;
	return true;
}

// The PS4 keeps its own backup copy of a save as "sce_bu_<name>": it goes
// with the save, not as another one.
std::string saveNameOf(const std::string &name)
{
	return startsWith(name, "sce_bu_") ? name.substr(7) : name;
}

// Whether an entry of the game's savedata_meta folder is the save's.
bool metaBelongsTo(const std::string &entry, const std::string &dir)
{
	const std::string name = saveNameOf(entry);
	return name == dir || startsWith(name, dir + "_") || startsWith(name, dir + ".");
}

// What a small file from the console is: a param.sfo or a PNG.
bool startsWithBytes(const std::string &path, const char *magic, size_t length)
{
	std::ifstream in(fs::u8path(path), std::ios::binary);
	std::string head(length, '\0');
	return in.read(&head[0], static_cast<std::streamsize>(length)) && head.compare(0, length, magic, length) == 0;
}

// Makes a folder on the console unless it is there: asking an FTP server
// to make one that exists is an error, and the client retries it.
void ensureRemoteDir(SaveRemote &remote, const std::string &dir)
{
	const size_t slash = dir.find_last_of('/');
	const std::string parent = slash == 0 ? "/" : dir.substr(0, slash);
	const std::string name = dir.substr(slash + 1);
	std::vector<FtpEntry> entries;
	std::string error;
	if(remote.list(parent, &entries, &error))
		for(const FtpEntry &entry : entries)
			if(entry.isDirectory && entry.name == name)
				return;
	remote.makeDirectory(dir);
}

} // namespace

int64_t SaveInfo::consoleBytes() const
{
	int64_t total = 0;
	for(const SaveFile &file : consoleFiles)
		total += file.size;
	return total;
}

int64_t SaveInfo::vaultBytes() const
{
	int64_t total = 0;
	for(const SaveFile &file : vaultFiles)
		total += file.size;
	return total;
}

SaveSync SaveInfo::sync() const
{
	if(onConsole && !inVault)
		return SaveSync::ConsoleOnly;
	if(!onConsole)
		return SaveSync::VaultOnly;
	// The encrypted image and its key decide; the icon and param.sfo follow them.
	std::map<std::string, const SaveFile *> backedUp;
	for(const SaveFile &file : vaultFiles)
		backedUp[file.relative] = &file;
	for(const SaveFile &file : consoleFiles)
	{
		if(!startsWith(file.relative, "savedata/"))
			continue;
		const auto match = backedUp.find(file.relative);
		if(match == backedUp.end() || match->second->size != file.size
			|| !sameModified(match->second->modified, file.modified))
			return SaveSync::Changed;
	}
	return SaveSync::Same;
}

SaveVault::SaveVault(std::string root) : root_(std::move(root)) {}

std::string SaveVault::consoleSaveDir(const std::string &account, const std::string &titleId)
{
	return "/user/home/" + account + "/savedata/" + titleId;
}

std::string SaveVault::consoleMetaRoot(const std::string &account, const std::string &titleId)
{
	return "/user/home/" + account + "/savedata_meta/user/" + titleId;
}

std::string SaveVault::saveFolder(const SaveInfo &save) const
{
	return joinPath(joinPath(joinPath(root_, save.account), save.titleId), save.dir);
}

std::string SaveVault::latestVersion(const SaveInfo &save) const
{
	std::string latest;
	std::error_code error;
	for(const auto &entry : fs::directory_iterator(fs::u8path(saveFolder(save)), error))
	{
		const std::string name = entry.path().filename().u8string();
		if(entry.is_directory() && fileExists(joinPath(entry.path().u8string(), "vault.json"))
			&& name > latest)
			latest = name;
	}
	return latest.empty() ? std::string() : joinPath(saveFolder(save), latest);
}

void SaveVault::readVault(std::map<std::string, SaveInfo> &out) const
{
	std::error_code error;
	for(const auto &account : fs::directory_iterator(fs::u8path(root_), error))
	{
		if(!account.is_directory() || !isAccountId(account.path().filename().u8string()))
			continue;
		for(const auto &title : fs::directory_iterator(account.path(), error))
		{
			if(!title.is_directory())
				continue;
			for(const auto &dir : fs::directory_iterator(title.path(), error))
			{
				if(!dir.is_directory())
					continue;
				SaveInfo save;
				save.account = account.path().filename().u8string();
				save.titleId = title.path().filename().u8string();
				save.dir = dir.path().filename().u8string();
				for(const auto &version : fs::directory_iterator(dir.path(), error))
					if(version.is_directory() && fileExists(joinPath(version.path().u8string(), "vault.json")))
						++save.versions;
				const std::string latest = latestVersion(save);
				if(latest.empty())
					continue;
				std::vector<uint8_t> data;
				if(!readFile(joinPath(latest, "vault.json"), &data))
					continue;
				const Json info = Json::parse(std::string(data.begin(), data.end()));
				save.inVault = true;
				save.gameTitle = info["game_title"].toString();
				save.saveTitle = info["save_title"].toString();
				save.detail = info["detail"].toString();
				save.accountId = info["account_id"].toString();
				save.backedUpAt = info["backed_up_at"].toString();
				for(const Json &item : info["files"].items())
				{
					SaveFile file { item["relative"].toString(), item["size"].toInt(),
						item["modified"].toString() };
					if(safeRelative(file.relative))
						save.vaultFiles.push_back(file);
				}
				const std::string icon = joinPath(joinPath(latest, "meta"), "icon0.png");
				if(fileExists(icon))
					save.iconPath = icon;
				out[save.key()] = save;
			}
		}
	}
}

std::vector<SaveInfo> SaveVault::vaultSaves() const
{
	std::map<std::string, SaveInfo> saves;
	readVault(saves);
	std::vector<SaveInfo> list;
	for(auto &pair : saves)
		list.push_back(std::move(pair.second));
	return list;
}

std::vector<SaveInfo> SaveVault::scan(SaveRemote &remote, std::string *error, const Progress &progress)
{
	std::map<std::string, SaveInfo> saves;
	readVault(saves);
	// What the vault knew stays; the console's side is filled in below.
	std::map<std::string, SaveInfo> merged = saves;

	std::vector<FtpEntry> accounts;
	if(!remote.list("/user/home", &accounts, error))
		return {};
	const std::string cache = joinPath(joinPath(root_, ".cache"), "console");

	std::vector<std::pair<std::string, std::string>> titles; // account, title
	for(const FtpEntry &account : accounts)
	{
		if(!account.isDirectory || !isAccountId(account.name))
			continue;
		std::vector<FtpEntry> games;
		std::string listError;
		if(!remote.list("/user/home/" + account.name + "/savedata", &games, &listError))
			continue;
		for(const FtpEntry &game : games)
			if(game.isDirectory && game.name != "." && game.name != "..")
				titles.emplace_back(account.name, game.name);
	}

	for(size_t t = 0; t < titles.size(); ++t)
	{
		const std::string &account = titles[t].first;
		const std::string &titleId = titles[t].second;
		if(progress)
			progress(titleId, titles.empty() ? 1.0 : static_cast<double>(t) / titles.size());
		std::vector<FtpEntry> files;
		std::string listError;
		if(!remote.list(consoleSaveDir(account, titleId), &files, &listError))
			continue;
		// sdimg_<name> and <name>.bin make one save (with the PS4's own
		// backup copy of them, sce_bu_<name>).
		std::map<std::string, std::vector<SaveFile>> byDir;
		for(const FtpEntry &file : files)
		{
			if(file.isDirectory)
				continue;
			std::string dir;
			if(startsWith(file.name, "sdimg_"))
				dir = file.name.substr(6);
			else if(endsWith(file.name, ".bin"))
				dir = file.name.substr(0, file.name.size() - 4);
			dir = saveNameOf(dir);
			if(dir.empty())
				continue;
			byDir[dir].push_back({ "savedata/" + file.name, file.size, file.modified });
		}
		if(byDir.empty())
			continue;

		// The game's savedata_meta folder, once: each save's entries there
		// are files (or, on some systems, folders) named after it.
		std::vector<FtpEntry> meta;
		std::string metaError;
		remote.list(consoleMetaRoot(account, titleId), &meta, &metaError);

		for(auto &pair : byDir)
		{
			SaveInfo &save = merged[account + "/" + titleId + "/" + pair.first];
			save.account = account;
			save.titleId = titleId;
			save.dir = pair.first;
			save.onConsole = true;
			save.consoleFiles = pair.second;

			const std::string local = joinPath(joinPath(joinPath(cache, account), titleId), save.dir);
			std::vector<SaveFile> metaFiles;
			for(const FtpEntry &entry : meta)
			{
				if(!metaBelongsTo(entry.name, save.dir))
					continue;
				if(!entry.isDirectory)
				{
					metaFiles.push_back({ "meta/" + entry.name, entry.size, entry.modified });
					continue;
				}
				std::vector<FtpEntry> inside;
				std::string insideError;
				if(!remote.list(consoleMetaRoot(account, titleId) + "/" + entry.name, &inside, &insideError))
					continue;
				for(const FtpEntry &file : inside)
					if(!file.isDirectory)
						metaFiles.push_back({ "meta/" + entry.name + "/" + file.name, file.size, file.modified });
			}
			for(const SaveFile &file : metaFiles)
			{
				if(!safeRelative(file.relative))
					continue;
				save.consoleFiles.push_back(file);
				// Small ones may be the save's param.sfo (names, account) or
				// its icon: a look at the first bytes says.
				if(file.size <= 0 || file.size > 512 * 1024)
					continue;
				ensureDir(local);
				std::string flat = file.relative.substr(5);
				std::replace(flat.begin(), flat.end(), '/', '_');
				const std::string target = joinPath(local, flat);
				if(fileSize(target) != file.size)
				{
					std::string downloadError;
					remote.download(consolePathOf(save, file.relative), target, &downloadError);
				}
				if(startsWithBytes(target, "\0PSF", 4))
					readSfo(target, save);
				else if(startsWithBytes(target, "\x89PNG", 4) && save.iconPath.empty())
					save.iconPath = target;
			}
		}
	}

	// A game's own name, for saves that do not carry one.
	for(auto &pair : merged)
		if(pair.second.gameTitle.empty())
			pair.second.gameTitle = gameTitle(&remote, pair.second.titleId);
	if(progress)
		progress(std::string(), 1.0);

	std::vector<SaveInfo> list;
	for(auto &pair : merged)
		list.push_back(std::move(pair.second));
	return list;
}

bool SaveVault::backup(SaveRemote &remote, SaveInfo &save, std::string *error, const Progress &progress)
{
	if(!save.onConsole || save.consoleFiles.empty())
	{
		if(error)
			*error = "This save is not on the console.";
		return false;
	}
	std::string version = joinPath(saveFolder(save), localStamp("%Y%m%d-%H%M%S"));
	for(int n = 2; directoryExists(version); ++n)
		version = joinPath(saveFolder(save), localStamp("%Y%m%d-%H%M%S") + "-" + std::to_string(n));

	for(size_t i = 0; i < save.consoleFiles.size(); ++i)
	{
		const SaveFile &file = save.consoleFiles[i];
		if(!safeRelative(file.relative))
			continue;
		if(progress)
			progress(file.relative, static_cast<double>(i) / save.consoleFiles.size());
		const std::string local = joinPath(version, file.relative);
		ensureDir(fs::u8path(local).parent_path().u8string());
		if(!remote.download(consolePathOf(save, file.relative), local, error))
		{
			std::error_code ignored;
			fs::remove_all(fs::u8path(version), ignored);
			return false;
		}
		// What arrived must be what the console listed.
		if(fileSize(local) != file.size)
		{
			if(error)
				*error = "Incomplete copy of " + file.relative + ".";
			std::error_code ignored;
			fs::remove_all(fs::u8path(version), ignored);
			return false;
		}
	}

	save.backedUpAt = localStamp("%Y-%m-%d %H:%M");
	Json info = Json::makeObject();
	info.set("account", Json::fromString(save.account));
	info.set("title_id", Json::fromString(save.titleId));
	info.set("dir", Json::fromString(save.dir));
	info.set("game_title", Json::fromString(save.gameTitle));
	info.set("save_title", Json::fromString(save.saveTitle));
	info.set("detail", Json::fromString(save.detail));
	info.set("account_id", Json::fromString(save.accountId));
	info.set("backed_up_at", Json::fromString(save.backedUpAt));
	info.set("files", filesToJson(save.consoleFiles));
	{
		std::ofstream out(fs::u8path(joinPath(version, "vault.json")), std::ios::binary | std::ios::trunc);
		out << info.dump();
		if(!out)
		{
			if(error)
				*error = "Could not write to the vault folder.";
			return false;
		}
	}
	prune(save);
	save.inVault = true;
	save.vaultFiles = save.consoleFiles;
	save.versions = std::min(save.versions + 1, kVersionsKept);
	const std::string icon = joinPath(joinPath(version, "meta"), "icon0.png");
	if(fileExists(icon))
		save.iconPath = icon;
	if(progress)
		progress(std::string(), 1.0);
	logInfo("Saves: backed up " + save.key());
	return true;
}

void SaveVault::prune(const SaveInfo &save) const
{
	std::vector<std::string> versions;
	std::error_code error;
	for(const auto &entry : fs::directory_iterator(fs::u8path(saveFolder(save)), error))
		if(entry.is_directory())
			versions.push_back(entry.path().u8string());
	std::sort(versions.begin(), versions.end());
	while(versions.size() > static_cast<size_t>(kVersionsKept))
	{
		std::error_code ignored;
		fs::remove_all(fs::u8path(versions.front()), ignored);
		versions.erase(versions.begin());
	}
}

bool SaveVault::restore(SaveRemote &remote, const SaveInfo &save, std::string *error, const Progress &progress)
{
	const std::string latest = latestVersion(save);
	if(latest.empty() || save.vaultFiles.empty())
	{
		if(error)
			*error = "This save has no backup in the vault.";
		return false;
	}
	// The folders, one level at a time: FTP servers do not make parents.
	const std::string home = "/user/home/" + save.account;
	std::vector<std::string> dirs = { home, home + "/savedata", consoleSaveDir(save.account, save.titleId) };
	std::set<std::string> metaDirs;
	for(const SaveFile &file : save.vaultFiles)
	{
		if(!startsWith(file.relative, "meta/"))
			continue;
		metaDirs.insert(consoleMetaRoot(save.account, save.titleId));
		const std::string rest = file.relative.substr(5);
		if(rest.find('/') != std::string::npos)
			metaDirs.insert(consoleMetaRoot(save.account, save.titleId) + "/" + rest.substr(0, rest.find('/')));
	}
	if(!metaDirs.empty())
	{
		dirs.push_back(home + "/savedata_meta");
		dirs.push_back(home + "/savedata_meta/user");
		dirs.insert(dirs.end(), metaDirs.begin(), metaDirs.end());
	}
	for(const std::string &dir : dirs)
		ensureRemoteDir(remote, dir);

	for(size_t i = 0; i < save.vaultFiles.size(); ++i)
	{
		const SaveFile &file = save.vaultFiles[i];
		if(!safeRelative(file.relative))
			continue;
		if(progress)
			progress(file.relative, static_cast<double>(i) / save.vaultFiles.size());
		if(!remote.upload(joinPath(latest, file.relative), consolePathOf(save, file.relative), error))
		{
			logWarning("Saves: putting back " + save.key() + " failed at " + file.relative);
			return false;
		}
	}

	// What the console has now, to check every file arrived whole — a save
	// put back in part is worse than none — and to take the console's new
	// dates (it dates what was put back as written now), so the backup still
	// reads as the same save and not as one changed since.
	std::vector<SaveFile> now;
	std::vector<std::pair<std::string, std::string>> listed = {
		{ consoleSaveDir(save.account, save.titleId), "savedata/" } };
	for(const std::string &dir : metaDirs)
	{
		const std::string root = consoleMetaRoot(save.account, save.titleId);
		listed.emplace_back(dir, dir == root ? "meta/" : "meta/" + dir.substr(root.size() + 1) + "/");
	}
	for(const auto &where : listed)
	{
		std::vector<FtpEntry> entries;
		std::string listError;
		if(!remote.list(where.first, &entries, &listError))
			continue;
		for(const FtpEntry &entry : entries)
			if(!entry.isDirectory)
				now.push_back({ where.second + entry.name, entry.size, entry.modified });
	}
	for(const SaveFile &file : save.vaultFiles)
	{
		if(!safeRelative(file.relative))
			continue;
		const auto found = std::find_if(now.begin(), now.end(),
			[&](const SaveFile &fresh) { return fresh.relative == file.relative; });
		if(found == now.end() || found->size != file.size)
		{
			if(error)
				*error = "The console did not take " + file.relative + " whole ("
					+ (found == now.end() ? std::string("missing") : std::to_string(found->size) + " of "
						+ std::to_string(file.size) + " bytes") + ").";
			logWarning("Saves: putting back " + save.key() + ": " + (error ? *error : std::string()));
			return false;
		}
	}
	std::vector<uint8_t> raw;
	if(readFile(joinPath(latest, "vault.json"), &raw))
	{
		Json info = Json::parse(std::string(raw.begin(), raw.end()));
		std::vector<SaveFile> files = save.vaultFiles;
		for(SaveFile &file : files)
			for(const SaveFile &fresh : now)
				if(fresh.relative == file.relative && fresh.size == file.size)
					file.modified = fresh.modified;
		info.set("files", filesToJson(files));
		std::ofstream out(fs::u8path(joinPath(latest, "vault.json")), std::ios::binary | std::ios::trunc);
		out << info.dump();
	}
	if(progress)
		progress(std::string(), 1.0);
	logInfo("Saves: put back " + save.key());
	return true;
}

std::string SaveVault::gameTitle(SaveRemote *remote, const std::string &titleId)
{
	const std::string dir = joinPath(joinPath(root_, ".cache"), "games");
	const std::string local = joinPath(dir, titleId + ".sfo");
	if(fileSize(local) <= 0 && remote)
	{
		ensureDir(dir);
		std::string error;
		if(!remote->download("/user/appmeta/" + titleId + "/param.sfo", local, &error))
		{
			std::error_code ignored;
			fs::remove(fs::u8path(local), ignored);
		}
	}
	std::vector<uint8_t> data;
	Sfo sfo;
	if(!readFile(local, &data) || !sfo.parse(data))
		return std::string();
	return sfo.stringValue("TITLE");
}

bool SaveVault::removeFromVault(const SaveInfo &save, std::string *error)
{
	std::error_code failure;
	fs::remove_all(fs::u8path(saveFolder(save)), failure);
	if(failure)
	{
		if(error)
			*error = failure.message();
		return false;
	}
	// Empty game and account folders go too.
	const fs::path title = fs::u8path(saveFolder(save)).parent_path();
	std::error_code ignored;
	if(fs::is_empty(title, ignored))
		fs::remove(title, ignored);
	if(fs::is_empty(title.parent_path(), ignored))
		fs::remove(title.parent_path(), ignored);
	logInfo("Saves: deleted from the vault " + save.key());
	return true;
}

std::string SaveVault::gameIcon(SaveRemote *remote, const std::string &titleId)
{
	const std::string dir = joinPath(joinPath(root_, ".cache"), "games");
	const std::string local = joinPath(dir, titleId + ".png");
	if(fileSize(local) > 0)
		return local;
	if(!remote)
		return std::string();
	ensureDir(dir);
	std::string error;
	if(!remote->download("/user/appmeta/" + titleId + "/icon0.png", local, &error) || fileSize(local) <= 0)
	{
		std::error_code ignored;
		fs::remove(fs::u8path(local), ignored);
		return std::string();
	}
	return local;
}

// ── FolderRemote

std::string FolderRemote::at(const std::string &path) const
{
	std::string rel = path;
	while(!rel.empty() && rel.front() == '/')
		rel.erase(rel.begin());
	return rel.empty() ? root_ : joinPath(root_, rel);
}

bool FolderRemote::list(const std::string &dir, std::vector<FtpEntry> *entries, std::string *error)
{
	entries->clear();
	std::error_code failure;
	fs::directory_iterator it(fs::u8path(at(dir)), failure);
	if(failure)
	{
		if(error)
			*error = failure.message();
		return false;
	}
	for(const auto &entry : it)
	{
		FtpEntry item;
		item.name = entry.path().filename().u8string();
		item.path = (dir == "/" ? std::string() : dir) + "/" + item.name;
		std::error_code ignored;
		item.isDirectory = entry.is_directory(ignored);
		item.size = item.isDirectory ? 0 : static_cast<int64_t>(entry.file_size(ignored));
		entries->push_back(item);
	}
	return true;
}

bool FolderRemote::download(const std::string &remote, const std::string &local, std::string *error)
{
	std::error_code failure;
	fs::copy_file(fs::u8path(at(remote)), fs::u8path(local), fs::copy_options::overwrite_existing, failure);
	if(failure && error)
		*error = failure.message();
	return !failure;
}

bool FolderRemote::upload(const std::string &local, const std::string &remote, std::string *error)
{
	std::error_code failure;
	fs::copy_file(fs::u8path(local), fs::u8path(at(remote)), fs::copy_options::overwrite_existing, failure);
	if(failure && error)
		*error = failure.message();
	return !failure;
}

bool FolderRemote::makeDirectory(const std::string &dir)
{
	std::error_code ignored;
	return fs::create_directory(fs::u8path(at(dir)), ignored);
}

bool FolderRemote::removeFile(const std::string &path, std::string *error)
{
	std::error_code failure;
	fs::remove(fs::u8path(at(path)), failure);
	if(failure && error)
		*error = failure.message();
	return !failure;
}

bool FolderRemote::removeDirectory(const std::string &path)
{
	std::error_code ignored;
	return fs::remove(fs::u8path(at(path)), ignored);
}

// ── The PS4's USB layout

namespace {

std::string usbRoot(const std::string &base)
{
	return (base == "/" ? std::string() : base) + "/PS4/SAVEDATA";
}

bool isPsid(const std::string &name)
{
	return name.size() == 16 && std::all_of(name.begin(), name.end(),
		[](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

// The PSID's bytes as param.sfo stores them (little-endian), in hex: the
// PSID folder name is the number written the usual way (big-endian).
std::string storedAccountId(const std::string &psid)
{
	std::string out;
	for(size_t i = psid.size(); i >= 2; i -= 2)
		out += psid.substr(i - 2, 2);
	return toLower(out);
}

} // namespace

bool SaveVault::exportToUsb(SaveRemote &target, const std::string &base, const SaveInfo &save,
	const std::string &psid, std::string *error)
{
	if(!isPsid(psid))
	{
		if(error)
			*error = "No PSID for this save's account.";
		return false;
	}
	const std::string latest = latestVersion(save);
	const std::string image = joinPath(joinPath(latest, "savedata"), "sdimg_" + save.dir);
	const std::string key = joinPath(joinPath(latest, "savedata"), save.dir + ".bin");
	if(latest.empty() || !fileExists(image) || !fileExists(key))
	{
		if(error)
			*error = "This save has no complete backup in the vault.";
		return false;
	}
	const std::string root = usbRoot(base);
	const std::string psidDir = root + "/" + toLower(psid);
	const std::string titleDir = psidDir + "/" + save.titleId;
	for(const std::string &dir : { (base == "/" ? std::string() : base) + "/PS4", root, psidDir, titleDir })
		ensureRemoteDir(target, dir);
	if(!target.upload(image, titleDir + "/" + save.dir, error)
		|| !target.upload(key, titleDir + "/" + save.dir + ".bin", error))
		return false;

	// Both there, and whole.
	std::vector<FtpEntry> entries;
	std::string listError;
	target.list(titleDir, &entries, &listError);
	auto sizeOf = [&](const std::string &name) -> int64_t {
		for(const FtpEntry &entry : entries)
			if(entry.name == name)
				return entry.size;
		return -1;
	};
	if(sizeOf(save.dir) != fileSize(image) || sizeOf(save.dir + ".bin") != fileSize(key))
	{
		if(error)
			*error = "The copy on the USB drive is not whole.";
		return false;
	}
	logInfo("Saves: exported to USB " + save.key());
	return true;
}

std::vector<UsbSave> SaveVault::usbSaves(SaveRemote &source, const std::string &base, std::string *error)
{
	std::vector<UsbSave> found;
	std::vector<FtpEntry> psids;
	if(!source.list(usbRoot(base), &psids, error))
		return found;
	for(const FtpEntry &psid : psids)
	{
		if(!psid.isDirectory || !isPsid(psid.name))
			continue;
		std::vector<FtpEntry> titles;
		std::string listError;
		if(!source.list(usbRoot(base) + "/" + psid.name, &titles, &listError))
			continue;
		for(const FtpEntry &title : titles)
		{
			if(!title.isDirectory)
				continue;
			std::vector<FtpEntry> files;
			if(!source.list(usbRoot(base) + "/" + psid.name + "/" + title.name, &files, &listError))
				continue;
			for(const FtpEntry &file : files)
			{
				if(file.isDirectory || endsWith(file.name, ".bin"))
					continue;
				const auto key = std::find_if(files.begin(), files.end(),
					[&](const FtpEntry &other) { return other.name == file.name + ".bin"; });
				if(key == files.end())
					continue;
				found.push_back({ toLower(psid.name), title.name, file.name, file.size, key->size,
					file.modified });
			}
		}
	}
	return found;
}

bool SaveVault::importFromUsb(SaveRemote &source, const std::string &base, const UsbSave &usb,
	const std::string &account, std::string *error)
{
	SaveInfo save;
	save.account = account;
	save.titleId = usb.titleId;
	save.dir = usb.dir;
	if(!isAccountId(account) || !safeRelative("savedata/" + usb.dir) || !safeRelative("savedata/" + usb.titleId))
	{
		if(error)
			*error = "Not a save this vault can keep.";
		return false;
	}
	std::string version = joinPath(saveFolder(save), localStamp("%Y%m%d-%H%M%S"));
	for(int n = 2; directoryExists(version); ++n)
		version = joinPath(saveFolder(save), localStamp("%Y%m%d-%H%M%S") + "-" + std::to_string(n));
	const std::string from = usbRoot(base) + "/" + usb.psid + "/" + usb.titleId + "/";
	const std::string data = joinPath(version, "savedata");
	ensureDir(data);
	const std::string image = joinPath(data, "sdimg_" + usb.dir);
	const std::string key = joinPath(data, usb.dir + ".bin");
	if(!source.download(from + usb.dir, image, error) || !source.download(from + usb.dir + ".bin", key, error)
		|| fileSize(image) != usb.imageSize || fileSize(key) != usb.keySize)
	{
		if(error && error->empty())
			*error = "Incomplete copy from the USB drive.";
		std::error_code ignored;
		fs::remove_all(fs::u8path(version), ignored);
		return false;
	}
	std::vector<SaveFile> files = { { "savedata/sdimg_" + usb.dir, usb.imageSize, std::string() },
		{ "savedata/" + usb.dir + ".bin", usb.keySize, std::string() } };
	Json info = Json::makeObject();
	info.set("account", Json::fromString(account));
	info.set("title_id", Json::fromString(usb.titleId));
	info.set("dir", Json::fromString(usb.dir));
	info.set("account_id", Json::fromString(storedAccountId(usb.psid)));
	info.set("backed_up_at", Json::fromString(localStamp("%Y-%m-%d %H:%M")));
	info.set("from_usb", Json::fromBool(true));
	info.set("files", filesToJson(files));
	std::ofstream out(fs::u8path(joinPath(version, "vault.json")), std::ios::binary | std::ios::trunc);
	out << info.dump();
	if(!out)
	{
		if(error)
			*error = "Could not write to the vault folder.";
		return false;
	}
	out.close();
	prune(save);
	logInfo("Saves: imported from USB " + save.key());
	return true;
}

} // namespace orbislink
