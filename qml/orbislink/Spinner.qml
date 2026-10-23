// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A short wait, in the app's colours: an arc of the accent colour turning
// over a faint ring. Where a list or a grid is on its way, skeletons
// (SkeletonRow, SkeletonTile) stand in instead.
import QtQuick

Item {
    id: spinner

    property bool running: visible
    property real size: 22
    property real thickness: Math.max(2, size / 9)
    property color color: Theme.accent

    implicitWidth: size
    implicitHeight: size

    // The faint ring.
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.width: spinner.thickness
        border.color: Theme.alpha(spinner.color, 0.18)
    }

    // The arc, a quarter and a bit of the ring, turning.
    Canvas {
        id: arc
        anchors.fill: parent
        antialiasing: true
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var r = (Math.min(width, height) - spinner.thickness) / 2
            ctx.lineWidth = spinner.thickness
            ctx.lineCap = "round"
            ctx.strokeStyle = spinner.color
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, r, -Math.PI / 2, Math.PI * 0.15)
            ctx.stroke()
        }
        RotationAnimator on rotation {
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
            running: spinner.running && spinner.visible
        }
    }
    onColorChanged: arc.requestPaint()
    onSizeChanged: arc.requestPaint()
}
