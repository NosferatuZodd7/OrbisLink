// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// What the console has installed: each game or app under /user/app, its name,
// version and icon from /user/appmeta (param.sfo on a PS4, param.json on a
// PS5), and the packages behind it — the game's own (app.pkg), its update
// (/user/patch) and its add-ons (/user/addcont) — which can be copied to this
// PC as they are.

#include "orbislink/library/shelf.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbislink {
namespace library {

// One package of an installed title, on the console.
struct InstalledPart
{
	std::string kind; // "app", "patch", "addon"
	std::string path;
	std::string name; // what it is called on the console ("app.pkg", "ac.pkg")
	// An add-on's folder name ("EXTRAPACK0000001"); empty for the others.
	std::string label;
	int64_t size = 0;
};

struct InstalledTitle
{
	std::string titleId;  // "CUSA00001", "PPSA01234"
	std::string title;    // its name, else the title ID
	std::string version;  // "01.02"
	std::string category; // "gd", "gde"…; empty when not known
	std::string platform; // "ps4", "ps5"
	std::string iconPath; // on the console, empty when there is none
	std::vector<InstalledPart> parts;

	int64_t size() const;
	// Whether there is a package of it to copy.
	bool copyable() const;
};

// Reads /user/app, /user/appmeta, /user/patch and /user/addcont of `source`
// (the console over FTP, or a copy of its folders in the tests). `root`
// prefixes those paths ("" on the console). `found`, when given, hears of
// each title as soon as it is read.
std::vector<InstalledTitle> readInstalled(FileSource &source, const std::string &root = std::string(),
	const std::atomic<bool> *cancel = nullptr,
	const std::function<void(const InstalledTitle &)> &found = nullptr);

// The name a copy of a part takes on this PC: "Title_CUSA00001.pkg",
// "Title_CUSA00001_patch_01.02.pkg", "Title_CUSA00001_EXTRAPACK0000001.pkg".
std::string copyFileName(const InstalledTitle &title, const InstalledPart &part);
// The folder a title's copies go in, inside the chosen one: "Title [CUSA00001]".
std::string copyFolderName(const InstalledTitle &title);

} // namespace library
} // namespace orbislink
