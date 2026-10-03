#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Produces the OrbisLink Windows x64 executable and the installer, from
# Linux, with mingw-w64 + NSIS. Needs neither Windows nor MSVC.
#
#   sudo apt install mingw-w64 nsis cmake ninja-build git zip
#   ./scripts/build-windows.sh
#
# ORBISLINK_WINDOWS_TESTS=ON also builds the tests as .exe (useful to
# run them under Wine — see scripts/test-windows-wine.sh).
#
# Output in dist/:
#   orbislink-cli.exe                     standalone executable (no extra DLLs)
#   OrbisLink-<version>-windows-x64.zip   portable version
#   OrbisLink-<version>-setup.exe         installer
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${ORBISLINK_VERSION:-0.1.0}"
VERSION="${VERSION#v}"   # "v1.2.3" -> "1.2.3"

# Windows' VIProductVersion must be X.X.X.X with numbers only; the visible
# version may be "1.2.3-dev". The numeric part is extracted and padded.
version_numeric() {
	local numeric
	numeric="$(printf '%s' "$1" | grep -oE '^[0-9]+(\.[0-9]+)*' || true)"
	[ -n "$numeric" ] || numeric="0"
	local IFS='.'
	# shellcheck disable=SC2206
	local parts=($numeric)
	while [ "${#parts[@]}" -lt 4 ]; do parts+=("0"); done
	echo "${parts[0]}.${parts[1]}.${parts[2]}.${parts[3]}"
}
VERSION_NUMERIC="$(version_numeric "$VERSION")"
CURL_TAG="${CURL_TAG:-curl-8_11_1}"
WORK="${ORBISLINK_BUILD_DIR:-$REPO/build-windows}"
CURL_PREFIX="$WORK/curl-install"
DIST="$REPO/dist"
STAGE="$DIST/windows"

need() { command -v "$1" >/dev/null || { echo "The '$1' command is missing." >&2; exit 1; }; }
need x86_64-w64-mingw32-g++
need cmake
need git

GENERATOR=(-G "Unix Makefiles")
if command -v ninja >/dev/null; then GENERATOR=(-G Ninja); fi

echo "==> 1/4 static libcurl for mingw-w64 ($CURL_TAG)"
if [ ! -f "$CURL_PREFIX/lib/libcurl.a" ]; then
	if [ ! -d "$WORK/curl-src" ]; then
		git clone --depth 1 --branch "$CURL_TAG" https://github.com/curl/curl "$WORK/curl-src"
	fi
	# No OpenSSL: Windows' own Schannel is enough and avoids dependencies.
	cmake -S "$WORK/curl-src" -B "$WORK/curl-build" "${GENERATOR[@]}" \
		-DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/toolchain-mingw-w64.cmake" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$CURL_PREFIX" \
		-DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
		-DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF \
		-DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF -DCURL_USE_LIBPSL=OFF \
		-DCURL_USE_LIBSSH2=OFF -DUSE_LIBIDN2=OFF -DCURL_ZLIB=OFF \
		-DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DCURL_DISABLE_LDAP=ON \
		-DENABLE_UNIX_SOCKETS=OFF
	cmake --build "$WORK/curl-build"
	cmake --install "$WORK/curl-build"
else
	echo "    (already built in $CURL_PREFIX)"
fi

echo "==> 2/4 orbislink-cli.exe"
cmake -S "$REPO" -B "$WORK/orbislink" "${GENERATOR[@]}" \
	-DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/toolchain-mingw-w64.cmake" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH="$CURL_PREFIX" \
	-DORBISLINK_BUILD_TESTS="${ORBISLINK_WINDOWS_TESTS:-OFF}" \
	-DORBISLINK_VERSION_NAME="$VERSION" \
	-DORBISLINK_REPOSITORY="${ORBISLINK_REPOSITORY:-}" \
	`# This build only makes the command line, which has no Remote Play. The` \
	`# graphical interface (the one with streaming) comes built from the MSVC job.` \
	-DORBISLINK_ENABLE_STREAM=OFF \
	-DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"
