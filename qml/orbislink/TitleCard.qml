// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A game, an app or a file on the shelf: its cover (or a disc), its name,
// the facts that tell copies apart (title ID, version, size) in mono, and on
// the cover what it is — a game, a patch, an add-on, an app — and, in green,
// whether the console has it installed. While several are being picked, a
// round mark in the top corner says whether this one is.
import QtQuick

ShelfTile {
    id: card

    property var info: ({})
    // Picking several: whether it can be picked, and whether it is.
    property bool picking: false
    property bool checked: false

    title: info.title || info.name || ""
    subtitle: [info.titleId || (info.platform ? info.platform.toUpperCase() : ""),
               info.version ? "v" + info.version : "", info.sizeText || ""].filter(function (part) {
        return part.length > 0
    }).join(" · ")
    dimmed: info.readable === false
    glow: info.installed === true ? Theme.ok : "transparent"

    // "PKG", "ISO", "APP"…: what the file is, for the placeholder's label.
    readonly property string typeText: info.app ? "APP"
                                     : info.ext ? info.ext.substring(1).toUpperCase() : ""
    // What it is, in a word, for the tag on the cover.
    readonly property string kindTag: {
        if (info.app) return qsTr("App")
        switch (info.kind) {
        case "installed":
        case "package":
            return info.category === "gp" || info.category === "gpd" ? qsTr("Patch")
                 : info.category === "ac" ? qsTr("DLC")
                 : info.category === "gdt" ? qsTr("Theme")
                 : info.kind === "package" ? qsTr("Game") : ""
        case "disc": return info.platform ? info.platform.toUpperCase() : typeText
        case "image": return qsTr("Game")
        case "payload": return qsTr("Payload")
        }
        return typeText
    }
    // An installed game whose update is on the console too.
    readonly property bool updated: (info.parts || []).some(function (p) { return p.what === "update" })

    // The cover fills the whole tile (the details show it whole).
    CoverArt {
        anchors.fill: parent
        crop: true
        radius: card.artRadius
        picture: card.info.picture || ""
        placeholder: card.info.placeholder || "bd"
        // Said once: not under the disc when the tag on the cover says it.
        readonly property string what: card.info.platform ? card.info.platform.toUpperCase() : card.typeText
        label: what === card.kindTag ? "" : what
    }

    // Picked: a veil of bright glass over the cover.
    Rectangle {
        anchors.fill: parent
        visible: card.picking && card.checked
        radius: card.artRadius
        color: Qt.rgba(1, 1, 1, Theme.light ? 0.30 : 0.12)
        border.width: 2
        border.color: Qt.rgba(1, 1, 1, Theme.light ? 1 : 0.85)
    }
    Rectangle {
        visible: card.picking
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
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

    // What it is and what state it is in, as tags along the cover's foot.
    // Each sits on a chip of frosted glass so it reads on any picture.
    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        spacing: 4
        layoutDirection: Qt.LeftToRight
        Repeater {
            model: [
                card.kindTag.length > 0 ? { text: card.kindTag, tone: "neutral", icon: "", mono: card.kindTag === card.typeText } : null,
                card.info.installed === true ? { text: card.updated ? qsTr("Updated") : qsTr("Installed"), tone: "ok",
                                                 icon: "check", mono: false } : null
            ].filter(function (t) { return t !== null })
            ShelfTile.CoverTag {
                required property var modelData
                text: modelData.text
                tone: modelData.tone
                mono: modelData.mono
                iconName: modelData.icon
            }
        }
    }
}
