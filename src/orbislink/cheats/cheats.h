// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Cheats and game patches for the games a console has installed.
//
// Cheats come from the GoldHEN cheat repository: three lists (json.txt,
// shn.txt, mc4.txt) of "<file>=<game>" name every file it has, the file
// saying the title ID and the game's version it is for. The console loads
// them from /data/GoldHEN/cheats/<format>/ (PS4, GoldHEN's cheat menu) or
// /data/etaHEN/cheats/<format>/ (PS5, etaHEN's), named
// "<TITLE ID>_<version>[...].<format>"; etaHEN only knows the files its own
// list (/data/etaHEN/cheats/<format>.txt) names.
//
// Game patches (60 FPS, no motion blur, skipped intros…) come from the
// patch archive of illusion0001's PS4-PS5-Game-Patch: one XML per title ID,
// each <Metadata> a patch for one version of the game. GoldHEN's game_patch
// plugin reads /data/GoldHEN/patches/xml/<TITLE ID>.xml and applies a patch
// when its switch, /data/GoldHEN/patches/settings/0x<hash>.txt, starts
// with "1".

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace orbislink {
namespace cheats {

enum class Format
{
	Json,
	Shn,
	Mc4,
};

// "json", "shn", "mc4": the list's name, the repository's folder and the
// console's.
std::string formatName(Format format);
const std::vector<Format> &allFormats();

// "01.09" from "01.09", "1.09", "01_09", "01.000.000" (a PS5 game's) kept
// as it is; empty when it does not look like a version.
std::string normalizeVersion(const std::string &version);

// One file of the repository.
struct CheatListing
{
	Format format = Format::Json;
	std::string file;    // as the list names it: "CUSA00207_01.09.json", "CUSA27973_01.03.mc4.xml"
	std::string titleId; // "CUSA00207"
	std::string version; // "01.09"
	// What follows the version, when anything does: the program it is for
	// ("bh1hd.elf") or a second set ("2").
	std::string variant;
	std::string game;    // the name after "="

	// Where it is in the repository, from its root: "json/<file>". An mc4
	// file is read from its ".mc4.xml" (the cheats' names, readable) and
	// sent as the ".mc4" beside it (what the consoles load).
	std::string readablePath() const;
	std::string loadablePath() const;
	// The name it takes on the console: "<TITLE ID>_<version>[_<variant>].<format>",
	// the version written the way the console matches it.
	std::string consoleName() const;
};

// The lines of one of the repository's lists. Lines that do not name a
// file of a title ID and a version are left out.
std::vector<CheatListing> parseCheatList(const std::string &text, Format format);

// A title's files (title IDs compared without case), those for `version`
// first, then the others newest first. `exact`, when given, is how many
// are for that version.
std::vector<CheatListing> cheatsFor(const std::vector<CheatListing> &all, const std::string &titleId,
	const std::string &version, size_t *exact = nullptr);

struct Cheat
{
	std::string name;
	std::string description;
};

// What a cheat file says: its cheats' names and who made it. JSON for
// .json, XML ("<Trainer>" of "<Cheat Text=…>") for .shn and .mc4.xml.
struct CheatFile
{
	bool ok = false;
	std::string game;
	std::string version;
	std::string process;
	std::string author;
	std::vector<Cheat> cheats;
};
CheatFile parseCheatFile(const std::string &text, Format format);

// ── the console's folders

enum class Console
{
	GoldHen, // PS4
	EtaHen,  // PS5
};

// "/data/GoldHEN/cheats/json", "/data/etaHEN/cheats/shn"…
std::string cheatFolder(Console console, Format format);
// etaHEN's list of a format: "/data/etaHEN/cheats/json.txt".
std::string etaHenList(Format format);
// etaHEN's list with `file` in it (once, as "<file>=<game>"), or without it.
std::string etaHenListAdd(const std::string &list, const std::string &file, const std::string &game);
std::string etaHenListRemove(const std::string &list, const std::string &file);

// Whether a file in a cheat folder is for that title (and that version,
// when one is given): its name starts with "<TITLE ID>_<version>".
bool cheatFileIsFor(const std::string &fileName, const std::string &titleId, const std::string &version = {});

// ── game patches

struct Patch
{
	std::string title;
	std::string name;
	std::string note;
	std::string author;
	std::string patchVersion;
	std::string appVersion; // "01.09", or "mask…"/"all…" for every version
	std::string appElf;     // "eboot.bin"
	int lines = 0;

	// Whether GoldHEN applies it to that version of the game.
	bool appliesTo(const std::string &version) const;
};

struct PatchFile
{
	bool ok = false;
	std::vector<std::string> titleIds; // what <TitleID> lists
	std::vector<Patch> patches;
};
PatchFile parsePatchFile(const std::string &xml);

// "/data/GoldHEN/patches/xml/CUSA00207.xml"
std::string patchXmlPath(const std::string &titleId);
// GoldHEN's switch for a patch of the title's XML: the djb2 hash of
// title, name, version, the XML's path and the program, as game_patch
// works it out (at most 255 bytes of them, each byte a signed char).
uint64_t patchHash(const Patch &patch, const std::string &titleId);
uint64_t patchHash(const std::string &title, const std::string &name, const std::string &appVersion,
	const std::string &xmlPath, const std::string &appElf);
// "/data/GoldHEN/patches/settings/0x0123456789abcdef.txt"
std::string patchSettingsPath(uint64_t hash);
// What a switch file says: on when it starts with "1".
bool patchSwitchOn(const std::string &content);

// Whether GoldHEN's plugins.ini loads game_patch for that title (in its
// "[default]" section or the title's own).
bool patchPluginLoads(const std::string &pluginsIni, const std::string &titleId);

// ── a little XML

// The elements of a document in order, nesting flattened: each with its
// attributes (entities decoded) and the text right after its start tag.
// Enough for the cheat and patch files, which are made by tools.
struct XmlElement
{
	std::string name;
	std::map<std::string, std::string> attributes;
	std::string text;
	int depth = 0;
	std::string attribute(const std::string &key) const;
};
std::vector<XmlElement> xmlElements(const std::string &text);
std::string xmlDecode(const std::string &text);

} // namespace cheats
} // namespace orbislink
