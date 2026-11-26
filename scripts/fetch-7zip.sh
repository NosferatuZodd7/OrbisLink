#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Puts the official 7-Zip command-line program in a folder, for an installer
# to carry: OrbisLink runs it (as a program of its own) to open the archives
# games are downloaded in. A fixed release, from the author's own
# repository (github.com/ip7z/7zip; the Windows file is the same one
# 7-zip.org serves), checked against its SHA-256 before anything is used.
#
#   ./scripts/fetch-7zip.sh windows <folder>   7z.exe, 7z.dll, License.txt, readme.txt
#   ./scripts/fetch-7zip.sh linux <folder>     7zz (the static build), License.txt, readme.txt
#
# 7-Zip is free software: GNU LGPL, with the unRAR restriction (its code
# may not be used to make a RAR compressor) and BSD parts; License.txt,
# which says so, goes with it. The downloads are kept in
# ORBISLINK_7ZIP_CACHE (build-7zip/ by default).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLATFORM="${1:-}"
DEST="${2:-}"
[ -n "$PLATFORM" ] && [ -n "$DEST" ] || { echo "usage: $0 windows|linux <folder>" >&2; exit 2; }

VERSION=24.09
BASE="https://github.com/ip7z/7zip/releases/download/$VERSION"
CACHE="${ORBISLINK_7ZIP_CACHE:-$REPO/build-7zip}"
mkdir -p "$CACHE" "$DEST"

fetch() {
	local name="$1" sha="$2" file="$CACHE/$1"
	if [ ! -f "$file" ] || ! echo "$sha  $file" | sha256sum -c --quiet - 2>/dev/null; then
		curl -fsSL --retry 4 -o "$file.part" "$BASE/$name"
		mv "$file.part" "$file"
		echo "$sha  $file" | sha256sum -c --quiet - || { echo "7-Zip: $name does not match its checksum" >&2; rm -f "$file"; exit 1; }
	fi
	echo "$file"
}

# The Linux build: the program itself on Linux, and what opens the Windows
# installer (a 7-Zip archive) on the machine that makes the Windows one.
LINUX_TAR="$(fetch 7z2409-linux-x64.tar.xz 914c7e20ad5ef8e4d3cf08620ff8894b28fe11b7eb99809d6930870fbe48a281)"
LINUX="$CACHE/linux"
rm -rf "$LINUX"
mkdir -p "$LINUX"
tar -xJf "$LINUX_TAR" -C "$LINUX"

case "$PLATFORM" in
	linux)
		# The static one: no library of the system it runs on is needed.
		install -m 755 "$LINUX/7zzs" "$DEST/7zz"
		cp "$LINUX/License.txt" "$LINUX/readme.txt" "$DEST/"
		;;
	windows)
		INSTALLER="$(fetch 7z2409-x64.exe bdd1a33de78618d16ee4ce148b849932c05d0015491c34887846d431d29f308e)"
		WIN="$CACHE/windows"
		rm -rf "$WIN"
		mkdir -p "$WIN"
		"$LINUX/7zzs" x -y -bd -o"$WIN" "$INSTALLER" 7z.exe 7z.dll License.txt readme.txt >/dev/null
		for file in 7z.exe 7z.dll License.txt readme.txt; do
			[ -f "$WIN/$file" ] || { echo "7-Zip: $file is not in the Windows installer" >&2; exit 1; }
		done
		cp "$WIN/7z.exe" "$WIN/7z.dll" "$WIN/License.txt" "$WIN/readme.txt" "$DEST/"
		;;
	*)
		echo "unknown platform: $PLATFORM (windows or linux)" >&2
		exit 2
		;;
esac
echo "7-Zip $VERSION ($PLATFORM) in $DEST"
