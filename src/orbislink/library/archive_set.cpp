// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/library/archive_set.h"

#include "orbislink/common/tr.h"
#include "orbislink/common/util.h"

#include <filesystem>
#include <map>
#include <sstream>
#include <system_error>

namespace orbislink::library {

namespace fs = std::filesystem;

namespace {

bool allDigits(const std::string &text)
{
	if(text.empty())
		return false;
	for(const char c : text)
		if(c < '0' || c > '9')
			return false;
	return true;
}

int number(const std::string &digits)
{
	int value = 0;
	for(const char c : digits)
		value = value * 10 + (c - '0');
	return value;
}

} // namespace

bool parseArchiveVolume(const std::string &fileName, ArchiveVolume *volume)
{
	const std::string lower = toLower(fileName);
	ArchiveVolume found;
	const size_t dot = lower.find_last_of('.');
	if(dot == std::string::npos || dot == 0)
		return false;
	const std::string last = lower.substr(dot);
	const std::string stem = fileName.substr(0, dot);
	const std::string lowerStem = lower.substr(0, dot);
	// game.7z.001, game.zip.001, game.rar.001
	if(last.size() >= 4 && allDigits(last.substr(1)) && last.size() <= 5)
	{
		const size_t inner = lowerStem.find_last_of('.');
		if(inner == std::string::npos || inner == 0)
			return false;
		const std::string ext = lowerStem.substr(inner);
		if(ext != ".7z" && ext != ".zip" && ext != ".rar")
			return false;
		const int n = number(last.substr(1));
		if(n < 1)
			return false;
		found.base = stem.substr(0, inner);
		found.index = n - 1;
		found.scheme = "split";
		found.ext = ext;
	}
	// game.r00, game.r01… (after game.rar)
	else if(last.size() == 4 && last[1] == 'r' && allDigits(last.substr(2)))
	{
		found.base = stem;
		found.index = number(last.substr(2)) + 1;
		found.scheme = "old";
		found.ext = ".rar";
	}
	else if(last == ".rar")
	{
		// game.part1.rar, game.part01.rar
		const size_t part = lowerStem.rfind(".part");
		if(part != std::string::npos && part > 0 && allDigits(lowerStem.substr(part + 5))
			&& lowerStem.size() - part - 5 <= 4)
		{
			const int n = number(lowerStem.substr(part + 5));
			if(n < 1)
				return false;
			found.base = stem.substr(0, part);
			found.index = n - 1;
			found.scheme = "part";
		}
		else
		{
			found.base = stem;
			found.index = 0;
			found.scheme = "single";
		}
		found.ext = ".rar";
	}
	else if(last == ".7z" || last == ".zip")
	{
		found.base = stem;
		found.index = 0;
		found.scheme = "single";
		found.ext = last;
	}
	else
		return false;
	if(found.base.empty())
		return false;
	if(volume)
		*volume = found;
	return true;
}

ArchiveVolumes findArchiveVolumes(const std::string &firstVolume)
{
	ArchiveVolumes set;
	const fs::path first = fs::u8path(firstVolume);
	ArchiveVolume own;
	if(!parseArchiveVolume(first.filename().u8string(), &own))
		return set;
	set.base = own.base;
	set.scheme = own.scheme;
	std::map<int, fs::path> byIndex;
	const fs::path folder = first.has_parent_path() ? first.parent_path() : fs::path(".");
	std::error_code ec;
	for(fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
	{
		std::error_code fileError;
		if(!it->is_regular_file(fileError) || fileError)
			continue;
		ArchiveVolume other;
		if(!parseArchiveVolume(it->path().filename().u8string(), &other) || other.base != own.base
			|| other.ext != own.ext)
			continue;
		// A lone game.rar is the first of an old-style set when .r00… are there.
		const bool sameScheme = other.scheme == own.scheme
			|| (own.scheme == "single" && own.ext == ".rar" && other.scheme == "old");
		if(!sameScheme)
			continue;
		if(other.scheme == "old")
			set.scheme = "old";
		byIndex[other.index] = it->path();
	}
	if(set.scheme == "single")
		byIndex = { { 0, first } };
	int expected = 0;
	for(const auto &[index, path] : byIndex)
	{
		if(index != expected && set.missingIndex < 0)
		{
			set.missingIndex = expected;
			set.missingName = set.scheme == "part" ? set.base + ".part" + std::to_string(expected + 1) + own.ext
				: set.scheme == "old" ? set.base + (expected == 0 ? std::string(".rar") : ".r" + std::string(expected - 1 < 10 ? "0" : "") + std::to_string(expected - 1))
				: set.base + own.ext + "." + std::string(expected + 1 < 10 ? "00" : expected + 1 < 100 ? "0" : "") + std::to_string(expected + 1);
		}
		std::error_code sizeError;
		const auto size = fs::file_size(path, sizeError);
		set.paths.push_back(path.u8string());
		set.totalSize += sizeError ? 0 : static_cast<int64_t>(size);
		expected = index + 1;
	}
	return set;
}

namespace {

bool has(const std::string &text, const char *what)
{
	return text.find(what) != std::string::npos;
}

} // namespace

ArchiveListing parseSevenZipListing(const std::string &output, int exitCode)
{
	ArchiveListing listing;
	std::istringstream lines(output);
	std::string line;
	ArchiveEntry entry;
	bool inEntry = false;
	auto flush = [&]() {
		if(inEntry && !entry.path.empty())
			listing.entries.push_back(entry);
		entry = ArchiveEntry();
		inEntry = false;
	};
	while(std::getline(lines, line))
	{
		if(!line.empty() && line.back() == '\r')
			line.pop_back();
		if(line.empty())
		{
			flush();
			continue;
		}
		if(startsWith(line, "Path = "))
		{
			flush();
			entry.path = line.substr(7);
			inEntry = true;
		}
		else if(inEntry && startsWith(line, "Size = "))
			entry.size = std::atoll(line.c_str() + 7);
		else if(inEntry && startsWith(line, "Folder = "))
			entry.folder = line.substr(9) == "+";
		else if(inEntry && startsWith(line, "Encrypted = "))
			entry.encrypted = line.substr(12) == "+";
	}
	flush();
	listing.error = sevenZipProblem(output, exitCode);
	listing.needsPassword = has(output, "Cannot open encrypted archive") && !has(output, "Wrong password");
	// 7-Zip says "Wrong password?" both when none was given and when it was
	// wrong; the caller knows which.
	listing.wrongPassword = has(output, "Wrong password");
	if(listing.wrongPassword && has(output, "Cannot open encrypted archive"))
		listing.needsPassword = true;
	const size_t missing = output.find("Missing volume : ");
	if(missing != std::string::npos)
	{
		const size_t end = output.find_first_of("\r\n", missing);
		listing.missingVolume = output.substr(missing + 17, end == std::string::npos ? std::string::npos : end - missing - 17);
	}
	listing.unsupported = has(output, "Unsupported Method") || has(output, "Cannot open the file as archive");
	listing.ok = exitCode == 0 && !listing.entries.empty();
	return listing;
}

std::string sevenZipProblem(const std::string &output, int exitCode)
{
	if(has(output, "Missing volume : "))
	{
		const size_t at = output.find("Missing volume : ");
		const size_t end = output.find_first_of("\r\n", at);
		return std::string(QT_TRANSLATE_NOOP("Messages", "A volume of the archive is missing")) + ": "
			+ output.substr(at + 17, end == std::string::npos ? std::string::npos : end - at - 17);
	}
	if(has(output, "Cannot open encrypted archive") || has(output, "Wrong password"))
		return QT_TRANSLATE_NOOP("Messages", "The archive's password is wrong or missing.");
	if(has(output, "Unsupported Method"))
		return QT_TRANSLATE_NOOP("Messages", "This 7-Zip cannot extract that archive (a build without RAR): install "
											 "7-Zip from 7-zip.org.");
	if(has(output, "Cannot open the file as archive"))
		return QT_TRANSLATE_NOOP("Messages", "The file is not an archive 7-Zip can open (damaged, or still "
											 "downloading).");
	if(has(output, "There is not enough space on the disk") || has(output, "No space left on device"))
		return QT_TRANSLATE_NOOP("Messages", "Not enough free space for the whole package");
	if(has(output, "CRC Failed") || has(output, "Data Error") || has(output, "Unexpected end of archive"))
		return QT_TRANSLATE_NOOP("Messages", "The archive is damaged or incomplete (a volume may still be "
											 "downloading).");
	if(exitCode != 0)
		return std::string(QT_TRANSLATE_NOOP("Messages", "7-Zip could not extract it")) + ": exit code "
			+ std::to_string(exitCode);
	return std::string();
}

bool contentsEncrypted(const ArchiveListing &listing)
{
	for(const ArchiveEntry &entry : listing.entries)
		if(entry.encrypted)
			return true;
	return false;
}

} // namespace orbislink::library
