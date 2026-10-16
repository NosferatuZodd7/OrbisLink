// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A PS1 or PS2 disc found on the PC, as a card of the same size and shape
// as the library's (ShelfTile, Theme.gameTile): its cover on top (a disc
// until it is found), the platform in its corner, the name and the serial
// under it. The round box in the other corner selects it, to do several at
// once; a click anywhere else opens it. While it is on its way to the
// console, the step's loader sits over the cover, a bar fills along its
// bottom, and the line under the name says where it is.
import QtQuick
import QtQuick.Controls.Basic

ShelfTile {
    id: card

    property var game: ({})
    // Picked, to do several at once.
    property bool checked: false
    // Where it is on its way to the console (games.progress), or null.
    property var progress: null
    readonly property string stage: progress ? progress.stage : ""
    readonly property bool busy: stage === "waiting" || stage === "converting"
                                 || stage === "sending" || stage === "installing"
    readonly property bool ps2: game.platform === "ps2"
    // The game's cover, from the collection by its serial (shelf.cover).
    readonly property string cover: shelf.coverRevision >= 0 ? shelf.cover(game.platform || "", game.serial || "") : ""

    signal open()
    signal toggle()

    artSize: Theme.gameTile
    title: (game.title || "") + (game.disc > 0 ? qsTr(" (disc %1)").arg(game.disc) : "")
    subtitle: stage.length > 0 ? stageText : [game.serial, game.sizeText].filter(function (s) {
        return s && s.length > 0
    }).join("  ·  ")
    // The tile's own click (ShelfTile's) covers the cover: on the round box,
    // or with Ctrl or Shift held, it picks; anywhere else it opens.
    function onBox(point) {
        return point.x >= box.x - 4 && point.x <= box.x + box.width + 4
            && point.y >= box.y - 4 && point.y <= box.y + box.height + 4
    }
    readonly property bool overBox: hover && onBox(pointer)
    onActivated: (modifiers) => {
        if (onBox(clickPoint) || (modifiers & (Qt.ControlModifier | Qt.ShiftModifier)))
            card.toggle()
        else
            card.open()
    }

    // What the step under way is, in a few words.
    readonly property string stageText: {
        var p = card.progress
        if (!p) return ""
        var pct = Math.floor(p.percent || 0) + "%"
        switch (p.stage) {
        case "waiting": return qsTr("Waiting to convert")
        case "converting":
            if (p.step === "download") return qsTr("Emulator files · %1").arg(pct)
            if (p.step === "unpack") return qsTr("Unpacking · %1").arg(pct)
            if (p.step === "cover") return qsTr("Getting the cover")
            return qsTr("Converting · %1").arg(pct)
        case "converted": return qsTr("Package ready")
        case "sending": return p.waiting ? qsTr("Waiting to send") : qsTr("Sending · %1").arg(pct)
        case "sent": return qsTr("On the console")
        case "installing": return p.waiting ? qsTr("Waiting to install") : qsTr("Installing · %1").arg(pct)
        case "installed": return qsTr("Installed")
        }
        return p.message ? p.message : qsTr("Failed")
    }

    CoverArt {
        anchors.fill: parent
        crop: true
        picture: card.cover
        placeholder: card.ps2 ? "dvd" : "cd"
        label: card.ps2 ? "PS2" : "PS1"
    }

    // Picked: the cover a shade of the accent colour, as in the library.
    Rectangle {
        anchors.fill: parent
        visible: card.checked
        radius: 6
        color: Theme.alpha(Theme.accent, 0.22)
        border.width: 2
        border.color: Theme.accent
    }

    // On its way: the cover dimmed, the step's loader over it, and a bar
    // along its bottom in the step's colour.
    Rectangle {
        anchors.fill: parent
        visible: card.stage.length > 0
        radius: 6
        color: Qt.rgba(0, 0, 0, 0.55)
        StageLoader {
            id: stageLoader
            anchors.centerIn: parent
            // Over and done (the package made, sent, installed): green.
            stage: card.stage === "converted" || card.stage === "sent" ? "done" : card.stage
            percent: card.progress ? card.progress.percent || 0 : 0
            waiting: card.progress ? card.progress.waiting === true : false
        }
        Rectangle {
            visible: card.busy
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 8
            height: 3
            radius: 1.5
            color: Theme.alpha(stageLoader.tone, 0.25)
            Rectangle {
                height: parent.height
                radius: parent.radius
                width: parent.width * (stageLoader.indeterminate ? 1
                                       : Math.max(0, Math.min(1, (card.progress ? card.progress.percent || 0 : 0) / 100)))
                color: stageLoader.tone
                opacity: stageLoader.indeterminate ? 0.4 : 1
                Behavior on width { NumberAnimation { duration: 250 } }
            }
        }
    }

    // The platform, in the corner, as the library shows a file's kind.
    Rectangle {
        visible: card.stage.length === 0
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 6
        radius: 6
        color: Theme.alpha(Theme.cardBottom, 0.88)
        border.width: 1
        border.color: Theme.cardEdge
        implicitWidth: platformLabel.implicitWidth + 12
        implicitHeight: 18
        Text {
            id: platformLabel
            anchors.centerIn: parent
            text: card.ps2 ? "PS2" : "PS1"
            color: card.ps2 ? Theme.accent : Theme.cardText
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 0.6
        }
    }

    // No emulator for it yet: it can be sent, not converted.
    Rectangle {
        visible: !card.game.convertible && card.stage.length === 0
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        width: 22
        height: 22
        radius: 11
        color: Theme.alpha(Theme.cardBottom, 0.88)
        Icon {
            anchors.centerIn: parent
            name: "warning"
            size: 13
            color: Theme.warn
        }
        HoverHandler { id: warnHover }
        ToolTip.visible: warnHover.hovered
        ToolTip.text: (card.game.bootFile || "").length > 0
                      ? qsTr("This disc starts %1, not a game: a disc of software or a tool (PS2 Linux, a demo "
                             + "disc…). There is no game in it to convert.").arg(card.game.bootFile)
                      : qsTr("No emulator for this game yet: it can be sent as it is, not converted")
    }

    // The round box that picks it, above the card's own click.
    Rectangle {
        id: box
        x: 6
        y: 6
        width: 24
        height: 24
        radius: 12
        visible: card.checked || card.hover
        color: card.checked ? Theme.accent : card.overBox ? Theme.alpha(Theme.accent, 0.55)
                                                          : Theme.alpha("#000000", 0.35)
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
        ToolTip.visible: card.overBox
        ToolTip.text: card.checked ? qsTr("Unselect") : qsTr("Select, to do several at once")
    }
}
