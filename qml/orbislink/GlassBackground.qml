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
    id: backdrop

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
        renderStrategy: Canvas.Cooperative

        // Repaints when the theme changes; it needs nothing else, it is static.
        readonly property color toneA: Theme.accent
        readonly property string themeName: Theme.name
        onToneAChanged: requestPaint()
        onThemeNameChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        function halo(ctx, x, y, radius, tone, force) {
            var g = ctx.createRadialGradient(x, y, 0, x, y, radius)
            g.addColorStop(0, Qt.rgba(tone.r, tone.g, tone.b, force))
            g.addColorStop(0.55, Qt.rgba(tone.r, tone.g, tone.b, force * 0.35))
            g.addColorStop(1, Qt.rgba(tone.r, tone.g, tone.b, 0))
            ctx.fillStyle = g
            ctx.fillRect(x - radius, y - radius, radius * 2, radius * 2)
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var r = Math.max(width, height)
            if (Theme.glass) {
                // The "wallpaper" the glass is laid over: big blurred shapes of
                // light, blue and a little violet and cyan.
                halo(ctx, width * 0.15, height * 0.15, r * 0.60, toneA, 0.55)
                halo(ctx, width * 0.85, height * 0.25, r * 0.45, Qt.rgba(0.47, 0.36, 1, 1), 0.30)
                halo(ctx, width * 0.55, height * 0.95, r * 0.55, Qt.rgba(0.13, 0.75, 0.95, 1), 0.28)
                halo(ctx, width * 0.95, height * 0.90, r * 0.35, toneA, 0.35)
            } else if (Theme.light) {
                halo(ctx, width * 0.15, height * 0.05, r * 0.55, toneA, 0.07)
                halo(ctx, width * 0.90, height * 0.85, r * 0.45, toneA, 0.04)
            } else {
                // Barely there: a soft blue glow from the top left.
                halo(ctx, width * 0.18, height * 0.08, r * 0.60, toneA, 0.10)
                halo(ctx, width * 0.85, height * 0.90, r * 0.45, toneA, 0.05)
            }
        }
    }
}
