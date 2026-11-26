// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/pkg/pkg_parts.h"

#include "orbislink/common/log.h"
#include "orbislink/common/tr.h"
#include "orbislink/common/util.h"
#include "orbislink/pkg/pkg_inspector.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>

namespace orbislink {

namespace fs = std::filesystem;

namespace {

// Paths come in UTF-8 (from Qt); on Windows a narrow path would be read in
// the ANSI code page instead.
fs::path pathOf(const std::string &utf8)
{
	return fs::u8path(utf8);
}

std::string utf8Of(const fs::path &path)
{
	return path.u8string();
}

// Up to `length` bytes of `file` from `offset`.
bool readAt(std::ifstream &file, int64_t offset, void *buffer, size_t length)
{
	file.clear();
	file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
	if(!file)
		return false;
	file.read(static_cast<char *>(buffer), static_cast<std::streamsize>(length));
	return file.gcount() == static_cast<std::streamsize>(length);
}

PkgInfo inspectHead(const std::string &path, int64_t size)
{
	PkgInspector::Options options;
	options.extractIcon = false;
	std::ifstream file(pathOf(path), std::ios::binary);
	if(!file)
	{
		PkgInfo info;
		info.path = path;
		info.error = QT_TRANSLATE_NOOP("Messages", "Could not open the file.");
		return info;
	}
	return PkgInspector(options).inspect(path, size, [&file](int64_t offset, void *buffer, size_t length) {
		return readAt(file, offset, buffer, length);
	});
}

} // namespace

// The parts read as the one file they make.
PkgInfo inspectPkgParts(const PkgPartSet &set)
{
	std::vector<std::ifstream> files(set.parts.size());
	auto read = [&](int64_t offset, void *buffer, size_t length) {
		auto *out = static_cast<char *>(buffer);
		int64_t start = 0;
		for(size_t i = 0; i < set.parts.size() && length > 0; ++i)
		{
			const int64_t end = start + set.sizes[i];
			if(offset < end)
			{
				if(!files[i].is_open())
					files[i].open(pathOf(set.parts[i]), std::ios::binary);
				const size_t here = static_cast<size_t>(std::min<int64_t>(end - offset, static_cast<int64_t>(length)));
				if(!readAt(files[i], offset - start, out, here))
					return false;
				out += here;
				offset += static_cast<int64_t>(here);
				length -= here;
			}
			start = end;
		}
		return length == 0;
	};
	PkgInspector::Options options;
	options.extractIcon = false;
	return PkgInspector(options).inspect(set.parts.empty() ? std::string() : set.parts.front(), set.totalSize, read);
}

bool parsePkgPartName(const std::string &fileName, std::string *base, int *index)
{
	if(fileName.size() < 7 || toLower(fileName.substr(fileName.size() - 4)) != ".pkg")
		return false;
	const std::string stem = fileName.substr(0, fileName.size() - 4);
	const size_t underscore = stem.find_last_of('_');
	if(underscore == std::string::npos || underscore == 0)
		return false;
	const std::string digits = stem.substr(underscore + 1);
	if(toLower(digits) == "sc")
	{
		if(base)
			*base = stem.substr(0, underscore);
		if(index)
			*index = kPkgContainerPart;
		return true;
	}
	if(digits.empty() || digits.size() > 4)
		return false;
	int value = 0;
	for(const char c : digits)
	{
		if(c < '0' || c > '9')
			return false;
		value = value * 10 + (c - '0');
	}
	if(base)
		*base = stem.substr(0, underscore);
	if(index)
		*index = value;
	return true;
}

PkgPartSet planPkgParts(const std::string &firstPart)
{
	PkgPartSet set;
	const fs::path first = pathOf(firstPart);
	set.folder = utf8Of(first.parent_path());
	int firstIndex = -1;
	if(!parsePkgPartName(utf8Of(first.filename()), &set.base, &firstIndex) || firstIndex != 0)
	{
		set.error = QT_TRANSLATE_NOOP("Messages", "Not the first part of a split package (<name>_0.pkg).");
		return set;
	}

	std::error_code ec;

	// Its pieces: the names with the same base (exactly, but the extension
	// in any case), by number. Two names for one number ("_1" and "_01")
	// cannot both be right.
	std::map<int, fs::path> byIndex;
	const fs::path folder = first.has_parent_path() ? first.parent_path() : fs::path(".");
	for(fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
	{
		std::error_code fileError;
		if(!it->is_regular_file(fileError) || fileError)
			continue;
		std::string base;
		int index = -1;
		if(!parsePkgPartName(utf8Of(it->path().filename()), &base, &index) || base != set.base)
			continue;
		const auto known = byIndex.find(index);
		if(known != byIndex.end())
		{
			set.duplicateIndex = index;
			set.found = static_cast<int>(byIndex.size()) + 1;
			set.error = std::string(QT_TRANSLATE_NOOP("Messages", "Two files are the same part of the package"))
				+ ": " + utf8Of(known->second.filename()) + ", " + utf8Of(it->path().filename());
			return set;
		}
		byIndex[index] = it->path();
	}
	if(ec)
	{
		set.error = std::string(QT_TRANSLATE_NOOP("Messages", "The folder could not be read")) + ": " + ec.message();
		return set;
	}

	set.found = static_cast<int>(byIndex.size());

	// A PS5 package's first part starts with its own header, which says
	// where its metadata container starts (0x58, or 0x30 for a patch).
	uint64_t containerOffset = 0;
	{
		unsigned char head[0x60] = {};
		std::ifstream file(first, std::ios::binary);
		file.read(reinterpret_cast<char *>(head), sizeof(head));
		set.ps5 = file.gcount() == static_cast<std::streamsize>(sizeof(head)) && head[0] == 0x7F && head[2] == 'I'
			&& head[3] == 'H' && (head[1] == 'F' || head[1] == 'L');
		if(set.ps5)
		{
			const size_t at = head[1] == 'L' ? 0x30 : 0x58;
			for(size_t i = 0; i < 8; ++i)
				containerOffset |= static_cast<uint64_t>(head[at + i]) << (8 * i);
		}
	}
	if(byIndex.count(kPkgContainerPart) && !set.ps5)
	{
		set.error = QT_TRANSLATE_NOOP("Messages", "A _sc part beside a PS4 package: how it joins is not known.");
		return set;
	}

	// In order from 0, with no number missing (a PS5 package's _sc part
	// last), until the pieces reach the declared size.
	int expected = 0;
	for(const auto &[index, path] : byIndex)
	{
		if(index != expected && index != kPkgContainerPart)
		{
			set.missingIndex = expected;
			set.error = std::string(QT_TRANSLATE_NOOP("Messages", "A part of the package is missing")) + ": "
				+ set.base + "_" + std::to_string(expected) + ".pkg";
			return set;
		}
		std::error_code sizeError;
		const auto size = fs::file_size(path, sizeError);
		if(sizeError)
		{
			set.error = std::string(QT_TRANSLATE_NOOP("Messages", "A part of the package cannot be read")) + ": "
				+ utf8Of(path.filename());
			return set;
		}
		set.parts.push_back(utf8Of(path));
		set.sizes.push_back(static_cast<int64_t>(size));
		set.totalSize += static_cast<int64_t>(size);
		++expected;
	}
	if(set.parts.size() < 2)
	{
		set.error = QT_TRANSLATE_NOOP("Messages", "The package has no other parts in its folder.");
		return set;
	}

	// A PS5 package's numbered parts end where its container starts, and
	// its _sc part is that container.
	if(set.ps5 && byIndex.count(kPkgContainerPart))
	{
		const uint64_t numbered = static_cast<uint64_t>(set.totalSize - set.sizes.back());
		if(numbered < containerOffset)
		{
			set.missingIndex = expected - 1;
			set.error = std::string(QT_TRANSLATE_NOOP("Messages", "The parts are smaller than the package (a part is missing or cut short)"))
				+ ": " + humanBytes(static_cast<int64_t>(numbered)) + " / " + humanBytes(static_cast<int64_t>(containerOffset));
			return set;
		}
		if(numbered > containerOffset)
		{
			set.error = std::string(QT_TRANSLATE_NOOP("Messages", "The parts are bigger than the package (a part does not belong to it)"))
				+ ": " + humanBytes(static_cast<int64_t>(numbered)) + " / " + humanBytes(static_cast<int64_t>(containerOffset));
			return set;
		}
	}

	// The header, read across the parts as the one file they make (its
	// entry table may lie past the end of a small first part): what the
	// package is, and how big the whole of it is.
	const PkgInfo head = inspectPkgParts(set);
	if(!head.valid)
	{
		set.error = head.error.empty() ? QT_TRANSLATE_NOOP("Messages", "Could not read the pkg header.") : head.error;
		return set;
	}
	set.contentId = head.contentId;
	set.title = head.displayTitle();
	// A PS5 package's sizes and digests are Packwright's to check.
	if(set.ps5)
	{
		set.ok = true;
		return set;
	}
	set.declaredSize = head.declaredSize;
	if(set.declaredSize == 0)
	{
		set.error = QT_TRANSLATE_NOOP("Messages", "The pkg header does not say the package's size.");
		return set;
	}
	const uint64_t total = static_cast<uint64_t>(set.totalSize);
	if(total < set.declaredSize)
	{
		set.missingIndex = expected;
		set.error = std::string(QT_TRANSLATE_NOOP("Messages", "The parts are smaller than the package (a part is missing or cut short)"))
			+ ": " + humanBytes(set.totalSize) + " / " + humanBytes(static_cast<int64_t>(set.declaredSize));
		return set;
	}
	if(total > set.declaredSize)
	{
		set.error = std::string(QT_TRANSLATE_NOOP("Messages", "The parts are bigger than the package (a part does not belong to it)"))
			+ ": " + humanBytes(set.totalSize) + " / " + humanBytes(static_cast<int64_t>(set.declaredSize));
		return set;
	}
	set.ok = true;
	return set;
}

std::string joinedPkgName(const PkgPartSet &set)
{
	return set.base + ".pkg";
}

bool joinPkgParts(const PkgPartSet &set, const std::string &output, const PkgJoinProgress &progress,
	std::string *error, bool *cancelled)
{
	if(cancelled)
		*cancelled = false;
	auto fail = [error](const std::string &why) {
		if(error)
			*error = why;
		return false;
	};
	if(!set.ok)
		return fail(set.error.empty() ? QT_TRANSLATE_NOOP("Messages", "The parts of the package are not complete.")
									  : set.error);
	if(set.ps5)
		return fail(QT_TRANSLATE_NOOP("Messages", "A PS5 package in parts is joined by Packwright (packwright merge)."));
	const fs::path target = pathOf(output);
	std::error_code ec;
	if(fs::exists(target, ec))
		return fail(std::string(QT_TRANSLATE_NOOP("Messages", "A file with that name is already there")) + ": "
			+ output);
	const fs::path folder = target.has_parent_path() ? target.parent_path() : fs::current_path(ec);
	// Room for all of it, checked before anything is written.
	const fs::space_info space = fs::space(folder, ec);
	if(!ec && space.available < static_cast<uintmax_t>(set.totalSize) + (16u << 20))
		return fail(std::string(QT_TRANSLATE_NOOP("Messages", "Not enough free space for the whole package")) + ": "
			+ humanBytes(static_cast<int64_t>(space.available)) + " / " + humanBytes(set.totalSize));

	fs::path partial = target;
	partial += ".part";
	// Whatever happens below, a half-written file does not stay.
	struct Cleanup
	{
		fs::path path;
		bool keep = false;
		~Cleanup()
		{
			if(!keep)
			{
				std::error_code ignored;
				fs::remove(path, ignored);
			}
		}
	} cleanup { partial };

	{
		std::ofstream out(partial, std::ios::binary | std::ios::trunc);
		if(!out)
			return fail(std::string(QT_TRANSLATE_NOOP("Messages", "Could not create the file")) + ": "
				+ utf8Of(partial));
		std::vector<char> buffer(4u << 20);
		const uint64_t total = static_cast<uint64_t>(set.totalSize);
		uint64_t done = 0;
		for(size_t i = 0; i < set.parts.size(); ++i)
		{
			// The piece as it was found: one changed since is not trusted.
			std::error_code sizeError;
			const auto now = fs::file_size(pathOf(set.parts[i]), sizeError);
			if(sizeError || static_cast<int64_t>(now) != set.sizes[i])
				return fail(std::string(QT_TRANSLATE_NOOP("Messages", "A part of the package changed while it was being joined"))
					+ ": " + utf8Of(pathOf(set.parts[i]).filename()));
			std::ifstream in(pathOf(set.parts[i]), std::ios::binary);
			if(!in)
				return fail(std::string(QT_TRANSLATE_NOOP("Messages", "A part of the package cannot be read")) + ": "
					+ utf8Of(pathOf(set.parts[i]).filename()));
			int64_t left = set.sizes[i];
			while(left > 0)
			{
				const std::streamsize want =
					static_cast<std::streamsize>(std::min<int64_t>(left, static_cast<int64_t>(buffer.size())));
				in.read(buffer.data(), want);
				if(in.gcount() != want)
					return fail(std::string(QT_TRANSLATE_NOOP("Messages", "A part of the package cannot be read"))
						+ ": " + utf8Of(pathOf(set.parts[i]).filename()));
				out.write(buffer.data(), want);
				if(!out)
					return fail(std::string(QT_TRANSLATE_NOOP("Messages", "Could not write the package (the disk may be full)"))
						+ ": " + utf8Of(partial));
				left -= want;
				done += static_cast<uint64_t>(want);
				if(progress && !progress(done, total))
				{
					if(cancelled)
						*cancelled = true;
					return fail(QT_TRANSLATE_NOOP("Messages", "Cancelled."));
				}
			}
		}
		out.flush();
		out.close();
		if(!out)
			return fail(std::string(QT_TRANSLATE_NOOP("Messages", "Could not write the package (the disk may be full)"))
				+ ": " + utf8Of(partial));
	}

	// What was written: the size the header asks for, and a header that
	// still reads.
	std::error_code sizeError;
	const auto written = fs::file_size(partial, sizeError);
	if(sizeError || written != set.declaredSize)
		return fail(std::string(QT_TRANSLATE_NOOP("Messages", "The joined package does not have the size its header says"))
			+ ": " + std::to_string(sizeError ? 0 : written) + " / " + std::to_string(set.declaredSize));
	const PkgInfo check = inspectHead(utf8Of(partial), static_cast<int64_t>(written));
	if(!check.valid || check.declaredSize != set.declaredSize || check.contentId != set.contentId)
		return fail(std::string(QT_TRANSLATE_NOOP("Messages", "The joined package could not be read back")) + ": "
			+ check.error);

	fs::rename(partial, target, ec);
	if(ec)
		return fail(std::string(QT_TRANSLATE_NOOP("Messages", "Could not name the joined package")) + ": "
			+ ec.message());
	cleanup.keep = true;
	logInfo("Joined " + std::to_string(set.parts.size()) + " parts of " + set.base + " into " + output + " ("
		+ humanBytes(set.totalSize) + ")");
	return true;
}

} // namespace orbislink
