// SPDX-License-Identifier: LGPL-3.0-only
//
// Builds a fake-signed PS4 application PKG (CNT, content type GD) from a set
// of files, the way LibOrbisPkg's PkgBuilder does (PKG/PkgBuilder.cs, Pkg.cs,
// Entry.cs, PkgWriter.cs, PlayGo/ChunkDat.cs, Rif/LicenseDat.cs, maxton,
// LGPL-3.0). Consoles with a homebrew enabler that accepts fake packages
// install it like any other.
#pragma once

#include "orbislink/fpkg/fpkg_crypto.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbislink::fpkg {

struct PkgSource
{
	// Path inside the package, "eboot.bin", "sce_sys/param.sfo"...
	std::string targetPath;
	// Either a file on disk...
	std::string sourcePath;
	// ...or bytes already in memory (used when sourcePath is empty).
	Bytes data;
};

struct PkgRequest
{
	std::string contentId;                          // 36 characters
	std::string passcode = std::string(32, '0');    // 32 characters
	// Seconds since 1970 written on every file of the image.
	int64_t volumeTime = 0;
	// "c_date=" of PUBTOOLINFO, as yyyyMMdd; an optional HHmmss adds c_time.
	std::string creationDate;
	std::string creationTime;
	// Must include sce_sys/param.sfo.
	std::vector<PkgSource> files;
};

// stage: "prepare", "image" (the bulk, bytes of the image), "digest",
// "finish". Return false to cancel.
using PkgProgress = std::function<bool(const std::string &stage, uint64_t done, uint64_t total)>;

bool buildFakePkg(const PkgRequest &request, const std::string &outputPath,
	const PkgProgress &progress, std::string *error);

} // namespace orbislink::fpkg
