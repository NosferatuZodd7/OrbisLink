// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What a card shows when there is no picture: a disc lying under glass —
// a smoked face, a few fine rings of data, a light catching it across, a
// silver hub — a CD for a PS1 game, a DVD for a PS2 one, a Blu-ray for the
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
    // Each disc a hint of its own shade over the graphite (a warm silver CD,
    // a cooler DVD, a blue-silver Blu-ray), so a shelf of them still reads
    // as one.
    readonly property color tint: variant === "cd" ? (Theme.light ? "#8A8172" : "#D9D0BF")
                                : variant === "dvd" ? (Theme.light ? "#6E7A84" : "#C3CCD3")
                                : Theme.light ? "#5F7890" : "#A9C0D6"
    readonly property real size: Math.min(width, height)

    // The glass the disc lies under: lighter at the top, a fine edge.
    Rectangle {
        anchors.fill: parent
        radius: placeholder.radius
        border.width: 1
        border.color: Theme.light ? Qt.rgba(1, 1, 1, 0.85) : Theme.glassEdge
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.light ? Qt.rgba(1, 1, 1, 0.70) : Qt.rgba(1, 1, 1, 0.075) }
            GradientStop { position: 1.0; color: Theme.light ? Qt.rgba(0.93, 0.92, 0.90, 0.60) : Qt.rgba(0, 0, 0, 0.18) }
        }
    }

    // The disc.
    Item {
        visible: placeholder.disc
        width: placeholder.size * 0.60
        height: width
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: placeholder.label.length > 0 ? -placeholder.size * 0.07 : 0

        // A soft shadow under it, so it lies on the glass.
        Rectangle {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: parent.width * 0.03
            width: parent.width * 1.02
            height: width
            radius: width / 2
            color: Qt.rgba(0, 0, 0, Theme.light ? 0.06 : 0.22)
        }
        // The face: smoked graphite (dark) or frosted silver (Daylight).
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.light ? Theme.mix(Qt.color("#FFFFFF"), placeholder.tint, 0.16) : Theme.mix(Qt.color("#1E1E21"), placeholder.tint, 0.12)
            border.width: 1
            border.color: Theme.alpha(placeholder.tint, Theme.light ? 0.55 : 0.45)
        }
        // The rings of data, fainter towards the hub.
        Repeater {
            model: [0.88, 0.76, 0.64, 0.52]
            Rectangle {
                required property real modelData
                required property int index
                anchors.centerIn: parent
                width: parent.width * modelData
                height: width
                radius: width / 2
                color: "transparent"
                border.width: 1
                border.color: Theme.alpha(placeholder.tint, (Theme.light ? 0.30 : 0.24) - index * 0.05)
            }
        }
        // A light catching the surface: a soft band across, the way a disc
        // under glass throws back the room.
        Rectangle {
            anchors.centerIn: parent
            width: parent.width - 2
            height: width
            radius: width / 2
            rotation: -38
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0) }
                GradientStop { position: 0.36; color: Qt.rgba(1, 1, 1, Theme.light ? 0.50 : 0.10) }
                GradientStop { position: 0.50; color: Qt.rgba(1, 1, 1, 0) }
                GradientStop { position: 0.70; color: Qt.rgba(1, 1, 1, Theme.light ? 0.22 : 0.04) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0) }
            }
        }
        // The hub: a clear ring of silver, the hole through it.
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.30
            height: width
            radius: width / 2
            color: Theme.light ? Qt.rgba(1, 1, 1, 0.85) : Qt.rgba(1, 1, 1, 0.10)
            border.width: 1
            border.color: Theme.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.28)
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.11
            height: width
            radius: width / 2
            color: Theme.light ? Qt.rgba(0.90, 0.89, 0.87, 1) : "#141416"
            border.width: 1
            border.color: Theme.light ? Qt.rgba(0, 0, 0, 0.14) : Qt.rgba(1, 1, 1, 0.20)
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
        color: placeholder.disc ? (Theme.light ? Qt.darker(placeholder.tint, 1.15) : placeholder.tint) : Theme.textSecondary
        font.family: Theme.fontMono
        font.pixelSize: Math.max(9, Math.round(placeholder.size * 0.075))
        font.weight: Font.DemiBold
        font.letterSpacing: 1.6
    }
}
