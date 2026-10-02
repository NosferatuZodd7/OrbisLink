// SPDX-License-Identifier: LGPL-3.0-only
//
// The PFS image inside a PKG: a small filesystem of 64 KiB blocks. Ported
// from LibOrbisPkg (PFS/PFSBuilder.cs, FSTree.cs, FlatPathTable.cs,
// PfsStructs.cs, PFSCWriter.cs, maxton, LGPL-3.0), with one difference:
// nothing is built in memory or in a temporary file. The image is described
// once (where every block goes) and its blocks are produced on demand, so a
// 4 GB disc goes straight from the ISO into the PKG.
#pragma once

#include "orbislink/fpkg/fpkg_crypto.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace orbislink::fpkg {

struct FsDir;

struct FsFile
{
	std::string name;
	FsDir *parent = nullptr;
	uint64_t size = 0;
	// Only differs from size for the PFSC-wrapped inner image.
	uint64_t compressedSize = 0;
	bool compressed = false;
	// Fills `length` bytes from `offset` of the file; throws on failure.
	std::function<void(uint64_t offset, uint8_t *out, size_t length)> read;

	std::string fullPath() const;
};

struct FsDir
{
	std::string name;
	FsDir *parent = nullptr;
	std::vector<std::unique_ptr<FsDir>> dirs;
	std::vector<std::unique_ptr<FsFile>> files;

	std::string fullPath() const;
	FsDir *childDir(const std::string &dirName);
	// "a/b/c" relative to this directory, or null.
	FsFile *findFile(const std::string &path);
	// Creates the directories on the way, in the order they first appear.
	FsDir *makeDirs(const std::string &path);
};

struct PfsOptions
{
	uint32_t blockSize = 0x10000;
	uint64_t minBlocks = 0;
	bool sign = false;
	bool encrypt = false;
	Bytes ekpfs;
	Bytes seed;
	int64_t fileTime = 0;
};

// Is this sce_sys file one the PKG keeps as an entry of its own (and so
// outside the filesystem)? Returns its entry id, or 0.
uint32_t pkgEntryIdForName(const std::string &name);

struct PfsLayout;

class PfsImage
{
public:
	PfsImage(FsDir &root, PfsOptions options);
	~PfsImage();
	PfsImage(const PfsImage &) = delete;
	PfsImage &operator=(const PfsImage &) = delete;

	uint64_t size() const;
	uint32_t blockSize() const { return options_.blockSize; }
	uint64_t blockCount() const;

	// Plain images: the content of one block (blockSize bytes).
	void readBlock(uint64_t block, uint8_t *out) const;

	// Signed (and maybe encrypted) images: produces every block in its final
	// form, each once, in no particular order. `progress` gets the bytes done
	// and returns false to cancel (then this throws).
	using BlockSink = std::function<void(uint64_t block, const uint8_t *data, const Digest &sha)>;
	void writeFinal(const BlockSink &sink, const std::function<bool(uint64_t)> &progress);

private:
	FsDir &root_;
	PfsOptions options_;
	std::unique_ptr<PfsLayout> layout_;
};

// The file an outer image holds: the inner image behind a PFSC header.
std::unique_ptr<FsFile> makePfscFile(const PfsImage &inner);

} // namespace orbislink::fpkg
