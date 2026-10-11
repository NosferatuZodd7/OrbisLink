// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A game, an app or a file on the shelf: its cover (or a disc), its name,
// what kind of file it is in the corner, and a mark when the console has it
// installed.
import QtQuick

ShelfTile {
    id: card

    property var info: ({})

    title: info.title || info.name || ""
    subtitle: [info.platform ? info.platform.toUpperCase() : "", info.version || ""].filter(function (part) {
        return part.length > 0
    }).join("  ·  ")
    dimmed: info.readable === false

    // "PKG", "ISO", "APP"…: what the file is.
    readonly property string typeText: info.app ? "APP"
                                     : info.ext ? info.ext.substring(1).toUpperCase() : ""

    CoverArt {
        anchors.fill: parent
        picture: card.info.picture || ""
        placeholder: card.info.placeholder || "bd"
        label: card.info.platform ? card.info.platform.toUpperCase() : card.typeText
    }

    Rectangle {
        visible: card.typeText.length > 0
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 6
        radius: 6
        color: Theme.alpha(Theme.cardBottom, 0.88)
        border.width: 1
        border.color: Theme.cardEdge
        implicitWidth: typeLabel.implicitWidth + 12
        implicitHeight: 18
        Text {
            id: typeLabel
            anchors.centerIn: parent
            text: card.typeText
            color: Theme.cardText
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 0.6
        }
    }

    Rectangle {
        visible: card.info.installed === true
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        width: 22
        height: 22
        radius: 11
        color: Theme.ok
        Icon {
            anchors.centerIn: parent
            name: "check"
            size: 14
            strokeWidth: 2.5
            color: "#FFFFFF"
        }
    }
}
