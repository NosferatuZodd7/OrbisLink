// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A PS1 or PS2 disc found on the PC, as a card like the consoles': the
// platform large, the name, the serial and region. The box in the corner
// selects it (for doing several at once); a click anywhere else opens it.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: card

    property var game: ({})
    property bool selected: false
    readonly property bool ps2: game.platform === "ps2"
    readonly property color tone: ps2 ? Theme.accent : Theme.textSecondary

    signal open()
    signal toggle()

    // A square, like every card.
    implicitWidth: 236
    implicitHeight: 236

    readonly property bool hover: area.containsMouse

    scale: area.pressed ? Theme.pressScale : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
    transform: Translate {
        y: card.hover && !area.pressed ? -5 : 0
        Behavior on y { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
    }

    // The same soft frame the console cards have.
    Repeater {
        model: 3
        Rectangle {
            anchors.fill: body
            anchors.margins: -(index + 1) * 3
            radius: body.radius + (index + 1) * 3
            color: "transparent"
            border.width: 3
            border.color: card.selected
                ? Theme.alpha(Theme.accent, (Theme.light ? 0.10 : 0.14) - index * 0.04)
                : Qt.rgba(0, 0, 0, Theme.light ? 0.025 - index * 0.008 : 0.10 - index * 0.03)
        }
    }

    Rectangle {
        id: body
        anchors.fill: parent
        radius: Theme.radius
        border.width: card.selected ? 2 : 1
        border.color: card.selected ? Theme.accent
                    : card.hover ? Theme.alpha(card.tone, 0.6) : Theme.cardEdge
        Behavior on border.color { ColorAnimation { duration: Theme.cardEase } }
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.cardTop }
            GradientStop { position: 1.0; color: card.selected ? Theme.cardActiveBottom : Theme.cardBottom }
        }

        // The platform, large, as the console cards show "PS4".
        Text {
            x: 20
            y: 34
            text: card.ps2 ? "PS2" : "PS1"
            color: card.ps2 ? Theme.accent : Theme.cardText
            font.pixelSize: 44
            font.weight: Font.Black
            font.letterSpacing: -1
        }
        Icon {
            anchors.right: parent.right
            anchors.rightMargin: 22
            y: 46
            name: "disc"
            size: 30
            color: Theme.alpha(card.tone, 0.55)
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            y: 100
            spacing: 6
            Text {
                width: parent.width
                text: card.game.title + (card.game.disc > 0 ? qsTr(" (disc %1)").arg(card.game.disc) : "")
                color: Theme.cardText
                font.pixelSize: Theme.fontCardTitle
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeight: 1.1
            }
            Text {
                width: parent.width
                text: [card.game.serial, card.game.region].filter(function (s) { return s && s.length > 0 }).join("  ·  ")
                color: Theme.cardTextMuted
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        // The file, at the bottom.
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 18
            spacing: 6
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "file"
                size: 13
                color: Theme.cardTextMuted
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: card.game.sizeText + "  ·  ." + card.game.format
                color: Theme.cardTextMuted
                font.pixelSize: 11
            }
        }
        // No emulator for it yet: it can be sent, not converted.
        Icon {
            visible: !card.game.convertible
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16
            name: "warning"
            size: 15
            color: Theme.warn
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: card.open()
    }

    // The selection box, above the click area so it gets its own click.
    Rectangle {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 14
        width: 30
        height: 30
        radius: 9
        color: boxArea.containsMouse ? Theme.controlHover : "transparent"
        Icon {
            anchors.centerIn: parent
            name: card.selected ? "square-check" : "square"
            size: 20
            color: card.selected ? Theme.accent : Theme.cardTextMuted
        }
        MouseArea {
            id: boxArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: card.toggle()
        }
        ToolTip.visible: boxArea.containsMouse
        ToolTip.text: card.selected ? qsTr("Unselect") : qsTr("Select, to do several at once")
    }
}
