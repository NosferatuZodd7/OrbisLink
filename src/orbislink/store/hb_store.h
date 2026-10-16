// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The stores the PS4's Homebrew Store reads (PKG-Zone, and the copies of it
// people host): a base address with a database of the apps, store.db (SQLite,
// one row per app), the MD5 of that database at api.php?db_check_hash=true,
// and each app's package (.pkg) behind its own address. They answer only to
// the store's own user agent. Nothing is signed: the database is checked
// against the MD5 the store gives, a package against its MD5 when the store
// has one. Reading the database itself is on the Qt side (Qt Sql).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orbislink::store {

// The user agent those stores let in (the Homebrew Store's own starts so).
extern const char *const kHbStoreAgent;

// One app as the database lists it (the "homebrews" table).
struct HbStoreApp
{
	std::string titleId;     // "ITEM00001", "CUSA01116"
	std::string name;
	std::string description; // what it is, in the store's few lines
	std::string author;
	std::string version;     // "1.08", as the store writes it
	std::string sizeText;    // "25.88 MB", as the store writes it
	std::string type;        // "Utility", "hb game", "Emulator"…
	std::string firmware;    // "5.05+", "4.55+, ps5_supported", "ps5_supported"
	std::string released;    // "2025-06-22"
	std::string image;       // its picture
	std::string package;     // where its package is downloaded
	std::string md5;         // the package's, when the store gives it
	// The PS5 build, when the store has one of its own.
	std::string ps5Version;
	std::string ps5Firmware;
	std::string ps5SizeText;

	// Whether it runs on each console, as the store says.
	bool forPs4() const;
	bool forPs5() const;
	// On a PS5 the store's package is the PS4 one, which only goes when the
	// PS5 build is that same package (most say so); a build of its own (not
	// handed out at the package's address) does not.
	bool ps5PackageIsThisOne() const;
	// The kind the store page sorts it under: "game", "tool", "emulator",
	// "media", "app", "theme", "mod"…
	std::string kind() const;
	// The PS4 firmware it needs, without the PS5 note ("5.05+"); "" when
	// the store does not say.
	std::string ps4Firmware() const;
};

// The address of a store as given ("https://api.pkg-zone.com/",
// "api.pkg-zone.com/store.db"…) as its base: scheme, no file, no trailing
// slash. "" when it is not an http(s) address.
std::string hbStoreBase(const std::string &address);
std::string hbStoreDatabaseUrl(const std::string &base);
std::string hbStoreHashUrl(const std::string &base);
// The hash the store answers ({"hash":"…"}), in lower case; "" when the
// answer is not one.
std::string parseHbStoreHash(const std::string &body);
// Whether the bytes begin as an SQLite database.
bool looksLikeSqlite(const std::string &bytes);

std::string md5Hex(const std::string &bytes);
// "" when the file cannot be read.
std::string md5File(const std::string &path);

// Compares two versions as the stores and consoles write them ("1.08",
// "01.08", "2.3.2"): by number, part after part. <0, 0, >0; 0 too when
// either has no number to compare.
int compareLooseVersions(const std::string &a, const std::string &b);

// The file name a package downloaded from a store takes on this PC:
// "Name_TITLEID_v1.08.pkg", with only what file names everywhere can hold.
std::string hbStorePackageName(const HbStoreApp &app);

} // namespace orbislink::store
