// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Reading a ZIP archive (the format homebrew apps are published in): the
// central directory, then each file inflated straight to disk. Stored and
// deflated entries, ZIP64 included; encrypted ones are refused.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbislink::store {

struct ZipEntry
{
	std::string name;       // '/'-separated, as in the archive
	bool directory = false;
	uint16_t method = 0;    // 0 stored, 8 deflated
	uint32_t crc = 0;
	uint64_t compressedSize = 0;
	uint64_t size = 0;
	uint64_t localHeaderOffset = 0;
};

// Every entry of the archive. False, with the reason, for an archive that
// is not a ZIP, is damaged, or holds a name that leads outside the folder
// it is unpacked into ("..", an absolute path, a drive letter).
bool readZipDirectory(const std::string &archivePath, std::vector<ZipEntry> *entries, std::string *error);

// One file of the archive to `destinationPath`, checked against its CRC-32
// and size. Progress in bytes written; returning false cancels.
bool extractZipEntry(const std::string &archivePath, const ZipEntry &entry, const std::string &destinationPath,
	const std::function<bool(uint64_t written)> &progress, std::string *error);

} // namespace orbislink::store
