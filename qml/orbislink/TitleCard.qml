// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A game, an app or a file on the shelf: its cover (or a disc), its name,
// what kind of file it is in the corner, and a green glow around it when the
// console has it installed. While several are being picked, a round mark in
// the top corner says whether this one is.
import QtQuick

ShelfTile {
    id: card

    property var info: ({})
    // Picking several: whether it can be picked, and whether it is.
    property bool picking: false
    property bool checked: false

    title: info.title || info.name || ""
    subtitle: [info.platform ? info.platform.toUpperCase() : "", info.version || ""].filter(function (part) {
        return part.length > 0
    }).join("  ·  ")
    dimmed: info.readable === false
    glow: info.installed === true ? Theme.ok : "transparent"

    // "PKG", "ISO", "APP"…: what the file is.
    readonly property string typeText: info.app ? "APP"
                                     : info.ext ? info.ext.substring(1).toUpperCase() : ""

    // The cover fills the whole tile (the details show it whole).
    CoverArt {
        anchors.fill: parent
        crop: true
        picture: card.info.picture || ""
        placeholder: card.info.placeholder || "bd"
        label: card.info.platform ? card.info.platform.toUpperCase() : card.typeText
    }

    // Picked: the cover a shade of the accent colour.
    Rectangle {
        anchors.fill: parent
        visible: card.picking && card.checked
        radius: 6
        color: Theme.alpha(Theme.accent, 0.22)
        border.width: 2
        border.color: Theme.accent
    }
    Rectangle {
        visible: card.picking
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 6
        width: 24
        height: 24
        radius: 12
        color: card.checked ? Theme.accent : Theme.alpha("#000000", 0.35)
        border.width: 2
        border.color: card.checked ? Theme.accent : "#FFFFFF"
        Icon {
            anchors.centerIn: parent
            visible: card.checked
            name: "check"
            size: 14
            strokeWidth: 2.5
            color: "#FFFFFF"
        }
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
}
