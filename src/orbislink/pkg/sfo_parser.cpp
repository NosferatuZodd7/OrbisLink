// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/pkg/sfo_parser.h"

#include "orbislink/common/bytes.h"

namespace orbislink {

namespace {
constexpr size_t kSfoHeaderSize = 0x14;
constexpr size_t kSfoIndexEntrySize = 0x10;
constexpr uint32_t kMaxEntries = 4096; // salvaguarda contra ficheiros corrompidos
} // namespace

std::string SfoEntry::asString() const
{
	if(format != SfoFormat::String && format != SfoFormat::StringSpecial)
		return std::string();
	size_t len = value.size();
	while(len > 0 && value[len - 1] == '\0')
		--len;
	return std::string(reinterpret_cast<const char *>(value.data()), len);
}

uint32_t SfoEntry::asUint32(uint32_t def) const
{
	if(format != SfoFormat::Uint32 || value.size() < 4)
		return def;
	return readLE32(value.data());
}

bool Sfo::parse(const uint8_t *data, size_t size, std::string *error)
{
	entries_.clear();
	auto fail = [&](const char *msg) {
		if(error)
			*error = msg;
		return false;
	};

	if(!data || size < kSfoHeaderSize)
		return fail("PARAM.SFO demasiado pequeno");
	if(!(data[0] == 0x00 && data[1] == 'P' && data[2] == 'S' && data[3] == 'F'))
		return fail("assinatura PARAM.SFO inválida");

	const uint32_t keyTableOffset = readLE32(data + 0x08);
	const uint32_t valueTableOffset = readLE32(data + 0x0C);
	const uint32_t entryCount = readLE32(data + 0x10);

	if(entryCount > kMaxEntries)
		return fail("número de entradas do PARAM.SFO implausível");
	if(keyTableOffset > size || valueTableOffset > size)
		return fail("tabelas do PARAM.SFO fora dos limites");

	const size_t indexSize = static_cast<size_t>(entryCount) * kSfoIndexEntrySize;
	if(kSfoHeaderSize + indexSize > size)
		return fail("índice do PARAM.SFO fora dos limites");

	for(uint32_t i = 0; i < entryCount; ++i)
	{
		const uint8_t *index = data + kSfoHeaderSize + static_cast<size_t>(i) * kSfoIndexEntrySize;
		const uint16_t keyOffset = readLE16(index + 0x00);
		const uint16_t format = readLE16(index + 0x02);
		const uint32_t valueSize = readLE32(index + 0x04);
		const uint32_t valueOffset = readLE32(index + 0x0C);

		const size_t keyStart = static_cast<size_t>(keyTableOffset) + keyOffset;
		if(keyStart >= size)
			continue; // entrada corrompida: ignora sem rejeitar o ficheiro todo
		size_t keyLen = 0;
		while(keyStart + keyLen < size && data[keyStart + keyLen] != '\0')
			++keyLen;

		const size_t valueStart = static_cast<size_t>(valueTableOffset) + valueOffset;
		if(valueStart > size || valueSize > size || valueStart + valueSize > size)
			continue;

		SfoEntry entry;
		entry.key.assign(reinterpret_cast<const char *>(data + keyStart), keyLen);
		entry.format = static_cast<SfoFormat>(format);
		entry.value.assign(data + valueStart, data + valueStart + valueSize);
		if(!entry.key.empty())
			entries_[entry.key] = std::move(entry);
	}

	if(entries_.empty())
		return fail("PARAM.SFO sem entradas legíveis");
	return true;
}

const SfoEntry *Sfo::find(const std::string &key) const
{
	auto it = entries_.find(key);
	return it == entries_.end() ? nullptr : &it->second;
}

std::string Sfo::stringValue(const std::string &key, const std::string &def) const
{
	const SfoEntry *entry = find(key);
	if(!entry)
		return def;
	const std::string value = entry->asString();
	return value.empty() ? def : value;
}

uint32_t Sfo::uintValue(const std::string &key, uint32_t def) const
{
	const SfoEntry *entry = find(key);
	return entry ? entry->asUint32(def) : def;
}

} // namespace orbislink
