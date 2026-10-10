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
