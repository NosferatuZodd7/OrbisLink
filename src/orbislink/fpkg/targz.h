// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Reading a .tar.gz without zlib: an inflater (RFC 1951) behind the gzip
// wrapper (RFC 1952), feeding a tar reader (ustar, with GNU long names and
// pax paths). It streams: the archive is never held in memory, only a 32 KiB
// window of it, and each file goes straight to its place on disk.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace orbislink::fpkg {

// For each regular file of the archive (its path inside, '/'-separated, and
// its size): where to write it, or an empty string to leave it out.
using TarDestination = std::function<std::string(const std::string &path, uint64_t size)>;

// Progress in bytes of the archive read so far; returning false cancels.
using TarProgress = std::function<bool(uint64_t read, uint64_t total)>;

bool extractTarGz(const std::string &archivePath, const TarDestination &destination,
	const TarProgress &progress, std::string *error);

// The plain inflater, exposed for the tests: decompresses a gzip stream held
// in memory, handing the output to `sink` as it comes.
bool gunzip(const uint8_t *data, size_t size,
	const std::function<bool(const uint8_t *, size_t)> &sink, std::string *error);

} // namespace orbislink::fpkg
