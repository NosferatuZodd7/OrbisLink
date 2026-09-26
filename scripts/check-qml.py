#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Apanha a classe de erro que já apareceu três vezes nesta aplicação.

Um Dialog, Popup ou Menu do QtQuick.Controls.Basic sem "background" próprio
é desenhado a BRANCO. Como o resto da aplicação é escura, o texto por cima
leva cores claras do tema — e o resultado é uma janela branca com o texto
invisível. Aconteceu no diálogo de apagar, no mapa do teclado e no pedido
do PIN da consola, e das três vezes só se descobriu com alguém a usar a
aplicação e a mandar uma fotografia.

Não se apanha com testes: o QML carrega, não há erro nenhum, e a janela
abre. Apanha-se lendo o ficheiro, que é o que isto faz.

Também recusa Theme.onStage dentro de um diálogo: essa cor é branca de
propósito, para escrever por cima do vídeo, e não tem nada que fazer sobre
uma superfície de vidro.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
KINDS = ("Dialog", "Popup", "Menu")


def blocks(message, kind):
    """Devolve (linha, corpo) de cada bloco `Tipo {` … `}` equilibrado."""
    for m in re.finditer(r"(?<![A-Za-z_.])" + kind + r"\s*\{", message):
        # An "Overlay.modal: Rectangle" or a "property var x: Dialog"
        # does not open one of these blocks; the regex above already excludes them by the dot.
        start = m.end() - 1
        level = 0
        for i in range(start, len(message)):
            if message[i] == "{":
                level += 1
            elif message[i] == "}":
                level -= 1
                if level == 0:
                    yield message[: m.start()].count("\n") + 1, message[start : i + 1]
                    break


def no_nesting(body):
    """O corpo sem os blocos de Dialog/Popup/Menu lá dentro.

    Sem isto, um diálogo sem background passaria por ter um filho que o
    tem — que é exactamente o caso que interessa apanhar.
    """
    for kind in KINDS:
        for _, inner in list(blocks(body[1:], kind)):
            body = body.replace(inner, "")
    return body


def main():
    problems = []
    for file in sorted((ROOT / "qml").rglob("*.qml")):
        message = file.read_text(encoding="utf-8")
        for kind in KINDS:
            for line, body in blocks(message, kind):
                own = no_nesting(body)
                name = f"{file.relative_to(ROOT)}:{line} ({kind})"
                if not re.search(r"^\s*background\s*:", own, re.M):
                    problems.append(
                        f"{name} não define background — "
                        f"o estilo Basic pinta-o de branco e o texto do tema desaparece"
                    )
                if "Theme.onStage" in own:
                    problems.append(
                        f"{name} usa Theme.onStage, que é branco e serve para "
                        f"escrever por cima do vídeo, não dentro de um diálogo"
                    )

    if problems:
        print("QML: encontrei superfícies que iam sair brancas:\n", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1

    print("QML: todos os diálogos, popups e menus têm fundo próprio.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
