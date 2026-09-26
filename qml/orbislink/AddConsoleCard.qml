// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A caixa ao lado das consolas para juntar mais uma à lista: sem fundo, com
// uma borda tracejada — é um lugar vazio à espera de uma consola.
import QtQuick

Item {
    id: caixa

    signal adicionar()

    // Acompanha a escala das caixas das consolas (desenhadas a 330 de altura).
    readonly property real escala: height / 330
    implicitWidth: 250
    implicitHeight: 330

    scale: area.pressed ? Theme.pressScale : (area.containsMouse ? Theme.hoverScale : 1.0)
    Behavior on scale { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.fill: parent
        radius: 30 * caixa.escala
        color: area.containsMouse ? Qt.rgba(Theme.cardGlow.r, Theme.cardGlow.g, Theme.cardGlow.b,
                                            Theme.claro ? 0.06 : 0.08)
                                  : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.cardEase } }
    }

    // A borda tracejada, desenhada traço a traço ao longo do contorno
    // arredondado (o Canvas não tem tracejado em todas as versões do Qt).
    Canvas {
        id: borda
        anchors.fill: parent
        readonly property color cor: area.containsMouse
            ? Theme.cardGlow
            : Qt.rgba(Theme.cardTextMuted.r, Theme.cardTextMuted.g, Theme.cardTextMuted.b, 0.6)
        onCorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var m = 1.5, w = width - 2 * m, h = height - 2 * m
            var r = Math.min(30 * caixa.escala, w / 2, h / 2)
            // O contorno como uma sequência de pontos, a cada 1 px.
            var pontos = []
            function reta(x0, y0, x1, y1) {
                var n = Math.max(1, Math.round(Math.hypot(x1 - x0, y1 - y0)))
                for (var i = 0; i < n; ++i)
                    pontos.push([x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n])
            }
            function arco(cx, cy, a0, a1) {
                var n = Math.max(2, Math.round(r * Math.abs(a1 - a0)))
                for (var i = 0; i < n; ++i) {
                    var a = a0 + (a1 - a0) * i / n
                    pontos.push([cx + r * Math.cos(a), cy + r * Math.sin(a)])
                }
            }
            reta(m + r, m, m + w - r, m)
            arco(m + w - r, m + r, -Math.PI / 2, 0)
            reta(m + w, m + r, m + w, m + h - r)
            arco(m + w - r, m + h - r, 0, Math.PI / 2)
            reta(m + w - r, m + h, m + r, m + h)
            arco(m + r, m + h - r, Math.PI / 2, Math.PI)
            reta(m, m + h - r, m, m + r)
            arco(m + r, m + r, Math.PI, 3 * Math.PI / 2)

            ctx.strokeStyle = cor
            ctx.lineWidth = 1.5
            ctx.lineCap = "round"
            var traco = 7, espaco = 6, ciclo = traco + espaco
            ctx.beginPath()
            for (var j = 0; j + 1 < pontos.length; ++j) {
                if (j % ciclo < traco) {
                    ctx.moveTo(pontos[j][0], pontos[j][1])
                    ctx.lineTo(pontos[j + 1][0], pontos[j + 1][1])
                }
            }
            ctx.stroke()
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 20 * caixa.escala

        // O "+" dentro de um círculo fino.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 84 * caixa.escala
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: area.containsMouse ? Theme.cardGlow : Theme.cardTextMuted
            Behavior on border.color { ColorAnimation { duration: Theme.cardEase } }
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.4; height: 2; radius: 1
                color: parent.border.color
            }
            Rectangle {
                anchors.centerIn: parent
                width: 2; height: parent.height * 0.4; radius: 1
                color: parent.border.color
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: caixa.width - 32
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Adicionar consola")
            color: area.containsMouse ? Theme.cardGlow : Theme.onIdleStage
            font.pixelSize: Math.max(11, Math.round(20 * caixa.escala))
            font.weight: Font.DemiBold
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: caixa.adicionar()
    }
}
