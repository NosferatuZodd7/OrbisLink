// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/store/store_installer.h"

#include "orbislink/common/json.h"
#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/ftp/ftp_client.h"
#include "orbislink/store/zip_reader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

namespace fs = std::filesystem;

namespace orbislink::store {

namespace {

bool fail(std::string *error, const std::string &why)
{
	if(error)
		*error = why;
	return false;
}

bool plainTitleId(const std::string &id)
{
	if(id.size() != 9)
		return false;
	for(size_t i = 0; i < 9; ++i)
		if(i < 4 ? !std::isupper(static_cast<unsigned char>(id[i])) : !std::isdigit(static_cast<unsigned char>(id[i])))
			return false;
	return true;
}

std::string parentOf(const std::string &path)
{
	const size_t slash = path.find_last_of('/');
	return slash == 0 || slash == std::string::npos ? std::string("/") : path.substr(0, slash);
}

std::string nameOf(const std::string &path)
{
	return path.substr(path.find_last_of('/') + 1);
}

// Whether the folder or file is in its parent's listing.
bool exists(StoreRemote &remote, const std::string &path, bool *isDirectory = nullptr)
{
	std::vector<FtpEntry> entries;
	std::string error;
	if(!remote.list(parentOf(path), &entries, &error))
		return false;
	for(const FtpEntry &entry : entries)
		if(entry.name == nameOf(path))
		{
			if(isDirectory)
				*isDirectory = entry.isDirectory;
			return true;
		}
	return false;
}

// Makes a folder unless it is there: making one that exists is an error
// the FTP client would retry.
void ensureDir(StoreRemote &remote, const std::string &dir)
{
	if(!exists(remote, dir))
		remote.makeDirectory(dir);
}

// The permissions in a LIST line: "drwxrwxrwx 1 …".
bool openToAll(const FtpEntry &entry)
{
	return entry.rawLine.size() >= 10 && entry.rawLine.compare(1, 9, "rwxrwxrwx") == 0;
}

bool readJsonFile(const std::string &path, Json *out)
{
	std::ifstream in(fs::u8path(path), std::ios::binary);
	if(!in)
		return false;
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	*out = Json::parse(text);
	return out->isObject();
}

// What the user put in the app's folder — games, saves, settings: what the
// new archive does not have — goes on into the new folder, as copying the
// new version over the old one would keep it. What cannot be moved is left
// where it is, and named.
void carryOver(StoreRemote &remote, const std::string &from, const std::string &to, const std::string &relative,
	const std::set<std::string> &shipped, InstallReport *report)
{
	std::vector<FtpEntry> entries;
	std::string why;
	if(!remote.list(relative.empty() ? from : from + "/" + relative, &entries, &why))
		return;
	for(const FtpEntry &entry : entries)
	{
		if(entry.name.empty() || entry.name == "." || entry.name == "..")
			continue;
		const std::string child = relative.empty() ? entry.name : relative + "/" + entry.name;
		if(shipped.count(child))
		{
			// The new version has it: a folder of both is gone through, a
			// file is the new one's.
			if(entry.isDirectory)
				carryOver(remote, from, to, child, shipped, report);
			continue;
		}
		if(remote.rename(from + "/" + child, to + "/" + child, &why))
			report->carried.push_back(child);
		else
			report->left.push_back(child);
	}
}

} // namespace

bool removeRemoteTree(StoreRemote &remote, const std::string &path, std::string *error)
{
	std::vector<FtpEntry> entries;
	if(!remote.list(path, &entries, error))
		return false;
	for(const FtpEntry &entry : entries)
	{
		if(entry.name == "." || entry.name == ".." || entry.name.empty())
			continue;
		const std::string child = path + "/" + entry.name;
		if(entry.isDirectory ? !removeRemoteTree(remote, child, error) : !remote.removeFile(child, error))
			return false;
	}
	return remote.removeDirectory(path, error);
}

StoreInstaller::StoreInstaller(std::string installRoot) : installRoot_(std::move(installRoot)) {}

std::vector<InstalledApp> StoreInstaller::installed(StoreRemote &remote, const std::string &scratchDir,
	std::string *error)
{
	std::vector<InstalledApp> out;
	std::vector<FtpEntry> entries;
	if(!remote.list(installRoot_, &entries, error))
		return out;
	std::error_code ignored;
	fs::create_directories(fs::u8path(scratchDir), ignored);
	for(const FtpEntry &entry : entries)
	{
		if(!entry.isDirectory || !plainTitleId(entry.name))
			continue;
		InstalledApp app;
		app.titleId = entry.name;
		app.folder = installRoot_ + "/" + entry.name;
		const std::string local = joinPath(scratchDir, entry.name + "-param.json");
		std::string why;
		Json param;
		if(remote.download(app.folder + "/sce_sys/param.json", local, &why) && readJsonFile(local, &param))
			app.contentVersion = param["contentVersion"].toString();
		fs::remove(fs::u8path(local), ignored);
		out.push_back(app);
	}
	return out;
}

bool StoreInstaller::install(StoreRemote &remote, const std::string &zipPath, const std::string &titleId,
	const std::string &scratchDir, const Progress &progress, std::string *error, InstallReport *report)
{
	InstallReport ignoredReport;
	if(!report)
		report = &ignoredReport;
	*report = InstallReport();
	if(!plainTitleId(titleId))
		return fail(error, "not a title ID: " + titleId);
	std::vector<ZipEntry> entries;
	if(!readZipDirectory(zipPath, &entries, error))
		return false;

	// The app folder: <TITLEID>/ at the top, as the catalog asks, or the
	// archive's root when eboot.bin is there.
	std::string prefix;
	bool found = false;
	for(const ZipEntry &entry : entries)
		if(entry.name == "eboot.bin")
		{
			found = true;
			prefix.clear();
			break;
		}
		else if(!found && endsWith(entry.name, "/eboot.bin")
			&& std::count(entry.name.begin(), entry.name.end(), '/') == 1)
		{
			found = true;
			prefix = entry.name.substr(0, entry.name.size() - std::string("eboot.bin").size());
		}
	const auto param = std::find_if(entries.begin(), entries.end(),
		[&prefix](const ZipEntry &e) { return e.name == prefix + "sce_sys/param.json"; });
	if(!found || param == entries.end())
		return fail(error, "the archive holds no app folder (eboot.bin and sce_sys/param.json)");

	std::error_code ignored;
	fs::create_directories(fs::u8path(scratchDir), ignored);
	// The app it says it is: what is unpacked must be the title asked for.
	const std::string localParam = joinPath(scratchDir, "param.json");
	if(!extractZipEntry(zipPath, *param, localParam, nullptr, error))
		return false;
	Json paramJson;
	const bool readable = readJsonFile(localParam, &paramJson);
	fs::remove(fs::u8path(localParam), ignored);
	if(!readable)
		return fail(error, "the app's sce_sys/param.json is not readable");
	const std::string declared = paramJson["titleId"].toString();
	if(!declared.empty() && declared != titleId)
		return fail(error, "the archive is " + declared + ", not " + titleId);

	// Into a staging folder outside what ShadowMountPlus scans.
	ensureDir(remote, "/data/orbislink");
	ensureDir(remote, stagingRoot());
	const std::string staging = stagingRoot() + "/" + titleId;
	if(exists(remote, staging) && !removeRemoteTree(remote, staging, error))
		return false;
	remote.makeDirectory(staging);

	std::set<std::string> folders;
	std::vector<const ZipEntry *> files;
	uint64_t total = 0;
	for(const ZipEntry &entry : entries)
	{
		if(!startsWith(entry.name, prefix) || entry.name.size() == prefix.size())
			continue;
		const std::string relative = entry.name.substr(prefix.size());
		std::string dir = entry.directory ? relative.substr(0, relative.size() - 1) : parentOf("/" + relative).substr(1);
		while(!dir.empty())
		{
			folders.insert(dir);
			const size_t slash = dir.find_last_of('/');
			dir = slash == std::string::npos ? std::string() : dir.substr(0, slash);
		}
		if(!entry.directory)
		{
			files.push_back(&entry);
			total += entry.size;
		}
	}
	// Parents before children: sorted, "a" comes before "a/b".
	for(const std::string &dir : folders)
		remote.makeDirectory(staging + "/" + dir);

	const std::string temporary = joinPath(scratchDir, "unpacking.tmp");
	uint64_t done = 0;
	for(const ZipEntry *entry : files)
	{
		const std::string relative = entry->name.substr(prefix.size());
		const uint64_t before = done;
		auto step = [&](uint64_t written) {
			return !progress || progress("unpack", total ? static_cast<double>(before + written) / total : 1.0);
		};
		std::string why;
		if(!extractZipEntry(zipPath, *entry, temporary, step, &why)
			|| !remote.upload(temporary, staging + "/" + relative, &why))
		{
			fs::remove(fs::u8path(temporary), ignored);
			std::string cleanup;
			removeRemoteTree(remote, staging, &cleanup);
			return fail(error, why == "cancelled" ? why : relative + ": " + why);
		}
		done += entry->size;
	}
	fs::remove(fs::u8path(temporary), ignored);

	// Open to everyone, folders and files: the console starts an app only
	// then (CE-107750-0 otherwise). Many servers write them so already.
	if(progress)
		progress("finish", 0.0);
	bool chmodWorks = true;
	std::vector<std::string> pending = { staging };
	std::vector<FtpEntry> top;
	std::string why;
	if(remote.list(stagingRoot(), &top, &why))
		for(const FtpEntry &entry : top)
			if(entry.name == titleId && !openToAll(entry))
				chmodWorks = remote.chmod(staging, "777", &why);
	while(chmodWorks && !pending.empty())
	{
		const std::string dir = pending.back();
		pending.pop_back();
		std::vector<FtpEntry> listing;
		if(!remote.list(dir, &listing, &why))
			break;
		for(const FtpEntry &entry : listing)
		{
			if(entry.name == "." || entry.name == "..")
				continue;
			const std::string child = dir + "/" + entry.name;
			if(entry.isDirectory)
				pending.push_back(child);
			if(!openToAll(entry) && !remote.chmod(child, "777", &why))
			{
				chmodWorks = false;
				break;
			}
		}
	}
	report->permissionsSet = chmodWorks;

	// In place: the copy it replaces goes to previous/, then the new one in.
	ensureDir(remote, installRoot_);
	const std::string target = installRoot_ + "/" + titleId;
	const std::string kept = previousRoot() + "/" + titleId;
	const bool replacing = exists(remote, target);
	if(replacing)
	{
		ensureDir(remote, previousRoot());
		if(exists(remote, kept) && !removeRemoteTree(remote, kept, error))
			return false;
		if(!remote.rename(target, kept, error))
			return false;
	}
	if(!remote.rename(staging, target, error))
	{
		if(replacing)
		{
			std::string back;
			remote.rename(kept, target, &back);
		}
		return false;
	}
	if(replacing)
	{
		report->replaced = true;
		report->earlierCopy = kept;
		std::set<std::string> shipped(folders.begin(), folders.end());
		for(const ZipEntry *entry : files)
			shipped.insert(entry->name.substr(prefix.size()));
		carryOver(remote, kept, target, std::string(), shipped, report);
	}
	if(progress)
		progress("finish", 1.0);
	logInfo("Store: installed " + titleId + " in " + target + (replacing ? " (previous copy kept)" : ""));
	return true;
}

bool StoreInstaller::uninstall(StoreRemote &remote, const std::string &titleId, std::string *error)
{
	if(!plainTitleId(titleId))
		return fail(error, "not a title ID: " + titleId);
	const std::string target = installRoot_ + "/" + titleId;
	if(!exists(remote, target))
		return true;
	if(!removeRemoteTree(remote, target, error))
		return false;
	logInfo("Store: removed " + target);
	return true;
}

} // namespace orbislink::store
