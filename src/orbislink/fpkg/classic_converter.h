// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A PS1 or PS2 disc into a PS4 "Classics" package: the official emulator,
// the disc and a few lines of emulator configuration, packed with the fake
// PKG builder.
//
// PS2 discs follow easy-ps2-fpkg step by step; PS1 discs, PS Classics fPKG
// Builder. The emulators are not part of OrbisLink: like easy-ps2-fpkg, it
// downloads them once (see classics_assets.h).
#pragma once

#include "orbislink/fpkg/disc_scanner.h"
#include "orbislink/fpkg/pkg_builder.h"

#include <string>
#include <vector>

namespace orbislink::fpkg {

struct EmulatorInfo
{
	std::string ps2Dir;       // emus/Jak v2: eboot.bin and ps2-emu-compiler.self
	std::string ps2Name;      // its folder name ("Jak v2"...)
	std::string ps1Dir;       // emus/ps1hd
	std::string luaInclude;   // the shared lua_include
	std::string titleDatabase; // ps2ids.txt
	std::string ps1TitleDatabase; // ps1ids.txt
	bool hasPs2() const { return !ps2Dir.empty(); }
	bool hasPs1() const { return !ps1Dir.empty(); }
};

// The emulators in a folder of downloaded files.
EmulatorInfo findEmulators(const std::string &folder);

// The game's name from ps2ids.txt ("SLUS20946;Title") or ps1ids.txt
// ("SLPS-00965;Title"), or empty.
std::string lookupTitle(const std::string &databasePath, const std::string &titleId);

struct ClassicOptions
{
	std::string title;       // empty: the disc's
	std::string outputDir;
	Bytes icon;              // icon0.png, 512×512; empty keeps the emulator's
	Bytes background;        // pic0.png / pic1.png, 1920×1080; may be empty
	int64_t now = 0;         // seconds since 1970 (the package's dates)
};

struct ClassicResult
{
	std::string pkgPath;
	std::string contentId;
	std::string titleId;
	std::string title;
};

// "Some-Game_SLUS20946.pkg": letters, digits and dashes only.
std::string packageFileName(const std::string &title, const std::string &titleId);

// What goes into the package, without building it (the tests look at it).
bool prepareClassic(const DiscInfo &disc, const EmulatorInfo &emulators,
	const ClassicOptions &options, PkgRequest *request, ClassicResult *result, std::string *error);

bool convertClassic(const DiscInfo &disc, const EmulatorInfo &emulators,
	const ClassicOptions &options, const PkgProgress &progress, ClassicResult *result,
	std::string *error);

} // namespace orbislink::fpkg
