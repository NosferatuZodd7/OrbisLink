// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A PS1 or PS2 disc into a PS4 "Classics" package: the official emulator,
// the disc and a few lines of emulator configuration, packed with the fake
// PKG builder.
//
// The emulator is Sony's and is not part of OrbisLink: it comes from a folder
// the person points at (their own copy, taken from a PS2 or PS1 Classic). The
// layouts of PS-Classics-fPKG-Builder ("emus/<name>") and of easy-ps2-fpkg
// are both understood.
#pragma once

#include "orbislink/fpkg/disc_scanner.h"
#include "orbislink/fpkg/pkg_builder.h"

#include <string>
#include <vector>

namespace orbislink::fpkg {

struct EmulatorInfo
{
	std::string ps2Dir;       // folder with eboot.bin and ps2-emu-compiler.self
	std::string ps2Name;      // its folder name ("Jak v2"...)
	std::string ps1Dir;       // folder of the PS1 emulator (eboot.bin, no ps2 compiler)
	std::string luaInclude;   // lua_include next to them, if any
	std::string titleDatabase; // ps2ids.txt, if any
	bool hasPs2() const { return !ps2Dir.empty(); }
	bool hasPs1() const { return !ps1Dir.empty(); }
};

// Looks for the emulators in `folder` and a few levels below it.
EmulatorInfo findEmulators(const std::string &folder);

// The game's name from ps2ids.txt ("SLUS20946;Title"), or empty.
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

// "Some Game [SLUS20946].pkg", without characters file systems refuse.
std::string packageFileName(const std::string &title, const std::string &titleId);

// What goes into the package, without building it (the tests look at it).
bool prepareClassic(const DiscInfo &disc, const EmulatorInfo &emulators,
	const ClassicOptions &options, PkgRequest *request, ClassicResult *result, std::string *error);

bool convertClassic(const DiscInfo &disc, const EmulatorInfo &emulators,
	const ClassicOptions &options, const PkgProgress &progress, ClassicResult *result,
	std::string *error);

} // namespace orbislink::fpkg
