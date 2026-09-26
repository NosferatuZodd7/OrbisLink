// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/pkg/pkg_inspector.h"

#include "orbislink/common/bytes.h"
#include "orbislink/common/log.h"
#include "orbislink/common/util.h"
#include "orbislink/pkg/sfo_parser.h"

#include <cstdio>
#include <fstream>

namespace orbislink {

namespace {

// Offsets do cabeçalho PKG (big-endian), confirmados em
// flatz/ps4_remote_pkg_installer/pkg.h.
constexpr size_t kHeaderReadSize = 0x1000; // chega para todos os campos até 0x430
constexpr size_t kOffMagic = 0x00;
constexpr size_t kOffEntryCount = 0x10;
constexpr size_t kOffEntryTableOffset = 0x18;
constexpr size_t kOffContentId = 0x40;
constexpr size_t kContentIdSize = 0x24; // 36
constexpr size_t kOffContentType = 0x74;
constexpr size_t kOffContentFlags = 0x78;
constexpr size_t kOffPackageSize = 0x430;
constexpr size_t kTableEntrySize = 0x20;
constexpr size_t kTableEntryOffOffset = 0x10;
constexpr size_t kTableEntryOffSize = 0x14;
constexpr uint32_t kMaxEntryCount = 8192;

bool readAt(std::ifstream &file, int64_t offset, void *buffer, size_t size)
{
	file.clear();
	file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
	if(!file)
		return false;
	file.read(reinterpret_cast<char *>(buffer), static_cast<std::streamsize>(size));
	return static_cast<size_t>(file.gcount()) == size;
}

} // namespace

const char *pkgCategoryCode(PkgCategory category)
{
	switch(category)
	{
		case PkgCategory::Game: return "gd";
		case PkgCategory::Patch: return "gp";
		case PkgCategory::Dlc: return "ac";
		case PkgCategory::Theme: return "gdt";
		case PkgCategory::DeltaPatch: return "gpd";
		case PkgCategory::Unknown: break;
	}
	return "";
}

const char *pkgCategoryLabelPt(PkgCategory category)
{
	switch(category)
	{
		case PkgCategory::Game: return "Jogo";
		case PkgCategory::Patch: return "Patch";
		case PkgCategory::Dlc: return "DLC";
		case PkgCategory::Theme: return "Tema";
		case PkgCategory::DeltaPatch: return "Patch delta";
		case PkgCategory::Unknown: break;
	}
	return "Desconhecido";
}

int pkgCategoryInstallOrder(PkgCategory category)
{
	switch(category)
	{
		case PkgCategory::Game: return 0;
		case PkgCategory::Patch: return 1;
		case PkgCategory::DeltaPatch: return 2;
		case PkgCategory::Dlc: return 3;
		case PkgCategory::Theme: return 4;
		case PkgCategory::Unknown: break;
	}
	return 5;
}

PkgInspector::PkgInspector() = default;

PkgInspector::PkgInspector(Options options) : options_(options) {}

std::string PkgInfo::displayTitle() const
{
	if(!title.empty())
		return title;
	if(!titleId.empty())
		return titleId;
	if(!path.empty())
		return baseName(path);
	return "(sem título)";
}

bool PkgInspector::hasPkgMagic(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	if(!file)
		return false;
	uint8_t magic[4] = { 0, 0, 0, 0 };
	file.read(reinterpret_cast<char *>(magic), 4);
	if(file.gcount() != 4)
		return false;
	return magic[0] == 0x7F && magic[1] == 'C' && magic[2] == 'N' && magic[3] == 'T';
}

PkgInfo PkgInspector::inspect(const std::string &path) const
{
	PkgInfo info;
	info.path = path;

	const int64_t size = fileSize(path);
	if(size < 0)
	{
		info.error = "Não foi possível ler o ficheiro.";
		return info;
	}
	info.fileSize = size;
	if(size < kMinPkgSize)
	{
		info.error = "Não é um pkg PS4 válido (ficheiro demasiado pequeno).";
		return info;
	}

	std::ifstream file(path, std::ios::binary);
	if(!file)
	{
		info.error = "Não foi possível abrir o ficheiro.";
		return info;
	}

	std::vector<uint8_t> header(kHeaderReadSize, 0);
	const size_t headerBytes = static_cast<size_t>(
		size < static_cast<int64_t>(kHeaderReadSize) ? size : static_cast<int64_t>(kHeaderReadSize));
	if(!readAt(file, 0, header.data(), headerBytes))
	{
		info.error = "Não foi possível ler o cabeçalho do pkg.";
		return info;
	}

	if(!(header[kOffMagic] == 0x7F && header[kOffMagic + 1] == 'C' && header[kOffMagic + 2] == 'N'
		   && header[kOffMagic + 3] == 'T'))
	{
		info.error = "Não é um pkg PS4 válido.";
		return info;
	}

	const uint32_t entryCount = readBE32(header.data() + kOffEntryCount);
	const uint32_t entryTableOffset = readBE32(header.data() + kOffEntryTableOffset);
	info.contentId = readFixedString(header.data() + kOffContentId, kContentIdSize);
	info.contentType = readBE32(header.data() + kOffContentType);
	info.contentFlags = readBE32(header.data() + kOffContentFlags);
	if(headerBytes >= kOffPackageSize + 8)
		info.declaredSize = readBE64(header.data() + kOffPackageSize);

	info.isPatch = (info.contentFlags & kFlagFirstPatch) != 0
		|| (info.contentFlags & kFlagSubsequentPatch) != 0;

	// TITLE_ID a partir do content id: UP0000-CUSA00000_00-...
	if(info.contentId.size() >= 16)
	{
		const size_t dash = info.contentId.find('-');
		if(dash != std::string::npos && info.contentId.size() >= dash + 10)
			info.titleId = info.contentId.substr(dash + 1, 9);
	}

	if(entryCount == 0 || entryCount > kMaxEntryCount)
	{
		info.error = "Tabela de entradas do pkg inválida.";
		return info;
	}
	const int64_t tableEnd = static_cast<int64_t>(entryTableOffset)
		+ static_cast<int64_t>(entryCount) * static_cast<int64_t>(kTableEntrySize);
	if(tableEnd > size)
	{
		info.error = "Tabela de entradas do pkg fora dos limites do ficheiro.";
		return info;
	}

	std::vector<uint8_t> table(static_cast<size_t>(entryCount) * kTableEntrySize);
	if(!readAt(file, entryTableOffset, table.data(), table.size()))
	{
		info.error = "Não foi possível ler a tabela de entradas do pkg.";
		return info;
	}

	int64_t sfoOffset = -1, sfoSize = 0, iconOffset = -1, iconSize = 0;
	for(uint32_t i = 0; i < entryCount; ++i)
	{
		const uint8_t *entry = table.data() + static_cast<size_t>(i) * kTableEntrySize;
		const uint32_t id = readBE32(entry);
		const uint32_t entryOffset = readBE32(entry + kTableEntryOffOffset);
		const uint32_t entrySize = readBE32(entry + kTableEntryOffSize);
		if(id == kEntryIdParamSfo)
		{
			sfoOffset = entryOffset;
			sfoSize = entrySize;
		}
		else if(id == kEntryIdIcon0Png)
		{
			iconOffset = entryOffset;
			iconSize = entrySize;
		}
	}

	// Metadados em falta não invalidam o pkg (§5.2): fica "(sem título)".
	if(sfoOffset >= 0 && sfoSize > 0 && sfoOffset + sfoSize <= size
		&& static_cast<size_t>(sfoSize) <= options_.maxSfoBytes)
	{
		std::vector<uint8_t> sfoData(static_cast<size_t>(sfoSize));
		if(readAt(file, sfoOffset, sfoData.data(), sfoData.size()))
		{
			Sfo sfo;
			std::string sfoError;
			if(sfo.parse(sfoData, &sfoError))
			{
				info.title = sfo.stringValue("TITLE");
				info.titleId = sfo.stringValue("TITLE_ID", info.titleId);
				info.category = sfo.stringValue("CATEGORY");
				info.appVersion = sfo.stringValue("APP_VER");
				info.version = sfo.stringValue("VERSION");
				const std::string sfoContentId = sfo.stringValue("CONTENT_ID");
				if(!sfoContentId.empty())
					info.contentId = sfoContentId;
			}
			else
				logDebug("PARAM.SFO de " + baseName(path) + " ilegível: " + sfoError);
		}
	}

	if(options_.extractIcon && iconOffset >= 0 && iconSize > 0 && iconOffset + iconSize <= size
		&& static_cast<size_t>(iconSize) <= options_.maxIconBytes)
	{
		std::vector<uint8_t> icon(static_cast<size_t>(iconSize));
		if(readAt(file, iconOffset, icon.data(), icon.size()))
			info.iconPng = std::move(icon);
	}

	// Classificação: o cabeçalho manda, o CATEGORY do SFO desempata.
	switch(info.contentType)
	{
		case kContentTypeGd:
			info.kind = info.isPatch ? PkgCategory::Patch : PkgCategory::Game;
			break;
		case kContentTypeAc:
		case kContentTypeAl:
			info.kind = PkgCategory::Dlc;
			break;
		case kContentTypeDp:
			info.kind = PkgCategory::DeltaPatch;
			break;
		default:
			info.kind = PkgCategory::Unknown;
			break;
	}
	if(info.kind == PkgCategory::Unknown || info.kind == PkgCategory::Dlc)
	{
		const std::string category = toLower(info.category);
		if(category == "gd")
			info.kind = PkgCategory::Game;
		else if(category == "gp" || category == "gpd")
			info.kind = PkgCategory::Patch;
		else if(category == "ac")
			info.kind = PkgCategory::Dlc;
		else if(category == "gdt" || category == "gdk" || startsWith(category, "theme"))
			info.kind = PkgCategory::Theme;
	}

	info.valid = true;
	return info;
}

} // namespace orbislink
