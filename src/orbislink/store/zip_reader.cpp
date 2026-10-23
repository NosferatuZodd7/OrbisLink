// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/store/zip_reader.h"

#include "orbislink/fpkg/targz.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace orbislink::store {

namespace {

uint16_t u16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t *p)
{
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16)
		| (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t u64(const uint8_t *p) { return static_cast<uint64_t>(u32(p)) | (static_cast<uint64_t>(u32(p + 4)) << 32); }

bool readAt(std::ifstream &in, uint64_t offset, uint8_t *out, size_t size)
{
	in.clear();
	in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
	in.read(reinterpret_cast<char *>(out), static_cast<std::streamsize>(size));
	return static_cast<size_t>(in.gcount()) == size;
}

bool fail(std::string *error, const std::string &why)
{
	if(error)
		*error = why;
	return false;
}

// A name that stays inside the folder it is unpacked into.
bool safeName(std::string *name)
{
	std::replace(name->begin(), name->end(), '\\', '/');
	if(name->empty() || name->front() == '/' || (name->size() > 1 && (*name)[1] == ':'))
		return false;
	size_t start = 0;
	while(start < name->size())
	{
		size_t end = name->find('/', start);
		if(end == std::string::npos)
			end = name->size();
		const std::string part = name->substr(start, end - start);
		if(part == ".." || part == "." || (part.empty() && end != name->size()))
			return false;
		start = end + 1;
	}
	return true;
}

} // namespace

bool readZipDirectory(const std::string &archivePath, std::vector<ZipEntry> *entries, std::string *error)
{
	entries->clear();
	std::ifstream in(fs::u8path(archivePath), std::ios::binary);
	std::error_code ec;
	const uint64_t fileSize = fs::file_size(fs::u8path(archivePath), ec);
	if(!in || ec || fileSize < 22)
		return fail(error, "not a ZIP archive");

	// The end of the central directory: the last record, before an optional
	// comment of up to 64 KiB.
	const uint64_t tailSize = std::min<uint64_t>(fileSize, 22 + 65535);
	std::vector<uint8_t> tail(static_cast<size_t>(tailSize));
	if(!readAt(in, fileSize - tailSize, tail.data(), tail.size()))
		return fail(error, "could not read the archive");
	int64_t found = -1;
	for(int64_t i = static_cast<int64_t>(tail.size()) - 22; i >= 0; --i)
		if(u32(&tail[static_cast<size_t>(i)]) == 0x06054b50)
		{
			found = i;
			break;
		}
	if(found < 0)
		return fail(error, "not a ZIP archive (no central directory)");
	const uint8_t *eocd = &tail[static_cast<size_t>(found)];
	const uint64_t eocdOffset = fileSize - tailSize + static_cast<uint64_t>(found);
	if(u16(eocd + 4) != 0 || u16(eocd + 6) != 0)
		return fail(error, "split ZIP archives are not supported");
	uint64_t count = u16(eocd + 10);
	uint64_t directorySize = u32(eocd + 12);
	uint64_t directoryOffset = u32(eocd + 16);

	if(count == 0xFFFF || directorySize == 0xFFFFFFFF || directoryOffset == 0xFFFFFFFF)
	{
		// ZIP64: a locator just before points at the 64-bit record.
		uint8_t locator[20];
		if(eocdOffset < 20 || !readAt(in, eocdOffset - 20, locator, sizeof(locator)) || u32(locator) != 0x07064b50)
			return fail(error, "the archive is damaged (ZIP64 locator)");
		uint8_t record[56];
		if(!readAt(in, u64(locator + 8), record, sizeof(record)) || u32(record) != 0x06064b50)
			return fail(error, "the archive is damaged (ZIP64 record)");
		count = u64(record + 32);
		directorySize = u64(record + 40);
		directoryOffset = u64(record + 48);
	}
	if(directoryOffset + directorySize > fileSize || directorySize > (256u << 20))
		return fail(error, "the archive is damaged (central directory)");

	std::vector<uint8_t> directory(static_cast<size_t>(directorySize));
	if(!readAt(in, directoryOffset, directory.data(), directory.size()))
		return fail(error, "could not read the archive");
	size_t pos = 0;
	for(uint64_t i = 0; i < count; ++i)
	{
		if(pos + 46 > directory.size() || u32(&directory[pos]) != 0x02014b50)
			return fail(error, "the archive is damaged (entry " + std::to_string(i) + ")");
		const uint8_t *h = &directory[pos];
		const uint16_t flags = u16(h + 8);
		ZipEntry entry;
		entry.method = u16(h + 10);
		entry.crc = u32(h + 16);
		entry.compressedSize = u32(h + 20);
		entry.size = u32(h + 24);
		const uint16_t nameLength = u16(h + 28);
		const uint16_t extraLength = u16(h + 30);
		const uint16_t commentLength = u16(h + 32);
		entry.localHeaderOffset = u32(h + 42);
		if(pos + 46 + nameLength + extraLength + commentLength > directory.size())
			return fail(error, "the archive is damaged (entry " + std::to_string(i) + ")");
		entry.name.assign(reinterpret_cast<const char *>(h + 46), nameLength);

		// 64-bit values, for the fields that do not fit in 32.
		const uint8_t *extra = h + 46 + nameLength;
		for(size_t e = 0; e + 4 <= extraLength;)
		{
			const uint16_t id = u16(extra + e);
			const uint16_t length = u16(extra + e + 2);
			if(e + 4 + length > extraLength)
				break;
			if(id == 0x0001)
			{
				size_t f = e + 4;
				auto take = [&](uint64_t *value) {
					if(*value == 0xFFFFFFFF && f + 8 <= e + 4 + length)
					{
						*value = u64(extra + f);
						f += 8;
					}
				};
				take(&entry.size);
				take(&entry.compressedSize);
				take(&entry.localHeaderOffset);
			}
			e += 4 + length;
		}

		if(flags & 1)
			return fail(error, "encrypted archives are not supported");
		if(!safeName(&entry.name))
			return fail(error, "the archive holds a path that leads elsewhere: " + entry.name);
		entry.directory = entry.name.back() == '/';
		if(!entry.directory && entry.method != 0 && entry.method != 8)
			return fail(error, "unsupported compression in " + entry.name + " (method "
				+ std::to_string(entry.method) + ")");
		if(entry.localHeaderOffset + 30 > fileSize)
			return fail(error, "the archive is damaged (" + entry.name + ")");
		entries->push_back(entry);
		pos += 46 + nameLength + extraLength + commentLength;
	}
	return true;
}

