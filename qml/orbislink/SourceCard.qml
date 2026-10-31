// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A place on the shelf's home: the console's library, a drive, a folder of
// this PC. Like a folder, it shows the covers of the first games in it, with
// the sign of what it is in the fourth corner; a tag says where it is
// (console, USB, PC, folder) and whether it answers, a brass star marks a
// favourite, and a place that is not there now (a drive taken out, the
// console off) is shown faded. The console's own places carry a second,
// brass hairline inside the edge, as the atlas marks what is singled out.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic

ShelfTile {
    id: card

    property var info: ({})
    readonly property var previews: info.previews || []
    readonly property int count: info.count === undefined ? -1 : info.count
    readonly property bool available: info.available !== false
    readonly property bool isConsole: info.kind === "console"

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
    subtitleMono: false
    dimmed: !available

    ToolTip.visible: hover && (info.path || "").length > 0
    ToolTip.delay: 600
    ToolTip.text: info.path || ""

    Rectangle {
        anchors.fill: parent
        radius: card.artRadius
        border.width: 1
        border.color: card.isConsole ? Theme.alpha(Theme.brass, 0.55) : Theme.cardEdge
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.cardTop }
            GradientStop { position: 1.0; color: card.isConsole ? Theme.cardActiveBottom : Theme.cardBottom }
        }
        Rectangle {
            visible: card.isConsole
            anchors.fill: parent
            anchors.margins: 4
            radius: card.artRadius - 3
            color: "transparent"
            border.width: 1
            border.color: Theme.alpha(Theme.brass, 0.22)
        }
    }

    Grid {
        visible: card.previews.length > 0
        anchors.fill: parent
        anchors.margins: 12
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
                    crop: true
                    visible: parent.index < 3 && parent.index < card.previews.length
                    picture: visible ? (card.previews[parent.index].picture || "") : ""
                    placeholder: visible ? (card.previews[parent.index].placeholder || "bd") : "bd"
                    radius: 6
                }
                Rectangle {
                    anchors.fill: parent
                    visible: parent.index < 3 && parent.index >= card.previews.length
                    radius: 6
                    color: Theme.alpha(Theme.ink, 0.04)
                    border.width: 1
                    border.color: Theme.alpha(Theme.ink, 0.06)
                }
                Icon {
                    visible: parent.index === 3
                    anchors.centerIn: parent
                    name: card.sign
                    size: parent.width * 0.46
                    strokeWidth: 1.5
                    color: Theme.alpha(Theme.ink, 0.8)
                }
            }
        }
    }

    // No covers yet: the sign alone, in a ring, with a brass star over it
    // for the console's own.
    Item {
        visible: card.previews.length === 0
        anchors.fill: parent
        Rectangle {
            id: seal
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -10
            width: parent.width * 0.42
            height: width
            radius: width / 2
            color: Theme.alpha(Theme.ink, 0.04)
            border.width: 1
            border.color: Theme.alpha(card.isConsole ? Theme.brass : Theme.ink, card.isConsole ? 0.5 : 0.18)
            Rectangle {
                anchors.fill: parent
                anchors.margins: 4
                radius: width / 2
                color: "transparent"
                border.width: 1
                border.color: Theme.alpha(card.isConsole ? Theme.brass : Theme.ink, 0.12)
            }
            Icon {
                anchors.centerIn: parent
                name: card.sign
                size: parent.width * 0.46
                strokeWidth: 1.4
                color: Theme.alpha(card.isConsole ? Theme.ink : Theme.textSecondary, 0.9)
            }
        }
    }

    // Where it is, and a dot that says whether it answers.
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 10
        width: whereTag.implicitWidth
        height: whereTag.implicitHeight
        radius: height / 2
        color: Theme.alpha(Theme.cardBottom, 0.94)
        Tag {
            id: whereTag
            anchors.fill: parent
            text: card.badge
            tone: card.available ? "ok" : "neutral"
            iconName: card.available ? "check" : "close"
        }
    }

    Icon {
        visible: card.info.favorite === true
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        name: "star"
        size: 16
        color: Theme.brass
    }
}
