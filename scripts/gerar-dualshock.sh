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
command -v rsvg-convert >/dev/null || { echo "Falta o rsvg-convert (apt install librsvg2-bin)." >&2; exit 1; }

gerar() {
	local cor="$1" saida="$2"
	sed "s/fill=\"CORPO\"/fill=\"$cor\"/" "$ICONS/dualshock.svg" | rsvg-convert -w 1590 -h 988 -o "$ICONS/$saida"
	echo "$ICONS/$saida"
}

gerar "#2B2A29" dualshock-corpo-escuro.png
gerar "#D4D5D6" dualshock-corpo-claro.png
