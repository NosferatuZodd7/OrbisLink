// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace orbislink {
namespace payloads {

// The community list of PS5 payloads PLK's Payload Manager reads
// (itsPLK/ps5-payloads-mirror): each one's latest release, mirrored, with
// its SHA-256 and where it really comes from.
constexpr const char *kCatalogUrl = "https://itsplk.github.io/ps5-payloads-mirror/payloads.json";

struct CatalogPayload
{
	std::string name;
	std::string filename;
	std::string url;          // the mirrored file
	std::string source;       // its author's releases page
	std::string sourceDirect; // the same file at its author's
	std::string description;
	std::string lastUpdate;
	std::string version;
	std::string category;
	std::string checksum;     // SHA-256, lower-case hex
};

// The list as published; entries without a name, file or address are
// left out, and so is anything but a plain file name.
std::vector<CatalogPayload> parseCatalog(const std::string &json, std::string *error);

// What PLK's Payload Manager keeps beside each payload it installs
// ("<file>.json"), so a payload put there by OrbisLink reads the same in it.
std::string detailsJson(const CatalogPayload &payload, const std::string &downloadedAt);

// The name and version in such a file ("" when it has none).
void readDetails(const std::string &json, std::string *name, std::string *version);

// Whether a file on the console is this payload: its catalog file name,
// or its name followed by a version or the extension
// ("kstuff-lite_v1.10.elf", "ftpsrv.elf"), whatever the case.
bool isFileOf(const CatalogPayload &payload, const std::string &fileName);

// ── An autoload list (the autoloader's and PLDMGR's autoload.txt) as a
// sequence: each file with the wait before it.
struct AutoloadStep
{
	std::string name;
	int delayMs = 0;
};
std::vector<AutoloadStep> autoloadSteps(const std::string &text);
// Written back: "!<ms>" before each that waits, one name per line. Comment
// lines are kept at the top.
std::string autoloadText(const std::vector<AutoloadStep> &steps, const std::string &previous);

} // namespace payloads
} // namespace orbislink
