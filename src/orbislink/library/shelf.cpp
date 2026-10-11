// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/library/shelf.h"

#include "orbislink/common/json.h"
#include "orbislink/common/util.h"
#include "orbislink/ftp/ftp_client.h"
#include "orbislink/pkg/pkg_inspector.h"
#include "orbislink/update/sha256.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace orbislink {
namespace library {

namespace {

std::string lower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

std::string upper(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return text;
}

std::string extensionOf(const std::string &name)
{
	const size_t dot = name.rfind('.');
	return dot == std::string::npos || dot == 0 ? std::string() : lower(name.substr(dot));
}

std::string stemOf(const std::string &name)
{
	const size_t dot = name.rfind('.');
	return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

bool isPicture(const std::string &ext)
{
	return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
}

// What is never worth showing: hidden files and the system's own folders.
bool skipped(const std::string &name)
{
	return name.empty() || name == "." || name == ".." || name[0] == '.' || name[0] == '$'
		|| name == "System Volume Information";
}

// Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm).
int64_t daysFromCivil(int64_t y, int m, int d)
{
	if(m <= 2)
		--y;
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const int64_t yoe = y - era * 400;
	const int64_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

int yearOf(int64_t unixSeconds)
{
	// Civil from days, only for the year.
	int64_t z = unixSeconds / 86400 + 719468;
	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = static_cast<unsigned>(z - era * 146097);
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned mp = (5 * doy + 2) / 153;
	const unsigned m = mp < 10 ? mp + 3 : mp - 9;
	return static_cast<int>(static_cast<int64_t>(yoe) + era * 400 + (m <= 2));
}

// Read through the source, a block at a time.
BlockReader readerFor(FileSource &source, const std::string &path, int64_t size)
{
	return BlockReader([&source, path](int64_t offset, size_t length, std::vector<uint8_t> *bytes) {
		return source.read(path, offset, length, bytes);
	}, size);
}

// A picture read from the source, or used where it is on this PC.
bool takePicture(FileSource &source, const ShelfEntry &file, const char *from, ShelfItem *item)
{
	if(source.local())
	{
		item->pictureFile = file.path;
	}
	else
	{
		// Covers are small; anything bigger is not one.
		if(file.size <= 0 || file.size > 4 * 1024 * 1024)
			return false;
		std::vector<uint8_t> bytes;
		if(!source.read(file.path, 0, static_cast<size_t>(file.size), &bytes) || bytes.empty())
			return false;
		item->picture = std::move(bytes);
		item->pictureExt = extensionOf(file.name) == ".png" ? ".png" : ".jpg";
	}
	item->pictureFrom = from;
	return true;
}

// Step 2: an image beside it, named after it (or after its title ID).
bool pictureBeside(FileSource &source, const FolderIndex &folder, const ShelfItem &item, ShelfItem *out)
{
	std::vector<std::string> stems { stemOf(item.name) };
	if(item.directory)
		stems = { item.name };
	if(!item.titleId.empty())
		stems.push_back(item.titleId);
	for(const std::string &stem : stems)
		for(const char *suffix : { "", "-cover", "_cover", " cover" })
			for(const char *ext : { ".jpg", ".png", ".jpeg" })
			{
				const ShelfEntry *found = folder.find(lower(stem + suffix + ext));
				if(found && !found->directory && takePicture(source, *found, "folder", out))
					return true;
			}
	return false;
}

} // namespace

// ── sources ──────────────────────────────────────────────────────────────

bool LocalSource::list(const std::string &dir, std::vector<ShelfEntry> *entries, std::string *error)
{
	entries->clear();
	std::error_code ec;
	fs::directory_iterator it(fs::u8path(dir), fs::directory_options::skip_permission_denied, ec);
	if(ec)
	{
		if(error)
			*error = ec.message();
		return false;
	}
	for(; it != fs::directory_iterator(); it.increment(ec))
	{
		if(ec)
			break;
		ShelfEntry entry;
		entry.name = it->path().filename().u8string();
		if(skipped(entry.name))
			continue;
		entry.path = it->path().u8string();
		std::error_code status;
		entry.directory = it->is_directory(status);
		if(!entry.directory)
		{
			std::error_code sized;
			const uintmax_t size = it->file_size(sized);
			entry.size = sized ? 0 : static_cast<int64_t>(size);
		}
		std::error_code timed;
		const fs::file_time_type when = it->last_write_time(timed);
		if(!timed)
		{
			const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
				when - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
			entry.modified = std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
		}
		entries->push_back(std::move(entry));
	}
	return true;
}

bool LocalSource::read(const std::string &path, int64_t offset, size_t length, std::vector<uint8_t> *bytes)
{
	bytes->clear();
	std::ifstream in(fs::u8path(path), std::ios::binary);
	if(!in)
		return false;
	in.seekg(offset);
	if(!in)
		return false;
	bytes->resize(length);
	in.read(reinterpret_cast<char *>(bytes->data()), static_cast<std::streamsize>(length));
	bytes->resize(static_cast<size_t>(in.gcount()));
	return true;
}

std::string LocalSource::join(const std::string &dir, const std::string &name) const
{
	return (fs::u8path(dir) / fs::u8path(name)).u8string();
}

bool FtpSource::list(const std::string &dir, std::vector<ShelfEntry> *entries, std::string *error)
{
	entries->clear();
	std::vector<FtpEntry> listed;
	const FtpResult result = ftp_.list(dir, &listed);
	if(!result.ok)
	{
		if(error)
			*error = result.message;
		return false;
	}
	const int64_t now = nowUnixSeconds();
	for(const FtpEntry &item : listed)
	{
		if(skipped(item.name))
			continue;
		ShelfEntry entry;
		entry.name = item.name;
		entry.path = item.path.empty() ? join(dir, item.name) : item.path;
		entry.directory = item.isDirectory;
		entry.size = item.size;
		entry.modified = ftpListingTime(item.modified, now);
		entries->push_back(std::move(entry));
	}
	return true;
}

bool FtpSource::read(const std::string &path, int64_t offset, size_t length, std::vector<uint8_t> *bytes)
{
	return ftp_.read(path, offset, length, bytes).ok;
}

std::string FtpSource::join(const std::string &dir, const std::string &name) const
{
	if(dir.empty() || dir.back() == '/')
		return dir + name;
	return dir + "/" + name;
}

int64_t ftpListingTime(const std::string &text, int64_t nowUnix)
{
	static const char *const months[] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep",
		"oct", "nov", "dec" };
	const std::vector<std::string> parts = [&]() {
		std::vector<std::string> out;
		for(const std::string &part : split(text, ' ', false))
			out.push_back(part);
		return out;
	}();
	if(parts.size() < 3)
		return 0;
	int month = 0;
	for(int i = 0; i < 12; ++i)
		if(lower(parts[0]).compare(0, 3, months[i]) == 0)
			month = i + 1;
	const int day = std::atoi(parts[1].c_str());
	if(month == 0 || day < 1 || day > 31)
		return 0;
	int year = yearOf(nowUnix);
	int hour = 0;
	int minute = 0;
	const size_t colon = parts[2].find(':');
	if(colon != std::string::npos)
	{
		hour = std::atoi(parts[2].substr(0, colon).c_str());
		minute = std::atoi(parts[2].substr(colon + 1).c_str());
		// "Oct 10 20:17" is within the last year: a date ahead of now was
		// last year's.
		const int64_t guess = daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60;
		if(guess > nowUnix + 86400)
			--year;
	}
	else
	{
		year = std::atoi(parts[2].c_str());
		if(year < 1970)
			return 0;
	}
	return daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60;
}

// ── telling things apart ─────────────────────────────────────────────────

FolderIndex::FolderIndex(const std::vector<ShelfEntry> &entries)
{
	for(const ShelfEntry &entry : entries)
		byName_[lower(entry.name)] = &entry;
}

const ShelfEntry *FolderIndex::find(const std::string &lowerName) const
{
	const auto found = byName_.find(lowerName);
	return found == byName_.end() ? nullptr : found->second;
}

bool shownOnShelf(const ShelfEntry &entry)
{
	if(skipped(entry.name))
		return false;
	return entry.directory || kindOf(entry.name, false, entry.size) != Kind::Other;
}

ShelfItem quickItem(const ShelfEntry &entry)
{
	ShelfItem item;
	item.path = entry.path;
	item.name = entry.name;
	item.directory = entry.directory;
	item.size = entry.size;
	item.modified = entry.modified;
	item.ext = entry.directory ? std::string() : extensionOf(entry.name);
	item.kind = kindOf(entry.name, entry.directory, entry.size);
	item.title = entry.directory ? entry.name : stemOf(entry.name);
	switch(item.kind)
	{
		case Kind::Folder: item.group = "folders"; break;
		case Kind::Package:
		case Kind::Disc:
		case Kind::Image: item.group = "games"; break;
		case Kind::Payload: item.group = "payloads"; break;
		case Kind::Archive:
		case Kind::Other: item.group = "other"; break;
	}
	if(item.kind == Kind::Image)
	{
		item.platform = "ps5";
		item.titleId = titleIdInName(entry.name);
	}
	return item;
}

std::set<std::string> cueTracks(FileSource &source, const std::vector<ShelfEntry> &entries, const DiscProbe &probe,
	std::map<std::string, std::string> *trackOf)
{
	std::set<std::string> tracks;
	if(!probe.cueTrack)
		return tracks;
	for(const ShelfEntry &entry : entries)
	{
		if(entry.directory || extensionOf(entry.name) != ".cue" || entry.size > 64 * 1024)
			continue;
		std::vector<uint8_t> text;
		if(!source.read(entry.path, 0, 64 * 1024, &text))
			continue;
		const std::string track = probe.cueTrack(std::string(text.begin(), text.end()));
		if(track.empty())
			continue;
		tracks.insert(lower(track));
		if(trackOf)
			(*trackOf)[entry.name] = track;
	}
	return tracks;
}

ShelfItem identify(FileSource &source, const ShelfEntry &entry, const FolderIndex &folder, const DiscProbe &probe,
	const std::string &cueTrack)
{
	ShelfItem item = quickItem(entry);
	switch(item.kind)
	{
		case Kind::Package:
		{
			// An image beside it saves reading its icon.
			ShelfItem beside = item;
			const bool hasPicture = pictureBeside(source, folder, item, &beside);
			PkgInspector::Options options;
			options.extractIcon = !hasPicture;
			options.maxIconBytes = 2 * 1024 * 1024;
			BlockReader reader = readerFor(source, entry.path, entry.size);
			const PkgInfo info = PkgInspector(options).inspect(entry.path, entry.size,
				[&reader](int64_t offset, void *buffer, size_t size) { return reader.read(offset, buffer, size); });
			if(!info.valid)
			{
				item.readable = false;
				item.group = "other";
				break;
			}
			item.title = info.displayTitle();
			item.titleId = info.titleId;
			item.version = info.appVersion;
			item.category = pkgCategoryCode(info.kind);
			item.contentId = info.contentId;
			item.platform = "ps4";
			switch(info.kind)
			{
				case PkgCategory::Patch:
				case PkgCategory::DeltaPatch:
				case PkgCategory::Dlc:
				case PkgCategory::Theme: item.group = "extras"; break;
				default: item.group = "games";
			}
			if(hasPicture)
			{
				item.pictureFile = beside.pictureFile;
				item.picture = std::move(beside.picture);
				item.pictureExt = beside.pictureExt;
				item.pictureFrom = beside.pictureFrom;
			}
			else if(!info.iconPng.empty())
			{
				item.picture = info.iconPng;
				item.pictureExt = ".png";
				item.pictureFrom = "metadata";
			}
			break;
		}
		case Kind::Disc:
		{
			if(!probe.inspect)
				break;
			const ShelfEntry *image = &entry;
			if(item.ext == ".cue")
			{
				image = cueTrack.empty() ? nullptr : folder.find(lower(cueTrack));
				if(!image)
				{
					item.readable = false;
					item.group = "other";
					break;
				}
				item.trackPath = image->path;
				item.size = image->size;
			}
			BlockReader reader = readerFor(source, image->path, image->size);
			const DiscId disc = probe.inspect(image->name, image->size,
				[&reader](uint64_t offset, uint8_t *out, size_t size) {
					return reader.read(static_cast<int64_t>(offset), out, size);
				});
			if(disc.platform.empty())
			{
				item.readable = false;
				item.group = "other";
				break;
			}
			item.platform = disc.platform;
			item.serial = disc.serial;
			item.titleId = disc.titleId;
			const std::string known = probe.knownTitle ? probe.knownTitle(disc.platform, disc.titleId) : std::string();
			item.title = !known.empty() ? known : !disc.title.empty() ? disc.title : item.title;
			pictureBeside(source, folder, item, &item);
			break;
		}
		case Kind::Image:
			pictureBeside(source, folder, item, &item);
			break;
		case Kind::Folder:
		{
			// An app or game folder says what it is in sce_sys.
			std::vector<uint8_t> bytes;
			AppParams params;
			if(source.read(source.join(source.join(entry.path, "sce_sys"), "param.json"), 0, 256 * 1024, &bytes)
				&& !bytes.empty())
			{
				params = readParamJson(std::string(bytes.begin(), bytes.end()));
				item.platform = "ps5";
			}
			if(!params.ok && source.read(source.join(source.join(entry.path, "sce_sys"), "param.sfo"), 0, 256 * 1024, &bytes)
				&& !bytes.empty())
			{
				params = readParamSfo(bytes);
				item.platform = "ps4";
			}
			if(!params.ok)
			{
				item.platform.clear();
				break; // a folder to open
			}
			item.appFolder = true;
			item.group = "games";
			item.titleId = params.titleId;
			item.version = params.version;
			item.contentId = params.contentId;
			if(!params.title.empty())
				item.title = params.title;
			if(pictureBeside(source, folder, item, &item))
				break;
			ShelfEntry icon;
			icon.name = "icon0.png";
			icon.path = source.join(source.join(entry.path, "sce_sys"), "icon0.png");
			if(source.local())
			{
				std::error_code ec;
				if(fs::is_regular_file(fs::u8path(icon.path), ec))
					takePicture(source, icon, "metadata", &item);
			}
			else
			{
				std::vector<uint8_t> png;
				if(source.read(icon.path, 0, 4 * 1024 * 1024, &png) && png.size() > 8)
				{
					item.picture = std::move(png);
					item.pictureExt = ".png";
					item.pictureFrom = "metadata";
				}
			}
			break;
		}
		case Kind::Payload:
		case Kind::Archive:
		case Kind::Other:
			break;
	}
	return item;
}

ShelfItem identify(FileSource &source, const ShelfEntry &entry, const std::vector<ShelfEntry> &siblings,
	const DiscProbe &probe)
{
	const FolderIndex folder(siblings);
	std::map<std::string, std::string> trackOf;
	if(extensionOf(entry.name) == ".cue")
		cueTracks(source, { entry }, probe, &trackOf);
	const auto track = trackOf.find(entry.name);
	return identify(source, entry, folder, probe, track == trackOf.end() ? std::string() : track->second);
}

std::vector<ShelfItem> readShelf(FileSource &source, const std::vector<ShelfEntry> &entries, const DiscProbe &probe,
	const std::atomic<bool> *cancel)
{
	std::vector<ShelfItem> items;
	const FolderIndex folder(entries);
	std::map<std::string, std::string> trackOf;
	const std::set<std::string> tracks = cueTracks(source, entries, probe, &trackOf);
	for(const ShelfEntry &entry : entries)
	{
		if(cancel && *cancel)
			break;
		if(!shownOnShelf(entry) || (!entry.directory && tracks.count(lower(entry.name))))
			continue;
		const auto track = trackOf.find(entry.name);
		items.push_back(identify(source, entry, folder, probe, track == trackOf.end() ? std::string() : track->second));
	}
	return items;
}

FolderPreview previewFolder(FileSource &source, const std::string &dir, const DiscProbe &probe, size_t max,
	size_t budget, const std::atomic<bool> *cancel)
{
	FolderPreview preview;
	std::vector<ShelfEntry> entries;
	if(!source.list(dir, &entries, nullptr))
		return preview;
	const FolderIndex folder(entries);
	std::map<std::string, std::string> trackOf;
	const std::set<std::string> tracks = cueTracks(source, entries, probe, &trackOf);
	std::vector<const ShelfEntry *> shown;
	std::vector<const ShelfEntry *> pictures;
	for(const ShelfEntry &entry : entries)
	{
		if(!entry.directory && tracks.count(lower(entry.name)))
			continue;
		if(shownOnShelf(entry))
			shown.push_back(&entry);
		else if(!entry.directory && isPicture(extensionOf(entry.name)))
			pictures.push_back(&entry);
	}
	preview.count = static_cast<int>(shown.size());
	// What most likely has a picture first: packages and app folders.
	std::stable_sort(shown.begin(), shown.end(), [](const ShelfEntry *a, const ShelfEntry *b) {
		auto rank = [](const ShelfEntry *e) {
			const Kind kind = kindOf(e->name, e->directory, e->size);
			return kind == Kind::Package ? 0 : kind == Kind::Folder ? 1 : kind == Kind::Disc || kind == Kind::Image ? 2 : 3;
		};
		return rank(a) < rank(b);
	});
	// The games first; a folder inside is shown too, as a tile of its own,
	// so it shows there is more than games in it.
	std::vector<ShelfItem> games;
	ShelfItem inner;
	bool hasFolder = false;
	size_t looked = 0;
	for(const ShelfEntry *entry : shown)
	{
		if((games.size() >= max && hasFolder) || looked >= budget || (cancel && *cancel))
			break;
		const Kind kind = kindOf(entry->name, entry->directory, entry->size);
		if(kind == Kind::Payload || kind == Kind::Archive || (kind != Kind::Folder && games.size() >= max))
			continue;
		++looked;
		const auto track = trackOf.find(entry->name);
		ShelfItem item = identify(source, *entry, folder, probe, track == trackOf.end() ? std::string() : track->second);
		if(item.directory && !item.appFolder)
		{
			if(!hasFolder)
			{
				inner = std::move(item);
				hasFolder = true;
			}
			continue;
		}
		if(!item.pictureFile.empty() || !item.picture.empty() || (!item.titleId.empty() && item.group == "games"))
			games.push_back(std::move(item));
	}
	const size_t room = hasFolder && max > 0 ? max - 1 : max;
	if(games.size() > room)
		games.resize(room);
	preview.pictures = std::move(games);
	// A folder of pictures shows them.
	for(const ShelfEntry *entry : pictures)
	{
		if(preview.pictures.size() >= room)
			break;
		ShelfItem item;
		item.path = entry->path;
		item.name = entry->name;
		item.title = stemOf(entry->name);
		if(takePicture(source, *entry, "folder", &item))
			preview.pictures.push_back(std::move(item));
	}
	if(hasFolder)
		preview.pictures.push_back(std::move(inner));
	return preview;
}

// ── the cache ────────────────────────────────────────────────────────────

namespace {

Json itemToJson(const ShelfItem &item)
{
	Json json = Json::makeObject();
	json.set("kind", Json::fromString(kindName(item.kind)));
	json.set("group", Json::fromString(item.group));
	json.set("title", Json::fromString(item.title));
	json.set("title_id", Json::fromString(item.titleId));
	json.set("version", Json::fromString(item.version));
	json.set("platform", Json::fromString(item.platform));
	json.set("serial", Json::fromString(item.serial));
	json.set("category", Json::fromString(item.category));
	json.set("content_id", Json::fromString(item.contentId));
	json.set("app_folder", Json::fromBool(item.appFolder));
	json.set("readable", Json::fromBool(item.readable));
	json.set("picture", Json::fromString(item.pictureFile));
	json.set("picture_from", Json::fromString(item.pictureFrom));
	json.set("track", Json::fromString(item.trackPath));
	return json;
}

Kind kindFromName(const std::string &name)
{
	for(Kind kind : { Kind::Package, Kind::Disc, Kind::Image, Kind::Folder, Kind::Payload, Kind::Archive })
		if(name == kindName(kind))
			return kind;
	return Kind::Other;
}

ShelfItem itemFromJson(const Json &json)
{
	ShelfItem item;
	item.kind = kindFromName(json["kind"].toString());
	item.group = json["group"].toString();
	item.title = json["title"].toString();
	item.titleId = json["title_id"].toString();
	item.version = json["version"].toString();
	item.platform = json["platform"].toString();
	item.serial = json["serial"].toString();
	item.category = json["category"].toString();
	item.contentId = json["content_id"].toString();
	item.appFolder = json["app_folder"].toLooseBool(false);
	item.readable = json["readable"].toLooseBool(true);
	item.pictureFile = json["picture"].toString();
	item.pictureFrom = json["picture_from"].toString();
	item.trackPath = json["track"].toString();
	return item;
}

bool exists(const std::string &file)
{
	std::error_code ec;
	return !file.empty() && fs::exists(fs::u8path(file), ec);
}

} // namespace

ShelfCache::ShelfCache(std::string folder) : folder_(std::move(folder))
{
	std::error_code ec;
	fs::create_directories(fs::u8path(folder_) / "img", ec);
	load();
}

std::string ShelfCache::keyOf(const std::string &place, const ShelfItem &item)
{
	return place + "|" + item.path + "|" + std::to_string(item.size) + "|" + std::to_string(item.modified);
}

bool ShelfCache::find(const std::string &key, ShelfItem *item) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	const auto found = items_.find(key);
	if(found == items_.end())
		return false;
	// The picture may have been cleared away since.
	if(!found->second.pictureFile.empty() && found->second.pictureFrom != "folder"
		&& !exists(found->second.pictureFile))
		return false;
	const ShelfItem &kept = found->second;
	item->kind = kept.kind;
	item->group = kept.group;
	item->title = kept.title;
	item->titleId = kept.titleId;
	item->version = kept.version;
	item->platform = kept.platform;
	item->serial = kept.serial;
	item->category = kept.category;
	item->contentId = kept.contentId;
	item->appFolder = kept.appFolder;
	item->readable = kept.readable;
	item->pictureFile = kept.pictureFile;
	item->pictureFrom = kept.pictureFrom;
	item->trackPath = kept.trackPath;
	return true;
}

std::string ShelfCache::keep(const std::vector<uint8_t> &bytes, const std::string &ext)
{
	if(bytes.empty())
		return std::string();
	const std::string name = sha256Hex(std::string(bytes.begin(), bytes.end())).substr(0, 32) + (ext.empty() ? ".png" : ext);
	const fs::path file = fs::u8path(folder_) / "img" / name;
	std::error_code ec;
	if(!fs::exists(file, ec))
	{
		std::ofstream out(file, std::ios::binary);
		out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if(!out)
			return std::string();
	}
	return file.u8string();
}

void ShelfCache::store(const std::string &key, ShelfItem *item)
{
	if(!item->picture.empty())
	{
		item->pictureFile = keep(item->picture, item->pictureExt);
		item->picture.clear();
	}
	std::lock_guard<std::mutex> lock(mutex_);
	ShelfItem kept = *item;
	kept.picture.clear();
	items_[key] = kept;
	// A picture of the game itself serves every other copy of it.
	if(!item->pictureFile.empty() && item->pictureFrom != "app" && item->group == "games")
	{
		if(!item->titleId.empty())
			byId_[upper(item->titleId)] = item->pictureFile;
		if(!item->serial.empty())
			byId_[upper(item->serial)] = item->pictureFile;
	}
	dirty_ = true;
}

std::string ShelfCache::pictureFor(const std::string &id) const
{
	if(id.empty())
		return std::string();
	std::lock_guard<std::mutex> lock(mutex_);
	const auto found = byId_.find(upper(id));
	return found != byId_.end() && exists(found->second) ? found->second : std::string();
}

void ShelfCache::remember(const std::string &id, const std::string &file)
{
	if(id.empty() || file.empty())
		return;
	std::lock_guard<std::mutex> lock(mutex_);
	byId_[upper(id)] = file;
	dirty_ = true;
}

bool ShelfCache::findPreview(const std::string &key, FolderPreview *preview) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	const auto found = previews_.find(key);
	if(found == previews_.end())
		return false;
	for(const ShelfItem &picture : found->second.pictures)
		if(!picture.pictureFile.empty() && !exists(picture.pictureFile))
			return false;
	*preview = found->second;
	return true;
}

