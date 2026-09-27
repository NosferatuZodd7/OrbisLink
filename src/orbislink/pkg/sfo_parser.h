// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace orbislink {

// PARAM.SFO (little-endian). Layout confirmed in
// flatz/ps4_remote_pkg_installer/sfo.c:
//   header 0x14: magic "\0PSF"(0x00), version(0x04), key_table_offset(0x08),
//                value_table_offset(0x0C), entry_count(0x10)
//   index 0x10 per entry: key_offset u16(0x00), format u16(0x02),
//                         size u32(0x04), max_size u32(0x08), value_offset u32(0x0C)
enum class SfoFormat : uint16_t {
	StringSpecial = 0x0004, // UTF-8 without terminator
	String = 0x0204,        // NUL-terminated UTF-8
	Uint32 = 0x0404,
};

struct SfoEntry
{
	std::string key;
	SfoFormat format = SfoFormat::String;
	std::vector<uint8_t> value;

	std::string asString() const;
	uint32_t asUint32(uint32_t def = 0) const;
};

class Sfo
{
public:
	// Returns false and fills in `error` if the data is not a valid PARAM.SFO.
	bool parse(const uint8_t *data, size_t size, std::string *error = nullptr);
	bool parse(const std::vector<uint8_t> &data, std::string *error = nullptr)
	{
		return parse(data.data(), data.size(), error);
	}

	const SfoEntry *find(const std::string &key) const;
	std::string stringValue(const std::string &key, const std::string &def = std::string()) const;
	uint32_t uintValue(const std::string &key, uint32_t def = 0) const;
	const std::map<std::string, SfoEntry> &entries() const { return entries_; }
	bool empty() const { return entries_.empty(); }

private:
	std::map<std::string, SfoEntry> entries_;
};

} // namespace orbislink
