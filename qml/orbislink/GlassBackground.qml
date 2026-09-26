// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The window background: a deep gradient with soft halos of light.
//
// The halos are not decoration. A translucent surface only reads as glass
// when what is behind it varies — over a flat colour, translucency is
// indistinguishable from opacity. This is where the glass gets its
// colour from.
//
// They are painted on a Canvas because QML's RadialGradient lives in
// Qt5Compat, which is not guaranteed. The Canvas draws once and stays.
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

        // Repaints when the theme changes; it needs nothing else, it is static.
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
