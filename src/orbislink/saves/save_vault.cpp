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

std::vector<SaveFile> filesFromJson(const Json &list)
{
	std::vector<SaveFile> files;
	for(const Json &item : list.items())
		files.push_back({ item["relative"].toString(), item["size"].toInt(), item["modified"].toString() });
	return files;
}

// The copies known on consoles: the most recent few, one per console user.
constexpr size_t kCopiesKept = 16;

Json copiesToJson(const std::vector<SaveCopy> &copies)
{
	Json list = Json::makeArray();
	for(const SaveCopy &copy : copies)
	{
		Json item = Json::makeObject();
		item.set("user", Json::fromString(copy.user));
		item.set("stamp", Json::fromString(copy.stamp));
		item.set("files", filesToJson(copy.files));
		list.push(std::move(item));
	}
	return list;
}

std::vector<SaveCopy> copiesFromJson(const Json &list)
{
	std::vector<SaveCopy> copies;
	for(const Json &item : list.items())
		copies.push_back({ item["user"].toString(), item["stamp"].toString(), filesFromJson(item["files"]) });
	return copies;
}

// This user's copy is now this one; the other users' stay.
void recordCopy(std::vector<SaveCopy> &copies, SaveCopy copy)
{
	copies.erase(std::remove_if(copies.begin(), copies.end(),
					 [&copy](const SaveCopy &c) { return c.user == copy.user; }),
		copies.end());
	copies.push_back(std::move(copy));
	if(copies.size() > kCopiesKept)
		copies.erase(copies.begin(), copies.end() - kCopiesKept);
}

Json stringsToJson(const std::vector<std::string> &values)
{
	Json list = Json::makeArray();
	for(const std::string &value : values)
		list.push(Json::fromString(value));
	return list;
}

std::vector<std::string> stringsFromJson(const Json &list)
{
	std::vector<std::string> values;
	for(const Json &item : list.items())
		if(!item.toString().empty())
			values.push_back(item.toString());
	return values;
}

