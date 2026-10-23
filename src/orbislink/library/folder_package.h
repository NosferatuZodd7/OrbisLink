// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// A PS4 app or game folder — what a converter leaves before packing: sce_sys,
// eboot.bin and the rest, often with a .gp4 beside it — made into a package.
// This works out what goes in and under which IDs; the package itself is
// built by the fake PKG builder (fpkg/pkg_builder.h).
//
// The .gp4 (the publishing tool's project, the converter's "recipe") is
// followed when there is one: it says which files go in, where each one goes
// in the package, and the content ID. Its paths may be from the PC the folder
// was made on; a file not found where it says is looked for in the folder by
// its place in the package. Without a .gp4, the whole folder goes in, but
// for the leftovers of the tools (the .gp4 itself, ".complete" markers,
// Windows' and macOS' own files).

#include <cstdint>
#include <string>
#include <vector>

namespace orbislink {
namespace library {

struct PackFile
{
	std::string target; // "eboot.bin", "sce_sys/param.sfo"…
	std::string source; // the file on this PC
	uint64_t size = 0;
};

struct PackPlan
{
	bool ok = false;
	std::string error;
	std::string contentId; // 36 characters
	std::string passcode;  // 32 characters, or empty for the usual zeros
	std::string titleId;
	std::string title;
	std::string version;
	std::string gp4;       // the recipe followed, when there was one
	std::vector<PackFile> files;
	uint64_t bytes = 0;
};

// The .gp4 for `folder`: one inside it (the only one, or the one named after
// the folder), or one beside it named after it. Empty when there is none.
std::string findGp4(const std::string &folder);

// What goes into the package made from `folder` (a folder with
// sce_sys/param.sfo).
PackPlan planFolderPackage(const std::string &folder);

} // namespace library
} // namespace orbislink
