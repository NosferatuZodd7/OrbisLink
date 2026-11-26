#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds the graphical interface for Linux and packs it, with the Qt and the
# libraries it needs, into one AppImage: dist/OrbisLink-<version>-x86_64.AppImage
# (with the command-line tool beside the interface inside it).
#
#   Needs cmake, Qt 6 with its QML modules and the Remote Play libraries (the
#   packages are listed in .github/workflows/release.yml, "Linux AppImage").
#   linuxdeploy and its Qt plugin are downloaded once into the build folder,
#   or taken from ORBISLINK_LINUXDEPLOY_DIR.
#
#   ./scripts/build-appimage.sh
#   ORBISLINK_PREBUILT=build ./scripts/build-appimage.sh   (packs what is built there)
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${ORBISLINK_VERSION:-0.1.0}"
VERSION="${VERSION#v}"
BUILD="${ORBISLINK_BUILD_DIR:-$REPO/build-appimage}"
# Whole paths: the last step runs from inside dist/.
mkdir -p "$BUILD"
BUILD="$(cd "$BUILD" && pwd)"
DIST="$REPO/dist"
TOOLS="${ORBISLINK_LINUXDEPLOY_DIR:-$BUILD/tools}"
mkdir -p "$TOOLS"
TOOLS="$(cd "$TOOLS" && pwd)"
QMAKE="${QMAKE:-$(command -v qmake6 || echo /usr/lib/qt6/bin/qmake)}"

if [ -n "${ORBISLINK_PREBUILT:-}" ]; then
	BIN="$(cd "$ORBISLINK_PREBUILT" && pwd)"
else
	GENERATOR=(-G "Unix Makefiles")
	if command -v ninja >/dev/null; then GENERATOR=(-G Ninja); fi
	cmake -S "$REPO" -B "$BUILD" "${GENERATOR[@]}" -DCMAKE_BUILD_TYPE=Release \
		-DORBISLINK_VERSION_NAME="$VERSION" \
		-DORBISLINK_REPOSITORY="${ORBISLINK_REPOSITORY:-}" \
		-DORBISLINK_BUILD_TESTS=OFF
	cmake --build "$BUILD" --target orbislink-gui orbislink-cli
	BIN="$BUILD"
fi
[ -x "$BIN/orbislink-gui" ] || { echo "No orbislink-gui in $BIN (was Qt 6 found?)" >&2; exit 1; }
# Remote Play is left out, with only a warning, when a library it needs is
# missing (libevent, FFmpeg…): an AppImage without it is not shipped.
if [ -z "${ORBISLINK_ALLOW_NO_STREAM:-}" ] && ! ldd "$BIN/orbislink-gui" | grep -q libavcodec; then
	echo "orbislink-gui was built without Remote Play: install its libraries (see release.yml)." >&2
	exit 1
fi

# The application's folder, as an AppImage lays it out.
APPDIR="$BUILD/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps" \
	"$APPDIR/usr/share/doc/orbislink"
cp "$BIN/orbislink-gui" "$APPDIR/usr/bin/"
[ -x "$BIN/orbislink-cli" ] && cp "$BIN/orbislink-cli" "$APPDIR/usr/bin/"
cp "$REPO/src/icons/logoorbis.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/orbislink.png"
cat > "$APPDIR/usr/share/applications/orbislink.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=OrbisLink
Comment=Remote Play, package installs, files and saves for a PS4 or a PS5
Exec=orbislink-gui
Icon=orbislink
Categories=Game;Network;Utility;
Terminal=false
DESKTOP
cp "$REPO/LICENSE" "$APPDIR/usr/share/doc/orbislink/"
cp "$REPO/README.md" "$APPDIR/usr/share/doc/orbislink/"
cp "$REPO/third-party/figtree/LICENSE.txt" "$APPDIR/usr/share/doc/orbislink/LICENSE-Figtree.txt"
cp "$REPO/third-party/jetbrains-mono/LICENSE.txt" "$APPDIR/usr/share/doc/orbislink/LICENSE-JetBrainsMono.txt"
cp "$REPO/third-party/lucide/LICENSE" "$APPDIR/usr/share/doc/orbislink/LICENSE-Lucide.txt"
cp "$REPO/third-party/liborbispkg/LICENSE" "$APPDIR/usr/share/doc/orbislink/LICENSE-LibOrbisPkg.txt"

