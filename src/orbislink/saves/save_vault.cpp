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

// "savedata/x" → the console path of that file.
std::string consolePathOf(const SaveInfo &save, const std::string &relative)
{
	if(startsWith(relative, "savedata/"))
		return SaveVault::consoleSaveDir(save.account, save.titleId) + "/" + relative.substr(9);
	return SaveVault::consoleMetaDir(save.account, save.titleId, save.dir) + "/" + relative.substr(5);
}

// Only plain names: a file name from the console or a vault.json must not
// lead outside its folder.
bool safeRelative(const std::string &relative)
{
	if(!startsWith(relative, "savedata/") && !startsWith(relative, "meta/"))
		return false;
	const std::string name = relative.substr(relative.find('/') + 1);
	return !name.empty() && name.find('/') == std::string::npos && name.find('\\') == std::string::npos
		&& name != "." && name != "..";
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

std::string SaveVault::consoleMetaDir(const std::string &account, const std::string &titleId,
	const std::string &dir)
{
	return "/user/home/" + account + "/savedata_meta/user/" + titleId + "/" + dir;
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
		// sdimg_<dir> and <dir>.bin make one save.
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
			if(dir.empty())
				continue;
			byDir[dir].push_back({ "savedata/" + file.name, file.size, file.modified });
		}
		for(auto &pair : byDir)
		{
			SaveInfo &save = merged[account + "/" + titleId + "/" + pair.first];
			save.account = account;
			save.titleId = titleId;
			save.dir = pair.first;
			save.onConsole = true;
			save.consoleFiles = pair.second;

			std::vector<FtpEntry> meta;
			std::string metaError;
			remote.list(consoleMetaDir(account, titleId, save.dir), &meta, &metaError);
			const std::string local = joinPath(joinPath(joinPath(cache, account), titleId), save.dir);
			for(const FtpEntry &file : meta)
			{
				if(file.isDirectory)
					continue;
				const SaveFile entry { "meta/" + file.name, file.size, file.modified };
				if(!safeRelative(entry.relative))
					continue;
				save.consoleFiles.push_back(entry);
				// The names and the icon, for the list.
				const bool sfo = iequals(file.name, "param.sfo");
				const bool icon = iequals(file.name, "icon0.png");
				if(!sfo && !icon)
					continue;
				ensureDir(local);
				const std::string target = joinPath(local, file.name);
				if(fileSize(target) != file.size || sfo)
				{
					std::string downloadError;
					remote.download(consolePathOf(save, entry.relative), target, &downloadError);
				}
				if(sfo)
					readSfo(target, save);
				else if(fileExists(target))
					save.iconPath = target;
			}
		}
	}
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
	for(const std::string &dir : { home, home + "/savedata", consoleSaveDir(save.account, save.titleId),
			 home + "/savedata_meta", home + "/savedata_meta/user",
			 home + "/savedata_meta/user/" + save.titleId,
			 consoleMetaDir(save.account, save.titleId, save.dir) })
		ensureRemoteDir(remote, dir);

	for(size_t i = 0; i < save.vaultFiles.size(); ++i)
	{
		const SaveFile &file = save.vaultFiles[i];
		if(!safeRelative(file.relative))
			continue;
		if(progress)
			progress(file.relative, static_cast<double>(i) / save.vaultFiles.size());
		if(!remote.upload(joinPath(latest, file.relative), consolePathOf(save, file.relative), error))
			return false;
	}

	// The console dates what was put back as written now. The backup takes
	// those dates, so it reads as the same save and not as one changed since.
	std::vector<SaveFile> now;
	for(const std::string &dir : { consoleSaveDir(save.account, save.titleId),
			 consoleMetaDir(save.account, save.titleId, save.dir) })
	{
		std::vector<FtpEntry> entries;
		std::string listError;
		if(!remote.list(dir, &entries, &listError))
			continue;
		const bool data = dir == consoleSaveDir(save.account, save.titleId);
		for(const FtpEntry &entry : entries)
			if(!entry.isDirectory)
				now.push_back({ (data ? "savedata/" : "meta/") + entry.name, entry.size, entry.modified });
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
	logInfo("Saves: restored " + save.key());
	return true;
}

bool SaveVault::removeFromConsole(SaveRemote &remote, const SaveInfo &save, std::string *error)
{
	for(const SaveFile &file : save.consoleFiles)
		if(safeRelative(file.relative) && !remote.removeFile(consolePathOf(save, file.relative), error))
			return false;
	remote.removeDirectory(consoleMetaDir(save.account, save.titleId, save.dir));
	logInfo("Saves: deleted from the console " + save.key());
	return true;
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

} // namespace orbislink
