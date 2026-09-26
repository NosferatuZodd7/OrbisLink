#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The console card wordmarks, written in Fugaz One (third-party/fugaz-one,
# SIL Open Font License 1.1). The letter outlines become SVG paths, so the
# app does not need the font installed.
#
# Fugaz One only comes in one weight; the bold comes from an outline of the
# same colour around each letter, and the italic is strengthened with extra slant.
#
# Generates:
#   src/icons/wordmark-ps4.svg, wordmark-ps5.svg   (black, the reference)
#   src/icons/wordmark-ps{4,5}-{black,white}.png   (the ones the app uses)
#   src/icons/wordmark-unknown-{black,white}.png   ("Unknown PlayStation",
#                                                  for when the console type
#                                                  is not known)
#   and, with --sheet <file.png>, the 2×2 presentation sheet with the
#   normal and narrow variants.
#
# Needs fontTools (pip install fonttools) and rsvg-convert
# (apt install librsvg2-bin).
import math
import pathlib
import subprocess
import sys

from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

REPO = pathlib.Path(__file__).resolve().parent.parent
ICONS = REPO / "src" / "icons"
FONTE = TTFont(REPO / "third-party" / "fugaz-one" / "FugazOne-Regular.ttf")
GLIFOS = FONTE.getGlyphSet()
MAPA = FONTE.getBestCmap()

NEGRITO = 16           # thickness of the extra outline, in font units
INCLINACAO = 6.0       # degrees beyond the font's own italic
TRACKING = 60          # letter spacing, already accounting for the bold
MARGEM = 40

ENTRELINHA = 820       # between the two lines of "Unknown PlayStation"


def wordmark(numero, estreita=False, cor="#000000"):
    """Devolve (svg, largura, altura) de "PS4", "PS5" ou, com numero=None,
    "Unknown PlayStation" em duas linhas (para quando não se sabe o tipo da
    consola)."""
    linhas = ["PS" + str(numero)] if numero else ["Unknown", "PlayStation"]
    sx = 0.84 if estreita else 1.0
    inclina = math.tan(math.radians(INCLINACAO))
    caminho = SVGPathPen(GLIFOS)
    limites = BoundsPen(GLIFOS)
    for n, texto in enumerate(linhas):
        # Each line centred: first the width, then the drawing.
        nomes = [MAPA[ord(letra)] for letra in texto]
        total = sum(GLIFOS[nome].width + TRACKING for nome in nomes) * sx
        x, base = -total / 2, n * ENTRELINHA
        for nome in nomes:
            # The font has y pointing up and SVG down; the middle term
            # slants the top to the right.
            matriz = (sx, 0, inclina, -1, x, base)
            GLIFOS[nome].draw(TransformPen(caminho, matriz))
            GLIFOS[nome].draw(TransformPen(limites, matriz))
            x += (GLIFOS[nome].width + TRACKING) * sx
    x0, y0, x1, y1 = limites.bounds
    pintura = (f'fill="{cor}" stroke="{cor}" stroke-width="{NEGRITO}" '
               f'stroke-linejoin="miter" stroke-miterlimit="4"')
    folga = MARGEM + NEGRITO / 2
    largura, altura = x1 - x0 + 2 * folga, y1 - y0 + 2 * folga
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" '
           f'viewBox="{x0 - folga:.1f} {y0 - folga:.1f} {largura:.1f} {altura:.1f}" '
           f'width="{largura:.0f}" height="{altura:.0f}">'
           f'<path {pintura} d="{caminho.getCommands()}"/></svg>')
    return svg, largura, altura


def png(svg, destino, largura):
    subprocess.run(["rsvg-convert", "-w", str(largura), "-o", str(destino)],
                   input=svg.encode(), check=True)


def folha(destino):
    """A folha de apresentação: 2×2, preto sobre branco, muito espaço."""
    celula_w, celula_h, margem = 900, 420, 120
    pecas = []
    for linha, estreita in enumerate([False, True]):
        for coluna, numero in enumerate([4, 5]):
            svg, w, h = wordmark(numero, estreita)
            escala = 420 / w
            x = margem + coluna * celula_w + (celula_w - w * escala) / 2
            y = margem + linha * celula_h + (celula_h - h * escala) / 2 - 24
            # Each wordmark goes in as a nested <svg>, with its own viewBox.
            pecas.append(svg.replace(
                f'width="{w:.0f}" height="{h:.0f}"',
                f'x="{x:.1f}" y="{y:.1f}" width="{w * escala:.1f}" height="{h * escala:.1f}"', 1))
            legenda = f"PS{numero} — {'Fugaz One estreita' if estreita else 'Fugaz One'}"
            pecas.append(f'<text x="{margem + coluna * celula_w + celula_w / 2}" '
                         f'y="{margem + linha * celula_h + celula_h - 70}" text-anchor="middle" '
                         f'font-family="sans-serif" font-size="20" fill="#9AA0A6">{legenda}</text>')
    total_w, total_h = 2 * margem + 2 * celula_w, 2 * margem + 2 * celula_h
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{total_w}" height="{total_h}">'
           f'<rect width="100%" height="100%" fill="#FFFFFF"/>{"".join(pecas)}</svg>')
    png(svg, destino, total_w)


def main():
    largura_ps4, altura_ps4 = wordmark(4)[1:]
    for numero in (4, 5, None):
        nome = f"ps{numero}" if numero else "desconhecida"
        svg, w, h = wordmark(numero)
        # About three times the width it appears at in the app, for scaled
        # displays. The unknown one's text, on two lines, goes at the same
        # height as the others.
        largura = round(720 * w / largura_ps4 * (altura_ps4 / h if not numero else 1))
        if numero:
            (ICONS / f"wordmark-{nome}.svg").write_text(svg + "\n")
        png(wordmark(numero, cor="#000000")[0], ICONS / f"wordmark-{nome}-preto.png", largura)
        png(wordmark(numero, cor="#FFFFFF")[0], ICONS / f"wordmark-{nome}-branco.png", largura)
        print(ICONS / f"wordmark-{nome}")
    if len(sys.argv) > 2 and sys.argv[1] == "--folha":
        folha(sys.argv[2])
        print(sys.argv[2])


if __name__ == "__main__":
    main()