bool extractZipEntry(const std::string &archivePath, const ZipEntry &entry, const std::string &destinationPath,
	const std::function<bool(uint64_t written)> &progress, std::string *error)
{
	std::ifstream in(fs::u8path(archivePath), std::ios::binary);
	uint8_t local[30];
	if(!in || !readAt(in, entry.localHeaderOffset, local, sizeof(local)) || u32(local) != 0x04034b50)
		return fail(error, "the archive is damaged (" + entry.name + ")");
	const uint64_t dataOffset = entry.localHeaderOffset + 30 + u16(local + 26) + u16(local + 28);
	in.clear();
	in.seekg(static_cast<std::streamoff>(dataOffset), std::ios::beg);

	std::ofstream out(fs::u8path(destinationPath), std::ios::binary | std::ios::trunc);
	if(!out)
		return fail(error, "could not write " + destinationPath);
	uint64_t written = 0;
	uint32_t crc = 0;
	bool cancelled = false;
	const std::function<bool(const uint8_t *, size_t)> sink = [&](const uint8_t *data, size_t size) {
		out.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
		written += size;
		if(progress && !progress(written))
		{
			cancelled = true;
			return false;
		}
		return static_cast<bool>(out);
	};

	if(entry.method == 0)
	{
		std::vector<uint8_t> buffer(1 << 20);
		uint64_t left = entry.compressedSize;
		while(left > 0)
		{
			const size_t chunk = static_cast<size_t>(std::min<uint64_t>(left, buffer.size()));
			in.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(chunk));
			if(static_cast<size_t>(in.gcount()) != chunk)
				return fail(error, "the archive ends too soon (" + entry.name + ")");
			crc = fpkg::crc32(crc, buffer.data(), chunk);
			if(!sink(buffer.data(), chunk))
				return fail(error, cancelled ? "cancelled" : "could not write " + destinationPath);
			left -= chunk;
		}
	}
	else
	{
		uint64_t size = 0;
		std::string why;
		if(!fpkg::inflateRaw(in, sink, &crc, &size, &why))
			return fail(error, cancelled ? std::string("cancelled") : "the archive is damaged (" + entry.name + ": " + why + ")");
	}
	out.close();
	if(!out)
		return fail(error, "could not write " + destinationPath);
	if(written != entry.size || crc != entry.crc)
		return fail(error, "the archive is damaged (" + entry.name + " does not match its checksum)");
	return true;
}

} // namespace orbislink::store
