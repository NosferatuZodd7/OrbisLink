#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds, tests and packages the Linux x86-64 version.
#   sudo apt install cmake ninja-build libcurl4-openssl-dev
#   ./scripts/build-linux.sh
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${ORBISLINK_VERSION:-0.1.0}"
VERSION="${VERSION#v}"
BUILD="${ORBISLINK_BUILD_DIR:-$REPO/build-release}"
DIST="$REPO/dist"
STAGE="$DIST/orbislink-$VERSION-linux-x86_64"

GENERATOR=(-G "Unix Makefiles")
if command -v ninja >/dev/null; then GENERATOR=(-G Ninja); fi

# The version has to be passed: without this the Linux package always came
# out with the CMakeLists default version, whatever the tag.
cmake -S "$REPO" -B "$BUILD" "${GENERATOR[@]}" -DCMAKE_BUILD_TYPE=Release \
	-DORBISLINK_VERSION_NAME="$VERSION" \
	-DORBISLINK_REPOSITORY="${ORBISLINK_REPOSITORY:-}"
cmake --build "$BUILD"
ctest --test-dir "$BUILD" --output-on-failure

rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$BUILD/orbislink-cli" "$STAGE/"
strip "$STAGE/orbislink-cli" 2>/dev/null || true
cp "$REPO/LICENSE" "$STAGE/"
cp "$REPO/README.md" "$STAGE/"
mkdir -p "$STAGE/mock-console"
cp "$REPO"/tools/mock-console/*.py "$STAGE/mock-console/"

tar -czf "$DIST/orbislink-$VERSION-linux-x86_64.tar.gz" -C "$DIST" \
	"orbislink-$VERSION-linux-x86_64"
echo "dist/orbislink-$VERSION-linux-x86_64.tar.gz"
