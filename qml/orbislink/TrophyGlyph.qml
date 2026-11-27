// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A trophy cup drawn in metal: platinum, gold, silver or bronze, lit from
// the upper left (a light band across the bowl, darker toward the right).
// Platinum keeps a faint blue halo. Drawn, not an image or an emoji.
import QtQuick

Canvas {
    id: glyph

    // "platinum", "gold", "silver", "bronze"; anything else is drawn dim.
    property string grade: "bronze"
    property bool dim: false
    property real size: 24

    width: size
    height: size
    // Platinum's halo reaches past the cup.
    antialiasing: true

    readonly property var metal: {
        switch (grade) {
        case "platinum": return ["#E9F8FF", "#75CFFF", "#3D7FB5"]
        case "gold": return ["#FFE7A3", "#E8B64D", "#9A6A1C"]
        case "silver": return ["#F4F7FC", "#BEC7D8", "#77819A"]
        case "bronze": return ["#F0B48C", "#C77B50", "#7A4127"]
        }
        return ["#9AA1B8", "#777F9B", "#4A5068"]
    }

    onGradeChanged: requestPaint()
    onDimChanged: requestPaint()
    onSizeChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        var s = width / 24
        ctx.save()
        ctx.scale(s, s)
        var m = metal
        var fill = ctx.createLinearGradient(5, 0, 19, 0)
        fill.addColorStop(0.0, m[1])
        fill.addColorStop(0.28, m[0])
        fill.addColorStop(0.55, m[1])
        fill.addColorStop(1.0, m[2])
        ctx.globalAlpha = dim ? 0.45 : 1.0
        if (grade === "platinum" && !dim) {
            ctx.shadowColor = "rgba(117, 207, 255, 0.55)"
            ctx.shadowBlur = 6 * s
        }
        // The handles.
        ctx.strokeStyle = fill
        ctx.lineWidth = 1.5
        ctx.beginPath()
        ctx.arc(6, 7, 2.6, Math.PI * 0.5, Math.PI * 1.5, false)
        ctx.stroke()
        ctx.beginPath()
        ctx.arc(18, 7, 2.6, Math.PI * 1.5, Math.PI * 0.5, false)
        ctx.stroke()
        // The bowl.
        ctx.fillStyle = fill
        ctx.beginPath()
        ctx.moveTo(5.6, 3)
        ctx.lineTo(18.4, 3)
        ctx.lineTo(18.4, 8.6)
        ctx.quadraticCurveTo(18.2, 14.6, 12, 15)
        ctx.quadraticCurveTo(5.8, 14.6, 5.6, 8.6)
        ctx.closePath()
        ctx.fill()
        ctx.shadowBlur = 0
        // A fine dark edge, so a pale cup still reads on a light page.
        ctx.strokeStyle = m[2]
        ctx.lineWidth = 0.6
        ctx.stroke()
        // The stem, the base and its plinth.
        ctx.fillRect(11, 14.6, 2, 3.6)
        ctx.beginPath()
        ctx.moveTo(8.4, 18.2)
        ctx.lineTo(15.6, 18.2)
        ctx.lineTo(16.4, 20.2)
        ctx.lineTo(7.6, 20.2)
        ctx.closePath()
        ctx.fill()
        ctx.stroke()
        ctx.fillRect(6.8, 20.2, 10.4, 1.8)
        // The light on the rim.
        ctx.fillStyle = "rgba(255, 255, 255, 0.55)"
        ctx.fillRect(6.2, 3.2, 11.6, 0.7)
        ctx.restore()
    }
}
