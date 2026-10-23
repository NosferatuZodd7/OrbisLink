// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A placeholder row while something is loading: grey blocks where the badge,
// the name, the details and the button will be, with a band of light
// sweeping across.
import QtQuick

Rectangle {
    id: row
    // A file's line (an icon, its name, its size) instead of a card.
    property bool compact: false
    // Rows one under the other start their shine a little apart.
    property int delay: 0

    implicitHeight: compact ? 34 : 60
    radius: compact ? 8 : 14
    color: compact ? "transparent" : Theme.panelAltFill
    border.color: compact ? "transparent" : Theme.glassEdge
    clip: true

    readonly property color block: Theme.light ? "#E4E9F0" : Qt.rgba(1, 1, 1, 0.07)

    Rectangle {
        visible: !row.compact
        x: 12; anchors.verticalCenter: parent.verticalCenter; width: 46; height: 34; radius: 10; color: row.block
    }
    Rectangle { visible: !row.compact; x: 70; y: 15; width: parent.width * 0.32; height: 12; radius: 6; color: row.block }
    Rectangle { visible: !row.compact; x: 70; y: 35; width: parent.width * 0.22; height: 10; radius: 5; color: row.block }
    Rectangle {
        visible: !row.compact
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 96; height: 36; radius: 12
        color: row.block
    }
    // Compact: the icon, the name, the size.
    Rectangle {
        visible: row.compact
        x: 10; anchors.verticalCenter: parent.verticalCenter; width: 18; height: 18; radius: 5; color: row.block
    }
    Rectangle {
        visible: row.compact
        x: 38; anchors.verticalCenter: parent.verticalCenter
        width: parent.width * (0.28 + 0.08 * (row.delay % 3) / 2); height: 10; radius: 5; color: row.block
    }
    Rectangle {
        visible: row.compact
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 54; height: 9; radius: 4
        color: row.block
    }

    // The shine: a soft band crossing from left to right, forever.
    Rectangle {
        id: shine
        width: parent.width * 0.4
        height: parent.height
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: Theme.light ? Qt.rgba(1, 1, 1, 0.7) : Qt.rgba(1, 1, 1, 0.06) }
            GradientStop { position: 1.0; color: "transparent" }
        }
        x: -width
        SequentialAnimation on x {
            running: row.visible
            loops: Animation.Infinite
            PauseAnimation { duration: row.delay }
            NumberAnimation { from: -shine.width; to: row.width; duration: 1300 }
        }
    }
}
