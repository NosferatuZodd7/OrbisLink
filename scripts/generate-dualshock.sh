#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Generates the two PNGs of the key map controller from
# src/icons/dualshock.svg: dark body (for the light theme) and light body
# (for the dark themes).
#
# The PNGs live in the repository so the build does not need an SVG
# renderer; this script is only run when the drawing changes.
# Needs rsvg-convert (apt install librsvg2-bin).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
ICONS="$REPO/src/icons"
command -v rsvg-convert >/dev/null || { echo "rsvg-convert is missing (apt install librsvg2-bin)." >&2; exit 1; }

generate() {
	local colour="$1" output="$2"
	sed "s/fill=\"BODY\"/fill=\"$colour\"/" "$ICONS/dualshock.svg" | rsvg-convert -w 1590 -h 988 -o "$ICONS/$output"
	echo "$ICONS/$output"
}

generate "#2B2A29" dualshock-body-dark.png
generate "#D4D5D6" dualshock-body-light.png
