// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What a card shows when there is no picture: a disc on the card's own
// colours — a CD for a PS1 game, a DVD for a PS2 one, a Blu-ray for the
// rest — with what it is under it; a folder, a payload or an archive gets
// its own sign the same way. Never an empty card.
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
    readonly property color tint: variant === "cd" ? (Theme.light ? "#9AA6B6" : "#C9D1DC")
                                : variant === "dvd" ? (Theme.light ? "#8F9FC4" : "#AEBFE6")
                                : Theme.accent
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

    // The disc: its face, a ring of data, the hub and the hole.
    Item {
        visible: placeholder.disc
        width: placeholder.size * 0.6
        height: width
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: placeholder.label.length > 0 ? -placeholder.size * 0.07 : 0

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.alpha(placeholder.tint, 0.22)
            border.width: Math.max(1, parent.width * 0.012)
            border.color: Theme.alpha(placeholder.tint, 0.65)
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.8
            height: width
            radius: width / 2
            color: "transparent"
            border.width: Math.max(1, parent.width * 0.06)
            border.color: Theme.alpha(placeholder.tint, 0.18)
        }
        // A light catching the surface.
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.92
            height: width
            radius: width / 2
            color: "transparent"
            border.width: Math.max(1, parent.width * 0.018)
            border.color: Theme.alpha("#FFFFFF", Theme.light ? 0.35 : 0.10)
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.34
            height: width
            radius: width / 2
            color: Theme.alpha(placeholder.tint, 0.35)
            border.width: 1
            border.color: Theme.alpha(placeholder.tint, 0.7)
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.12
            height: width
            radius: width / 2
            color: Theme.cardBottom
            border.width: 1
            border.color: Theme.alpha(placeholder.tint, 0.5)
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
        size: placeholder.size * (placeholder.variant === "folder" ? 0.52 : 0.36)
        strokeWidth: 1.5
        color: placeholder.variant === "folder" ? Theme.alpha(Theme.accent, 0.9) : Theme.alpha(Theme.textSecondary, 0.85)
    }

    Text {
        visible: placeholder.label.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: placeholder.size * 0.08
        text: placeholder.label
        color: Theme.alpha(placeholder.disc ? placeholder.tint : Theme.textSecondary, 0.95)
        font.pixelSize: Math.max(9, placeholder.size * 0.085)
        font.weight: Font.DemiBold
        font.letterSpacing: 1.2
    }
}
