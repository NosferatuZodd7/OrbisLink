// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orbislink/pkg/pkg_inspector.h"

#include "orbislink/common/bytes.h"
#include "orbislink/common/json.h"
#include "orbislink/common/log.h"
#include "orbislink/common/tr.h"
#include "orbislink/common/util.h"
#include "orbislink/pkg/sfo_parser.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace orbislink {

namespace {

// PKG header offsets (big-endian), confirmed in
// flatz/ps4_remote_pkg_installer/pkg.h.
constexpr size_t kHeaderReadSize = 0x1000; // enough for every field up to 0x430
constexpr size_t kOffMagic = 0x00;
constexpr size_t kOffPkgType = 0x04;
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

// A PS5 param.json: its title (in the default language), IDs, version and
// the system software it needs ("0x0960000000000000" is 9.60).
void readParamJson(const std::string &text, PkgInfo *info)
{
	std::string error;
	const Json root = Json::parse(text, &error);
	if(!root.isObject())
		return;
	const std::string titleId = root["titleId"].toString();
	if(!titleId.empty())
		info->titleId = titleId;
	const std::string contentId = root["contentId"].toString();
	if(!contentId.empty())
		info->contentId = contentId;
	info->appVersion = root["contentVersion"].toString(info->appVersion);
	const Json &localized = root["localizedParameters"];
	const std::string language = localized["defaultLanguage"].toString("en-US");
	std::string title = localized[language]["titleName"].toString();
	if(title.empty())
		title = localized["en-US"]["titleName"].toString();
	if(!title.empty())
		info->title = title;
	const std::string required = root["requiredSystemSoftwareVersion"].toString();
	if(required.size() > 2 && (required.rfind("0x", 0) == 0 || required.rfind("0X", 0) == 0))
	{
		const unsigned long long value = std::strtoull(required.c_str() + 2, nullptr, 16);
		info->requiredFirmware = static_cast<uint16_t>(value >> 48);
	}
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

const char *pkgCategoryLabel(PkgCategory category)
{
	switch(category)
	{
		case PkgCategory::Game: return QT_TRANSLATE_NOOP("Messages", "Game");
		case PkgCategory::Patch: return QT_TRANSLATE_NOOP("Messages", "Patch");
		case PkgCategory::Dlc: return QT_TRANSLATE_NOOP("Messages", "DLC");
		case PkgCategory::Theme: return QT_TRANSLATE_NOOP("Messages", "Theme");
		case PkgCategory::DeltaPatch: return QT_TRANSLATE_NOOP("Messages", "Delta patch");
		case PkgCategory::Unknown: break;
	}
	return QT_TRANSLATE_NOOP("Messages", "Unknown");
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

std::string firmwareText(uint16_t version)
{
	if(version == 0)
		return std::string();
	char text[8];
	std::snprintf(text, sizeof text, "%X.%02X", (version >> 8) & 0xFF, version & 0xFF);
	return text;
}

uint16_t firmwareFromSystemVer(uint32_t value)
{
	// 0x09008001 (SYSTEM_VER, sdk_ver) or already 0x0900.
	const uint32_t compact = value <= 0xFFFF ? value : value >> 16;
	return static_cast<uint16_t>(compact);
}

uint16_t firmwareFromDiscovery(const std::string &digits)
{
	// "09000000": the first four digits are the version, read as BCD.
	if(digits.size() < 4)
		return 0;
	uint16_t version = 0;
	for(size_t i = 0; i < 4; ++i)
	{
		const char c = digits[i];
		if(c < '0' || c > '9')
			return 0;
		version = static_cast<uint16_t>((version << 4) | static_cast<uint16_t>(c - '0'));
	}
	return version;
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
	return "(untitled)";
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
	const int64_t size = fileSize(path);
	if(size < 0)
	{
		PkgInfo info;
		info.path = path;
		info.error = QT_TRANSLATE_NOOP("Messages", "Could not read the file.");
		return info;
	}
	std::ifstream file(path, std::ios::binary);
	if(!file)
	{
		PkgInfo info;
		info.path = path;
		info.fileSize = size;
		info.error = QT_TRANSLATE_NOOP("Messages", "Could not open the file.");
		return info;
	}
	return inspect(path, size, [&file](int64_t offset, void *buffer, size_t length) {
		return readAt(file, offset, buffer, length);
	});
}

PkgInfo PkgInspector::inspect(const std::string &path, int64_t size, const ByteReader &read) const
{
	PkgInfo info;
	info.path = path;
	info.fileSize = size;
	if(size < kMinPkgSize)
	{
		info.error = QT_TRANSLATE_NOOP("Messages", "Not a valid PS4 pkg (file too small).");
		return info;
	}

	std::vector<uint8_t> header(kHeaderReadSize, 0);
	const size_t headerBytes = static_cast<size_t>(
		size < static_cast<int64_t>(kHeaderReadSize) ? size : static_cast<int64_t>(kHeaderReadSize));
	if(!read(0, header.data(), headerBytes))
	{
		info.error = QT_TRANSLATE_NOOP("Messages", "Could not read the pkg header.");
		return info;
	}

	// A PS5 package: "\x7FFIH" (or "\x7FLIH", a scene backport). Its
	// details live in a PS4-style container ("\x7FCNT") further in, whose
	// place the header gives (0x58, little-endian; 0x30 in a backport) —
	// read like a PS4 package, from there on.
	const bool fih = header[kOffMagic] == 0x7F && header[kOffMagic + 1] == 'F' && header[kOffMagic + 2] == 'I'
		&& header[kOffMagic + 3] == 'H';
	const bool lih = header[kOffMagic] == 0x7F && header[kOffMagic + 1] == 'L' && header[kOffMagic + 2] == 'I'
		&& header[kOffMagic + 3] == 'H';
	if(fih || lih)
	{
		const uint64_t inner = readLE64(header.data() + (fih ? 0x58 : 0x30));
		const bool official = fih && header[5] == 0x80;
		uint8_t magic[4] = { 0, 0, 0, 0 };
		const bool hasInner = inner > 0 && static_cast<int64_t>(inner) + kMinPkgSize <= size
			&& read(static_cast<int64_t>(inner), magic, 4) && magic[0] == 0x7F && magic[1] == 'C' && magic[2] == 'N'
			&& magic[3] == 'T';
		if(!hasInner)
		{
			info.ps5 = true;
			info.official = official;
			info.error = official
				? QT_TRANSLATE_NOOP("Messages", "An official PS5 store package (encrypted): a jailbroken PS5 installs "
					"fake packages (fpkg), not this.")
				: QT_TRANSLATE_NOOP("Messages", "A PS5 package whose details could not be read.");
			return info;
		}
		const int64_t base = static_cast<int64_t>(inner);
		PkgInfo ps5 = inspect(path, size - base, [&read, base](int64_t offset, void *buffer, size_t length) {
			return read(base + offset, buffer, length);
		});
		ps5.path = path;
		ps5.fileSize = size;
		ps5.declaredSize = 0;
		ps5.ps5 = true;
		ps5.backport = lih;
		ps5.official = official;
		// What is inside has no PS4 type to go by: a game unless it says otherwise.
		if(ps5.valid && ps5.kind == PkgCategory::Unknown)
			ps5.kind = PkgCategory::Game;
		return ps5;
	}

	if(!(header[kOffMagic] == 0x7F && header[kOffMagic + 1] == 'C' && header[kOffMagic + 2] == 'N'
		   && header[kOffMagic + 3] == 'T'))
	{
		// The older consoles' packages start with "\x7FPKG", then their
		// platform (1: PS3; 2: PSP and PS Vita): not for a PS4, but files an
		// emulator on the console (RPCS3) may want copied over.
		const bool older = header[kOffMagic] == 0x7F && header[kOffMagic + 1] == 'P' && header[kOffMagic + 2] == 'K'
			&& header[kOffMagic + 3] == 'G';
		const unsigned platform = older ? (static_cast<unsigned>(header[4]) << 8) | header[5] : 0;
		info.error = older && platform == 1 ? QT_TRANSLATE_NOOP("Messages", "A PS3 package, not a PS4 one.")
			: older && platform == 2 ? QT_TRANSLATE_NOOP("Messages", "A PSP or PS Vita package, not a PS4 one.")
			: QT_TRANSLATE_NOOP("Messages", "Not a valid PS4 or PS5 package.");
		return info;
	}

	const uint32_t entryCount = readBE32(header.data() + kOffEntryCount);
	const uint32_t entryTableOffset = readBE32(header.data() + kOffEntryTableOffset);
	info.contentId = readFixedString(header.data() + kOffContentId, kContentIdSize);
	info.contentType = readBE32(header.data() + kOffContentType);
	info.contentFlags = readBE32(header.data() + kOffContentFlags);
	info.pkgType = readBE32(header.data() + kOffPkgType);
	{
		const uint32_t type = info.pkgType >> 16;
		const bool officialDelta = (info.contentType & 0xFF) == kContentTypeDp && (info.contentFlags >> 24) == 0x43;
		info.official = officialDelta || type == 0x8100 || type == 0x8300;
	}
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
		info.error = QT_TRANSLATE_NOOP("Messages", "Invalid pkg entry table.");
		return info;
	}
	const int64_t tableEnd = static_cast<int64_t>(entryTableOffset)
		+ static_cast<int64_t>(entryCount) * static_cast<int64_t>(kTableEntrySize);
	if(tableEnd > size)
	{
		info.error = QT_TRANSLATE_NOOP("Messages", "The pkg entry table is outside the file.");
		return info;
	}

	std::vector<uint8_t> table(static_cast<size_t>(entryCount) * kTableEntrySize);
	if(!read(entryTableOffset, table.data(), table.size()))
	{
		info.error = QT_TRANSLATE_NOOP("Messages", "Could not read the pkg entry table.");
		return info;
	}

	int64_t sfoOffset = -1, sfoSize = 0, iconOffset = -1, iconSize = 0, jsonOffset = -1, jsonSize = 0;
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
		else if(id == kEntryIdParamJson)
		{
			jsonOffset = entryOffset;
			jsonSize = entrySize;
		}
	}

	// A PS5 package's details: param.json, where a PS4's PARAM.SFO is.
	if(sfoOffset < 0 && jsonOffset >= 0 && jsonSize > 0 && jsonOffset + jsonSize <= size
		&& static_cast<size_t>(jsonSize) <= options_.maxSfoBytes)
	{
		std::string text(static_cast<size_t>(jsonSize), '\0');
		if(read(jsonOffset, text.data(), text.size()))
			readParamJson(text, &info);
	}

	// Missing metadata does not invalidate the pkg (§5.2): it becomes "(untitled)".
	if(sfoOffset >= 0 && sfoSize > 0 && sfoOffset + sfoSize <= size
		&& static_cast<size_t>(sfoSize) <= options_.maxSfoBytes)
	{
		std::vector<uint8_t> sfoData(static_cast<size_t>(sfoSize));
		if(read(sfoOffset, sfoData.data(), sfoData.size()))
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
				info.requiredFirmware = firmwareFromSystemVer(sfo.uintValue("SYSTEM_VER"));
				// "c_date=20150319,sdk_ver=02508011,st_type=digital50,…"
				const std::string tools = sfo.stringValue("PUBTOOLINFO");
				const size_t sdk = tools.find("sdk_ver=");
				if(sdk != std::string::npos)
				{
					const uint16_t version = firmwareFromSystemVer(static_cast<uint32_t>(
						std::strtoul(tools.substr(sdk + 8, 8).c_str(), nullptr, 16)));
					if(version != 0)
						info.requiredFirmware = version;
				}
			}
			else
				logDebug("PARAM.SFO of " + baseName(path) + " unreadable: " + sfoError);
		}
	}

	if(options_.extractIcon && iconOffset >= 0 && iconSize > 0 && iconOffset + iconSize <= size
		&& static_cast<size_t>(iconSize) <= options_.maxIconBytes)
	{
		std::vector<uint8_t> icon(static_cast<size_t>(iconSize));
		if(read(iconOffset, icon.data(), icon.size()))
			info.iconPng = std::move(icon);
	}

	// Classification: the header decides, the SFO CATEGORY breaks ties.
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
