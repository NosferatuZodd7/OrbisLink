// SPDX-License-Identifier: AGPL-3.0-or-later
//
// O mapa do teclado desenhado como um teclado: cada tecla que faz alguma
// coisa mostra a letra dela e, dentro da própria tecla, o botão da
// PlayStation que faz (o P leva o "PS", o V leva o △…). As outras teclas
// ficam apagadas, só para o desenho se reconhecer como um teclado.
//
// O mapa em si vive em src/orbislink/qt/input_map.cpp. Mudar uma tecla lá
// obriga a mudá-la aqui — não há nada que os ligue sozinho.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: mapa

    // O botão por baixo do rato, para o desenho do comando o acender.
    property string destaque: ""
    property string descricao: ""

    readonly property color corCruz: "#7CB2E8"
    readonly property color corCirculo: "#FF6B6B"
    readonly property color corQuadrado: "#E88BD6"
    readonly property color corTriangulo: "#40E0B0"

    // O que cada tecla faz. "alvo" é a parte do comando que se acende;
    // "tipo" diz como se desenha o sinal dentro da tecla.
    readonly property var funcoes: ({
        "Esc":   { sinal: qsTr("sair"), tipo: "aviso", alvo: "", texto: qsTr("Esc — sai do stream") },
        "1":     { sinal: "L1", tipo: "botao", alvo: "l1", texto: qsTr("1 — L1") },
        "2":     { sinal: "L2", tipo: "botao", alvo: "l2", texto: qsTr("2 — L2 (gatilho)") },
        "3":     { sinal: "R1", tipo: "botao", alvo: "r1", texto: qsTr("3 — R1") },
        "4":     { sinal: "L3", tipo: "botao", alvo: "lstick", texto: qsTr("4 — L3 (carregar no analógico esquerdo)") },
        "5":     { sinal: "R2", tipo: "botao", alvo: "r2", texto: qsTr("5 — R2 (gatilho)") },
        "6":     { sinal: "R3", tipo: "botao", alvo: "rstick", texto: qsTr("6 — R3 (carregar no analógico direito)") },
        "⌫":     { sinal: "◯", tipo: "simbolo", cor: corCirculo, alvo: "circle", texto: qsTr("Backspace — Círculo") },
        "Enter": { sinal: "✕", tipo: "simbolo", cor: corCruz, alvo: "cross", texto: qsTr("Enter — Cruz") },
        "C":     { sinal: "▢", tipo: "simbolo", cor: corQuadrado, alvo: "square", texto: qsTr("C — Quadrado") },
        "V":     { sinal: "△", tipo: "simbolo", cor: corTriangulo, alvo: "triangle", texto: qsTr("V — Triângulo") },
        "W":     { sinal: "L↑", tipo: "botao", alvo: "lstick", texto: qsTr("W — analógico esquerdo para cima") },
        "A":     { sinal: "L←", tipo: "botao", alvo: "lstick", texto: qsTr("A — analógico esquerdo para a esquerda") },
        "S":     { sinal: "L↓", tipo: "botao", alvo: "lstick", texto: qsTr("S — analógico esquerdo para baixo") },
        "D":     { sinal: "L→", tipo: "botao", alvo: "lstick", texto: qsTr("D — analógico esquerdo para a direita") },
        "I":     { sinal: "R↑", tipo: "botao", alvo: "rstick", texto: qsTr("I — analógico direito para cima") },
        "J":     { sinal: "R←", tipo: "botao", alvo: "rstick", texto: qsTr("J — analógico direito para a esquerda") },
        "K":     { sinal: "R↓", tipo: "botao", alvo: "rstick", texto: qsTr("K — analógico direito para baixo") },
        "L":     { sinal: "R→", tipo: "botao", alvo: "rstick", texto: qsTr("L — analógico direito para a direita") },
        "O":     { sinal: "OPT", tipo: "botao", alvo: "options", texto: qsTr("O — Options") },
        "F":     { sinal: "SHR", tipo: "botao", alvo: "share", texto: qsTr("F — Share") },
        "T":     { sinal: "PAD", tipo: "botao", alvo: "touchpad", texto: qsTr("T — carregar no touchpad") },
        "P":     { sinal: "PS", tipo: "ps", alvo: "ps", texto: qsTr("P — botão PS") },
        "↑":     { sinal: "✚", tipo: "botao", alvo: "dpad", texto: qsTr("Seta para cima — cruzeta para cima") },
        "↓":     { sinal: "✚", tipo: "botao", alvo: "dpad", texto: qsTr("Seta para baixo — cruzeta para baixo") },
        "←":     { sinal: "✚", tipo: "botao", alvo: "dpad", texto: qsTr("Seta para a esquerda — cruzeta para a esquerda") },
        "→":     { sinal: "✚", tipo: "botao", alvo: "dpad", texto: qsTr("Seta para a direita — cruzeta para a direita") }
    })

    // As teclas, em unidades de uma tecla: [rótulo, x, linha, largura].
    // Um teclado a sério, reduzido às quatro filas que interessam e às setas.
    readonly property var teclas: [
        ["Esc", 0, 0, 1],
        ["1", 1.5, 0, 1], ["2", 2.5, 0, 1], ["3", 3.5, 0, 1], ["4", 4.5, 0, 1],
        ["5", 5.5, 0, 1], ["6", 6.5, 0, 1], ["7", 7.5, 0, 1], ["8", 8.5, 0, 1],
        ["9", 9.5, 0, 1], ["0", 10.5, 0, 1], ["⌫", 11.5, 0, 2],
        ["Tab", 0, 1, 1.5],
        ["Q", 1.5, 1, 1], ["W", 2.5, 1, 1], ["E", 3.5, 1, 1], ["R", 4.5, 1, 1],
        ["T", 5.5, 1, 1], ["Y", 6.5, 1, 1], ["U", 7.5, 1, 1], ["I", 8.5, 1, 1],
        ["O", 9.5, 1, 1], ["P", 10.5, 1, 1],
        ["Caps", 0, 2, 1.75],
        ["A", 1.75, 2, 1], ["S", 2.75, 2, 1], ["D", 3.75, 2, 1], ["F", 4.75, 2, 1],
        ["G", 5.75, 2, 1], ["H", 6.75, 2, 1], ["J", 7.75, 2, 1], ["K", 8.75, 2, 1],
        ["L", 9.75, 2, 1], ["Enter", 10.75, 2, 2.75],
        ["Shift", 0, 3, 2.25],
        ["Z", 2.25, 3, 1], ["X", 3.25, 3, 1], ["C", 4.25, 3, 1], ["V", 5.25, 3, 1],
        ["B", 6.25, 3, 1], ["N", 7.25, 3, 1], ["M", 8.25, 3, 1],
        ["↑", 15, 2, 1],
        ["←", 14, 3, 1], ["↓", 15, 3, 1], ["→", 16, 3, 1]
    ]

    // 17 unidades de largura; cada unidade encolhe se a janela for estreita.
    readonly property real unidade: Math.min(40, width / 17)
    readonly property real folga: Math.max(3, unidade * 0.1)

    implicitWidth: 17 * 40
    implicitHeight: 4 * unidade

    Repeater {
        model: mapa.teclas

        Rectangle {
            id: tecla
            readonly property string rotulo: modelData[0]
            readonly property var funcao: mapa.funcoes[rotulo]
            readonly property bool util: funcao !== undefined
            readonly property bool sobre: area.containsMouse && util

            x: modelData[1] * mapa.unidade
            y: modelData[2] * mapa.unidade
            width: modelData[3] * mapa.unidade - mapa.folga
            height: mapa.unidade - mapa.folga
            radius: Math.round(mapa.unidade * 0.2)

            // No claro o painel é branco, como o diálogo: as teclas úteis
            // levam um cinzento-gelo e uma aresta escura, senão seriam as
            // apagadas a ver-se.
            color: sobre ? Theme.accentFill
                 : !util ? "transparent"
                 : Theme.claro ? "#EEF1F6"
                 : Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b, 0.9)
            border.width: 1
            border.color: sobre ? Theme.accent
                        : !util ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.10)
                        : Theme.claro ? Qt.rgba(0, 0, 0, 0.22)
                        : Theme.glassEdge
            scale: sobre ? 1.06 : 1.0
            z: sobre ? 1 : 0
            Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
            Behavior on color { ColorAnimation { duration: Theme.fast } }

            // A tecla como está impressa no teclado: em cima, à esquerda.
            Text {
                x: 5
                y: 3
                text: tecla.rotulo
                color: tecla.util ? Theme.text
                                  : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.28)
                font.pixelSize: Math.max(8, Math.round(mapa.unidade * (tecla.rotulo.length > 1 ? 0.24 : 0.3)))
                font.bold: tecla.util
            }

            // E o botão da PlayStation, dentro da mesma tecla, em baixo à
            // direita. É isto que se procura quando se olha para o mapa.
            Rectangle {
                visible: tecla.util
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 3
                readonly property string tipo: tecla.util ? tecla.funcao.tipo : ""
                width: Math.max(height, sinal.implicitWidth + 6)
                height: Math.round(mapa.unidade * 0.42)
                radius: height / 2
                color: tipo === "ps" ? Theme.accent
                     : tipo === "aviso" ? Qt.rgba(Theme.warn.r, Theme.warn.g, Theme.warn.b, 0.20)
                     : tipo === "simbolo" ? "transparent"
                     : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.10)
                Text {
                    id: sinal
                    anchors.centerIn: parent
                    text: tecla.util ? tecla.funcao.sinal : ""
                    color: parent.tipo === "ps" ? "#FFFFFF"
                         : parent.tipo === "simbolo" ? tecla.funcao.cor
                         : parent.tipo === "aviso" ? Theme.warn
                         : Theme.text
                    font.pixelSize: parent.tipo === "simbolo"
                                    ? Math.round(mapa.unidade * 0.36)
                                    : Math.max(7, Math.round(mapa.unidade * 0.22))
                    font.bold: true
                }
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                onContainsMouseChanged: {
                    if (containsMouse && tecla.util) {
                        mapa.destaque = tecla.funcao.alvo
                        mapa.descricao = tecla.funcao.texto
                    } else if (!containsMouse && mapa.descricao === (tecla.util ? tecla.funcao.texto : "")) {
                        mapa.destaque = ""
                        mapa.descricao = ""
                    }
                }
            }
        }
    }
}
