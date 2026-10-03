// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A service in the top bar: a pill with a dot in the state's colour and the
// service's name. The dot pulses while it is being checked.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root
    property string label: ""
    property string state_: "unknown"
    property string hint: ""
    // Narrow window: the dot alone; the name moves to the tooltip.
    property bool compact: false
    // What the name takes up, so the top bar knows what compact saves.
    readonly property real labelWidth: nameText.implicitWidth + row.spacing

    implicitWidth: compact ? implicitHeight : row.implicitWidth + 28
    implicitHeight: 32

    readonly property color tone: Theme.stateColor(root.state_)

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: hoverHandler.hovered ? Theme.controlHover : Theme.controlFill
        border.color: root.state_ === "unavailable" ? Theme.alpha(Theme.error, 0.35) : Theme.glassEdge
        border.width: 1
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8

        Rectangle {
            width: 8; height: 8; radius: 4
            anchors.verticalCenter: parent.verticalCenter
            color: root.tone
            // A lit dot has a soft ring; that is what tells it apart from a pixel.
            Rectangle {
                anchors.centerIn: parent
                width: 14; height: 14; radius: 7
                color: "transparent"
                border.width: 3
                border.color: Theme.alpha(root.tone, 0.22)
            }

            SequentialAnimation on opacity {
                running: root.state_ === "checking"
                loops: Animation.Infinite
                NumberAnimation { to: 0.3; duration: 600 }
                NumberAnimation { to: 1.0; duration: 600 }
            }
        }

        Text {
            id: nameText
            visible: !root.compact
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: Theme.text
            font.pixelSize: 12
            font.weight: Font.Medium
        }
    }

    ToolTip.visible: hoverHandler.hovered && (root.compact || root.hint.length > 0)
    ToolTip.text: root.compact ? (root.hint.length > 0 ? root.label + " — " + root.hint : root.label)
                               : root.hint
    ToolTip.delay: 200
    HoverHandler { id: hoverHandler }
}