void addUser(std::vector<std::string> &users, const std::string &user)
{
	if(!user.empty() && std::find(users.begin(), users.end(), user) == users.end())
		users.push_back(user);
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

// A PSID folder: sixteen hex digits.
bool isPsid(const std::string &name)
{
	return name.size() == 16 && std::all_of(name.begin(), name.end(),
		[](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

// The bytes of a hex number the other way round.
std::string reverseHex(const std::string &hex)
{
	std::string out;
	for(size_t i = hex.size(); i >= 2; i -= 2)
		out += hex.substr(i - 2, 2);
	return toLower(out);
}

// A save's name as a plain file name: nothing that leads elsewhere.
bool plainName(const std::string &name)
{
	return !name.empty() && name != "." && name != ".." && name.find('/') == std::string::npos
		&& name.find('\\') == std::string::npos;
}

std::string imageRelative(const std::string &dir) { return "savedata/sdimg_" + dir; }
std::string keyRelative(const std::string &dir) { return "savedata/" + dir + ".bin"; }

const SaveFile *findFile(const std::vector<SaveFile> &files, const std::string &relative)
{
	for(const SaveFile &file : files)
		if(file.relative == relative)
			return &file;
	return nullptr;
}

Json readJson(const std::string &path)
{
	std::vector<uint8_t> data;
	if(!readFile(path, &data))
		return Json();
	return Json::parse(std::string(data.begin(), data.end()));
}

bool writeJson(const std::string &path, const Json &json)
{
	std::ofstream out(fs::u8path(path), std::ios::binary | std::ios::trunc);
	out << json.dump();
	return static_cast<bool>(out);
}

bool copyFile(const std::string &from, const std::string &to)
{
	std::error_code failure;
	fs::copy_file(fs::u8path(from), fs::u8path(to), fs::copy_options::overwrite_existing, failure);
	return !failure;
}

// Earlier backups in a save's info folder, oldest first.
std::vector<std::string> historyOf(const std::string &infoFolder)
{
	std::vector<std::string> versions;
	std::error_code error;
	for(const auto &entry : fs::directory_iterator(fs::u8path(infoFolder), error))
	{
		const std::string name = entry.path().filename().u8string();
		if(entry.is_directory() && !name.empty() && name.front() != '.')
			versions.push_back(entry.path().u8string());
	}
	std::sort(versions.begin(), versions.end());
	return versions;
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

// Takes away a folder if nothing is left in it.
void removeIfEmpty(const fs::path &folder)
{
	std::error_code ignored;
	if(fs::is_directory(folder, ignored) && fs::is_empty(folder, ignored))
		fs::remove(folder, ignored);
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

namespace {

// Whether the console's files are these: the encrypted image and its key
// decide, they are what is kept.
bool sameFiles(const std::vector<SaveFile> &console, const std::vector<SaveFile> &kept, const std::string &dir)
{
	for(const std::string &relative : { imageRelative(dir), keyRelative(dir) })
	{
		const SaveFile *a = findFile(console, relative);
		const SaveFile *b = findFile(kept, relative);
		if(!a && !b)
			continue;
		if(!a || !b || a->size != b->size || !sameModified(b->modified, a->modified))
			return false;
	}
	return true;
}

} // namespace

SaveSync SaveInfo::sync() const
{
	if(onConsole && !inVault)
		return SaveSync::ConsoleOnly;
	if(!onConsole)
		return SaveSync::VaultOnly;
	// The copy this console user was last known to have: backed up from
	// there, or put there from the vault; of the latest backup, or of one
	// before it (another console's since).
	for(const SaveCopy &copy : copies)
		if(copy.user == account && sameFiles(consoleFiles, copy.files, dir))
			return copy.stamp == stamp ? SaveSync::Same : SaveSync::Older;
	// None known for this user: as the latest backup or not, when it can be
	// that copy (one never there is another console's own save of that name).
	const bool beenHere = users.empty() || std::find(users.begin(), users.end(), account) != users.end();
	return beenHere && sameFiles(consoleFiles, vaultFiles, dir) ? SaveSync::Same : SaveSync::Changed;
}

SaveVault::SaveVault(std::string root) : root_(std::move(root)) {}

void SaveVault::setLinks(std::map<std::string, std::string> psidOfUser)
{
	links_.clear();
	for(auto &pair : psidOfUser)
		if(isAccountId(pair.first) && isPsid(pair.second))
			links_[pair.first] = toLower(pair.second);
}

std::string SaveVault::consoleSaveDir(const std::string &account, const std::string &titleId)
{
	return "/user/home/" + account + "/savedata/" + titleId;
}

std::string SaveVault::consoleMetaRoot(const std::string &account, const std::string &titleId)
{
	return "/user/home/" + account + "/savedata_meta/user/" + titleId;
}

std::string SaveVault::psidOfAccountId(const std::string &storedHex)
{
	return isPsid(storedHex) ? reverseHex(storedHex) : std::string();
}

std::string SaveVault::psidFor(const SaveInfo &save) const
{
	// The save's own ACCOUNT_ID says whose it is; else the user's link.
	const std::string own = psidOfAccountId(save.accountId);
	if(!own.empty())
		return own;
	const auto link = links_.find(save.account);
	if(link != links_.end())
		return link->second;
	return save.psid;
}

std::string SaveVault::userOf(const std::string &psid) const
{
	for(const auto &pair : links_)
		if(pair.second == psid)
			return pair.first;
	return std::string();
}

std::string SaveVault::imageDir(const std::string &psid, const std::string &titleId) const
{
	return joinPath(joinPath(joinPath(joinPath(root_, "PS4"), "SAVEDATA"), psid), titleId);
}

std::string SaveVault::infoDir(const std::string &psid, const std::string &titleId, const std::string &dir) const
{
	return joinPath(joinPath(joinPath(joinPath(root_, ".vault"), psid), titleId), dir);
}

void SaveVault::prune(const std::string &infoFolder) const
{
	std::vector<std::string> versions = historyOf(infoFolder);
	// The latest is the one in PS4/SAVEDATA; these are the ones before it.
	while(versions.size() > static_cast<size_t>(kVersionsKept - 1))
	{
		std::error_code ignored;
		fs::remove_all(fs::u8path(versions.front()), ignored);
		versions.erase(versions.begin());
	}
}

void SaveVault::readVault(std::map<std::string, SaveInfo> &out)
{
	migrateLegacy();
	readLegacy(out);
	const fs::path saves = fs::u8path(joinPath(joinPath(root_, "PS4"), "SAVEDATA"));
	std::error_code error;
	for(const auto &owner : fs::directory_iterator(saves, error))
	{
		const std::string psid = toLower(owner.path().filename().u8string());
		if(!owner.is_directory() || !isPsid(psid))
			continue;
		for(const auto &title : fs::directory_iterator(owner.path(), error))
		{
			if(!title.is_directory())
				continue;
			for(const auto &file : fs::directory_iterator(title.path(), error))
			{
				const std::string name = file.path().filename().u8string();
				const fs::path key = title.path() / fs::u8path(name + ".bin");
				if(file.is_directory() || endsWith(name, ".bin") || !fs::is_regular_file(key, error))
					continue;
				SaveInfo save;
				save.psid = psid;
				save.titleId = title.path().filename().u8string();
				save.dir = name;
				save.inVault = true;
				const std::string info = infoDir(psid, save.titleId, save.dir);
				const Json json = readJson(joinPath(info, "info.json"));
				std::vector<SaveFile> listed;
				if(json.isObject())
				{
					save.account = json["account"].toString();
					save.gameTitle = json["game_title"].toString();
					save.saveTitle = json["save_title"].toString();
					save.detail = json["detail"].toString();
					save.accountId = json["account_id"].toString();
					save.backedUpAt = json["backed_up_at"].toString();
					save.stamp = json["stamp"].toString();
					listed = filesFromJson(json["files"]);
					save.copies = copiesFromJson(json["copies"]);
					save.users = stringsFromJson(json["users"]);
					// Kept before copies were: the one it was backed up from.
					if(!json.contains("copies") && isAccountId(save.account))
						save.copies.push_back({ save.account, save.stamp, listed });
					addUser(save.users, isAccountId(save.account) ? save.account : std::string());
				}
				if(!isAccountId(save.account))
					save.account = userOf(psid);
				// The files as they are, with the console's dates when the
				// sizes still match what info.json says.
				const std::pair<std::string, fs::path> files[] = {
					{ imageRelative(name), file.path() }, { keyRelative(name), key } };
				for(const auto &pair : files)
				{
					SaveFile kept { pair.first, static_cast<int64_t>(fs::file_size(pair.second, error)), std::string() };
					const SaveFile *known = findFile(listed, pair.first);
					if(known && known->size == kept.size)
						kept.modified = known->modified;
					save.vaultFiles.push_back(kept);
				}
				save.versions = 1 + static_cast<int>(historyOf(info).size());
				const std::string icon = joinPath(info, "icon0.png");
				if(fileExists(icon))
					save.iconPath = icon;
				out[save.key()] = save;
			}
		}
	}
}

// Backups made before the vault took the PS4's layout:
// <root>/<user>/<TITLE_ID>/<dir>/<YYYYMMDD-HHMMSS>/{savedata,meta}/… and
// vault.json. They are moved over as soon as their PSID is known; until
// then they are shown as they are.
void SaveVault::readLegacy(std::map<std::string, SaveInfo> &out) const
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
				std::vector<std::string> versions;
				for(const auto &version : fs::directory_iterator(dir.path(), error))
					if(version.is_directory() && fileExists(joinPath(version.path().u8string(), "vault.json")))
						versions.push_back(version.path().u8string());
				if(versions.empty())
					continue;
				std::sort(versions.begin(), versions.end());
				const std::string latest = versions.back();
				const Json info = readJson(joinPath(latest, "vault.json"));
				SaveInfo save;
				save.account = account.path().filename().u8string();
				save.titleId = title.path().filename().u8string();
				save.dir = dir.path().filename().u8string();
				save.inVault = true;
				save.versions = static_cast<int>(versions.size());
				save.gameTitle = info["game_title"].toString();
				save.saveTitle = info["save_title"].toString();
				save.detail = info["detail"].toString();
				save.accountId = info["account_id"].toString();
				save.backedUpAt = info["backed_up_at"].toString();
				for(const Json &item : info["files"].items())
				{
					SaveFile file { item["relative"].toString(), item["size"].toInt(), item["modified"].toString() };
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

void SaveVault::migrateLegacy()
{
	std::error_code error;
	std::vector<fs::path> accounts;
	for(const auto &account : fs::directory_iterator(fs::u8path(root_), error))
		if(account.is_directory() && isAccountId(account.path().filename().u8string()))
			accounts.push_back(account.path());
	for(const fs::path &account : accounts)
	{
		std::vector<fs::path> titles;
		for(const auto &title : fs::directory_iterator(account, error))
			if(title.is_directory())
				titles.push_back(title.path());
		for(const fs::path &title : titles)
		{
			std::vector<fs::path> dirs;
			for(const auto &dir : fs::directory_iterator(title, error))
				if(dir.is_directory())
					dirs.push_back(dir.path());
			for(const fs::path &folder : dirs)
			{
				std::vector<std::string> versions;
				for(const auto &version : fs::directory_iterator(folder, error))
					if(version.is_directory() && fileExists(joinPath(version.path().u8string(), "vault.json")))
						versions.push_back(version.path().u8string());
				if(versions.empty())
					continue;
				std::sort(versions.begin(), versions.end());
				SaveInfo save;
				save.account = account.filename().u8string();
				save.titleId = title.filename().u8string();
				save.dir = folder.filename().u8string();
				const Json latestInfo = readJson(joinPath(versions.back(), "vault.json"));
				save.accountId = latestInfo["account_id"].toString();
				const std::string psid = psidFor(save);
				if(!isPsid(psid) || !plainName(save.dir) || !plainName(save.titleId))
					continue;

				const std::string images = imageDir(psid, save.titleId);
				const std::string info = infoDir(psid, save.titleId, save.dir);
				const std::string image = joinPath(images, save.dir);
				const std::string key = image + ".bin";
				bool whole = true;
				bool haveLatest = fileExists(image) && fileExists(key);
				for(size_t i = 0; i < versions.size(); ++i)
				{
					const std::string data = joinPath(versions[i], "savedata");
					const std::string fromImage = joinPath(data, "sdimg_" + save.dir);
					const std::string fromKey = joinPath(data, save.dir + ".bin");
					if(!fileExists(fromImage) || !fileExists(fromKey))
						continue;
					const std::string stamp = fs::u8path(versions[i]).filename().u8string();
					if(i + 1 == versions.size() && !haveLatest)
					{
						ensureDir(images);
						ensureDir(info);
						whole = copyFile(fromImage, image) && copyFile(fromKey, key) && whole;
						Json json = Json::makeObject();
						json.set("account", Json::fromString(save.account));
						json.set("title_id", Json::fromString(save.titleId));
						json.set("dir", Json::fromString(save.dir));
						for(const char *field : { "game_title", "save_title", "detail", "account_id", "backed_up_at" })
							json.set(field, Json::fromString(latestInfo[field].toString()));
						json.set("stamp", Json::fromString(stamp));
						std::vector<SaveFile> files;
						for(const Json &item : latestInfo["files"].items())
						{
							const std::string relative = item["relative"].toString();
							if(relative == imageRelative(save.dir) || relative == keyRelative(save.dir))
								files.push_back({ relative, item["size"].toInt(), item["modified"].toString() });
						}
						json.set("files", filesToJson(files));
						whole = writeJson(joinPath(info, "info.json"), json) && whole;
						const std::string icon = joinPath(joinPath(versions[i], "meta"), "icon0.png");
						if(fileExists(icon))
							copyFile(icon, joinPath(info, "icon0.png"));
						haveLatest = true;
						continue;
					}
					const std::string earlier = joinPath(info, stamp);
					if(directoryExists(earlier))
						continue;
					ensureDir(earlier);
					whole = copyFile(fromImage, joinPath(earlier, save.dir))
						&& copyFile(fromKey, joinPath(earlier, save.dir + ".bin")) && whole;
				}
				if(!whole)
				{
					logWarning("Saves: could not move " + save.key() + " to the new layout; kept as it was.");
					continue;
				}
				prune(info);
				std::error_code ignored;
				fs::remove_all(folder, ignored);
				logInfo("Saves: moved " + save.key() + " to PS4/SAVEDATA/" + psid);
			}
			removeIfEmpty(title);
		}
		removeIfEmpty(account);
	}
}

std::vector<SaveInfo> SaveVault::vaultSaves()
{
	std::map<std::string, SaveInfo> saves;
	readVault(saves);
	std::vector<SaveInfo> list;
	for(auto &pair : saves)
		list.push_back(std::move(pair.second));
	return list;
}

namespace {

// The name a console keeps for its user (username.dat: up to 16 bytes,
// padded with zeros), if it reads as one.
std::string readUserName(const std::string &path)
{
	std::vector<uint8_t> data;
	if(!readFile(path, &data) || data.empty() || data.size() > 64)
		return std::string();
	std::string name;
	for(uint8_t byte : data)
	{
		if(byte == 0)
			break;
		if(byte < 0x20 || byte == 0x7F)
			return std::string();
		name += static_cast<char>(byte);
	}
	return trim(name);
}

} // namespace

bool SaveVault::listUsers(SaveRemote &remote, std::vector<ConsoleUser> *users, std::string *error)
{
	users->clear();
	std::vector<FtpEntry> entries;
	if(!remote.list("/user/home", &entries, error))
		return false;
	const std::string cache = joinPath(joinPath(root_, ".cache"), "console");
	for(const FtpEntry &entry : entries)
	{
		if(!entry.isDirectory || !isAccountId(entry.name))
			continue;
		ConsoleUser user;
		user.folder = entry.name;
		// The name the console shows (a PS5 keeps it there; a PS4 may not).
		const std::string folder = joinPath(cache, entry.name);
		ensureDir(folder);
		const std::string local = joinPath(folder, "username.dat");
		std::string why;
		std::error_code ignored;
		if(remote.download("/user/home/" + entry.name + "/username.dat", local, &why))
			user.name = readUserName(local);
		else
			fs::remove(fs::u8path(local), ignored);
		const auto link = links_.find(entry.name);
		if(link != links_.end())
		{
			user.psid = link->second;
			user.linked = true;
		}
		users->push_back(user);
	}
	return true;
}

std::vector<ConsoleUser> SaveVault::readUsers(SaveRemote &remote, std::string *error)
{
	std::vector<ConsoleUser> users;
	listUsers(remote, &users, error);
	return users;
}

std::vector<SaveInfo> SaveVault::scan(SaveRemote &remote, std::string *error, const Progress &progress)
{
	std::map<std::string, SaveInfo> merged;
	readVault(merged);

	if(!listUsers(remote, &users_, error))
		return {};
	const std::string cache = joinPath(joinPath(root_, ".cache"), "console");

	std::vector<std::pair<std::string, std::string>> titles; // account, title
	for(const ConsoleUser &user : users_)
	{
		std::vector<FtpEntry> games;
		std::string listError;
		if(!remote.list("/user/home/" + user.folder + "/savedata", &games, &listError))
			continue;
		for(const FtpEntry &game : games)
			if(game.isDirectory && plainName(game.name))
				titles.emplace_back(user.folder, game.name);
	}

	std::vector<SaveInfo> found;
	// Saves whose own image or key is gone, only the system's copies left:
	// game/name → the user they are in.
	std::map<std::string, std::string> brokenHere;
	for(size_t t = 0; t < titles.size(); ++t)
	{
		const std::string &account = titles[t].first;
		const std::string &titleId = titles[t].second;
		if(progress)
			progress(titleId, static_cast<double>(t) / titles.size());
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
			if(!plainName(dir))
				continue;
			byDir[dir].push_back({ "savedata/" + file.name, file.size, file.modified });
		}
		if(byDir.empty())
			continue;

		// The game's savedata_meta folder, once: each save's entries there
		// are files (or, on some systems, folders) named after it. Some
		// servers do not let it be read; the names then come from the game.
		std::vector<FtpEntry> meta;
		std::string metaError;
		remote.list(consoleMetaRoot(account, titleId), &meta, &metaError);

		for(auto &pair : byDir)
		{
			// Without its own image and key the save is not there, even when
			// the system's backup copies of them are: the console shows it
			// broken, and it is what putting back is for.
			if(!findFile(pair.second, imageRelative(pair.first)) || !findFile(pair.second, keyRelative(pair.first)))
			{
				brokenHere[titleId + "/" + pair.first] = account;
				continue;
			}
			SaveInfo save;
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
			save.psid = psidFor(save);
			found.push_back(std::move(save));
		}
	}

	// A user's PSID where no link says it: what its saves' ACCOUNT_ID says,
	// when most of them agree.
	std::map<std::string, std::map<std::string, int>> votes;
	for(const SaveInfo &save : found)
	{
		const std::string own = psidOfAccountId(save.accountId);
		if(!own.empty())
			++votes[save.account][own];
	}
	for(ConsoleUser &user : users_)
	{
		if(!user.psid.empty())
			continue;
		int total = 0;
		std::pair<std::string, int> best;
		for(const auto &vote : votes[user.folder])
		{
			total += vote.second;
			if(vote.second > best.second)
				best = vote;
		}
		if(best.second * 2 > total)
			user.psid = best.first;
	}
	auto psidOfUser = [this](const std::string &folder) {
		for(const ConsoleUser &user : users_)
			if(user.folder == folder)
				return user.psid;
		return std::string();
	};

	// One save whichever console it is on: the console's is the vault's of
	// the same PSID, game and name.
	for(SaveInfo &save : found)
	{
		if(save.psid.empty())
			save.psid = psidOfUser(save.account);
		// Still not known: the vault's save of this user, game and name.
		if(save.psid.empty())
			for(const auto &pair : merged)
			{
				const SaveInfo &kept = pair.second;
				if(kept.inVault && isPsid(kept.psid) && kept.titleId == save.titleId && kept.dir == save.dir
					&& std::find(kept.users.begin(), kept.users.end(), save.account) != kept.users.end())
				{
					save.psid = kept.psid;
					break;
				}
			}
		const auto it = merged.find(save.key());
		if(it == merged.end())
		{
			merged[save.key()] = std::move(save);
			continue;
		}
		SaveInfo &kept = it->second;
		kept.account = save.account;
		kept.onConsole = true;
		kept.consoleFiles = save.consoleFiles;
		if(!save.gameTitle.empty())
			kept.gameTitle = save.gameTitle;
		if(!save.saveTitle.empty())
			kept.saveTitle = save.saveTitle;
		if(!save.detail.empty())
			kept.detail = save.detail;
		if(!save.accountId.empty())
			kept.accountId = save.accountId;
		if(!save.iconPath.empty() && (kept.iconPath.empty() || !kept.inVault))
			kept.iconPath = save.iconPath;
	}

	// A save of the vault not on this console: the user here it would go
	// to. The one with its PSID; else one it was on, or whose folder still
	// has the system's copies of it, while that user's PSID is not known.
	for(auto &pair : merged)
	{
		SaveInfo &save = pair.second;
		if(save.onConsole)
			continue;
		std::string user;
		for(const ConsoleUser &candidate : users_)
			if(!save.psid.empty() && candidate.psid == save.psid)
				user = candidate.folder;
		if(user.empty())
		{
			std::vector<std::string> been = save.users;
			addUser(been, save.account);
			const auto broken = brokenHere.find(save.titleId + "/" + save.dir);
			if(broken != brokenHere.end())
				addUser(been, broken->second);
			for(const ConsoleUser &candidate : users_)
				if(candidate.psid.empty() && std::find(been.begin(), been.end(), candidate.folder) != been.end())
					user = candidate.folder;
		}
		save.account = user;
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
	const SaveFile *image = findFile(save.consoleFiles, imageRelative(save.dir));
	const SaveFile *key = findFile(save.consoleFiles, keyRelative(save.dir));
	if(!save.onConsole || !image || !key || !plainName(save.dir) || !plainName(save.titleId))
	{
		if(error)
			*error = "This save is not whole on the console.";
		return false;
	}
	const std::string psid = psidFor(save);
	if(!isPsid(psid))
	{
		if(error)
			*error = "No PSID known for this console user: link it to an Account ID first.";
		return false;
	}
	const std::string images = imageDir(psid, save.titleId);
	const std::string info = infoDir(psid, save.titleId, save.dir);
	const std::string incoming = joinPath(info, ".incoming");
	std::error_code ignored;
	fs::remove_all(fs::u8path(incoming), ignored);
	ensureDir(incoming);

	// Both files in first, whole, before anything kept is touched.
	const SaveFile *parts[] = { image, key };
	const std::string names[] = { save.dir, save.dir + ".bin" };
	for(int i = 0; i < 2; ++i)
	{
		if(progress)
			progress(parts[i]->relative, i / 2.0);
		const std::string local = joinPath(incoming, names[i]);
		const std::string remotePath = consoleSaveDir(save.account, save.titleId) + "/" + parts[i]->relative.substr(9);
		const bool copied = remote.download(remotePath, local, error);
		// What arrived must be what the console listed.
		if(!copied || fileSize(local) != parts[i]->size)
		{
			if(copied && error)
				*error = "Incomplete copy of " + parts[i]->relative.substr(9) + ".";
			fs::remove_all(fs::u8path(incoming), ignored);
			return false;
		}
	}

	// The backup before goes with the earlier ones.
	const std::string latestImage = joinPath(images, save.dir);
	const std::string latestKey = latestImage + ".bin";
	const Json before = readJson(joinPath(info, "info.json"));
	if(fileExists(latestImage) && fileExists(latestKey))
	{
		std::string stamp = before.isObject() ? before["stamp"].toString() : std::string();
		if(stamp.empty() || !plainName(stamp) || stamp.front() == '.')
			stamp = localStamp("%Y%m%d-%H%M%S") + "-before";
		std::string earlier = joinPath(info, stamp);
		for(int n = 2; directoryExists(earlier); ++n)
			earlier = joinPath(info, stamp + "-" + std::to_string(n));
		ensureDir(earlier);
		fs::rename(fs::u8path(latestImage), fs::u8path(joinPath(earlier, save.dir)), ignored);
		fs::rename(fs::u8path(latestKey), fs::u8path(joinPath(earlier, save.dir + ".bin")), ignored);
	}
	ensureDir(images);
	std::error_code moved;
	fs::rename(fs::u8path(joinPath(incoming, names[0])), fs::u8path(latestImage), moved);
	if(!moved)
		fs::rename(fs::u8path(joinPath(incoming, names[1])), fs::u8path(latestKey), moved);
	fs::remove_all(fs::u8path(incoming), ignored);
	if(moved)
	{
		if(error)
			*error = "Could not write to the vault folder: " + moved.message();
		return false;
	}

	save.backedUpAt = localStamp("%Y-%m-%d %H:%M");
	save.psid = psid;
	save.vaultFiles = { *image, *key };
	// The copies on other consoles are of the backups before this one now.
	save.copies.clear();
	save.users.clear();
	if(before.isObject())
	{
		save.copies = copiesFromJson(before["copies"]);
		save.users = stringsFromJson(before["users"]);
		if(!before.contains("copies") && isAccountId(before["account"].toString()))
			save.copies.push_back({ before["account"].toString(), before["stamp"].toString(),
				filesFromJson(before["files"]) });
		addUser(save.users, isAccountId(before["account"].toString()) ? before["account"].toString() : std::string());
	}
	// Its own name: a copy of an earlier backup must not read as of this one.
	const std::string now = localStamp("%Y%m%d-%H%M%S");
	std::string stamp = now;
	auto taken = [&](const std::string &candidate) {
		if(before.isObject() && before["stamp"].toString() == candidate)
			return true;
		return std::any_of(save.copies.begin(), save.copies.end(),
			[&candidate](const SaveCopy &copy) { return copy.stamp == candidate; });
	};
	for(int n = 2; taken(stamp); ++n)
		stamp = now + "-" + std::to_string(n);
	save.stamp = stamp;
	recordCopy(save.copies, { save.account, stamp, save.vaultFiles });
	addUser(save.users, save.account);
	Json json = Json::makeObject();
	json.set("account", Json::fromString(save.account));
	json.set("title_id", Json::fromString(save.titleId));
	json.set("dir", Json::fromString(save.dir));
	json.set("game_title", Json::fromString(save.gameTitle));
	json.set("save_title", Json::fromString(save.saveTitle));
	json.set("detail", Json::fromString(save.detail));
	json.set("account_id", Json::fromString(save.accountId));
	json.set("backed_up_at", Json::fromString(save.backedUpAt));
	json.set("stamp", Json::fromString(stamp));
	json.set("files", filesToJson(save.vaultFiles));
	json.set("copies", copiesToJson(save.copies));
	json.set("users", stringsToJson(save.users));
	if(!writeJson(joinPath(info, "info.json"), json))
	{
		if(error)
			*error = "Could not write to the vault folder.";
		return false;
	}
	const std::string icon = joinPath(info, "icon0.png");
	if(!save.iconPath.empty() && save.iconPath != icon && fileExists(save.iconPath))
		copyFile(save.iconPath, icon);
	if(fileExists(icon))
		save.iconPath = icon;
	prune(info);
	save.inVault = true;
	save.versions = 1 + static_cast<int>(historyOf(info).size());
	if(progress)
		progress(std::string(), 1.0);
	logInfo("Saves: backed up " + save.key() + " to PS4/SAVEDATA/" + psid);
	return true;
}

bool SaveVault::restore(SaveRemote &remote, const SaveInfo &save, const std::string &account, std::string *error,
	const Progress &progress)
{
	auto fail = [error](const std::string &why) {
		if(error)
			*error = why;
		return false;
	};
	const std::string user = account.empty() ? save.account : account;
	if(!isAccountId(user))
		return fail("No console user for this save: link its PSID to a console user first.");
	if(!save.inVault || !isPsid(save.psid) || !plainName(save.dir) || !plainName(save.titleId))
		return fail("This save has no backup in the vault.");
	const std::string image = joinPath(imageDir(save.psid, save.titleId), save.dir);
	const std::string key = image + ".bin";
	if(!fileExists(image) || !fileExists(key))
		return fail("This save's backup is not whole in the vault.");

	// It goes back to the user it belongs to, who must be on this console.
	std::vector<FtpEntry> users;
	std::string listError;
	if(!remote.list("/user/home", &users, &listError))
		return fail(listError.empty() ? std::string("The console's users could not be read.") : listError);
	if(std::none_of(users.begin(), users.end(),
		   [&user](const FtpEntry &entry) { return entry.isDirectory && entry.name == user; }))
		return fail("This console has no user " + user + ": a save only goes back to its own.");

	// The folders, one level at a time: FTP servers do not make parents.
	const std::string target = consoleSaveDir(user, save.titleId);
	ensureRemoteDir(remote, "/user/home/" + user + "/savedata");
	ensureRemoteDir(remote, target);
	const std::pair<std::string, std::string> files[] = {
		{ image, target + "/sdimg_" + save.dir }, { key, target + "/" + save.dir + ".bin" } };
	for(size_t i = 0; i < 2; ++i)
	{
		if(progress)
			progress(files[i].second, i / 2.0);
		if(!remote.upload(files[i].first, files[i].second, error))
		{
			logWarning("Saves: putting back " + save.key() + " failed at " + files[i].second);
			return false;
		}
	}

	// Both there, and whole: a save put back in part is worse than none.
	std::vector<FtpEntry> now;
	remote.list(target, &now, &listError);
	auto listed = [&now](const std::string &name) -> const FtpEntry * {
		for(const FtpEntry &entry : now)
			if(!entry.isDirectory && entry.name == name)
				return &entry;
		return nullptr;
	};
	const std::pair<std::string, int64_t> expected[] = {
		{ "sdimg_" + save.dir, fileSize(image) }, { save.dir + ".bin", fileSize(key) } };
	for(const auto &want : expected)
	{
		const FtpEntry *entry = listed(want.first);
		if(!entry || entry->size != want.second)
		{
			const std::string got = entry ? std::to_string(entry->size) + " of " + std::to_string(want.second) + " bytes"
										  : std::string("missing");
			logWarning("Saves: putting back " + save.key() + ": " + want.first + " " + got);
			return fail("The console did not take " + want.first + " whole (" + got + ").");
		}
	}

	// The console dates what was put there as written now: that user's copy
	// is recorded with those dates, so it reads as the latest backup and
	// not as changed, on this console and on the others.
	const std::string info = infoDir(save.psid, save.titleId, save.dir);
	Json json = readJson(joinPath(info, "info.json"));
	if(json.isObject())
	{
		std::vector<SaveFile> fresh;
		for(const auto &want : expected)
			if(const FtpEntry *entry = listed(want.first))
				fresh.push_back({ "savedata/" + want.first, entry->size, entry->modified });
		std::vector<SaveCopy> copies = copiesFromJson(json["copies"]);
		std::vector<std::string> been = stringsFromJson(json["users"]);
		const std::string from = json["account"].toString();
		if(!json.contains("copies") && isAccountId(from))
			copies.push_back({ from, json["stamp"].toString(), filesFromJson(json["files"]) });
		addUser(been, isAccountId(from) ? from : std::string());
		recordCopy(copies, { user, json["stamp"].toString(), fresh });
		addUser(been, user);
		json.set("copies", copiesToJson(copies));
		json.set("users", stringsToJson(been));
		writeJson(joinPath(info, "info.json"), json);
	}
	if(progress)
		progress(std::string(), 1.0);
	logInfo("Saves: put back " + save.key() + " for user " + user);
	return true;
}

std::string SaveVault::saveDbPath(const std::string &account)
{
	return "/system_data/savedata/" + account + "/db/user/savedata.db";
}

SaveDbEntry SaveVault::dbEntry(const SaveInfo &save, const std::string &account) const
{
	SaveDbEntry entry;
	const std::string user = account.empty() ? save.account : account;
	entry.titleId = save.titleId;
	entry.dir = save.dir;
	entry.mainTitle = save.gameTitle;
	entry.subTitle = save.saveTitle;
	entry.detail = save.detail;
	if(const SaveFile *image = findFile(save.vaultFiles, imageRelative(save.dir)))
		entry.blocks = image->size / 32768;
	if(isPsid(save.psid))
		entry.accountId = static_cast<int64_t>(std::stoull(save.psid, nullptr, 16));
	if(isAccountId(user))
		entry.userId = static_cast<uint32_t>(std::stoul(user, nullptr, 16));
	return entry;
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
	if(!plainName(save.dir) || !plainName(save.titleId))
		return false;
	std::error_code failure;
	if(isPsid(save.psid))
	{
		const fs::path images = fs::u8path(imageDir(save.psid, save.titleId));
		fs::remove(images / fs::u8path(save.dir), failure);
		if(!failure)
			fs::remove(images / fs::u8path(save.dir + ".bin"), failure);
		const fs::path info = fs::u8path(infoDir(save.psid, save.titleId, save.dir));
		if(!failure)
			fs::remove_all(info, failure);
		// Empty game and PSID folders go too.
		removeIfEmpty(images);
		removeIfEmpty(images.parent_path());
		removeIfEmpty(info.parent_path());
		removeIfEmpty(info.parent_path().parent_path());
	}
	// One not moved to the PS4 layout yet.
	if(!failure && isAccountId(save.account))
	{
		const fs::path legacy = fs::u8path(joinPath(joinPath(joinPath(root_, save.account), save.titleId), save.dir));
		fs::remove_all(legacy, failure);
		removeIfEmpty(legacy.parent_path());
		removeIfEmpty(legacy.parent_path().parent_path());
	}
	if(failure)
	{
		if(error)
			*error = failure.message();
		return false;
	}
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
