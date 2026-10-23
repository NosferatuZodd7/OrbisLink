// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbislink {

// The console library: one folder, OrbisLinkFPKG, on the console's own
// memory, on a USB drive or on the extended storage, where games and apps of
// any kind are put as they come; the app says what each one is and does what
// it needs to be played:
//
//   .pkg                      a PS4 package: installed by the installer
//                             (on a PS5 by its path there).
//   .iso .bin .img .cue       a PS1/PS2 disc: fetched to the PC, made into a
//                             PS4 package, sent back and installed.
//   .ffpkg .exfat .ffpfs      a PS5 game image: moved next door, into the
//                             folder ShadowMountPlus mounts from on the same
//                             drive (a move there is instant).
//   a folder with sce_sys     a PS5 app or game folder: moved the same way.
//   .elf .lua .js .jar        a payload: run on the console.
namespace library {

constexpr const char *kFolderName = "OrbisLinkFPKG";

// Where the library is read from, on every drive the console can have.
std::vector<std::string> libraryFolders();

enum class Kind
{
	Package,
	Disc,
	Image,
	Folder,
	Payload,
	Archive,
	Other,
};
// What a file or folder is, from its name (and size: a small .bin is a
// payload, a big one a disc).
Kind kindOf(const std::string &name, bool directory, int64_t size);
const char *kindName(Kind kind);

// "PPSA01234" / "CUSA12345" / "SLUS20946" in a name, written with or without
// a dash or an underscore; empty when there is none.
std::string titleIdInName(const std::string &name);

// "internal", "usb" or "ext": the drive a console path is on.
std::string driveOf(const std::string &path);

// The folder ShadowMountPlus mounts from on the drive of `path`
// (/data/homebrew, /mnt/usbN/homebrew, /mnt/extN/homebrew); empty when the
// drive has none.
std::string mountFolderFor(const std::string &path);

// What an app's sce_sys says about it.
struct AppParams
{
	bool ok = false;
	std::string titleId;
	std::string title;
	std::string version;
	std::string contentId;
};
// A PS5 app's param.json.
AppParams readParamJson(const std::string &json);
// A PS4 app's param.sfo.
AppParams readParamSfo(const std::vector<uint8_t> &sfo);

// Small reads served from blocks fetched once: reading a disc's or a
// package's header over the network takes a handful of requests, not one per
// sector. `fetch` gets a block's offset and length.
class BlockReader
{
public:
	using Fetch = std::function<bool(int64_t offset, size_t length, std::vector<uint8_t> *bytes)>;
	BlockReader(Fetch fetch, int64_t size, size_t blockSize = 64 * 1024);

	bool read(int64_t offset, void *out, size_t length);
	// How many blocks were fetched (for tests).
	size_t fetches() const { return fetches_; }

private:
	struct Block
	{
		int64_t offset = 0;
		std::vector<uint8_t> bytes;
	};
	const Block *block(int64_t index);

	Fetch fetch_;
	int64_t size_ = 0;
	size_t blockSize_ = 0;
	std::vector<Block> cache_;
	size_t fetches_ = 0;
};

} // namespace library
} // namespace orbislink
