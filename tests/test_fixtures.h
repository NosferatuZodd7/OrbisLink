// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Construtores de PARAM.SFO e de .pkg sintéticos, para testar o PkgInspector
// sem precisar de ficheiros reais (que não podem ser distribuídos).

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace orbislink_test {

inline void putLE16(std::vector<uint8_t> &buffer, size_t offset, uint16_t value)
{
	buffer[offset] = static_cast<uint8_t>(value & 0xFF);
	buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

inline void putLE32(std::vector<uint8_t> &buffer, size_t offset, uint32_t value)
{
	for(int i = 0; i < 4; ++i)
		buffer[offset + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
}

inline void putBE32(std::vector<uint8_t> &buffer, size_t offset, uint32_t value)
{
	for(int i = 0; i < 4; ++i)
		buffer[offset + i] = static_cast<uint8_t>((value >> (8 * (3 - i))) & 0xFF);
}

inline void putBE64(std::vector<uint8_t> &buffer, size_t offset, uint64_t value)
{
	for(int i = 0; i < 8; ++i)
		buffer[offset + i] = static_cast<uint8_t>((value >> (8 * (7 - i))) & 0xFF);
}

// PARAM.SFO com entradas do tipo string (formato 0x0204).
inline std::vector<uint8_t> buildSfo(const std::vector<std::pair<std::string, std::string>> &entries)
{
	const size_t headerSize = 0x14;
	const size_t indexSize = entries.size() * 0x10;

	std::vector<uint8_t> keyTable;
	std::vector<uint8_t> valueTable;
	std::vector<uint32_t> keyOffsets, valueOffsets, valueSizes;

	for(const auto &entry : entries)
	{
		keyOffsets.push_back(static_cast<uint32_t>(keyTable.size()));
		keyTable.insert(keyTable.end(), entry.first.begin(), entry.first.end());
		keyTable.push_back(0);

		valueOffsets.push_back(static_cast<uint32_t>(valueTable.size()));
		valueTable.insert(valueTable.end(), entry.second.begin(), entry.second.end());
		valueTable.push_back(0);
		valueSizes.push_back(static_cast<uint32_t>(entry.second.size() + 1));
	}
	while(keyTable.size() % 4 != 0)
		keyTable.push_back(0);

	const size_t keyTableOffset = headerSize + indexSize;
	const size_t valueTableOffset = keyTableOffset + keyTable.size();

	std::vector<uint8_t> sfo(valueTableOffset + valueTable.size(), 0);
	sfo[0] = 0x00;
	sfo[1] = 'P';
	sfo[2] = 'S';
	sfo[3] = 'F';
	putLE32(sfo, 0x04, 0x00000101);
	putLE32(sfo, 0x08, static_cast<uint32_t>(keyTableOffset));
	putLE32(sfo, 0x0C, static_cast<uint32_t>(valueTableOffset));
	putLE32(sfo, 0x10, static_cast<uint32_t>(entries.size()));

	for(size_t i = 0; i < entries.size(); ++i)
	{
		const size_t base = headerSize + i * 0x10;
		putLE16(sfo, base + 0x00, static_cast<uint16_t>(keyOffsets[i]));
		putLE16(sfo, base + 0x02, 0x0204); // string terminada em NUL
		putLE32(sfo, base + 0x04, valueSizes[i]);
		putLE32(sfo, base + 0x08, valueSizes[i]);
		putLE32(sfo, base + 0x0C, valueOffsets[i]);
	}
	std::copy(keyTable.begin(), keyTable.end(), sfo.begin() + static_cast<long>(keyTableOffset));
	std::copy(valueTable.begin(), valueTable.end(), sfo.begin() + static_cast<long>(valueTableOffset));
	return sfo;
}

struct PkgOptions
{
	std::string contentId = "UP0001-CUSA12345_00-ORBISLINKTEST001";
	uint32_t contentType = 0x1A; // GD
	uint32_t contentFlags = 0;
	bool includeSfo = true;
	bool includeIcon = true;
	bool validMagic = true;
	std::vector<std::pair<std::string, std::string>> sfoEntries = {
		{ "APP_VER", "01.00" },
		{ "CATEGORY", "gd" },
		{ "CONTENT_ID", "UP0001-CUSA12345_00-ORBISLINKTEST001" },
		{ "TITLE", "Jogo de Teste" },
		{ "TITLE_ID", "CUSA12345" },
		{ "VERSION", "01.00" },
	};
};

// Gera um .pkg mínimo mas estruturalmente correto.
inline std::vector<uint8_t> buildPkg(const PkgOptions &options)
{
	const std::vector<uint8_t> sfo = options.includeSfo ? buildSfo(options.sfoEntries)
													   : std::vector<uint8_t>();
	// PNG de 1x1 suficiente para o teste (só se verifica que os bytes batem certo).
	const std::vector<uint8_t> icon = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0x01, 0x02 };

	std::vector<std::pair<uint32_t, std::vector<uint8_t>>> entries;
	if(options.includeSfo)
		entries.push_back({ 0x1000, sfo });
	if(options.includeIcon)
		entries.push_back({ 0x1200, icon });

	const size_t headerSize = 0x2000;
	const size_t tableOffset = 0x1000;
	size_t dataOffset = headerSize;

	std::vector<uint8_t> pkg(headerSize, 0);
	if(options.validMagic)
	{
		pkg[0] = 0x7F;
		pkg[1] = 'C';
		pkg[2] = 'N';
		pkg[3] = 'T';
	}
	else
	{
		pkg[0] = 'P';
		pkg[1] = 'K';
		pkg[2] = '0';
		pkg[3] = '3';
	}

	putBE32(pkg, 0x10, static_cast<uint32_t>(entries.size()));
	putBE32(pkg, 0x18, static_cast<uint32_t>(tableOffset));
	for(size_t i = 0; i < options.contentId.size() && i < 0x24; ++i)
		pkg[0x40 + i] = static_cast<uint8_t>(options.contentId[i]);
	putBE32(pkg, 0x74, options.contentType);
	putBE32(pkg, 0x78, options.contentFlags);

	std::vector<uint8_t> payload;
	for(size_t i = 0; i < entries.size(); ++i)
	{
		const size_t base = tableOffset + i * 0x20;
		putBE32(pkg, base + 0x00, entries[i].first);
		putBE32(pkg, base + 0x10, static_cast<uint32_t>(dataOffset));
		putBE32(pkg, base + 0x14, static_cast<uint32_t>(entries[i].second.size()));
		payload.insert(payload.end(), entries[i].second.begin(), entries[i].second.end());
		dataOffset += entries[i].second.size();
	}

	pkg.insert(pkg.end(), payload.begin(), payload.end());
	// Enchimento para o ficheiro ter um tamanho plausível.
	pkg.resize(pkg.size() + 1024, 0xAB);
	putBE64(pkg, 0x430, static_cast<uint64_t>(pkg.size()));
	return pkg;
}

inline std::string writeTempFile(const std::string &nameHint, const std::vector<uint8_t> &data)
{
	const std::string path = std::string(".orbislink-test-") + nameHint;
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
	file.close();
	return path;
}

inline void removeTempFile(const std::string &path) { std::remove(path.c_str()); }

} // namespace orbislink_test