# linuxdeploy and its Qt plugin, once: fixed releases, checked against their
# SHA-256 before they run.
mkdir -p "$TOOLS"
fetch_tool() {
	local tool="$1" tag="$2" sha="$3" file="$TOOLS/$1-x86_64.AppImage"
	if [ ! -x "$file" ] || ! echo "$sha  $file" | sha256sum -c --quiet - 2>/dev/null; then
		curl -fsSL --retry 4 -o "$file" \
			"https://github.com/linuxdeploy/$tool/releases/download/$tag/$tool-x86_64.AppImage"
		echo "$sha  $file" | sha256sum -c --quiet - || { echo "$tool: checksum does not match" >&2; exit 1; }
		chmod +x "$file"
	fi
}
fetch_tool linuxdeploy 1-alpha-20251107-1 c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
fetch_tool linuxdeploy-plugin-qt 1-alpha-20250213-1 15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724
export PATH="$TOOLS:$PATH"
# A build machine (or a container) without FUSE runs them unpacked.
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE
# The QML the interface imports, so its modules come along.
export QML_SOURCES_PATHS="$REPO/qml/orbislink"
LINUXDEPLOY="$TOOLS/linuxdeploy-x86_64.AppImage"

# First the Qt libraries, plugins and QML modules.
"$LINUXDEPLOY" --appdir "$APPDIR" --plugin qt \
	--desktop-file "$APPDIR/usr/share/applications/orbislink.desktop" \
	--icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/orbislink.png"

# The file dialogs' own QML needs Qt.labs.folderlistmodel, which the import
# scan does not see (it is imported from inside Qt's dialogs): without it
# the first "Choose…" closes the application.
QML_DIR="$("$QMAKE" -query QT_INSTALL_QML)"
for module in Qt/labs/folderlistmodel Qt/labs/settings; do
	if [ -d "$QML_DIR/$module" ] && [ ! -d "$APPDIR/usr/qml/$module" ]; then
		mkdir -p "$APPDIR/usr/qml/$(dirname "$module")"
		cp -r "$QML_DIR/$module" "$APPDIR/usr/qml/$module"
	fi
done
# …and the libraries those modules need.
for plugin in "$APPDIR"/usr/qml/Qt/labs/*/*.so; do
	[ -f "$plugin" ] && "$LINUXDEPLOY" --appdir "$APPDIR" --deploy-deps-only "$plugin" >/dev/null
done

# 7-Zip beside the interface, which runs it to open the archives games come
# in (its static build: it needs no library). ORBISLINK_NO_7ZIP=1 leaves it out.
if [ -z "${ORBISLINK_NO_7ZIP:-}" ]; then
	SEVEN="$BUILD/7-zip"
	ORBISLINK_7ZIP_CACHE="${ORBISLINK_7ZIP_CACHE:-$BUILD/7-zip-cache}" "$REPO/scripts/fetch-7zip.sh" linux "$SEVEN"
	install -m 755 "$SEVEN/7zz" "$APPDIR/usr/bin/7zz"
	cp "$SEVEN/License.txt" "$APPDIR/usr/share/doc/orbislink/LICENSE-7-Zip.txt"
fi

# Then the AppImage itself.
mkdir -p "$DIST"
OUTPUT_NAME="OrbisLink-$VERSION-x86_64.AppImage"
export LDAI_OUTPUT="$OUTPUT_NAME" OUTPUT="$OUTPUT_NAME"
(cd "$DIST" && "$LINUXDEPLOY" --appdir "$APPDIR" --output appimage)
echo "dist/$OUTPUT_NAME"
