// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Finds PS1 and PS2 discs in a folder and says what each one is, from the
// disc itself: the boot line of SYSTEM.CNF (BOOT2 on a PS2, BOOT on a PS1)
// carries the game's serial, and the serial its region.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbislink::fpkg {

struct DiscInfo
{
	std::string path;      // the file to convert (the .bin of a .cue)
	std::string listedPath; // what was found in the folder (.cue, .iso...)
	std::string fileName;
	std::string platform;  // "ps1", "ps2", or empty when unknown
	std::string serial;    // "SLUS-20946"
	std::string titleId;   // "SLUS20946"
	std::string region;    // "USA", "Europe", "Japan", "Asia" or empty
	std::string title;     // from the file name, tidied
	std::string format;    // "iso", "bin", "img"
	uint64_t size = 0;
	int discNumber = 0;    // from "(Disc 2)" in the name, 0 if none
	std::string problem;   // why it cannot be read, if it cannot
};

// Reads one disc image (or cue sheet).
DiscInfo inspectDisc(const std::string &path);

// Walks `folder` (and up to three levels below) for disc images. `progress`
// gets each file as it is looked at; returning false stops the scan.
std::vector<DiscInfo> scanFolder(const std::string &folder,
	const std::function<bool(const std::string &)> &progress = {});

// "SLUS_209.46" or "SLUS-20946" → "SLUS-20946"; empty if it is not a serial.
std::string normaliseSerial(const std::string &text);
std::string regionOfSerial(const std::string &serial);
// "God of War (USA) (Disc 1).iso" → "God of War"
std::string titleFromFileName(const std::string &fileName, int *discNumber = nullptr);

} // namespace orbislink::fpkg
