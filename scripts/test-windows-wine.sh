#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Runs the OrbisLink tests built for Windows, under Wine, from
# Linux. Validates the Windows-specific code paths (Winsock,
# GetAdaptersAddresses, the HTTP server) without needing a Windows machine.
#
#   sudo apt install mingw-w64 wine64 cmake ninja-build
#   ORBISLINK_WINDOWS_TESTS=ON ./scripts/build-windows.sh
#   ./scripts/test-windows-wine.sh
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${ORBISLINK_BUILD_DIR:-$REPO/build-windows}"
TESTS_DIR="$WORK/orbislink/tests"

WINE="${WINE:-}"
if [ -z "$WINE" ]; then
	for candidate in wine64 wine /usr/lib/wine/wine64; do
		if command -v "$candidate" >/dev/null 2>&1; then WINE="$candidate"; break; fi
		if [ -x "$candidate" ]; then WINE="$candidate"; break; fi
	done
fi
[ -n "$WINE" ] || { echo "Wine not found (apt install wine64)." >&2; exit 1; }

export WINEPREFIX="${WINEPREFIX:-$WORK/wineprefix}"
export WINEDEBUG="${WINEDEBUG:--all}"

shopt -s nullglob
executables=("$TESTS_DIR"/*.exe)
if [ ${#executables[@]} -eq 0 ]; then
	echo "There are no tests in $TESTS_DIR." >&2
	echo "Build them with: ORBISLINK_WINDOWS_TESTS=ON ./scripts/build-windows.sh" >&2
	exit 1
fi

failures=0
for executable in "${executables[@]}"; do
	name="$(basename "$executable")"
	printf '%-30s ' "$name"
	if output="$("$WINE" "$executable" 2>/dev/null)"; then
		echo "${output##*$'\n'}"
	else
		echo "FAILED"
		echo "$output"
		failures=$((failures + 1))
	fi
done

if [ "$failures" -ne 0 ]; then
	echo "$failures test executable(s) failed." >&2
	exit 1
fi
echo "All Windows tests passed under Wine."
