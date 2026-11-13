// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The window background: the room the glass floats in.
//
// A soft, out-of-focus environment — a few broad pools of warm light
// (champagne, amber) and one cool one, over a graphite or stone gradient,
// darkened a little towards the edges. It is what makes the panes read as
// glass: a translucent surface only looks like glass when what is behind it
// varies. It never carries detail of its own, so it never competes with what
// is on top.
//
// Everything is painted on a Canvas, once, and again only when the window or
// the theme changes (QML's RadialGradient lives in Qt5Compat, which is not
// guaranteed). Painted already blurred: there is no live blur to pay for.
import QtQuick

Item {
    id: room

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.background }
            GradientStop { position: 1.0; color: Theme.backgroundDeep }
        }
    }

    Canvas {
        id: light
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        readonly property string themeName: Theme.name
        readonly property color warm: Theme.brass
        readonly property color cool: Theme.accent
        onThemeNameChanged: requestPaint()
        onWarmChanged: requestPaint()
        onCoolChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        // A pool of light: a radial gradient fading to nothing.
        function pool(ctx, x, y, radius, colour, strength) {
            var g = ctx.createRadialGradient(x, y, 0, x, y, radius)
            g.addColorStop(0.0, Qt.rgba(colour.r, colour.g, colour.b, strength))
            g.addColorStop(0.45, Qt.rgba(colour.r, colour.g, colour.b, strength * 0.45))
            g.addColorStop(1.0, Qt.rgba(colour.r, colour.g, colour.b, 0))
            ctx.fillStyle = g
            ctx.fillRect(x - radius, y - radius, radius * 2, radius * 2)
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var w = width, h = height
            var big = Math.max(w, h)
            var dayLight = Theme.light
            var amber = Qt.color("#E0A458")
            var white = Qt.color("#FFFFFF")
            // Strength: light enough to read as a room, never as a picture.
            var k = dayLight ? 0.65 : Theme.glass ? 0.42 : 0.30

            // Warm light from the upper left, as from a window.
            pool(ctx, w * 0.12, h * 0.05, big * 0.62, dayLight ? white : light.warm, k)
            // An amber lamp low on the right.
            pool(ctx, w * 0.92, h * 0.88, big * 0.48, amber, k * (dayLight ? 0.30 : 0.75))
            // A cool reflection across the middle.
            pool(ctx, w * 0.62, h * 0.30, big * 0.40, light.cool, k * (dayLight ? 0.25 : 0.55))
            // A soft glow under where the content sits.
            pool(ctx, w * 0.45, h * 0.70, big * 0.55, dayLight ? white : light.warm, k * 0.45)

            // Darker towards the edges: the eye stays in the middle.
            var v = ctx.createRadialGradient(w / 2, h / 2, Math.min(w, h) * 0.35, w / 2, h / 2, big * 0.78)
            v.addColorStop(0, "rgba(0,0,0,0)")
            v.addColorStop(1, dayLight ? "rgba(60,50,40,0.10)" : "rgba(0,0,0,0.38)")
            ctx.fillStyle = v
            ctx.fillRect(0, 0, w, h)
        }
    }
}
