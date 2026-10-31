// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What a card shows when there is no picture: a disc engraved on the card's
// own colours, the way the atlas draws an orbit — fine concentric rings, a
// brass hub — a CD for a PS1 game, a DVD for a PS2 one, a Blu-ray for the
// rest, with what it is under it in spaced mono capitals; a folder, a
// payload or an archive gets its own sign the same way. Never an empty card.
import QtQuick

Item {
    id: placeholder

    // "cd", "dvd", "bd", "folder", "payload", "archive" or "file".
    property string variant: "bd"
    // Written under it: "PS2", "PKG"…
    property string label: ""
    property real radius: 6

    readonly property bool disc: variant === "cd" || variant === "dvd" || variant === "bd"
    // Each disc its shade, kept quiet so a shelf of them still reads as one.
    readonly property color tint: variant === "cd" ? (Theme.light ? "#7F8D91" : "#D4CDB8")
                                : variant === "dvd" ? (Theme.light ? "#5F7D93" : "#A6BFC6")
                                : Theme.light ? Qt.darker(Theme.accent, 1.1) : Theme.accent
    readonly property real size: Math.min(width, height)

    Rectangle {
        anchors.fill: parent
        radius: placeholder.radius
        border.width: 1
        border.color: Theme.cardEdge
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.cardTop }
            GradientStop { position: 1.0; color: Theme.cardBottom }
        }
    }

    // The disc: a faint face, a few engraved rings of data, the brass hub.
    Item {
        visible: placeholder.disc
        width: placeholder.size * 0.58
        height: width
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: placeholder.label.length > 0 ? -placeholder.size * 0.07 : 0

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.alpha(placeholder.tint, Theme.light ? 0.10 : 0.12)
            border.width: 1
            border.color: Theme.alpha(placeholder.tint, 0.7)
        }
        // The rings, thinner and fainter towards the hub.
        Repeater {
            model: [0.86, 0.74, 0.62]
            Rectangle {
                required property real modelData
                required property int index
                anchors.centerIn: parent
                width: parent.width * modelData
                height: width
                radius: width / 2
                color: "transparent"
                border.width: 1
                border.color: Theme.alpha(placeholder.tint, 0.34 - index * 0.08)
            }
        }
        // A light catching the surface, along one side.
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.94
            height: width
            radius: width / 2
            color: "transparent"
            border.width: Math.max(1, parent.width * 0.02)
            border.color: Theme.alpha("#FFFFFF", Theme.light ? 0.4 : 0.07)
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.3
            height: width
            radius: width / 2
            color: Theme.alpha(placeholder.tint, 0.16)
            border.width: 1
            border.color: Theme.alpha(Theme.brass, 0.75)
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.1
            height: width
            radius: width / 2
            color: Theme.cardBottom
            border.width: 1
            border.color: Theme.alpha(Theme.brass, 0.6)
        }
    }

    Icon {
        visible: !placeholder.disc
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: placeholder.label.length > 0 ? -placeholder.size * 0.07 : 0
        name: placeholder.variant === "folder" ? "folder" : placeholder.variant === "payload" ? "zap"
            : placeholder.variant === "archive" ? "archive" : "file"
        // A folder inside a folder reads as a picture of its own.
        size: placeholder.size * (placeholder.variant === "folder" ? 0.48 : 0.34)
        strokeWidth: 1.4
        color: placeholder.variant === "folder" ? Theme.alpha(Theme.ink, 0.75) : Theme.alpha(Theme.textSecondary, 0.85)
    }

    Text {
        visible: placeholder.label.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: placeholder.size * 0.08
        text: placeholder.label
        color: Theme.alpha(placeholder.disc ? placeholder.tint : Theme.textSecondary, 0.95)
        font.family: Theme.fontMono
        font.pixelSize: Math.max(9, Math.round(placeholder.size * 0.075))
        font.weight: Font.DemiBold
        font.letterSpacing: 1.6
    }
}
