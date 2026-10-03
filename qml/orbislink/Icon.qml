// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A line icon from the set in Icons.js, in any size and colour.
//
// Drawn with Shape instead of loading SVG files: no Qt SVG module to ship,
// and the colour follows the theme like text does. The paths are on Lucide's
// 24×24 grid; the layer with multisampling is what keeps the thin strokes
// smooth at small sizes.
import QtQuick
import QtQuick.Shapes
import "Icons.js" as Icons

Item {
    id: icon

    property string name: ""
    property color color: Theme.text
    property real size: 18
    // Lucide draws with 2; the design asks for a slightly finer line.
    property real strokeWidth: 1.75
    // Turns forever, for the waiting states (the "loader" icon).
    property bool spinning: false

    implicitWidth: size
    implicitHeight: size
    visible: name.length > 0

    layer.enabled: true
    layer.samples: 4
    layer.smooth: true

    // The path is scaled, not the item: the curves are then flattened at the
    // final size, so a circle stays round at any size instead of showing the
    // segments of a 24-pixel drawing blown up.
    Shape {
        id: drawing
        anchors.fill: parent
        readonly property real factor: icon.width / 24

        ShapePath {
            scale: Qt.size(drawing.factor, drawing.factor)
            strokeColor: icon.color
            strokeWidth: icon.strokeWidth * drawing.factor
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: Icons.paths[icon.name] || "" }
        }
    }

    RotationAnimator on rotation {
        running: icon.spinning && icon.visible
        from: 0
        to: 360
        duration: 900
        loops: Animation.Infinite
        onStopped: icon.rotation = 0
    }
}
