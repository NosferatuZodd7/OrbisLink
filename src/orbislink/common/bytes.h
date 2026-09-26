// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace orbislink {

// Leitores de inteiros com endianness explícita.
// Os cabeçalhos PKG da PS4 são big-endian; o PARAM.SFO é little-endian.

inline uint16_t readBE16(const uint8_t *p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

inline uint32_t readBE32(const uint8_t *p)
{
	return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
		| (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline uint64_t readBE64(const uint8_t *p)
{
	return (static_cast<uint64_t>(readBE32(p)) << 32) | static_cast<uint64_t>(readBE32(p + 4));
}

inline uint16_t readLE16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

inline uint32_t readLE32(const uint8_t *p)
{
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
		| (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Copia no máximo `max` bytes e corta no primeiro NUL, como os campos ASCII do PKG.
inline std::string readFixedString(const uint8_t *p, size_t max)
{
	size_t len = 0;
	while(len < max && p[len] != '\0')
		++len;
	return std::string(reinterpret_cast<const char *>(p), len);
}

} // namespace orbislink
