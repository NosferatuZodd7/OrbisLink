// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orbislink {

// Where the homebrew enablers keep their payloads and plugins on the
// console, and how each one decides what starts by itself:
//
// PS4 (GoldHEN)
//   /data/GoldHEN/payloads   goldhen.bin: GoldHEN itself, loaded at every
//                            boot by the PPPwn loader. Removing it stops the
//                            jailbreak from coming back.
//   /data/payloads           the payload library Payload Guest lists.
//   /data/GoldHEN/plugins    .prx plugins; /data/GoldHEN/plugins.ini says
//                            which load: "[default]" for every game, a title
//                            ID for one, each line "<path>=true|false".
//   Payloads are sent to GoldHEN's BinLoader, port 9090.
//
// PS5 (etaHEN, and the autoloader that runs after the exploit)
//   /data/etaHEN/payloads    ELF payloads; /data/etaHEN/plugins plugins.
//                            A file starts with etaHEN when "<name>.auto_start"
//                            sits next to it. Settings: /data/etaHEN/config.ini.
//   /data/ps5_autoloader     ELFs and autoload.txt: one file name per line,
//                            in order, "!<ms>" lines waiting between them.
//   /data/pldmgr/payloads    PLK's Payload Manager (PLDMGR): a folder per
//                            payload, "<file>.json" beside it; its own
//                            /data/pldmgr/autoload.txt, the same format.
//   ELF payloads go to the ELF loader on port 9021.
namespace payloads {

enum class Kind
{
	Ps4,
	Ps5,
};

// How a folder marks the ones that start by themselves.
enum class AutoStart
{
	None,
	Marker,   // "<name>.auto_start" next to the file (etaHEN)
	List,     // listed in a text file (the PS5 autoloader's autoload.txt)
	Ini,      // "[default]" of GoldHEN's plugins.ini
};

struct Folder
{
	std::string id;
	std::string path;
	// The files it is about, lower case with the dot (".elf").
	std::vector<std::string> extensions;
	AutoStart autoStart = AutoStart::None;
	// The text file that decides it (List, Ini).
	std::string configPath;
	// A file in it the jailbreak itself depends on.
	std::string criticalFile;
	// The files sit one level down, a folder each (PLDMGR).
	bool nested = false;
};

std::vector<Folder> folders(Kind kind);

// The settings files worth editing by hand on that console.
std::vector<std::string> configFiles(Kind kind);

// The loader that takes a file like this one right now: PS4 GoldHEN's
// BinLoader (9090); on a PS5, ELF/SELF to 9021, the exploit's .bin loader
// to 9020, and the Lua (9026), JAR (9025) and JS (50000) loaders.
uint16_t loaderPort(Kind kind, const std::string &fileName);

// Whether the console can be sent the file to run now.
bool sendable(Kind kind, const std::string &fileName);

std::string lowerExtension(const std::string &fileName);

// ── autoload.txt (PS5 autoloader)

// The file names it starts, in order.
std::vector<std::string> autoloadEntries(const std::string &text);
bool autoloadListed(const std::string &text, const std::string &name);
// Adds the name at the end, or takes it out together with the "!<ms>" wait
// right before it.
std::string autoloadSet(const std::string &text, const std::string &name, bool listed);
std::string autoloadRename(const std::string &text, const std::string &from, const std::string &to);

// ── plugins.ini (GoldHEN)

// Whether the plugin at `path` loads for every game.
bool pluginEnabled(const std::string &ini, const std::string &path);
// Turns it on or off in "[default]" (made when missing); the other
// sections are left as they are.
std::string pluginSet(const std::string &ini, const std::string &path, bool enabled);
// The plugin moved: every line naming it follows, in every section.
std::string pluginRename(const std::string &ini, const std::string &from, const std::string &to);
// Every line naming it goes, in every section.
std::string pluginRemove(const std::string &ini, const std::string &path);

} // namespace payloads
} // namespace orbislink
