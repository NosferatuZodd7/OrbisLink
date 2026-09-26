#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Gera os dois PNG do comando do mapa das teclas a partir do
# src/icons/dualshock.svg: corpo escuro (para o tema claro) e corpo claro
# (para os temas escuros).
#
# Os PNG ficam no repositório para a compilação não precisar de um
# renderizador de SVG; este script só se corre quando o desenho muda.
# Precisa do rsvg-convert (apt install librsvg2-bin).
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
