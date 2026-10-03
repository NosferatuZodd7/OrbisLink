// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The console everything goes to, in the top bar, left of the services.
//
// * It answers: its name with the app's glow, breathing slowly — in gold,
//   with the jailbreak's name, when it has one (GoldHEN, etaHEN, HEN).
//   A click opens the consoles page.
// * It does not answer: its name faded. A click looks for it again.
// * No console saved: nothing.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: chip

    signal showConsoles()

    readonly property bool hasConsole: app.consoleAddress.length > 0 && app.consoles.length > 0
    readonly property bool streaming: typeof stream !== "undefined" && stream !== null && stream.streaming
    readonly property bool ftpUp: app.ftpState === "available"
    readonly property bool connected: ftpUp || app.remotePlayState === "available" || streaming
    readonly property bool checking: !connected && (app.ftpState === "checking"
                                                    || app.remotePlayState === "checking")
    readonly property string jailbreak: ftpUp ? (app.jailbreaks[app.consoleAddress] || "") : ""
    readonly property bool gold: ftpUp
    readonly property color tone: gold ? Theme.hen : Theme.accent

    visible: hasConsole
    implicitWidth: visible ? row.implicitWidth + 30 : 0
    implicitHeight: 32
    opacity: connected ? 1.0 : (hover.hovered ? 0.85 : 0.55)
    Behavior on opacity { NumberAnimation { duration: Theme.normal } }

    // The glow: a soft ring that breathes while the console answers.
    Rectangle {
        id: glow
        anchors.fill: body
        anchors.margins: -4
        radius: height / 2
        color: "transparent"
        border.width: 4
        border.color: Theme.alpha(chip.tone, 0.35)
        visible: chip.connected
        opacity: 0.35
        SequentialAnimation on opacity {
            running: chip.connected && chip.visible
            loops: Animation.Infinite
            NumberAnimation { to: 1.0; duration: 1400; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.35; duration: 1400; easing.type: Easing.InOutSine }
        }
    }

    Rectangle {
        id: body
        anchors.fill: parent
        radius: height / 2
        color: chip.connected ? Theme.alpha(chip.tone, chip.gold ? 0.16 : 0.12)
             : hover.hovered ? Theme.controlHover : Theme.controlFill
        border.width: 1
        border.color: chip.connected ? Theme.alpha(chip.tone, 0.8) : Theme.glassEdge
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: chip.checking ? "loader" : chip.gold ? "unlock" : "gamepad"
            spinning: chip.checking
            size: 14
            color: chip.connected ? (Theme.light ? Qt.darker(chip.tone, 1.2) : chip.tone) : Theme.textSecondary
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: app.consoleName
            color: chip.connected ? Theme.text : Theme.textSecondary
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: text.length > 0
            text: chip.jailbreak.length > 0 ? chip.jailbreak
                : chip.checking ? qsTr("looking…")
                : !chip.connected ? qsTr("not connected") : ""
            color: chip.jailbreak.length > 0 ? (Theme.light ? Qt.darker(Theme.hen, 1.25) : Theme.hen)
                                             : Theme.textMuted
            font.pixelSize: 11
            font.weight: chip.jailbreak.length > 0 ? Font.Bold : Font.Normal
        }
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler {
        onTapped: {
            if (chip.connected)
                chip.showConsoles()
            else
                app.checkServicesNow()
        }
    }

    ToolTip.visible: hover.hovered
    ToolTip.delay: 300
    ToolTip.text: !chip.connected
        ? qsTr("%1 (%2) is not answering. Click to look for it again.").arg(app.consoleName).arg(app.consoleAddress)
        : chip.gold
        ? qsTr("Connected to %1 (%2) — %3: sending and installing work.")
            .arg(app.consoleName).arg(app.consoleAddress).arg(chip.jailbreak.length > 0 ? chip.jailbreak : qsTr("FTP"))
        : qsTr("%1 (%2) answers, but has no FTP: Remote Play works; sending and installing need a jailbreak.")
            .arg(app.consoleName).arg(app.consoleAddress)
}
