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
    // Where it is on its way to the console (games.progress), or null.
    property var progress: null
    readonly property string stage: progress ? progress.stage : ""
    readonly property bool busy: stage === "waiting" || stage === "converting"
                                 || stage === "sending" || stage === "installing"
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
        border.width: card.selected || card.busy ? 2 : 1
        border.color: card.busy ? Theme.alpha(stageLoader.tone, 0.75)
                    : card.selected ? Theme.accent
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

        // The file, at the bottom (or, while it is on its way, where it is).
        Row {
            visible: card.stage.length === 0
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
            visible: !card.game.convertible && card.stage.length === 0
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16
            name: "warning"
            size: 15
            color: Theme.warn
        }

        // ── On its way: the step's own loader, what it is doing, and a
        // bar along the bottom edge in the step's colour.
        Row {
            visible: card.stage.length > 0
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16
            spacing: 10
            StageLoader {
                id: stageLoader
                anchors.verticalCenter: parent.verticalCenter
                stage: card.stage
                percent: card.progress ? card.progress.percent || 0 : 0
                waiting: card.progress ? card.progress.waiting === true : false
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - stageLoader.width - parent.spacing
                spacing: 1
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: {
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
                        return qsTr("Failed")
                    }
                    color: Theme.light ? Qt.darker(stageLoader.tone, 1.15) : Qt.lighter(stageLoader.tone, 1.2)
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: {
                        var s = card.stage
                        if (s === "converting" && card.progress && (card.progress.step === "download"
                                                                   || card.progress.step === "unpack"))
                            return qsTr("downloaded once")
                        if (s === "waiting" || s === "converting") return qsTr("into a PS4 package")
                        if (s === "converted") return qsTr("saved on this PC")
                        if (s === "sending") return qsTr("over FTP to the console")
                        if (s === "sent") return qsTr("sent over FTP")
                        if (s === "installing") return qsTr("on the console")
                        if (s === "installed") return qsTr("ready to play")
                        return card.progress && card.progress.message ? card.progress.message : ""
                    }
                    color: Theme.cardTextMuted
                    font.pixelSize: 11
                }
            }
        }

        // A bar near the bottom edge, in the step's colour, inset so it
        // stays clear of the rounded corners.
        Rectangle {
            visible: card.busy
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.radius
            anchors.rightMargin: Theme.radius
            anchors.bottomMargin: 6
            height: 3
            radius: 1.5
            color: Theme.alpha(stageLoader.tone, 0.16)
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
