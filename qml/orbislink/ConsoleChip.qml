// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The console everything goes to, at the left of the toolbar: a pill with
// its kind, its name, its address and firmware, and its jailbreak in
// champagne. A click opens the list of saved consoles to switch to another
// (and a way to the consoles page); the state is in the ring round the
// kind — accent when it answers, champagne with a jailbreak, faded when it
// does not.
//
// * compact (a narrow window): the kind and the name only.
// * No console saved: an invitation to add one.
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

    implicitWidth: pill.implicitWidth
    implicitHeight: 40

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

    Rectangle {
        id: pill
        anchors.fill: parent
        implicitWidth: row.implicitWidth + 12 + 16
        radius: height / 2
        color: mouse.pressed ? Theme.controlHover : mouse.containsMouse ? Theme.controlHover : Theme.controlFill
        border.width: 1
        border.color: Theme.light ? Qt.rgba(1, 1, 1, 0.95) : Qt.rgba(1, 1, 1, mouse.containsMouse ? Theme.glassBorder + 0.10 : Theme.glassBorder)
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        opacity: chip.connected || !chip.hasConsole ? 1.0 : 0.7

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: height / 2
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.6 : 0.08) }
                GradientStop { position: 0.6; color: Qt.rgba(1, 1, 1, 0) }
            }
        }

        RowLayout {
            id: row
            anchors.left: parent.left
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            // The seal: the console's kind inside a ring in the state's colour.
            Rectangle {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: 16
                color: Theme.alpha(chip.tone, chip.connected ? 0.20 : 0.06)
                border.width: 1.5
                border.color: chip.connected ? chip.tone : Theme.glassEdge
                Text {
                    anchors.centerIn: parent
                    text: chip.kind
                    color: Theme.ink
                    font.pixelSize: 10
                    font.weight: Font.Bold
                }
                // It breathes while the console answers.
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
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

            ColumnLayout {
                spacing: 0
                Text {
                    text: chip.hasConsole ? app.consoleName : qsTr("No console yet")
                    color: Theme.ink
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.maximumWidth: 200
                }
                Text {
                    visible: !chip.compact
                    text: !chip.hasConsole ? qsTr("Add one to begin")
                        : chip.checking ? qsTr("looking…")
                        : !chip.connected ? qsTr("not connected")
                        : app.consoleAddress + (chip.firmware.length > 0 ? " · " + chip.firmware : "")
                    color: Theme.textSecondary
                    font.family: chip.hasConsole && chip.connected ? Theme.fontMono : Theme.fontSans
                    font.pixelSize: 10
                }
            }

            Tag {
                visible: chip.jailbreak.length > 0 && !chip.compact
                text: chip.henLabel
                tone: "brass"
                iconName: "unlock"
            }

            Icon {
                name: "chevron-down"
                size: 14
                color: Theme.textSecondary
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (!chip.hasConsole)
                chip.showConsoles()
            else
                picker.popup(0, chip.height + 6)
        }
    }
    ToolTip.visible: mouse.containsMouse && !picker.visible
    ToolTip.delay: 600
    ToolTip.text: chip.tip

    // The saved consoles: pick another to make it the one in use.
    Menu {
        id: picker
        topPadding: 6
        bottomPadding: 6
        background: Rectangle {
            implicitWidth: 280
            color: Theme.menuFill
            radius: 16
            border.width: 1
            border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Theme.glassEdge
        }

        Repeater {
            model: app.consoles
            delegate: StyledMenuItem {
                required property var modelData
                text: modelData.name + "  ·  " + modelData.address
                iconName: modelData.active ? "check" : (modelData.type === "ps5" ? "monitor" : "gamepad")
                enabled: !(chip.streaming && !modelData.active)
                onTriggered: if (!modelData.active) app.selectConsole(modelData.address)
            }
        }
        MenuSeparator {
            contentItem: Rectangle { implicitHeight: 1; color: Theme.glassEdge }
        }
        StyledMenuItem {
            text: qsTr("Check the services now")
            iconName: "refresh"
            onTriggered: app.checkServicesNow()
        }
        StyledMenuItem {
            text: qsTr("Show the consoles")
            iconName: "monitor"
            onTriggered: chip.showConsoles()
        }
    }
}
