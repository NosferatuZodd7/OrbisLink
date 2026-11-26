// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// A PS4 package split into parts: one file cut into pieces named
// "<name>_0.pkg", "<name>_1.pkg"… (as downloads of large games come). The
// first piece is the package's head, with its header; the others are the
// rest of the same file, cut where the piece before it ends. Put back
// together in their order, byte for byte, they are the package again.
//
// Nothing here trusts the names alone: the header of the first piece says
// how big the whole package is (package_size, 0x430), and the pieces have to
// add up to exactly that, with no number missing or repeated, before
// anything is written. The result is written beside its final name and only
// renamed to it once it is complete and checked, so a failure, a full disk
// or a cancel never leaves something that looks like a finished package.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbislink {

// A PS5 download's last part, "<base>_sc.pkg": the package's metadata
// container, joined after the numbered parts.
constexpr int kPkgContainerPart = 10000;

// "<base>_<n>.pkg" (the extension in any case, n in decimal, at most four
// digits), or "<base>_sc.pkg" (n is kPkgContainerPart): the base and n.
// False for any other name.
bool parsePkgPartName(const std::string &fileName, std::string *base, int *index);

// The pieces of one split package, found from its first piece.
struct PkgPartSet
{
	std::string base;                 // "<base>" of "<base>_0.pkg"
	std::string folder;               // where the pieces are (UTF-8)
	std::vector<std::string> parts;   // their paths, in order: _0, _1…
	std::vector<int64_t> sizes;
	// Every file named as one of its parts, whether they join or not.
	int found = 0;
	int64_t totalSize = 0;            // what the pieces add up to
	uint64_t declaredSize = 0;        // what the header says the package is
	std::string contentId;
	std::string title;
	// A PS5 package (its first part starts "\x7FFIH", or "\x7FLIH" for a
	// patch): its parts end with the container part (_sc) when there is
	// one, and they are joined by Packwright, which checks the container's
	// and the image's digests (this only checks that none is missing and
	// that the header reads across them). declaredSize stays 0.
	bool ps5 = false;
	// Every piece there, in order, adding up to the declared size.
	bool ok = false;
	// Why not (in English, for the log and translateMessage), and which
	// piece number is missing or repeated (-1 when none is).
	std::string error;
	int missingIndex = -1;
	int duplicateIndex = -1;
};

// Reads the first piece's header and its folder: which pieces belong to it,
// in what order, and whether they are all there. `firstPart` is the path of
// "<base>_0.pkg" (UTF-8).
PkgPartSet planPkgParts(const std::string &firstPart);

// The package's details (title, IDs…) read across its parts as the one
// file they make: a PS5 package's first part alone does not hold them.
struct PkgInfo;
PkgInfo inspectPkgParts(const PkgPartSet &set);

// The whole package's file name: "<base>.pkg".
std::string joinedPkgName(const PkgPartSet &set);

// Called as bytes are written (done, total); returning false cancels.
using PkgJoinProgress = std::function<bool(uint64_t done, uint64_t total)>;

// Writes the pieces, in order, into `output` (UTF-8, must not exist yet).
// Written first as `output` + ".part", then checked (its size against the
// header's, and its header read again) and only then renamed to `output`;
// on any failure or a cancel the partial file is removed. False with
// `error` set (and `*cancelled` true when it was cancelled).
bool joinPkgParts(const PkgPartSet &set, const std::string &output, const PkgJoinProgress &progress,
	std::string *error, bool *cancelled = nullptr);

} // namespace orbislink
