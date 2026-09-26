#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# As wordmarks "PS4" e "PS5" da caixa da consola, escritas em Fugaz One
# (third-party/fugaz-one), e o texto da consola desconhecida em Fredoka
# (third-party/fredoka), ambas sob a SIL Open Font License 1.1. Os contornos das letras
# passam a caminhos SVG, por isso a app não precisa da fonte instalada.
#
# A Fugaz One só existe num peso; o negrito vem de um contorno da mesma cor
# à volta de cada letra, e o itálico é reforçado com uma inclinação extra.
#
# Gera:
#   src/icons/wordmark-ps4.svg, wordmark-ps5.svg      (preto, a referência)
#   src/icons/wordmark-ps{4,5}-{preto,branco}.png     (os que a app usa)
#   src/icons/wordmark-ps-{claro,escuro}.png          ("Unknown PlayStation" em
#                                                     Fredoka, para quando não
#                                                     se sabe o tipo da consola)
#   e, com --folha <ficheiro.png>, a folha de apresentação 2×2 com as
#   variantes normal e estreita.
#
# Precisa do fontTools (pip install fonttools) e do rsvg-convert
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
FREDOKA = TTFont(REPO / "third-party" / "fredoka" / "Fredoka-SemiBold.ttf")

NEGRITO = 16           # espessura do contorno extra, em unidades da fonte
INCLINACAO = 6.0       # graus a mais do que o itálico da própria fonte
TRACKING = 60          # espaço entre letras, já a contar com o negrito
MARGEM = 40

# O "Unknown PlayStation": as letras vão rodando por estas inclinações e
# alturas, como num desenho animado — (graus, deslocamento para cima em
# unidades da fonte).
SALTOS = [(-5, 0), (3, 30), (6, -10), (-3, 20)]

# As cores do "Unknown PlayStation": o cinzento do texto secundário da caixa da consola
# (Theme.cardTextMuted), num tom para cada tema.
DESCONHECIDA = {"escuro": "#A8B3C7", "claro": "#5B6B82"}


def wordmark(numero, estreita=False, cor="#000000"):
    """Devolve (svg, largura, altura) de "PS4", "PS5" ou, com numero=None,
    "Unknown PlayStation" (para quando não se sabe o tipo da consola)."""
    if not numero:
        return desconhecida(cor)
    texto = "PS" + str(numero)
    sx = 0.84 if estreita else 1.0
    inclina = math.tan(math.radians(INCLINACAO))
    caminho = SVGPathPen(GLIFOS)
    limites = BoundsPen(GLIFOS)
    x = 0.0
    for letra in texto:
        nome = MAPA[ord(letra)]
        # A fonte tem o y para cima e o SVG para baixo; o termo do meio
        # inclina o topo para a direita.
        matriz = (sx, 0, inclina, -1, x, 0)
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


def desconhecida(cor):
    """O "Unknown PlayStation" da consola cujo tipo não se sabe: outro
    desenho de propósito, para não passar por uma consola conhecida — letras
    redondas e direitas, em duas linhas, cada letra ligeiramente aos saltos."""
    glifos, mapa = FREDOKA.getGlyphSet(), FREDOKA.getBestCmap()
    caminho, limites = SVGPathPen(glifos), BoundsPen(glifos)
    for linha, texto in enumerate(("Unknown", "PlayStation")):
        # Cada linha centrada: primeiro a largura, depois o desenho.
        total = sum(glifos[mapa[ord(l)]].width + 10 for l in texto) - 10
        x, base = -total / 2, linha * 820
        for n, letra in enumerate(texto):
            glifo = glifos[mapa[ord(letra)]]
            graus, sobe = SALTOS[n % len(SALTOS)]
            c, s = math.cos(math.radians(graus)), math.sin(math.radians(graus))
            centro = glifo.width / 2
            # Roda a letra à volta do meio da sua base, já com o y virado
            # para baixo, e sobe-a.
            matriz = (c, s, s, -c, x + centro * (1 - c) + sobe * s,
                      base - centro * s - sobe * c)
            glifo.draw(TransformPen(caminho, matriz))
            glifo.draw(TransformPen(limites, matriz))
            x += glifo.width + 10
    x0, y0, x1, y1 = limites.bounds
    largura, altura = x1 - x0 + 2 * MARGEM, y1 - y0 + 2 * MARGEM
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" '
           f'viewBox="{x0 - MARGEM:.1f} {y0 - MARGEM:.1f} {largura:.1f} {altura:.1f}" '
           f'width="{largura:.0f}" height="{altura:.0f}">'
           f'<path fill="{cor}" d="{caminho.getCommands()}"/></svg>')
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
            # Cada wordmark entra como um <svg> aninhado, com a sua viewBox.
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
    largura_ps4 = wordmark(4)[1]
    for numero in (4, 5, None):
        nome = f"ps{numero}" if numero else "ps"
        svg, w, _ = wordmark(numero)
        # Umas três vezes a largura a que aparece na app, para ecrãs com
        # ampliação. O texto da desconhecida, com duas linhas, vai à mesma
        # altura que os outros.
        largura = round(720 * w / largura_ps4) if numero else round(
            720 * w / largura_ps4 * wordmark(4)[2] / wordmark(None)[2])
        if numero:
            (ICONS / f"wordmark-{nome}.svg").write_text(svg + "\n")
            png(wordmark(numero, cor="#000000")[0], ICONS / f"wordmark-{nome}-preto.png", largura)
            png(wordmark(numero, cor="#FFFFFF")[0], ICONS / f"wordmark-{nome}-branco.png", largura)
        else:
            for tema, cor in DESCONHECIDA.items():
                png(wordmark(None, cor=cor)[0], ICONS / f"wordmark-ps-{tema}.png", largura)
        print(ICONS / f"wordmark-{nome}")
    if len(sys.argv) > 2 and sys.argv[1] == "--folha":
        folha(sys.argv[2])
        print(sys.argv[2])


if __name__ == "__main__":
    main()
