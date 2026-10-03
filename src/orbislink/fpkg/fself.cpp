// SPDX-License-Identifier: GPL-3.0-only
#include "orbislink/fpkg/fself.h"

#include <cstring>

namespace orbislink::fpkg {

namespace {

constexpr uint32_t kPtLoad = 1;
constexpr uint32_t kPtSceDynlibData = 0x61000000;
constexpr uint32_t kPtSceRelro = 0x61000010;
constexpr uint32_t kPtSceVersion = 0x6FFFFF01;

constexpr uint64_t kBlockSize = 0x4000;
constexpr size_t kHeaderSize = 0x20;
constexpr size_t kEntrySize = 0x20;
constexpr size_t kElfHeaderSize = 0x40;
constexpr size_t kPhdrSize = 0x38;
constexpr size_t kExInfoSize = 0x40;
constexpr size_t kNpdrmBlockSize = 0x30;
constexpr size_t kMetaBlockSize = 0x50;
constexpr size_t kMetaFooterSize = 0x50;
constexpr size_t kSignatureSize = 0x100;

// make_fself.py's defaults.
constexpr uint64_t kPaid = 0x3100000000000002ull;
constexpr uint64_t kPtypeFake = 1;

uint16_t rd16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
uint64_t rd64(const uint8_t *p) { return uint64_t(rd32(p)) | (uint64_t(rd32(p + 4)) << 32); }

void wr16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void wr32(uint8_t *p, uint32_t v) { for(int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }
void wr64(uint8_t *p, uint64_t v) { for(int i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8 * i)); }

uint64_t alignUp(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

struct Entry
{
	uint64_t props = 0;
	uint64_t offset = 0;
	uint64_t size = 0;
	uint64_t sourceOffset = 0; // in the ELF; meta entries are zeros
	bool meta = false;
};

} // namespace

bool isPlainElf(const uint8_t *data, size_t size)
{
	// ELFCLASS64, little-endian.
	return size >= kElfHeaderSize && std::memcmp(data, "\x7F" "ELF", 4) == 0 && data[4] == 2 && data[5] == 1;
}

Bytes makeFself(const Bytes &elf, std::string *error)
{
	auto fail = [&](const char *message) {
		if(error)
			*error = message;
		return Bytes();
	};
	if(!isPlainElf(elf.data(), elf.size()))
		return fail("not a 64-bit ELF");
	const uint64_t phoff = rd64(elf.data() + 0x20);
	const uint16_t phentsize = rd16(elf.data() + 0x36);
	const uint16_t phnum = rd16(elf.data() + 0x38);
	if(phentsize != kPhdrSize || phoff + uint64_t(phnum) * kPhdrSize > elf.size())
		return fail("the ELF's program headers are out of the file");

	std::vector<Entry> entries;
	Bytes version;
	for(uint16_t i = 0; i < phnum; ++i)
	{
		const uint8_t *ph = elf.data() + phoff + uint64_t(i) * kPhdrSize;
		const uint32_t type = rd32(ph);
		const uint64_t off = rd64(ph + 0x08);
		const uint64_t filesz = rd64(ph + 0x20);
		if(off + filesz > elf.size())
			return fail("an ELF segment is out of the file");
		if(type == kPtSceVersion)
		{
			version.assign(elf.begin() + static_cast<std::ptrdiff_t>(off),
				elf.begin() + static_cast<std::ptrdiff_t>(off + filesz));
			continue;
		}
		if(type != kPtLoad && type != kPtSceRelro && type != kPtSceDynlibData)
			continue;
		Entry meta;
		meta.meta = true;
		// Signed, with digests, describing the entry that follows.
		meta.props = (1ull << 2) | (1ull << 16) | (uint64_t(entries.size() + 1) << 20);
		meta.size = alignUp(filesz, kBlockSize) / kBlockSize * 0x20;
		Entry data;
		// Signed, in blocks of 0x4000 (2^(12+2)), for program header i.
		data.props = (1ull << 2) | (1ull << 11) | (2ull << 12) | (uint64_t(i) << 20);
		data.size = filesz;
		data.sourceOffset = off;
		entries.push_back(meta);
		entries.push_back(data);
	}
	if(entries.empty())
		return fail("the ELF has nothing to load");

	uint64_t headerSize = kHeaderSize + entries.size() * kEntrySize + kElfHeaderSize + uint64_t(phnum) * kPhdrSize;
	headerSize = alignUp(headerSize, 0x10);
	headerSize += kExInfoSize + kNpdrmBlockSize;
	const uint64_t metaSize = entries.size() * kMetaBlockSize + kMetaFooterSize + kSignatureSize;
	if(headerSize > 0xFFFF || metaSize > 0xFFFF)
		return fail("the ELF has too many segments");

	uint64_t offset = headerSize + metaSize;
	for(Entry &e : entries)
	{
		e.offset = offset;
		offset = alignUp(offset + e.size, 0x10);
	}
	const uint64_t fileSize = offset;

	Bytes out(static_cast<size_t>(fileSize), 0);
	uint8_t *p = out.data();
	wr32(p, 0x1D3D154F);    // magic
	p[4] = 0;               // version
	p[5] = 1;               // mode: specific user
	p[6] = 1;               // little-endian
	p[7] = 0x12;            // attributes
	wr32(p + 8, 0x101);     // key type
	wr16(p + 12, static_cast<uint16_t>(headerSize));
	wr16(p + 14, static_cast<uint16_t>(metaSize));
	wr64(p + 16, fileSize);
	wr16(p + 24, static_cast<uint16_t>(entries.size()));
	wr16(p + 26, 0x2 | (2 << 4)); // two signed blocks

	size_t at = kHeaderSize;
	for(const Entry &e : entries)
	{
		wr64(p + at, e.props);
		wr64(p + at + 8, e.offset);
		wr64(p + at + 16, e.size);
		wr64(p + at + 24, e.size);
		at += kEntrySize;
	}
	std::memcpy(p + at, elf.data(), kElfHeaderSize);
	at += kElfHeaderSize;
	std::memcpy(p + at, elf.data() + phoff, size_t(phnum) * kPhdrSize);
	at += size_t(phnum) * kPhdrSize;
	at = static_cast<size_t>(alignUp(at, 0x10));

	// Extended info: paid, program type, app and firmware versions, digest.
	wr64(p + at, kPaid);
	wr64(p + at + 8, kPtypeFake);
	const Digest digest = sha256(elf);
	std::memcpy(p + at + 32, digest.data(), digest.size());
	at += kExInfoSize;
	wr16(p + at, 3); // NPDRM control block; the rest stays zero
	at += kNpdrmBlockSize;
	at += entries.size() * kMetaBlockSize; // meta blocks: zeros
	wr32(p + at + 0x30, 0x10000);          // meta footer
	at += kMetaFooterSize + kSignatureSize; // signature: zeros

	for(const Entry &e : entries)
		if(!e.meta && e.size)
			std::memcpy(p + e.offset, elf.data() + e.sourceOffset, static_cast<size_t>(e.size));
	// The file ends with the last segment, without its padding.
	out.resize(static_cast<size_t>(entries.back().offset + entries.back().size));
	out.insert(out.end(), version.begin(), version.end());
	return out;
}

} // namespace orbislink::fpkg
