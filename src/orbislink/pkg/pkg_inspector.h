// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbislink {

// Category used by the queue to order game -> patch -> DLC (§5.6).
enum class PkgCategory { Unknown, Game, Patch, Dlc, Theme, DeltaPatch };

const char *pkgCategoryCode(PkgCategory category);       // "gd", "gp", "ac", ...
const char *pkgCategoryLabel(PkgCategory category);    // "Game", "Patch", "DLC", ...
// Install order: lower installs first.
int pkgCategoryInstallOrder(PkgCategory category);

// A system software version as the PS4 encodes it: 0xMMmm in BCD (0x0900 is
// 9.00, 0x1100 is 11.00). 0 when unknown.
std::string firmwareText(uint16_t version);
// From a PARAM.SFO SYSTEM_VER, or PUBTOOLINFO's sdk_ver (0x09008001 is 9.00).
uint16_t firmwareFromSystemVer(uint32_t value);
// From the system version Remote Play discovery reports ("09000000" is 9.00,
// "11008001" is 11.00); 0 when it is not one.
uint16_t firmwareFromDiscovery(const std::string &digits);

struct PkgInfo
{
	std::string path;
	int64_t fileSize = 0;
	std::string contentId;   // UP0000-CUSA00000_00-XXXXXXXXXXXXXXXX
	std::string titleId;     // CUSA00000
	std::string title;       // TITLE do PARAM.SFO
	std::string category;    // CATEGORY do PARAM.SFO ("gd", "gp", "ac", ...)
	std::string appVersion;  // APP_VER
	std::string version;     // VERSION
	PkgCategory kind = PkgCategory::Unknown;
	uint32_t contentType = 0;  // 0x1A GD, 0x1B AC, 0x1C AL, 0x1E DP
	uint32_t contentFlags = 0;
	uint64_t declaredSize = 0; // the header's package_size (0x430)
	bool isPatch = false;
	// Built and signed by Sony (a store package, which installs only with its
	// licence), rather than a fake package (fpkg) a jailbroken console takes:
	// the header's type field (0x04) says 0x8100 or 0x8300, or a delta patch
	// says 0x43 in its content flags — as PS4 PKG Tool tells them apart.
	bool official = false;
	uint32_t pkgType = 0; // the header's field at 0x04
	// The system software it needs (firmwareText()); 0 when the package
	// does not say. PUBTOOLINFO's SDK version wins over SYSTEM_VER.
	uint16_t requiredFirmware = 0;
	std::vector<uint8_t> iconPng; // ICON0.PNG, empty if missing/not requested
	bool valid = false;
	std::string error;

	// Title for the UI; never returns an empty string.
	std::string displayTitle() const;
};

class PkgInspector
{
public:
	struct Options
	{
		bool extractIcon = true;
		// Safety limits so huge garbage is not loaded into memory.
		size_t maxSfoBytes = 4u * 1024 * 1024;
		size_t maxIconBytes = 8u * 1024 * 1024;
	};

	PkgInspector();
	explicit PkgInspector(Options options);

	// Reads only the offsets needed (the file is never loaded whole).
	PkgInfo inspect(const std::string &path) const;
	// The same from any source of bytes (a package on the console, read in
	// pieces): `read` fills `buffer` with `size` bytes from `offset`.
	using ByteReader = std::function<bool(int64_t offset, void *buffer, size_t size)>;
	PkgInfo inspect(const std::string &path, int64_t size, const ByteReader &read) const;

	// Cheap magic check, so drag and drop can filter quickly.
	static bool hasPkgMagic(const std::string &path);

	static constexpr uint32_t kEntryIdParamSfo = 0x1000;
	static constexpr uint32_t kEntryIdIcon0Png = 0x1200;
	static constexpr uint32_t kContentTypeGd = 0x1A;
	static constexpr uint32_t kContentTypeAc = 0x1B;
	static constexpr uint32_t kContentTypeAl = 0x1C;
	static constexpr uint32_t kContentTypeDp = 0x1E;
	static constexpr uint32_t kFlagFirstPatch = 0x00100000;
	static constexpr uint32_t kFlagSubsequentPatch = 0x40000000;
	static constexpr int64_t kMinPkgSize = 4096;

private:
	Options options_;
};

} // namespace orbislink
