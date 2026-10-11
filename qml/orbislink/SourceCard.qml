// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A place on the shelf's home: the console's library, a drive, a folder of
// this PC. Like a folder, it shows the covers of the first games in it, with
// the sign of what it is in the fourth corner; a badge says where it is
// (console, USB, PC, folder), a star marks a favourite, and a place that is
// not there now (a drive taken out, the console off) is shown faded.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic

ShelfTile {
    id: card

    property var info: ({})
    readonly property var previews: info.previews || []
    readonly property int count: info.count === undefined ? -1 : info.count
    readonly property bool available: info.available !== false

    readonly property string sign: info.kind === "console" ? "gamepad"
                                 : info.kind === "usb" ? "usb"
                                 : info.kind === "drive" ? "hard-drive"
                                 : info.kind === "pc" ? "laptop" : "folder"
    readonly property string badge: info.kind === "console" ? qsTr("Console")
                                  : info.kind === "usb" ? "USB"
                                  : info.kind === "drive" ? qsTr("Drive")
                                  : info.kind === "pc" ? qsTr("PC") : qsTr("Folder")

    title: info.title || ""
    subtitle: !available ? qsTr("Not available")
            : count < 0 ? qsTr("Reading…") : qsTr("%n item(s)", "", count)
    dimmed: !available

    ToolTip.visible: hover && (info.path || "").length > 0
    ToolTip.delay: 600
    ToolTip.text: info.path || ""

    Rectangle {
        anchors.fill: parent
        radius: 6
        border.width: 1
        border.color: Theme.cardEdge
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.cardTop }
            GradientStop { position: 1.0; color: card.info.kind === "console" ? Theme.cardActiveBottom : Theme.cardBottom }
        }
    }

    Grid {
        visible: card.previews.length > 0
        anchors.fill: parent
        anchors.margins: 8
        columns: 2
        spacing: 6
        readonly property real cell: (width - spacing) / 2
        Repeater {
            model: 4
            Item {
                required property int index
                width: parent.cell
                height: parent.cell
                CoverArt {
                    anchors.fill: parent
                    visible: parent.index < 3 && parent.index < card.previews.length
                    picture: visible ? (card.previews[parent.index].picture || "") : ""
                    placeholder: visible ? (card.previews[parent.index].placeholder || "bd") : "bd"
                    radius: 4
                }
                Rectangle {
                    anchors.fill: parent
                    visible: parent.index < 3 && parent.index >= card.previews.length
                    radius: 4
                    color: Theme.alpha(Theme.text, 0.04)
                }
                Icon {
                    visible: parent.index === 3
                    anchors.centerIn: parent
                    name: card.sign
                    size: parent.width * 0.5
                    strokeWidth: 1.5
                    color: Theme.alpha(Theme.text, 0.85)
                }
            }
        }
    }

    // No covers yet: the sign alone, large.
    Icon {
        visible: card.previews.length === 0
        anchors.centerIn: parent
        name: card.sign
        size: parent.width * 0.34
        strokeWidth: 1.4
        color: Theme.alpha(card.info.kind === "console" ? Theme.accent : Theme.textSecondary, 0.9)
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 8
        radius: 8
        color: Theme.alpha(Theme.cardBottom, 0.9)
        border.width: 1
        border.color: Theme.cardEdge
        implicitWidth: badgeRow.implicitWidth + 14
        implicitHeight: 22
        Row {
            id: badgeRow
            anchors.centerIn: parent
            spacing: 5
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 7
                height: 7
                radius: 3.5
                color: card.available ? Theme.ok : Theme.textSecondary
            }
            Text {
                text: card.badge
                color: Theme.cardText
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 0.4
            }
        }
    }

    Icon {
        visible: card.info.favorite === true
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        name: "star"
        size: 16
        color: Theme.hen
    }
}