cmake --build "$WORK/orbislink"
x86_64-w64-mingw32-strip "$WORK/orbislink/orbislink-cli.exe"

echo "==> 3/4 distribution folder"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$WORK/orbislink/orbislink-cli.exe" "$STAGE/"
# Application icon: used by the installer, the shortcuts and the entry in
# "Installed apps".
cp "$REPO/src/icons/orbisDDM.ico" "$STAGE/orbislink.ico"
# Text files in CRLF, so they open properly in Notepad.
sed 's/$/\r/' "$REPO/LICENSE" > "$STAGE/LICENSE.txt"
# The font and the icons built into the interface carry their own licences.
sed 's/$/\r/' "$REPO/third-party/inter/LICENSE.txt" > "$STAGE/LICENSE-Inter.txt"
sed 's/$/\r/' "$REPO/third-party/lucide/LICENSE" > "$STAGE/LICENSE-Lucide.txt"
sed 's/$/\r/' "$REPO/third-party/liborbispkg/LICENSE" > "$STAGE/LICENSE-LibOrbisPkg.txt"
sed 's/$/\r/' "$REPO/third-party/create-fself/LICENSE" > "$STAGE/LICENSE-create-fself.txt"
# The project address comes from outside (ORBISLINK_REPO_URL); in CI it is the
# repository the build ran in. When it is missing, the source code line
# disappears instead of being left half written.
if [ -n "${ORBISLINK_REPO_URL:-}" ]; then
	sed "s/@VERSION@/$VERSION/g; s|@REPO_URL@|${ORBISLINK_REPO_URL}|g" \
		"$REPO/packaging/windows/README.txt"
else
	sed "s/@VERSION@/$VERSION/g; /@REPO_URL@/d" "$REPO/packaging/windows/README.txt"
fi | sed 's/$/\r/' > "$STAGE/README.txt"
# The .bat files really need CRLF.
sed 's/$/\r/' "$REPO/packaging/windows/diagnostics.bat" > "$STAGE/diagnostics.bat"

# If there is an already built graphical interface (from the Qt+MSVC job,
# which runs on another machine), it goes into the installer and the zip.
GUI_DEFINE=()
if [ -n "${ORBISLINK_GUI_DIR:-}" ] && [ -d "$ORBISLINK_GUI_DIR" ]; then
	echo "    including the graphical interface from $ORBISLINK_GUI_DIR"
	mkdir -p "$STAGE/gui"
	cp -r "$ORBISLINK_GUI_DIR"/. "$STAGE/gui/"
	GUI_DEFINE=("-DINCLUDE_GUI=1")
fi

if command -v zip >/dev/null; then
	(cd "$STAGE" && zip -q -r "$DIST/OrbisLink-$VERSION-windows-x64.zip" .)
	echo "    dist/OrbisLink-$VERSION-windows-x64.zip"
fi

echo "==> 4/4 instalador"
if command -v makensis >/dev/null; then
	NSIS_URL=()
	[ -n "${ORBISLINK_REPO_URL:-}" ] && NSIS_URL=("-DAPP_URL=${ORBISLINK_REPO_URL}")
	makensis -V2 \
		"${GUI_DEFINE[@]}" \
		"${NSIS_URL[@]+"${NSIS_URL[@]}"}" \
		"-DAPP_VERSION=$VERSION" \
		"-DAPP_VERSION_NUMERIC=$VERSION_NUMERIC" \
		"-DSOURCE_DIR=$STAGE" \
		"-DOUTPUT_FILE=$DIST/OrbisLink-$VERSION-setup.exe" \
		"$REPO/packaging/windows/orbislink.nsi"
	echo "    dist/OrbisLink-$VERSION-setup.exe"
else
	echo "    makensis not found: installer not generated (apt install nsis)."
fi

echo
echo "Done. Contents of dist/:"
ls -lh "$DIST" | tail -n +2
