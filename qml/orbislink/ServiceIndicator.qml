// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A service in the top bar, in as little room as possible: its icon (or,
// for FTP, the word in small bold letters) with a dot in the state's colour
// at the corner. The dot breathes while the service is being checked; the
// name and what it means are in the tooltip.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root
    property string label: ""
    property string state_: "unknown"
    property string hint: ""
    // Empty: the label itself is drawn, in small bold letters.
    property string iconName: ""

    readonly property color tone: Theme.stateColor(root.state_)
    readonly property bool up: root.state_ === "available"

    implicitWidth: Math.max(34, glyph.width + 18)
    implicitHeight: 32

    Rectangle {
        anchors.fill: parent
        radius: 10
        color: hoverHandler.hovered ? Theme.controlHover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
    }

    Item {
        id: glyph
        anchors.centerIn: parent
        width: root.iconName.length > 0 ? 18 : word.implicitWidth
        height: 18

        Icon {
            visible: root.iconName.length > 0
            anchors.centerIn: parent
            name: root.iconName
            size: 18
            color: root.up ? Theme.text : Theme.textSecondary
        }
        Text {
            id: word
            visible: root.iconName.length === 0
            anchors.centerIn: parent
            text: root.label
            color: root.up ? Theme.text : Theme.textSecondary
            font.pixelSize: 11
            font.weight: Font.Black
            font.letterSpacing: 0.6
        }

        // The state, at the corner.
        Rectangle {
            id: dot
            x: parent.width - 3
            y: -3
            width: 7
            height: 7
            radius: 3.5
            color: root.tone
            border.width: 1.5
            border.color: Theme.panel
            // A soft ring when it is up, like the other lit things.
            Rectangle {
                anchors.centerIn: parent
                width: 13
                height: 13
                radius: 6.5
                color: "transparent"
                border.width: 2.5
                border.color: Theme.alpha(root.tone, 0.25)
                visible: root.up
            }
            SequentialAnimation on opacity {
                running: root.state_ === "checking"
                loops: Animation.Infinite
                onStopped: dot.opacity = 1
                NumberAnimation { to: 0.25; duration: 600; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
            }
        }
    }

    ToolTip.visible: hoverHandler.hovered
    ToolTip.text: root.hint.length > 0 ? root.label + " — " + root.hint : root.label
    ToolTip.delay: 200
    HoverHandler { id: hoverHandler }
}