void ShelfCache::storePreview(const std::string &key, FolderPreview *preview)
{
	for(ShelfItem &picture : preview->pictures)
		if(!picture.picture.empty())
		{
			picture.pictureFile = keep(picture.picture, picture.pictureExt);
			picture.picture.clear();
		}
	std::lock_guard<std::mutex> lock(mutex_);
	previews_[key] = *preview;
	dirty_ = true;
}

void ShelfCache::load()
{
	std::ifstream in(fs::u8path(folder_) / "index.json", std::ios::binary);
	if(!in)
		return;
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	const Json root = Json::parse(text);
	if(!root.isObject())
		return;
	for(const auto &entry : root["items"].members())
		items_[entry.first] = itemFromJson(entry.second);
	for(const auto &entry : root["ids"].members())
		byId_[entry.first] = entry.second.toString();
	for(const auto &entry : root["previews"].members())
	{
		FolderPreview preview;
		preview.count = static_cast<int>(entry.second["count"].toInt());
		for(const Json &picture : entry.second["pictures"].items())
		{
			ShelfItem item = itemFromJson(picture);
			item.path = picture["path"].toString();
			preview.pictures.push_back(item);
		}
		previews_[entry.first] = preview;
	}
}

void ShelfCache::save()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if(!dirty_)
		return;
	// Kept within bounds: past a size it starts again.
	if(items_.size() > 40000)
		items_.clear();
	if(previews_.size() > 4000)
		previews_.clear();
	Json root = Json::makeObject();
	Json items = Json::makeObject();
	for(const auto &entry : items_)
		items.set(entry.first, itemToJson(entry.second));
	root.set("items", items);
	Json ids = Json::makeObject();
	for(const auto &entry : byId_)
		ids.set(entry.first, Json::fromString(entry.second));
	root.set("ids", ids);
	Json previews = Json::makeObject();
	for(const auto &entry : previews_)
	{
		Json preview = Json::makeObject();
		preview.set("count", Json::fromInt(entry.second.count));
		Json pictures = Json::makeArray();
		for(const ShelfItem &picture : entry.second.pictures)
		{
			Json json = itemToJson(picture);
			json.set("path", Json::fromString(picture.path));
			pictures.push(json);
		}
		preview.set("pictures", pictures);
		previews.set(entry.first, preview);
	}
	root.set("previews", previews);
	const fs::path file = fs::u8path(folder_) / "index.json";
	const fs::path partial = fs::u8path(folder_) / "index.json.partial";
	{
		std::ofstream out(partial, std::ios::binary | std::ios::trunc);
		out << root.dump();
		if(!out)
			return;
	}
	std::error_code ec;
	fs::rename(partial, file, ec);
	if(!ec)
		dirty_ = false;
}

} // namespace library
} // namespace orbislink
