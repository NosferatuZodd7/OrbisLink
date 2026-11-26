// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// A game downloaded as an archive: one file (.zip, .7z, .rar) or several
// volumes of one ("game.part1.rar", "game.part2.rar"…; "game.7z.001",
// "game.7z.002"…; "game.rar", "game.r00", "game.r01"…). This part only
// knows names and what 7-Zip says about an archive: it is opened and
// extracted by 7-Zip (the interface's ArchiveTool), never here.

#include <cstdint>
#include <string>
#include <vector>

namespace orbislink::library {

// What a file name says about the archive it belongs to: its name without
// the volume ending ("game"), which volume it is (0 the first) and how its
// volumes are named.
struct ArchiveVolume
{
	std::string base;
	int index = 0;
	// "single" (game.zip, game.7z, game.rar alone), "part" (game.partN.rar),
	// "old" (game.rar, game.r00, game.r01…), "split" (game.7z.001…).
	std::string scheme;
	std::string ext; // ".7z", ".rar" or ".zip" (lower case)
};

// False when the name is not an archive or one of its volumes. A ".rar"
// alone says "single": it is the first of an "old" set when ".r00" files
// are beside it (findArchiveVolumes tells).
bool parseArchiveVolume(const std::string &fileName, ArchiveVolume *volume);

// The volumes of an archive, found from its first one in its folder.
struct ArchiveVolumes
{
	std::string base;
	std::string scheme;
	std::vector<std::string> paths; // in order (UTF-8)
	int64_t totalSize = 0;
	// The first number missing between the first and the last one found
	// (-1 when none is): a volume still downloading, or lost.
	int missingIndex = -1;
	std::string missingName;
};
// `firstVolume` is the path of the first volume (or the only file).
ArchiveVolumes findArchiveVolumes(const std::string &firstVolume);

// What 7-Zip's `l -slt` listing says.
struct ArchiveEntry
{
	std::string path; // inside the archive ('/' or '\' as 7-Zip gives it)
	int64_t size = 0;
	bool folder = false;
	bool encrypted = false;
};
struct ArchiveListing
{
	std::vector<ArchiveEntry> entries;
	// Its names are encrypted too: nothing is known without the password.
	bool needsPassword = false;
	bool wrongPassword = false;
	// A volume 7-Zip looked for and did not find.
	std::string missingVolume;
	// This 7-Zip cannot open it (a build without RAR, say).
	bool unsupported = false;
	std::string error;
	bool ok = false;
};
// `output` is what 7-Zip printed (stdout and stderr together); `exitCode`
// its exit code.
ArchiveListing parseSevenZipListing(const std::string &output, int exitCode);
// The same messages, from an extraction's output: what went wrong in words
// the interface translates (empty when nothing did).
std::string sevenZipProblem(const std::string &output, int exitCode);

// Any entry encrypted: its contents need the password.
bool contentsEncrypted(const ArchiveListing &listing);

} // namespace orbislink::library
