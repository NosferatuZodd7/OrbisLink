// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A service of the console in use, under its card in the navigation: a dot
// in the state's colour, the name and the state in a word (listed), or —
// with the navigation folded — the icon alone with the dot at its corner.
// The dot breathes while the service is being checked; what it means and
// what to do are in the card that opens on hover.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root
    property string label: ""
    property string state_: "unknown"
    property string hint: ""
    // Empty: the label itself is drawn, in small bold letters.
    property string iconName: ""
    // A full-width line (dot, name, state) instead of the bare icon.
    property bool listed: false

    readonly property color tone: Theme.stateColor(root.state_)
    readonly property bool up: root.state_ === "available"

    implicitWidth: listed ? line.implicitWidth + 16 : Math.max(34, glyph.width + 18)
    implicitHeight: listed ? 24 : 32

    Rectangle {
        anchors.fill: parent
        radius: root.listed ? 8 : height / 2
        color: hoverHandler.hovered ? Theme.controlHover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
    }

    // Listed: the dot, the name, and the state on the right.
    Item {
        id: line
        visible: root.listed
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        implicitWidth: lineDot.width + 8 + lineName.implicitWidth + 12 + lineState.implicitWidth
        Rectangle {
            id: lineDot
            anchors.verticalCenter: parent.verticalCenter
            width: 7
            height: 7
            radius: 3.5
            color: root.tone
            SequentialAnimation on opacity {
                running: root.listed && root.state_ === "checking"
                loops: Animation.Infinite
                onStopped: lineDot.opacity = 1
                NumberAnimation { to: 0.25; duration: 600; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
            }
        }
        Text {
            id: lineName
            anchors.left: lineDot.right
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: root.up ? Theme.text : Theme.textSecondary
            font.pixelSize: 12
            font.weight: Font.Medium
        }
        Text {
            id: lineState
            anchors.right: parent.right
            anchors.left: lineName.right
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignRight
            text: root.stateWord
            color: root.up ? (Theme.light ? Qt.darker(root.tone, 1.3) : root.tone) : Theme.textSecondary
            font.pixelSize: 11
            elide: Text.ElideRight
        }
    }

    Item {
        id: glyph
        visible: !root.listed
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
            border.color: Theme.menuFill
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
        x: root.listed ? root.width + 12 : (root.width - width) / 2
        y: root.listed ? (root.height - height) / 2 : root.height + 8
        margins: 12
        padding: 14
        topPadding: 12
        bottomPadding: 12
        width: Math.min(380, Math.max(titleRow.implicitWidth, hintText.implicitWidth) + leftPadding + rightPadding)

        // A small card of floating glass.
        background: Plate {
            level: "secondary"
            radius: 16
            color: Theme.menuFill
            border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Theme.glassEdge
        }
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast; easing.type: Theme.easeOut }
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
                    color: Theme.ink
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.stateWord
                    color: Theme.light ? Qt.darker(root.tone, 1.3) : root.tone
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
