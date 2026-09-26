#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# As wordmarks "PS4" e "PS5" da caixa da consola, desenhadas de raiz: traço
# largo e uniforme, itálico a 13°, cantos vivos e as pontas
# cortadas na diagonal (é o itálico que inclina os cortes a direito).
#
# Gera, a partir da mesma geometria:
#   src/icons/wordmark-ps4.svg, wordmark-ps5.svg      (preto, a referência)
#   src/icons/wordmark-ps{4,5,}-{preto,branco}.png    (os que a app usa; "ps"
#                                                     é só "PS", para o tipo
#                                                     desconhecido)
#   e, com --folha <ficheiro.png>, a folha de apresentação 2×2 com as
#   variantes normal e estreita.
#
# Precisa do rsvg-convert (apt install librsvg2-bin).
import math
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
ICONS = REPO / "src" / "icons"

ALTURA = 100.0        # altura das maiúsculas
TRACO = 17.0          # espessura do traço
FINO = 11.0           # a barra do 4
INCLINACAO = 13.0     # graus, para a direita
MEIO = TRACO / 2


def letra_p(e):
    """P: barra de cima longa, curva aberta e haste direita."""
    m, w = MEIO, 74 * e
    return [(TRACO, f"M {m} {ALTURA} V {m} H {w - 22} "
                    f"C {w - 4} {m} {w} {18} {w} {30} "
                    f"C {w} {44} {w - 6} {54} {w - 24} {54} H {26}")], 74 * e + MEIO


def letra_s(e):
    """S: contínuo, curvas largas, pontas horizontais direitas."""
    m, w, b = MEIO, 74 * e, ALTURA - MEIO
    return [(TRACO, f"M {w + 4} {m} H {26} C {m + 2} {m} {m} {20} {m} {30} "
                    f"C {m} {44} {12} {51} {28} {51} H {w - 24} "
                    f"C {w - 2} {51} {w} {60} {w} {72} C {w} {86} {w - 6} {b} {w - 26} {b} "
                    f"H {m - 4}")], 74 * e + MEIO


def letra_4(e):
    """4: haste e diagonal num traço só, fechadas numa ponta aguda em cima;
    a barra atravessa a haste e sai cortada."""
    w = 82 * e
    haste = w - 22
    return [
        # Em ponta de mitra a ponta subia acima das outras letras; cortada
        # (bevel) fica à altura delas, com o topo em bisel.
        (TRACO, f"M {haste} {ALTURA} V {MEIO} L {MEIO} {68} H {w + 6}",
         'stroke-linejoin="bevel"'),
    ], w + 6


def letra_5(e):
    """5: como o P e o S, mas com a curva de baixo trocada por chanfros a
    45° — a mesma largura de traço, com os cantos partidos."""
    m, w, b = MEIO, 76 * e, ALTURA - MEIO
    return [(TRACO, f"M {w + 2} {m} H {14} L {10} {46} H {w - 12} "
                    f"L {w} {58} V {b - 18} L {w - 18} {b} H {-4}")], w + 2


def wordmark(numero, estreita=False, cor="#000000"):
    """Devolve (svg, largura, altura) de "PS4", "PS5" ou, com numero=None,
    só "PS" (para quando não se sabe o tipo da consola)."""
    e = 0.82 if estreita else 1.0
    espaco = 16 if estreita else 22         # tracking ligeiramente positivo
    letras = [letra_p(e), letra_s(e)]
    if numero == 4:
        letras.append(letra_4(e))
    elif numero == 5:
        letras.append(letra_5(e))
    grupos, x = [], 0.0
    for tracos, largura in letras:
        caminhos = "".join(
            f'<path d="{t[1]}" stroke-width="{t[0]}" {t[2] if len(t) > 2 else ""}/>'
            for t in tracos)
        grupos.append(f'<g transform="translate({x:.2f} 0)">{caminhos}</g>')
        x += largura + espaco
    largura_total = x - espaco
    desvio = math.tan(math.radians(INCLINACAO)) * ALTURA
    margem = TRACO
    largura_svg = largura_total + desvio + 2 * margem
    altura_svg = ALTURA + 2 * margem
    # O itálico: skewX negativo inclina o topo para a direita; a translação
    # põe a base de volta dentro da caixa.
    corpo = (f'<g transform="translate({margem + desvio:.2f} {margem}) '
             f'skewX(-{INCLINACAO})" fill="none" stroke="{cor}" '
             f'stroke-linejoin="miter" stroke-miterlimit="10" stroke-linecap="butt">{"".join(grupos)}</g>')
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {largura_svg:.2f} {altura_svg:.2f}" '
           f'width="{largura_svg:.0f}" height="{altura_svg:.0f}">{corpo}</svg>')
    return svg, largura_svg, altura_svg


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
            interior = svg[svg.index(">") + 1:svg.rindex("</svg>")]
            legenda = f"PS{numero} — {'Italic estreito' if estreita else 'Italic'}"
            pecas.append(f'<g transform="translate({x:.1f} {y:.1f}) scale({escala:.4f})">{interior}</g>')
            pecas.append(f'<text x="{margem + coluna * celula_w + celula_w / 2}" '
                         f'y="{margem + linha * celula_h + celula_h - 70}" text-anchor="middle" '
                         f'font-family="sans-serif" font-size="20" fill="#9AA0A6">{legenda}</text>')
    total_w, total_h = 2 * margem + 2 * celula_w, 2 * margem + 2 * celula_h
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{total_w}" height="{total_h}">'
           f'<rect width="100%" height="100%" fill="#FFFFFF"/>{"".join(pecas)}</svg>')
    png(svg, destino, total_w)


def main():
    for numero in (4, 5, None):
        nome = f"ps{numero}" if numero else "ps"
        svg, w, _ = wordmark(numero)
        if numero:
            (ICONS / f"wordmark-{nome}.svg").write_text(svg + "\n")
        # Umas três vezes a largura a que aparece na app, para ecrãs com
        # ampliação; o "PS" sozinho na mesma escala que os outros.
        largura = round(720 * w / wordmark(4)[1])
        png(wordmark(numero, cor="#000000")[0], ICONS / f"wordmark-{nome}-preto.png", largura)
        png(wordmark(numero, cor="#FFFFFF")[0], ICONS / f"wordmark-{nome}-branco.png", largura)
        print(ICONS / f"wordmark-{nome}-preto.png")
    if len(sys.argv) > 2 and sys.argv[1] == "--folha":
        folha(sys.argv[2])
        print(sys.argv[2])


if __name__ == "__main__":
    main()
