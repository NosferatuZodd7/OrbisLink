// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The console everything goes to, at the top of the navigation: its name,
// its address and firmware, its jailbreak in brass, and its three services
// in a word each. A click opens the consoles page; the arrows look for
// the services again.
//
// * It answers: a ring in the accent around its seal — in brass, with the
//   jailbreak's name, when it has one (GoldHEN, etaHEN, HEN).
// * It does not answer: the card faded. A click looks for it again.
// * No console saved: an invitation to add one.
// * compact (the navigation folded): the seal alone, the rest on hover.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: chip

    signal showConsoles()

    property bool compact: false

    readonly property bool hasConsole: app.consoleAddress.length > 0 && app.consoles.length > 0
    readonly property bool streaming: typeof stream !== "undefined" && stream !== null && stream.streaming
    readonly property bool ftpUp: app.ftpState === "available"
    readonly property bool connected: ftpUp || app.remotePlayState === "available" || streaming
    readonly property bool checking: !connected && (app.ftpState === "checking"
                                                    || app.remotePlayState === "checking")
    readonly property string jailbreak: ftpUp ? (app.jailbreaks[app.consoleAddress] || "") : ""
    readonly property string henVersion: jailbreak.length > 0 ? (app.henVersions[app.consoleAddress] || "") : ""
    // "GoldHEN 2.4b18", or the name alone when its version is not known.
    readonly property string henLabel: henVersion.length > 0 ? jailbreak + " " + henVersion : jailbreak
    readonly property string firmware: connected ? app.consoleFirmware : ""
    readonly property bool gold: ftpUp
    readonly property color tone: gold ? Theme.henFill : Theme.accent
    // "PS4" or "PS5", for the seal.
    readonly property string kind: {
        for (var i = 0; i < app.consoles.length; ++i) {
            var c = app.consoles[i]
            if (c.address === app.consoleAddress)
                return c.type === "ps5" ? "PS5" : "PS4"
        }
        return "PS"
    }

    implicitWidth: compact ? 48 : 200
    implicitHeight: compact ? 48 : card.implicitHeight

    readonly property string tip: (!chip.hasConsole ? qsTr("No console saved yet. Click to add one.")
        : !chip.connected
        ? qsTr("%1 (%2) is not answering. Click to look for it again.").arg(app.consoleName).arg(app.consoleAddress)
        : chip.gold
        ? qsTr("Connected to %1 (%2) — %3: sending and installing work.")
            .arg(app.consoleName).arg(app.consoleAddress).arg(chip.jailbreak.length > 0 ? chip.henLabel : qsTr("FTP"))
        : qsTr("%1 (%2) answers, but has no FTP: Remote Play works; sending and installing need a jailbreak.")
            .arg(app.consoleName).arg(app.consoleAddress))
        + (chip.firmware.length > 0 ? "\n" + qsTr("Firmware %1.").arg(chip.firmware) : "")
        + (chip.jailbreak.length > 0 && chip.henVersion.length === 0
           ? "\n" + qsTr("%1 does not say its version over the network.").arg(chip.jailbreak) : "")

    function activate() {
        if (chip.connected || !chip.hasConsole)
            chip.showConsoles()
        else
            app.checkServicesNow()
    }

    // The seal: the console's kind inside a ring in the state's colour.
    component Seal: Rectangle {
        id: seal
        property real size: 40
        width: size
        height: size
        radius: size / 2
        color: Theme.alpha(chip.tone, chip.connected ? (Theme.light ? 0.16 : 0.18) : 0.06)
        border.width: 1.5
        border.color: chip.connected ? chip.tone : Theme.glassEdge
        Text {
            anchors.centerIn: parent
            text: chip.kind
            color: chip.connected ? Theme.ink : Theme.textSecondary
            font.family: Theme.fontDisplay
            font.pixelSize: seal.size * 0.34
            font.weight: Font.DemiBold
        }
        // It breathes while the console answers.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.alpha(chip.tone, 0.45)
            visible: chip.connected
            opacity: 0.3
            SequentialAnimation on opacity {
                running: chip.connected && chip.visible
                loops: Animation.Infinite
                NumberAnimation { to: 1.0; duration: 1600; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.3; duration: 1600; easing.type: Easing.InOutSine }
            }
        }
    }

    // ── Folded: the seal alone.
    Seal {
        visible: chip.compact
        anchors.centerIn: parent
        size: 40
        opacity: chip.connected || !chip.hasConsole ? 1.0 : 0.6
        HoverHandler { id: sealHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: chip.activate() }
        ToolTip.visible: sealHover.hovered
        ToolTip.delay: 300
        ToolTip.text: chip.tip
    }

    // ── Open: the card.
    Plate {
        id: card
        visible: !chip.compact
        anchors.left: parent.left
        anchors.right: parent.right
        implicitHeight: body.implicitHeight + 24
        radius: 14
        color: hover.hovered ? Theme.alpha(Theme.panel, 1.0) : Theme.panelFill
        ornate: chip.gold && chip.connected

        // Under the card's contents, so its own button keeps its clicks.
        MouseArea {
            id: hover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            readonly property bool hovered: containsMouse
            onClicked: chip.activate()
        }
        ToolTip.visible: hover.hovered && !services.containsHover
        ToolTip.delay: 500
        ToolTip.text: chip.tip

        ColumnLayout {
            id: body
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                opacity: chip.connected || !chip.hasConsole ? 1.0 : 0.65

                Seal { size: 38 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        text: chip.hasConsole ? app.consoleName : qsTr("No console yet")
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: !chip.hasConsole ? qsTr("Add one to begin")
                            : app.consoleAddress + (chip.firmware.length > 0 ? " · " + chip.firmware : "")
                        color: Theme.textSecondary
                        font.family: chip.hasConsole ? Theme.fontMono : Theme.fontSans
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

            }

            Tag {
                visible: chip.jailbreak.length > 0 || (chip.hasConsole && !chip.connected)
                text: chip.jailbreak.length > 0 ? chip.henLabel
                    : chip.checking ? qsTr("looking…") : qsTr("not connected")
                tone: chip.jailbreak.length > 0 ? "brass" : chip.checking ? "warn" : "neutral"
                solid: chip.jailbreak.length > 0
                iconName: chip.jailbreak.length > 0 ? "unlock" : ""
            }

            // The three services, a word each, under a hairline with the
            // button that looks for them again.
            RowLayout {
                Layout.fillWidth: true
                visible: chip.hasConsole
                spacing: 6
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: Theme.glassEdge
                }
                StyledToolButton {
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 26
                    iconName: chip.checking ? "loader" : "refresh"
                    spinning: chip.checking
                    iconSize: 13
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Check the services now")
                    onClicked: app.checkServicesNow()
                }
            }
            Column {
                id: services
                visible: chip.hasConsole
                Layout.fillWidth: true
                Layout.leftMargin: -8
                Layout.rightMargin: -8
                spacing: 0
                readonly property bool containsHover: rp.hovered || ftp.hovered || inst.hovered
                ServiceIndicator {
                    id: rp
                    readonly property bool hovered: hoverArea.hovered
                    HoverHandler { id: hoverArea }
                    width: parent.width
                    listed: true
                    label: qsTr("Remote Play")
                    iconName: "gamepad"
                    state_: app.remotePlayState
                    hint: app.remotePlayHint
                }
                ServiceIndicator {
                    id: ftp
                    readonly property bool hovered: ftpHover.hovered
                    HoverHandler { id: ftpHover }
                    width: parent.width
                    listed: true
                    label: qsTr("FTP")
                    state_: app.ftpState
                    hint: app.ftpHint
                }
                ServiceIndicator {
                    id: inst
                    readonly property bool hovered: instHover.hovered
                    HoverHandler { id: instHover }
                    width: parent.width
                    listed: true
                    label: qsTr("Installer")
                    iconName: "package"
                    state_: app.installerState
                    hint: app.installerHint
                }
            }
        }
    }
}
