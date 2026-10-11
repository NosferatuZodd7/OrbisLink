// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/ftp/remote_move.h"

#include "orbislink/common/log.h"
#include "orbislink/common/tr.h"
#include "orbislink/common/util.h"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace orbislink {

namespace {

std::string parentOf(const std::string &path)
{
	const size_t slash = path.find_last_of('/');
	return slash == 0 || slash == std::string::npos ? std::string("/") : path.substr(0, slash);
}

std::string nameOf(const std::string &path)
{
	return path.substr(path.find_last_of('/') + 1);
}

// What is at `path`: found in its parent's listing.
bool lookUp(FtpClient &ftp, const std::string &path, FtpEntry *found)
{
	std::vector<FtpEntry> entries;
	if(!ftp.list(parentOf(path), &entries).ok)
		return false;
	for(const FtpEntry &entry : entries)
		if(entry.name == nameOf(path))
		{
			if(found)
				*found = entry;
			return true;
		}
	return false;
}

struct Item
{
	std::string relative; // "" for the top
	bool directory = false;
	int64_t size = 0;
};

// Everything under a folder, folders before what is in them.
bool walk(FtpClient &ftp, const std::string &root, const std::string &relative, std::vector<Item> *items,
	std::string *error)
{
	std::vector<FtpEntry> entries;
	const FtpResult listed = ftp.list(relative.empty() ? root : root + "/" + relative, &entries);
	if(!listed.ok)
	{
		*error = listed.message;
		return false;
	}
	for(const FtpEntry &entry : entries)
	{
		if(entry.name.empty() || entry.name == "." || entry.name == "..")
			continue;
		const std::string child = relative.empty() ? entry.name : relative + "/" + entry.name;
		items->push_back({ child, entry.isDirectory, entry.isDirectory ? 0 : entry.size });
		if(entry.isDirectory && !walk(ftp, root, child, items, error))
			return false;
	}
	return true;
}

} // namespace

FtpResult moveOnConsole(FtpClient &ftp, const std::string &fromPath, const std::string &toPath,
	const std::string &scratchDir, const FtpProgressCallback &progress, bool *throughPc)
{
	if(throughPc)
		*throughPc = false;
	const std::string from = normalizeRemotePath(fromPath);
	const std::string to = normalizeRemotePath(toPath);
	if(from == to)
		return FtpResult::success();
	// Into itself: no rename does it, and a copy would be deleted with the original.
	if(startsWith(to, from + "/"))
		return FtpResult::failure(QT_TRANSLATE_NOOP("Messages", "A folder cannot be moved into itself."));
	if(lookUp(ftp, to, nullptr))
		return FtpResult::failure(std::string(QT_TRANSLATE_NOOP("Messages",
			"There is already something with that name there")) + ": " + to);

	const FtpResult renamed = ftp.rename(from, to);
	if(renamed.ok || renamed.cancelled)
		return renamed;

	// Not renamed: most likely another disk. Through this PC, then — if it is
	// still there to copy.
	FtpEntry top;
	if(!lookUp(ftp, from, &top))
		return renamed;
	std::vector<Item> items { { std::string(), top.isDirectory, top.isDirectory ? 0 : top.size } };
	std::string why;
	if(top.isDirectory && !walk(ftp, from, std::string(), &items, &why))
		return FtpResult::failure(why);

	int64_t bytes = 0;
	int64_t largest = 0;
	for(const Item &item : items)
	{
		bytes += item.size;
		largest = std::max(largest, item.size);
	}
	std::error_code ec;
	fs::create_directories(fs::u8path(scratchDir), ec);
	const fs::space_info room = fs::space(fs::u8path(scratchDir), ec);
	if(!ec && static_cast<int64_t>(room.available) < largest + 64LL * 1024 * 1024)
		return FtpResult::failure(std::string(QT_TRANSLATE_NOOP("Messages",
			"Not enough free space on this PC to move it between disks")) + " (" + humanBytes(largest) + ")");

	logInfo("FTP: " + from + " cannot be renamed to " + to + " (" + renamed.message
		+ "); copying it through this PC.");
	if(throughPc)
		*throughPc = true;
	const int64_t total = bytes * 2;
	int64_t done = 0;
	const std::string local = joinPath(scratchDir, "moving.part");
	auto at = [&](const Item &item, const std::string &root) {
		return item.relative.empty() ? root : root + "/" + item.relative;
	};

	for(const Item &item : items)
	{
		const std::string source = at(item, from);
		const std::string target = at(item, to);
		if(item.directory)
		{
			const FtpResult made = ftp.makeDirectory(target);
			if(!made.ok)
				return made;
			continue;
		}
		const int64_t before = done;
		FtpResult step = ftp.download(source, local, [&](int64_t got, int64_t) {
			return !progress || progress(before + got, total);
		});
		if(step.ok)
			step = ftp.upload(local, target, [&](int64_t sent, int64_t) {
				return !progress || progress(before + item.size + sent, total);
			});
		int64_t landed = -1;
		if(step.ok && (!ftp.remoteSize(target, &landed).ok || landed != item.size))
			step = FtpResult::failure(std::string(QT_TRANSLATE_NOOP("Messages",
				"The copy on the console is not the same size")) + ": " + target);
		fs::remove(fs::u8path(local), ec);
		if(!step.ok)
		{
			// The half-made copy goes; the original was never touched.
			ftp.removeFile(target);
			return step;
		}
		done = before + item.size * 2;
		if(progress && !progress(done, total))
		{
			FtpResult cancelled = FtpResult::failure("cancelled");
			cancelled.cancelled = true;
			return cancelled;
		}
	}

	// All of it is in place: the original goes, what is in folders first.
	for(auto it = items.rbegin(); it != items.rend(); ++it)
	{
		const std::string source = at(*it, from);
		const FtpResult removed = it->directory ? ftp.removeDirectory(source) : ftp.removeFile(source);
		if(!removed.ok)
			return FtpResult::failure(std::string(QT_TRANSLATE_NOOP("Messages",
				"Copied, but the original could not be deleted")) + ": " + removed.message);
	}
	return FtpResult::success();
}

} // namespace orbislink
