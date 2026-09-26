// SPDX-License-Identifier: AGPL-3.0-or-later
//
// O fundo da janela: um gradiente profundo com halos de luz suaves.
//
// Os halos não são decoração. Uma superfície translúcida só se lê como
// vidro quando o que está por trás tem variação — sobre uma cor chapada,
// translucidez é indistinguível de opacidade. É daqui que o vidro tira a
// cor.
//
// São pintados num Canvas porque o RadialGradient do QML vive no
// Qt5Compat, que não está garantido. O Canvas desenha uma vez e fica.
import QtQuick

Item {
    id: fundo

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.background }
            GradientStop { position: 1.0; color: Theme.backgroundDeep }
        }
    }

    Canvas {
        id: halos
        anchors.fill: parent
        opacity: Theme.claro ? 0.55 : 0.75
        renderStrategy: Canvas.Cooperative

        // Repinta quando o tema muda; não precisa de mais nada, é estático.
        readonly property color corA: Theme.accent
        readonly property color corB: Theme.ok
        onCorAChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        function halo(ctx, x, y, raio, cor, forca) {
            var g = ctx.createRadialGradient(x, y, 0, x, y, raio)
            g.addColorStop(0, Qt.rgba(cor.r, cor.g, cor.b, forca))
            g.addColorStop(0.55, Qt.rgba(cor.r, cor.g, cor.b, forca * 0.35))
            g.addColorStop(1, Qt.rgba(cor.r, cor.g, cor.b, 0))
            ctx.fillStyle = g
            ctx.fillRect(x - raio, y - raio, raio * 2, raio * 2)
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var r = Math.max(width, height)
            halo(ctx, width * 0.18, height * 0.12, r * 0.55, corA, Theme.claro ? 0.10 : 0.16)
            halo(ctx, width * 0.88, height * 0.78, r * 0.50, corB, Theme.claro ? 0.06 : 0.09)
            halo(ctx, width * 0.62, height * 0.05, r * 0.35, corA, Theme.claro ? 0.05 : 0.07)
        }
    }
}
