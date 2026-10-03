// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The PS4 controller of the key map. The "highlighted" part lights up —
// that is what links a key on the keyboard drawing to the button it presses.
//
// A line drawing, like the app's icons: the outline of the body and each
// part stroked, the △ ◯ ✕ ▢ symbols in their colours, and the lit part
// filled with the accent.
//
// Positions are in the units of src/icons/dualshock.svg (1590×988), the
// drawing it was traced from.
import QtQuick

Item {
    id: controller

    // "cross", "circle", "square", "triangle", "l1", "l2", "r1", "r2",
    // "lstick", "rstick", "dpad", "options", "share", "touchpad", "ps" or "".
    property string highlight: ""
    // Parts pressed right now (same names), lit while held.
    property var lit: []
    // In edit mode the buttons can be clicked: actionClicked gives the
    // action id of the part ("cross", "dpad_up", "l1"…).
    property bool clickable: false
    signal actionClicked(string action)

    function isLit(name) { return name.length > 0 && (highlight === name || lit.indexOf(name) >= 0) }

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
    onLitChanged: redraw()
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

    // The controller as a line drawing, in the style of the icons: the
    // outline, each part stroked, the four symbols in their colours, and
    // the lit part filled with the accent.
    Canvas {
        id: bottom
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            controller.prepareContext(ctx)
            var k = controller.scaleFactor > 0 ? controller.scaleFactor : 1
            var line = Theme.light ? "#475467" : "#C9D2DE"
            var y0 = controller.shoulderHeight
            ctx.lineJoin = "round"
            ctx.lineCap = "round"

            // The lit part first, underneath the strokes.
            for (var name in controller.zones) {
                if (!controller.isLit(name))
                    continue
                controller.path(ctx, controller.zones[name])
                ctx.fillStyle = Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                                        controller.lit.indexOf(name) >= 0 ? 0.7 : 0.45)
                ctx.fill()
            }

            function circle(x, y, r) {
                ctx.beginPath()
                ctx.arc(x, y + y0, r, 0, Math.PI * 2)
                ctx.stroke()
            }
            function box(x, y, w, h, r) {
                ctx.beginPath()
                ctx.roundedRect(x, y + y0, w, h, r, r)
                ctx.stroke()
            }

            ctx.strokeStyle = line
            ctx.lineWidth = 1.8 / k

            // The body.
            ctx.beginPath()
            ctx.moveTo(150, 110 + y0)
            ctx.bezierCurveTo(190, 40 + y0, 250, 10 + y0, 330, 8 + y0)
            ctx.lineTo(420, 8 + y0)
            ctx.bezierCurveTo(440, 30 + y0, 460, 48 + y0, 520, 48 + y0)
            ctx.lineTo(1070, 48 + y0)
            ctx.bezierCurveTo(1130, 48 + y0, 1150, 30 + y0, 1170, 8 + y0)
            ctx.lineTo(1260, 8 + y0)
            ctx.bezierCurveTo(1340, 10 + y0, 1400, 40 + y0, 1440, 110 + y0)
            ctx.bezierCurveTo(1510, 230 + y0, 1540, 420 + y0, 1570, 620 + y0)
            ctx.bezierCurveTo(1596, 800 + y0, 1592, 900 + y0, 1556, 948 + y0)
            ctx.bezierCurveTo(1516, 990 + y0, 1400, 996 + y0, 1320, 958 + y0)
            ctx.bezierCurveTo(1260, 928 + y0, 1230, 860 + y0, 1190, 700 + y0)
            ctx.bezierCurveTo(1180, 660 + y0, 1162, 642 + y0, 1142, 640 + y0)
            ctx.bezierCurveTo(1110, 664 + y0, 1080, 672 + y0, 1040, 672 + y0)
            ctx.lineTo(550, 672 + y0)
            ctx.bezierCurveTo(510, 672 + y0, 480, 664 + y0, 448, 640 + y0)
            ctx.bezierCurveTo(428, 642 + y0, 410, 660 + y0, 400, 700 + y0)
            ctx.bezierCurveTo(360, 860 + y0, 330, 928 + y0, 270, 958 + y0)
            ctx.bezierCurveTo(190, 996 + y0, 74, 990 + y0, 34, 948 + y0)
            ctx.bezierCurveTo(-2, 900 + y0, -6, 800 + y0, 20, 620 + y0)
            ctx.bezierCurveTo(50, 420 + y0, 80, 230 + y0, 150, 110 + y0)
            ctx.closePath()
            ctx.stroke()

            // Touchpad, Share and Options.
            box(533, 70, 524, 256, 30)
            box(438, 80, 60, 86, 22)
            box(1087, 80, 60, 86, 22)

            // The d-pad: four arms.
            box(264, 146, 70, 108, 14)
            box(264, 302, 70, 108, 14)
            box(167, 243, 108, 70, 14)
            box(323, 243, 108, 70, 14)

            // The sticks: rim and cap; the PS button.
            circle(541, 492, 104)
            circle(541, 492, 60)
            circle(1045, 492, 104)
            circle(1045, 492, 60)
            circle(795, 502, 42)

            // The speaker, as dots.
            ctx.fillStyle = line
            var dots = [[739, 388], [767, 388], [795, 388], [823, 388], [851, 388],
                        [753, 410], [781, 410], [809, 410], [837, 410],
                        [767, 432], [795, 432], [823, 432]]
            for (var d = 0; d < dots.length; ++d) {
                ctx.beginPath()
                ctx.arc(dots[d][0], dots[d][1] + y0, 5, 0, Math.PI * 2)
                ctx.fill()
            }

            // The four face buttons, and their symbols in colour.
            circle(1286, 163, 52)
            circle(1404, 278, 52)
            circle(1286, 393, 52)
            circle(1171, 278, 52)
            ctx.lineWidth = 2.2 / k
            ctx.strokeStyle = controller.triangleColor
            ctx.beginPath()
            ctx.moveTo(1286, 138 + y0); ctx.lineTo(1310, 180 + y0); ctx.lineTo(1262, 180 + y0)
            ctx.closePath()
            ctx.stroke()
            ctx.strokeStyle = controller.circleColor
            circle(1404, 278, 22)
            ctx.strokeStyle = controller.crossColor
            ctx.beginPath()
            ctx.moveTo(1268, 375 + y0); ctx.lineTo(1304, 411 + y0)
            ctx.moveTo(1304, 375 + y0); ctx.lineTo(1268, 411 + y0)
            ctx.stroke()
            ctx.strokeStyle = controller.squareColor
            box(1152, 259, 38, 38, 4)
        }
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

            for (var name in controller.zones) {
                var zoneName = controller.zones[name]
                if (!controller.isLit(name) || !zoneName.c || zoneName.c[2] >= 100)
                    continue
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
                var lit = controller.isLit(o[0])
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

    // Which button is under a point of the drawing (item coordinates):
    // the d-pad by arm, the sticks as L3/R3, L1/R1 on the shoulder strip.
    // The triggers are axes and are not remapped.
    function actionAt(px, py) {
        if (scaleFactor <= 0)
            return ""
        var x = (px - originX) / scaleFactor
        var y = (py - originY) / scaleFactor
        if (y >= 100 && y <= 188) {
            if (x >= 185 && x <= 425) return "l1"
            if (x >= 1165 && x <= 1405) return "r1"
        }
        var iy = y - shoulderHeight
        var arms = [["dpad_up", 264, 146, 70, 108], ["dpad_down", 264, 302, 70, 108],
                    ["dpad_left", 167, 243, 108, 70], ["dpad_right", 323, 243, 108, 70]]
        for (var a = 0; a < arms.length; ++a) {
            var r = arms[a]
            if (x >= r[1] && x <= r[1] + r[3] && iy >= r[2] && iy <= r[2] + r[4])
                return r[0]
        }
        var names = { "triangle": "triangle", "circle": "circle", "cross": "cross", "square": "square",
                      "ps": "ps", "lstick": "l3", "rstick": "r3", "touchpad": "touchpad",
                      "share": "share", "options": "options" }
        for (var name in names) {
            var zone = zones[name]
            if (zone.c) {
                var dx = x - zone.c[0], dy = iy - zone.c[1]
                if (dx * dx + dy * dy <= (zone.c[2] + 10) * (zone.c[2] + 10))
                    return names[name]
            } else if (x >= zone.r[0] && x <= zone.r[0] + zone.r[2]
                       && iy >= zone.r[1] && iy <= zone.r[1] + zone.r[3]) {
                return names[name]
            }
        }
        return ""
    }

    MouseArea {
        anchors.fill: parent
        enabled: controller.clickable
        hoverEnabled: true
        cursorShape: controller.clickable && controller.actionAt(mouseX, mouseY).length > 0
                     ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: function (mouse) {
            var action = controller.actionAt(mouse.x, mouse.y)
            if (action.length > 0)
                controller.actionClicked(action)
        }
    }
}
