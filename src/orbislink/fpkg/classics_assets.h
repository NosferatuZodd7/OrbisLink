// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The PS1/PS2 emulator files, fetched once the way easy-ps2-fpkg does: from
// the release of SvenGDK's PS-Classics-fPKG-Builder, keeping only the
// emulators ("emus/Jak v2", "emus/Rogue v1", "emus/ps1hd"...), their Lua
// includes and configs, and the title lists. OrbisLink ships none of them.
#pragma once

#include "orbislink/fpkg/targz.h"

#include <string>

namespace orbislink::fpkg {

// The archive easy-ps2-fpkg downloads (about 109 MB).
extern const char *const kClassicsAssetsUrl;

// Where a file of that archive goes inside the assets folder, or empty when
// it is not needed (the builder's own program, its Windows tools...).
std::string classicsAssetPath(const std::string &pathInArchive);

// Whether `dir` already holds the emulators.
bool hasClassicsAssets(const std::string &dir);

// Unpacks the downloaded archive into `dir`. It goes to a folder beside it
// first and takes its place only when complete, so a cancelled or failed
// unpacking never leaves half the files behind.
bool installClassicsAssets(const std::string &archive, const std::string &dir,
	const TarProgress &progress, std::string *error);

} // namespace orbislink::fpkg
