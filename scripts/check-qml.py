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

RAIZ = pathlib.Path(__file__).resolve().parent.parent
TIPOS = ("Dialog", "Popup", "Menu")


def blocos(texto, tipo):
    """Devolve (linha, corpo) de cada bloco `Tipo {` … `}` equilibrado."""
    for m in re.finditer(r"(?<![A-Za-z_.])" + tipo + r"\s*\{", texto):
        # Um "Overlay.modal: Rectangle" ou um "property var x: Dialog"
        # não abrem um bloco destes; o regex acima já os exclui pelo ponto.
        inicio = m.end() - 1
        nivel = 0
        for i in range(inicio, len(texto)):
            if texto[i] == "{":
                nivel += 1
            elif texto[i] == "}":
                nivel -= 1
                if nivel == 0:
                    yield texto[: m.start()].count("\n") + 1, texto[inicio : i + 1]
                    break


def sem_aninhados(corpo):
    """O corpo sem os blocos de Dialog/Popup/Menu lá dentro.

    Sem isto, um diálogo sem background passaria por ter um filho que o
    tem — que é exactamente o caso que interessa apanhar.
    """
    for tipo in TIPOS:
        for _, interno in list(blocos(corpo[1:], tipo)):
            corpo = corpo.replace(interno, "")
    return corpo


def main():
    problemas = []
    for ficheiro in sorted((RAIZ / "qml").rglob("*.qml")):
        texto = ficheiro.read_text(encoding="utf-8")
        for tipo in TIPOS:
            for linha, corpo in blocos(texto, tipo):
                proprio = sem_aninhados(corpo)
                nome = f"{ficheiro.relative_to(RAIZ)}:{linha} ({tipo})"
                if not re.search(r"^\s*background\s*:", proprio, re.M):
                    problemas.append(
                        f"{nome} não define background — "
                        f"o estilo Basic pinta-o de branco e o texto do tema desaparece"
                    )
                if "Theme.onStage" in proprio:
                    problemas.append(
                        f"{nome} usa Theme.onStage, que é branco e serve para "
                        f"escrever por cima do vídeo, não dentro de um diálogo"
                    )

    if problemas:
        print("QML: encontrei superfícies que iam sair brancas:\n", file=sys.stderr)
        for p in problemas:
            print(f"  {p}", file=sys.stderr)
        return 1

    print("QML: todos os diálogos, popups e menus têm fundo próprio.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
