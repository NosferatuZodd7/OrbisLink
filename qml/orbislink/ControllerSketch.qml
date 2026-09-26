// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The PS4 controller of the key map. The "highlighted" part lights up —
// that is what links a key on the keyboard drawing to the button it presses.
//
// The image (src/icons/dualshock.svg, drawn for OrbisLink) has the
// buttons, touchpad, PS button and sticks cut out
// (transparent). So the highlight is painted UNDER it: the colour
// fills the cut-out and the controller outline stays sharp on top, without
// needing masks or QtQuick.Effects (which the screenshots' Qt 6.4
// lacks). The △ ◯ ✕ ▢ symbols are cut-outs too, and that is how they
// appear in their colours.
//
// Two images: the light body on the dark themes (a black controller on a
// black background would disappear) and the dark body on the light theme.
//
// Positions are in SVG units (1590×988) and match the ones in the file:
// changing one in one place means changing it in the other.
import QtQuick

Item {
    id: controller

    // "cross", "circle", "square", "triangle", "l1", "l2", "r1", "r2",
    // "lstick", "rstick", "dpad", "options", "share", "touchpad", "ps" or "".
    property string highlight: ""

    readonly property color crossColor: "#7CB2E8"
    readonly property color circleColor: "#FF6B6B"
    readonly property color squareColor: "#E88BD6"
    readonly property color triangleColor: "#40E0B0"

    // The image and, on top of it, the shoulder strip (L1/L2/R1/R2 cannot
    // be seen from the front, so they go as labels).
    readonly property real imageWidth: 1590
    readonly property real imageHeight: 988
    readonly property real shoulderHeight: 210
    readonly property real totalHeight: imageHeight + shoulderHeight

    readonly property real scaleFactor: Math.min(width / imageWidth, height / totalHeight)
    readonly property real originX: (width - imageWidth * scaleFactor) / 2
    readonly property real originY: (height - totalHeight * scaleFactor) / 2

    implicitWidth: 240
    implicitHeight: Math.round(240 * totalHeight / imageWidth)

    // The cut-out zones of the image, and their colour at rest.
    // The right-hand buttons and the PS button are circles; the touchpad and
    // Share/Options are rounded rectangles.
    readonly property var zones: ({
        "triangle": { c: [1286, 163, 50], tone: triangleColor },
        "circle":   { c: [1404, 278, 50], tone: circleColor },
        "cross":    { c: [1286, 393, 50], tone: crossColor },
        "square":   { c: [1171, 278, 50], tone: squareColor },
        "ps":       { c: [795, 502, 44] },
        "lstick":   { c: [541, 492, 148] },
        "rstick":   { c: [1045, 492, 148] },
        "dpad":     { c: [299, 278, 186] },
        "touchpad": { r: [533, 70, 524, 256, 30] },
        "share":    { r: [438, 80, 60, 86, 22] },
        "options":  { r: [1087, 80, 60, 86, 22] }
    })

    function redraw() { bottom.requestPaint(); top.requestPaint() }
    onHighlightChanged: redraw()
    onWidthChanged: redraw()
    onHeightChanged: redraw()
    Connections {
        target: Theme
        function onAccentChanged() { controller.redraw() }
        function onNameChanged() { controller.redraw() }
    }

    function prepareContext(ctx) {
        ctx.reset()
        ctx.translate(originX, originY)
        ctx.scale(scaleFactor, scaleFactor)
    }
    function path(ctx, zoneName) {
        ctx.beginPath()
        if (zoneName.c)
            ctx.arc(zoneName.c[0], zoneName.c[1] + shoulderHeight, zoneName.c[2], 0, Math.PI * 2)
        else
            ctx.roundedRect(zoneName.r[0], zoneName.r[1] + shoulderHeight, zoneName.r[2], zoneName.r[3],
                            zoneName.r[4], zoneName.r[4])
    }

    // Underneath: the symbol colours, and the lit cut-out.
    Canvas {
        id: bottom
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            controller.prepareContext(ctx)
            for (var name in controller.zones) {
                var zoneName = controller.zones[name]
                var lit = controller.highlight === name
                if (!lit && !zoneName.tone)
                    continue
                controller.path(ctx, zoneName)
                ctx.fillStyle = lit ? Theme.accent : zoneName.tone
                ctx.fill()
            }
        }
    }

    Image {
        x: controller.originX
        y: controller.originY + controller.shoulderHeight * controller.scaleFactor
        width: controller.imageWidth * controller.scaleFactor
        height: controller.imageHeight * controller.scaleFactor
        source: Theme.light ? "qrc:/icons/dualshock-dark.png" : "qrc:/icons/dualshock-light.png"
        sourceSize.width: 720
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }

    // On top: a ring around what is lit (a small cut-out, like a button
    // symbol, does not catch the eye on its own) and the shoulder
    // labels.
    Canvas {
        id: top
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            controller.prepareContext(ctx)

            var zoneName = controller.zones[controller.highlight]
            if (zoneName && zoneName.c && zoneName.c[2] < 100) {
                ctx.beginPath()
                ctx.arc(zoneName.c[0], zoneName.c[1] + controller.shoulderHeight, zoneName.c[2] + 16, 0,
                        Math.PI * 2)
                ctx.lineWidth = 12
                ctx.strokeStyle = Theme.accent
                ctx.stroke()
            }

            // L2 above L1, as on the controller: the trigger sits behind.
            var shoulders = [
                ["l2", "L2", 215, 0], ["l1", "L1", 185, 100],
                ["r2", "R2", 1195, 0], ["r1", "R1", 1165, 100]
            ]
            var neutral = Theme.light ? "#2B2A29" : "#D4D5D6"
            ctx.font = "bold 62px sans-serif"
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            for (var i = 0; i < shoulders.length; ++i) {
                var o = shoulders[i]
                var lit = controller.highlight === o[0]
                var span = o[0] === "l1" || o[0] === "r1" ? 240 : 180
                ctx.beginPath()
                ctx.roundedRect(o[2], o[3], span, 88, 44, 44)
                if (lit) {
                    ctx.fillStyle = Theme.accent
                    ctx.fill()
                } else {
                    ctx.lineWidth = 7
                    ctx.strokeStyle = neutral
                    ctx.stroke()
                }
                ctx.fillStyle = lit ? "#FFFFFF" : neutral
                ctx.fillText(o[1], o[2] + span / 2, o[3] + 46)
            }
        }
    }
}
