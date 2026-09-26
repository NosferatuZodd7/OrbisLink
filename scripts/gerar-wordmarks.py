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
FONT = TTFont(REPO / "third-party" / "fugaz-one" / "FugazOne-Regular.ttf")
GLYPHS = FONT.getGlyphSet()
MAP = FONT.getBestCmap()

BOLD = 16           # thickness of the extra outline, in font units
SLANT = 6.0       # degrees beyond the font's own italic
TRACKING = 60          # letter spacing, already accounting for the bold
MARGIN = 40

LINE_GAP = 820       # between the two lines of "Unknown PlayStation"


def wordmark(number, narrow=False, tone="#000000"):
    """Devolve (svg, largura, altura) de "PS4", "PS5" ou, com numero=None,
    "Unknown PlayStation" em duas linhas (para quando não se sabe o tipo da
    consola)."""
    lines = ["PS" + str(number)] if number else ["Unknown", "PlayStation"]
    sx = 0.84 if narrow else 1.0
    slant = math.tan(math.radians(SLANT))
    path = SVGPathPen(GLYPHS)
    limits = BoundsPen(GLYPHS)
    for n, message in enumerate(lines):
        # Each line centred: first the width, then the drawing.
        names = [MAP[ord(letter)] for letter in message]
        total = sum(GLYPHS[name].width + TRACKING for name in names) * sx
        x, base = -total / 2, n * LINE_GAP
        for name in names:
            # The font has y pointing up and SVG down; the middle term
            # slants the top to the right.
            matrix = (sx, 0, slant, -1, x, base)
            GLYPHS[name].draw(TransformPen(path, matrix))
            GLYPHS[name].draw(TransformPen(limits, matrix))
            x += (GLYPHS[name].width + TRACKING) * sx
    x0, y0, x1, y1 = limits.bounds
    paint = (f'fill="{tone}" stroke="{tone}" stroke-width="{BOLD}" '
               f'stroke-linejoin="miter" stroke-miterlimit="4"')
    slack = MARGIN + BOLD / 2
    span, height = x1 - x0 + 2 * slack, y1 - y0 + 2 * slack
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" '
           f'viewBox="{x0 - slack:.1f} {y0 - slack:.1f} {span:.1f} {height:.1f}" '
           f'width="{span:.0f}" height="{height:.0f}">'
           f'<path {paint} d="{path.getCommands()}"/></svg>')
    return svg, span, height


def png(svg, destination, span):
    subprocess.run(["rsvg-convert", "-w", str(span), "-o", str(destination)],
                   input=svg.encode(), check=True)


def sheet(destination):
    """A folha de apresentação: 2×2, preto sobre branco, muito espaço."""
    cell_w, cell_h, margin = 900, 420, 120
    pieces = []
    for line, narrow in enumerate([False, True]):
        for column, number in enumerate([4, 5]):
            svg, w, h = wordmark(number, narrow)
            scaleFactor = 420 / w
            x = margin + column * cell_w + (cell_w - w * scaleFactor) / 2
            y = margin + line * cell_h + (cell_h - h * scaleFactor) / 2 - 24
            # Each wordmark goes in as a nested <svg>, with its own viewBox.
            pieces.append(svg.replace(
                f'width="{w:.0f}" height="{h:.0f}"',
                f'x="{x:.1f}" y="{y:.1f}" width="{w * scaleFactor:.1f}" height="{h * scaleFactor:.1f}"', 1))
            caption = f"PS{number} — {'Fugaz One narrow' if narrow else 'Fugaz One'}"
            pieces.append(f'<text x="{margin + column * cell_w + cell_w / 2}" '
                         f'y="{margin + line * cell_h + cell_h - 70}" text-anchor="middle" '
                         f'font-family="sans-serif" font-size="20" fill="#9AA0A6">{caption}</text>')
    total_w, total_h = 2 * margin + 2 * cell_w, 2 * margin + 2 * cell_h
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{total_w}" height="{total_h}">'
           f'<rect width="100%" height="100%" fill="#FFFFFF"/>{"".join(pieces)}</svg>')
    png(svg, destination, total_w)


def main():
    ps4_width, ps4_height = wordmark(4)[1:]
    for number in (4, 5, None):
        name = f"ps{number}" if number else "desconhecida"
        svg, w, h = wordmark(number)
        # About three times the width it appears at in the app, for scaled
        # displays. The unknown one's text, on two lines, goes at the same
        # height as the others.
        span = round(720 * w / ps4_width * (ps4_height / h if not number else 1))
        if number:
            (ICONS / f"wordmark-{name}.svg").write_text(svg + "\n")
        png(wordmark(number, tone="#000000")[0], ICONS / f"wordmark-{name}-preto.png", span)
        png(wordmark(number, tone="#FFFFFF")[0], ICONS / f"wordmark-{name}-branco.png", span)
        print(ICONS / f"wordmark-{name}")
    if len(sys.argv) > 2 and sys.argv[1] == "--folha":
        sheet(sys.argv[2])
        print(sys.argv[2])


if __name__ == "__main__":
    main()
