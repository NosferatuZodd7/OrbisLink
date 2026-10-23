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

    // In words, for the card under it.
    readonly property string stateWord: root.state_ === "available" ? qsTr("available")
        : root.state_ === "checking" ? qsTr("checking…")
        : root.state_ === "unavailable" ? qsTr("not answering")
        : root.state_ === "not-applicable" ? qsTr("not running")
        : qsTr("not checked yet")

    // A small card under it: the name and its state in the state's colour,
    // then what it means and what to do — not a bare line of text.
    ToolTip {
        id: tip
        visible: hoverHandler.hovered
        delay: 200
        x: (root.width - width) / 2
        y: root.height + 8
        margins: 12
        padding: 14
        topPadding: 12
        bottomPadding: 12
        width: Math.min(380, Math.max(titleRow.implicitWidth, hintText.implicitWidth) + leftPadding + rightPadding)

        background: Rectangle {
            color: Theme.dialogFill
            radius: 14
            border.width: 1
            border.color: Theme.border
        }

        contentItem: Column {
            spacing: 6
            Row {
                id: titleRow
                spacing: 8
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: root.tone
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.label
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.stateWord
                    color: root.tone
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
            }
            Text {
                id: hintText
                visible: root.hint.length > 0
                width: tip.availableWidth
                text: root.hint
                color: Theme.textSecondary
                font.pixelSize: 12
                lineHeight: 1.2
                wrapMode: Text.WordWrap
            }
        }
    }
    HoverHandler { id: hoverHandler }
}
