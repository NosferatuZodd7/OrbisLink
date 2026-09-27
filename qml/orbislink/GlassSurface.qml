// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A liquid glass surface: the building block of the whole
// interface.
//
// What makes glass look like glass is not the blur — it is the stacking of
// translucent layers, the edge of light at the top and the soft shadow
// underneath. All of that is here and runs on any machine, including those
// that fall back to software rendering.
//
// A real blur of what is behind needs QtQuick.Effects, which only exists in
// Qt 6.5+. It is left out on purpose: the screenshots and tests run on
// Qt 6.4, and an import that does not exist there stops the whole window
// from opening. This application's background is a soft gradient, and over
// gradients the difference between blurring and stacking translucency
// cannot be seen.
import QtQuick

Item {
    id: surface

    // The content goes in here, already with the theme's padding.
    default property alias content: area.data
    property int radius: Theme.radius
    property real fill: Theme.panelOpacity
    property color tint: Theme.panel
    // A halo in the accent colour behind, for whatever is active.
    property bool glowing: false
    property color glowColor: Theme.accent
    // The inner padding. 24–32 is what the design language asks for.
    property int padding: Theme.padding
    property alias contentItem: area

    implicitWidth: area.implicitWidth + padding * 2
    implicitHeight: area.implicitHeight + padding * 2

    // ── soft shadow
    // Three layers, each larger and fainter: without blur, this is how
    // you make a shadow with no edge.
    Repeater {
        model: 3
        delegate: Rectangle {
            z: -2
            anchors.fill: parent
            anchors.margins: -(index + 1) * 3
            radius: surface.radius + (index + 1) * 3
            color: "transparent"
            border.width: 3
            border.color: Qt.rgba(Theme.shadow.r, Theme.shadow.g, Theme.shadow.b,
                                  Theme.shadow.a * (0.22 - index * 0.06))
        }
    }

    // ── accent halo, when active
    Rectangle {
        z: -1
        anchors.fill: parent
        anchors.margins: -6
        radius: surface.radius + 6
        visible: surface.glowing
        color: "transparent"
        border.width: 6
        border.color: Qt.rgba(surface.glowColor.r, surface.glowColor.g,
                              surface.glowColor.b, 0.16)
    }

    // ── o vidro
    Rectangle {
        id: glass
        anchors.fill: parent
        radius: surface.radius
        color: Qt.rgba(surface.tint.r, surface.tint.g, surface.tint.b, surface.fill)
        border.width: 1
        border.color: Theme.glassEdge

        // Almost imperceptible inner gradient: without it the surface looks
        // flat and stops looking like a body.
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.glassHighlight * 0.9) }
                GradientStop { position: 0.35; color: Qt.rgba(1, 1, 1, Theme.glassHighlight * 0.15) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, Theme.light ? 0.02 : 0.10) }
            }
        }

        // The edge of light at the top, the specular reflection of curved
        // glass. It is a line, but it does the most for the illusion.
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 1
            anchors.leftMargin: parent.radius * 0.5
            anchors.rightMargin: parent.radius * 0.5
            height: 1
            radius: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, Theme.light ? 0.9 : 0.35) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        Behavior on color { ColorAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
    }

    Item {
        id: area
        anchors.fill: parent
        anchors.margins: surface.padding
    }
}
