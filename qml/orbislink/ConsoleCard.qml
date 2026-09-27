// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A saved console, in a card you click.
//
// Two ways in, one button each at the bottom:
//
// * Remote Play: the app asks the console how it is, wakes it if it is in
//   rest mode, waits for it to be ready and connects — or opens registration,
//   if this PC is not registered on it yet. While that is going on, the
//   button cancels. FTP and the installer work alongside.
// * FTP: only the file browser, as an alternative to FileZilla.
//
// A click anywhere else on the card starts the console's preferred one
// (chosen when adding or editing it). On another console (not the one in
// use), either makes it the console in use first; the ✕ in its corner
// removes it from the list.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: card

    // "ready", "standby", "offline" or "unknown", as it comes from the stream.
    property string status: "unknown"
    property bool registered: false
    property bool connecting: false
    property bool searching: false
    // The one-click connection step: "", "checking" or "waking".
    property string stage: ""
    property bool available: true   // false in a build without Remote Play
    // "ps4", "ps5", or empty when the console never answered — and then no
    // number is made up.
    property string kind: ""
    readonly property bool known: kind === "ps4" || kind === "ps5"
    property string name: ""
    property string address: ""
    // False for the other consoles in the list.
    property bool current: true
    // Whether the console answers on FTP. Without it (a PS5 with no
    // jailbreak, a PS4 without GoldHEN's FTP) there is no FTP button, and
    // the card always starts Remote Play.
    property bool ftpAvailable: false
    // What a click on the card starts: "remoteplay" or "ftp".
    property string startMode: "remoteplay"
    readonly property bool prefersFtp: startMode === "ftp" && ftpAvailable
    // The card's colour: green when the console answers on FTP, blue otherwise.
    readonly property color tone: ftpAvailable ? Theme.ok : Theme.accent

    signal connect()
    signal cancel()
    signal edit()
    signal choose()
    signal remove()
    signal openFtp()

    // The ✕ asks for a second click before removing.
    property bool confirmRemoval: false
    Timer {
        running: card.confirmRemoval
        interval: 3500
        onTriggered: card.confirmRemoval = false
    }

    // What the click does now.
    readonly property string actionName: {
        if (prefersFtp) return "ftp"
        if (!available) return ""
        if (!current) return "choose"
        if (connecting || stage.length > 0) return "cancel"
        return "connect"
    }
    readonly property bool busy: connecting || stage.length > 0 || (current && searching)
    readonly property bool checking: busy || (status === "unknown" && available)

    // The state as one word, which decides the colour and the icon.
    readonly property string mood: {
        if (!available) return "off"
        if (confirmRemoval) return "remove"
        if (busy) return "busy"
        if (status === "offline") return "offline"
        if (status === "unknown") return "busy"
        if (current && !registered) return "unregistered"
        if (status === "standby") return "standby"
        return "ready"
    }

    readonly property color statusColor: mood === "ready" || mood === "unregistered" ? Theme.accent
                                       : mood === "busy" ? Theme.accent
                                       : mood === "standby" ? Theme.warn
                                       : mood === "offline" || mood === "remove" ? Theme.error
                                       : Theme.textSecondary

    readonly property string stateIcon: mood === "ready" ? "play"
                                      : mood === "busy" ? "loader"
                                      : mood === "standby" ? "moon"
                                      : mood === "offline" ? "warning"
                                      : mood === "unregistered" ? "link"
                                      : mood === "remove" ? "close"
                                      : "info"

    // The state in a few words; what to do is on the buttons below.
    readonly property string statusText: {
        if (!available) return qsTr("Remote Play is not in this build")
        if (confirmRemoval) return qsTr("Click ✕ again to remove")
        if (current && connecting) return qsTr("Connecting…")
        if (current && stage === "waking") return qsTr("Waking the console…")
        if (current && stage === "checking") return qsTr("Checking the console…")
        if (current && searching) return qsTr("Searching for the console…")
        if (status === "offline") return qsTr("Not responding")
        if (status === "unknown") return qsTr("Checking…")
        if (current && !registered) return qsTr("Not registered for Remote Play")
        if (status === "standby") return qsTr("In rest mode")
        return qsTr("Ready")
    }

    function runAction(name) {
        switch (name) {
        case "connect": card.connect(); break
        case "cancel": card.cancel(); break
        case "choose": card.choose(); break
        case "ftp": card.openFtp(); break
        }
    }

    implicitWidth: 300
    implicitHeight: 312

    readonly property bool hover: area.containsMouse && actionName.length > 0

    opacity: !available || (status === "offline" && !area.containsMouse) ? 0.85 : 1.0
    Behavior on opacity { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }

    // With the mouse over it, it lifts; when pressed, it sinks a little.
    scale: area.pressed && hover ? Theme.pressScale : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
    transform: Translate {
        y: card.hover && !area.pressed ? -5 : 0
        Behavior on y { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
    }

    // ── A soft glow in the accent around the console in use; a faint
    // shadow under the others.
    Repeater {
        model: 3
        Rectangle {
            anchors.fill: body
            anchors.margins: -(index + 1) * 3
            radius: body.radius + (index + 1) * 3
            color: "transparent"
            border.width: 3
            border.color: card.current && card.available
                ? Theme.alpha(card.tone, (Theme.light ? 0.10 : 0.14) - index * 0.04)
                : Qt.rgba(0, 0, 0, Theme.light ? 0.025 - index * 0.008 : 0.10 - index * 0.03)
        }
    }

    Rectangle {
        id: body
        anchors.fill: parent
        radius: Theme.radius
        border.width: card.current && card.available ? 2 : 1
        border.color: card.current && card.available ? card.tone
                    : card.hover ? Theme.alpha(card.tone, 0.6)
                    : card.ftpAvailable ? Theme.alpha(Theme.ok, 0.35)
                    : Theme.cardEdge
        Behavior on border.color { ColorAnimation { duration: Theme.cardEase } }
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.cardTop }
            GradientStop {
                position: 1.0
                color: card.ftpAvailable
                       ? (card.current ? Theme.cardFtpActiveBottom : Theme.cardFtpBottom)
                       : (card.current && card.available ? Theme.cardActiveBottom : Theme.cardBottom)
            }
        }

        // A faint light along the top edge.
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: parent.border.width
            height: 70
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.0 : 0.05) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: card.actionName.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: card.runAction(card.actionName)
    }

    // ── Top: the state dot and the badge on the left; on the right the
    // registration (on the console in use) or the ✕ (on the others).
    Row {
        x: 20
        y: 20
        height: 28
        spacing: 10

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 10; height: 10; radius: 5
            color: card.statusColor
            Rectangle {
                anchors.centerIn: parent
                width: 18; height: 18; radius: 9
                color: "transparent"
                border.width: 4
                border.color: Theme.alpha(card.statusColor, 0.22)
            }
            SequentialAnimation on opacity {
                running: card.checking
                loops: Animation.Infinite
                onStopped: parent.opacity = 1
                NumberAnimation { to: 0.3; duration: 550 }
                NumberAnimation { to: 1.0; duration: 550 }
            }
        }

        // "In use" on the console in use, "Registered" on the others that are.
        Rectangle {
            readonly property bool inUse: card.current && card.available
            visible: inUse || card.registered
            anchors.verticalCenter: parent.verticalCenter
            height: 24
            width: badge.implicitWidth + 20
            radius: 12
            color: inUse ? Theme.alpha(Theme.ok, 0.14) : Theme.controlFill
            border.width: 1
            border.color: inUse ? Theme.alpha(Theme.ok, 0.30) : Theme.glassEdge
            Row {
                id: badge
                anchors.centerIn: parent
                spacing: 6
                Rectangle {
                    visible: parent.parent.inUse
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6; height: 6; radius: 3
                    color: Theme.ok
                }
                Icon {
                    visible: !parent.parent.inUse
                    anchors.verticalCenter: parent.verticalCenter
                    name: "link"
                    size: 12
                    color: Theme.textSecondary
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.parent.inUse ? qsTr("In use") : qsTr("Registered")
                    color: parent.parent.inUse ? Theme.ok : Theme.textSecondary
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }
            }
        }
    }

    StyledToolButton {
        x: parent.width - width - 12
        y: 14
        width: 32
        height: 32
        visible: card.available && card.current
        iconName: "link"
        iconSize: 16
        ToolTip.visible: hovered
        ToolTip.text: card.registered ? qsTr("Register this PC again")
                                      : qsTr("Register this PC on the console")
        onClicked: card.edit()
    }
    StyledToolButton {
        x: parent.width - width - 12
        y: 14
        width: 32
        height: 32
        visible: !card.current
        danger: card.confirmRemoval
        iconName: "close"
        iconSize: 16
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Remove this console from the list")
        onClicked: {
            if (card.confirmRemoval)
                card.remove()
            else
                card.confirmRemoval = true
        }
    }

    // ── Middle: PS4/PS5 in large type, the name and the IP.
    Column {
        x: 22
        y: 74
        width: parent.width - 44
        spacing: 6

        Text {
            text: card.known ? card.kind.toUpperCase() : qsTr("Console")
            color: Theme.cardText
            font.pixelSize: card.known ? 38 : 28
            font.weight: Font.Bold
            font.letterSpacing: -0.5
        }
        Item { width: 1; height: 2 }
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: card.name.length > 0 ? card.name : qsTr("Console")
            color: Theme.cardText
            font.pixelSize: Theme.fontCardTitle
            font.weight: Font.DemiBold
        }
        Text {
            visible: card.address.length > 0
            text: card.address
            color: Theme.cardTextMuted
            font.pixelSize: 13
        }
    }

    // ── Bottom: the state in one line, and the two ways in.
    Row {
        id: stateLine
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: buttons.top
        anchors.leftMargin: 20
        anchors.rightMargin: 16
        anchors.bottomMargin: 12
        spacing: 8
        Icon {
            id: stateGlyph
            anchors.verticalCenter: parent.verticalCenter
            name: card.stateIcon
            size: 15
            spinning: card.mood === "busy"
            color: Theme.light ? card.statusColor : Qt.lighter(card.statusColor, 1.25)
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - stateGlyph.width - parent.spacing
            elide: Text.ElideRight
            text: card.statusText
            color: Theme.light ? Qt.darker(card.statusColor, 1.2) : Qt.lighter(card.statusColor, 1.35)
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
    }

    Row {
        id: buttons
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        spacing: 8
        readonly property real half: (width - spacing) / 2

        // Remote Play: connects (or wakes, or registers); on the console
        // being connected, cancels.
        StyledButton {
            // Alone, it takes the whole width.
            width: card.ftpAvailable ? buttons.half : buttons.width
            leftPadding: 10
            rightPadding: 10
            readonly property bool cancelling: card.current && card.busy
            text: cancelling ? qsTr("Cancel") : qsTr("Remote Play")
            iconName: cancelling ? "loader" : "play"
            primary: !card.prefersFtp
            enabled: card.available
            ToolTip.visible: hovered && !card.available
            ToolTip.text: qsTr("Remote Play is not in this build")
            onClicked: card.runAction(!card.current ? "choose"
                                      : cancelling ? "cancel" : "connect")
        }
        // FTP: only the file browser.
        StyledButton {
            visible: card.ftpAvailable
            width: buttons.half
            leftPadding: 10
            rightPadding: 10
            text: qsTr("FTP")
            iconName: "folder"
            primary: card.prefersFtp
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Browse the console's files over FTP, without Remote Play")
            onClicked: card.runAction("ftp")
        }
    }
}
