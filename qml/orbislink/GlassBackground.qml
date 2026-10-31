// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The window background: a plate of the Celestial Archive.
//
// On the light theme, aged paper: a warm vignette and a faint grain, as on a
// printed sheet. On the dark ones, the observatory's night: a few still stars
// and soft halos. On both, engraved orbits drawn in thin ink in one corner —
// the ringed planet's paths, never competing with what is on top.
//
// The halos are not only decoration: a translucent surface (the glass theme)
// reads as glass only when what is behind it varies.
//
// Everything is painted on a Canvas, once, and again only when the window or
// the theme changes (QML's RadialGradient lives in Qt5Compat, which is not
// guaranteed).
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
        id: plate
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        readonly property color toneA: Theme.accent
        readonly property color inkTone: Theme.light ? Theme.ink : Theme.text
        readonly property string themeName: Theme.name
        onToneAChanged: requestPaint()
        onInkToneChanged: requestPaint()
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

        // The same sky every time: a small seeded generator, not Math.random.
        function seeded(seed) {
            var s = seed
            return function () {
                s = (s * 16807) % 2147483647
                return (s - 1) / 2147483646
            }
        }

        // A four-pointed star, as engraved on the logo.
        function sparkle(ctx, x, y, size) {
            ctx.beginPath()
            ctx.moveTo(x, y - size)
            ctx.quadraticCurveTo(x, y, x + size, y)
            ctx.quadraticCurveTo(x, y, x, y + size)
            ctx.quadraticCurveTo(x, y, x - size, y)
            ctx.quadraticCurveTo(x, y, x, y - size)
            ctx.fill()
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var r = Math.max(width, height)
            var ink = inkTone

            if (Theme.light) {
                // The sheet's edges, a little darker than its middle.
                halo(ctx, width * 0.5, height * 0.45, r * 0.75, Qt.rgba(1, 0.98, 0.92, 1), 0.35)
                halo(ctx, width * 0.05, height * 0.98, r * 0.40, Qt.rgba(0.55, 0.42, 0.22, 1), 0.10)
                halo(ctx, width * 0.98, height * 0.02, r * 0.35, Qt.rgba(0.55, 0.42, 0.22, 1), 0.07)
            } else if (Theme.glass) {
                halo(ctx, width * 0.15, height * 0.15, r * 0.60, toneA, 0.40)
                halo(ctx, width * 0.85, height * 0.25, r * 0.45, Qt.rgba(0.26, 0.44, 0.54, 1), 0.35)
                halo(ctx, width * 0.55, height * 0.95, r * 0.55, Qt.rgba(0.70, 0.54, 0.29, 1), 0.14)
                halo(ctx, width * 0.95, height * 0.90, r * 0.35, toneA, 0.28)
            } else {
                halo(ctx, width * 0.18, height * 0.08, r * 0.60, toneA, 0.10)
                halo(ctx, width * 0.85, height * 0.90, r * 0.45, Qt.rgba(0.70, 0.54, 0.29, 1), 0.05)
            }

            // Engraved orbits, around a planet just off the bottom right.
            var cx = width * 0.96, cy = height * 1.02
            ctx.save()
            ctx.translate(cx, cy)
            ctx.rotate(-0.32)
            ctx.strokeStyle = Qt.rgba(ink.r, ink.g, ink.b, Theme.light ? 0.10 : 0.06)
            for (var i = 0; i < 4; ++i) {
                var a = r * (0.30 + i * 0.075), b = a * 0.32
                ctx.lineWidth = i === 1 ? 1.6 : 0.8
                ctx.beginPath()
                ctx.ellipse(-a, -b, a * 2, b * 2)
                ctx.stroke()
            }
            ctx.restore()

            // A few stars: dots, and here and there an engraved sparkle.
            var rand = seeded(17)
            var count = Math.round(width * height / (Theme.light ? 52000 : 26000))
            for (var s = 0; s < count; ++s) {
                var x = rand() * width, y = rand() * height
                var big = rand() > 0.93
                ctx.fillStyle = Qt.rgba(ink.r, ink.g, ink.b, (Theme.light ? 0.10 : 0.16) + rand() * 0.10)
                if (big) {
                    sparkle(ctx, x, y, 4 + rand() * 4)
                } else {
                    ctx.beginPath()
                    ctx.arc(x, y, 0.6 + rand() * 1.0, 0, Math.PI * 2)
                    ctx.fill()
                }
            }
        }
    }

    // The paper's grain, tiled; only the light theme is paper.
    Image {
        anchors.fill: parent
        visible: Theme.light
        source: "qrc:/icons/grain.png"
        fillMode: Image.Tile
        opacity: 0.55
        smooth: false
    }
}
