// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// The shelf: the games page's library, where every place games can be — a
// folder of this PC, a drive, a folder of the console — is shown as a
// collection of covers instead of a file list. This is the part that has no
// interface: reading a folder (of this PC, or of the console over FTP),
// telling what each thing in it is, finding a picture for it, and keeping
// what was found so a folder opened again is shown at once.
//
// A picture is looked for in this order (the first one found wins):
//   1. one the app already has for the game (by title ID or serial);
//   2. an image beside it, or in its folder: "<name>.jpg", "cover.png"…;
//   3. its own metadata: a package's ICON0, an app's sce_sys/icon0.png;
//   4. a cover collection, by the disc's serial (done by the caller, which
//      has the network);
//   5. none: the interface draws a disc in its place.

#include "orbislink/library/console_library.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace orbislink {

class FtpClient;

namespace library {

// One name in a folder.
struct ShelfEntry
{
	std::string name;
	std::string path;
	bool directory = false;
	int64_t size = 0;
	// Seconds since 1970 (UTC); 0 when it is not known.
	int64_t modified = 0;
};

// Where a folder is read from.
class FileSource
{
public:
	virtual ~FileSource() = default;
	virtual bool list(const std::string &dir, std::vector<ShelfEntry> *entries, std::string *error) = 0;
	// Up to `length` bytes from `offset` (fewer at the end of the file).
	virtual bool read(const std::string &path, int64_t offset, size_t length, std::vector<uint8_t> *bytes) = 0;
	virtual std::string join(const std::string &dir, const std::string &name) const = 0;
	// A folder of this PC: its pictures are used where they are.
	virtual bool local() const = 0;
};

// A folder of this PC (paths in UTF-8).
class LocalSource : public FileSource
{
public:
	bool list(const std::string &dir, std::vector<ShelfEntry> *entries, std::string *error) override;
	bool read(const std::string &path, int64_t offset, size_t length, std::vector<uint8_t> *bytes) override;
	std::string join(const std::string &dir, const std::string &name) const override;
	bool local() const override { return true; }
};

// A folder of the console, over its FTP.
class FtpSource : public FileSource
{
public:
	explicit FtpSource(FtpClient &ftp) : ftp_(ftp) {}
	bool list(const std::string &dir, std::vector<ShelfEntry> *entries, std::string *error) override;
	bool read(const std::string &path, int64_t offset, size_t length, std::vector<uint8_t> *bytes) override;
	std::string join(const std::string &dir, const std::string &name) const override;
	bool local() const override { return false; }

private:
	FtpClient &ftp_;
};

// The date of an FTP listing line ("Oct 10 20:17", "Oct 10  2025"), in
// seconds since 1970; 0 when it cannot be read. `nowUnix` gives the year of
// the first form (the last twelve months).
int64_t ftpListingTime(const std::string &text, int64_t nowUnix);

// What a PS1/PS2 disc is, from the converter (which this core does not
// link): the image a cue sheet names, and the disc's platform and serial.
struct DiscId
{
	std::string platform; // "ps1"/"ps2", empty when it is not a disc the converter reads
	std::string serial;   // "SLUS-20946"
	std::string titleId;  // "SLUS20946"
	std::string title;    // from the file name, tidied
};
struct DiscProbe
{
	std::function<std::string(const std::string &cueText)> cueTrack;
	std::function<DiscId(const std::string &fileName, int64_t size,
		const std::function<bool(uint64_t offset, uint8_t *out, size_t size)> &read)> inspect;
	// A known title for the serial ("Neon Drift"), when there is one.
	std::function<std::string(const std::string &platform, const std::string &titleId)> knownTitle;
};

// One thing on the shelf.
struct ShelfItem
{
	std::string path;
	std::string name;
	std::string ext;          // ".pkg", ".iso"… (lower case); empty for a folder
	bool directory = false;
	int64_t size = 0;
	int64_t modified = 0;
	Kind kind = Kind::Other;
	// "folders" (to open), "games" (games and apps), "extras" (updates,
	// add-ons, themes), "payloads", "other" (archives, what cannot be read).
	std::string group;
	std::string title;
	std::string titleId;
	std::string version;
	std::string platform;     // "ps4", "ps5", "ps1", "ps2" or empty
	std::string serial;
	std::string category;     // a package's: "gd", "gp", "ac"…
	std::string contentId;
	bool appFolder = false;   // a folder with sce_sys: an app, not a folder to open
	bool readable = true;     // a .pkg or a disc that could not be read
	// The picture: a file to show as it is, or bytes still to be kept.
	std::string pictureFile;
	std::vector<uint8_t> picture;
	std::string pictureExt;   // ".png"/".jpg" for `picture`
	// Where it came from: "app", "folder", "metadata", "collection" or "".
	std::string pictureFrom;
	// For a cue sheet: the track it plays, shown as one with it.
	std::string trackPath;
};

// A folder's names, to find one (in lower case) without walking the list.
class FolderIndex
{
public:
	explicit FolderIndex(const std::vector<ShelfEntry> &entries);
	const ShelfEntry *find(const std::string &lowerName) const;

private:
	std::map<std::string, const ShelfEntry *> byName_;
};

// The tracks the folder's cue sheets name (in lower case), not shown apart;
// `trackOf` gets each cue sheet's track by the sheet's name.
std::set<std::string> cueTracks(FileSource &source, const std::vector<ShelfEntry> &entries, const DiscProbe &probe,
	std::map<std::string, std::string> *trackOf);

// The things worth showing in a listing, told apart and named, in the order
// they came: a cue sheet's track is folded into it, images and anything the
// app has no use for are left out. Reads what each one needs to be named
// (a package's header, a disc's SYSTEM.CNF, an app's param file) and finds
// its picture (steps 2 and 3). `cancel` stops it between items.
std::vector<ShelfItem> readShelf(FileSource &source, const std::vector<ShelfEntry> &entries,
	const DiscProbe &probe, const std::atomic<bool> *cancel = nullptr);
// The same for one entry, with the rest of the folder around it.
ShelfItem identify(FileSource &source, const ShelfEntry &entry, const std::vector<ShelfEntry> &siblings,
	const DiscProbe &probe);
// As above, with the folder already indexed (and a cue sheet's track).
ShelfItem identify(FileSource &source, const ShelfEntry &entry, const FolderIndex &folder, const DiscProbe &probe,
	const std::string &cueTrack);
// Only the kind, title and group, from the name: what is shown before
// anything is read.
ShelfItem quickItem(const ShelfEntry &entry);
// Whether the shelf shows this entry at all (and what it is).
bool shownOnShelf(const ShelfEntry &entry);

// A folder's look: up to `max` things in it to show on its card — the first
// games (with their pictures, or a disc where they have none) and, when it
// has folders of its own, one of them last, shown as a folder tile — and how
// many things there are to show. Reads at most `budget` items to find them.
struct FolderPreview
{
	int count = 0;
	std::vector<ShelfItem> pictures;
};
FolderPreview previewFolder(FileSource &source, const std::string &dir, const DiscProbe &probe,
	size_t max = 4, size_t budget = 16, const std::atomic<bool> *cancel = nullptr);

// What was found, kept on disk: the metadata of each item by a key that
// changes with the file (its path, size and date), the pictures as files,
// and the pictures already found for a title ID or serial (step 1).
class ShelfCache
{
public:
	explicit ShelfCache(std::string folder);

	static std::string keyOf(const std::string &place, const ShelfItem &item);
	bool find(const std::string &key, ShelfItem *item) const;
	// Keeps the item; picture bytes go to a file, which `item` then points to.
	void store(const std::string &key, ShelfItem *item);
	// A picture for a title ID or serial, when one was kept.
	std::string pictureFor(const std::string &id) const;
	void remember(const std::string &id, const std::string &file);
	// Writes a picture's bytes to the cache and returns its file.
	std::string keep(const std::vector<uint8_t> &bytes, const std::string &ext);
	// Folder previews, by a key of the folder (its path and date).
	bool findPreview(const std::string &key, FolderPreview *preview) const;
	void storePreview(const std::string &key, FolderPreview *preview);
	// Writes the index when something changed.
	void save();
	const std::string &folder() const { return folder_; }

private:
	void load();

	std::string folder_;
	mutable std::mutex mutex_;
	std::map<std::string, ShelfItem> items_;
	std::map<std::string, std::string> byId_;
	std::map<std::string, FolderPreview> previews_;
	bool dirty_ = false;
};

} // namespace library
} // namespace orbislink
