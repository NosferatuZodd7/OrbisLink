// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A saved console, in a card you click.
//
// One click connects: the app asks the console how it is, wakes it if it is
// in rest mode, waits for it to be ready and connects — or opens registration,
// if this PC is not registered on it yet. While that is going on, another
// click cancels. What is happening is written on the button at the bottom.
//
// On another console (not the one in use), the click makes it the console in
// use and connects in the same go; the ✕ in its corner removes it from the list.
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

    signal connect()
    signal cancel()
    signal edit()
    signal choose()
    signal remove()

    // The ✕ asks for a second click before removing.
    property bool confirmRemoval: false
    Timer {
        running: card.confirmRemoval
        interval: 3500
        onTriggered: card.confirmRemoval = false
    }

    // What the click does now.
    readonly property string actionName: {
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

    readonly property string statusText: {
        if (!available) return qsTr("Remote Play is not in this build")
        if (confirmRemoval) return qsTr("Click ✕ again to remove")
        if (!current) {
            if (status === "ready") return qsTr("Ready — click to connect")
            if (status === "standby") return qsTr("In rest mode — click to wake and connect")
            if (status === "offline") return qsTr("Not responding — click to try to connect")
            return qsTr("Checking — click to connect")
        }
        if (connecting) return qsTr("Connecting… — click to cancel")
        if (stage === "waking") return qsTr("Waking the console… — click to cancel")
        if (stage === "checking") return qsTr("Checking the console… — click to cancel")
        if (searching) return qsTr("Searching for the console…")
        if (status === "offline") return qsTr("Not responding — click to try to connect")
        if (status === "unknown") return qsTr("Click to connect")
        if (!registered) return qsTr("Not registered — click to register")
        if (status === "standby") return qsTr("In rest mode — click to wake and connect")
        return qsTr("Ready — click to connect")
    }

    implicitWidth: 264
    implicitHeight: 296

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
                ? Theme.alpha(Theme.accent, (Theme.light ? 0.10 : 0.14) - index * 0.04)
                : Qt.rgba(0, 0, 0, Theme.light ? 0.025 - index * 0.008 : 0.10 - index * 0.03)
        }
    }

    Rectangle {
        id: body
        anchors.fill: parent
        radius: Theme.radius
        border.width: card.current && card.available ? 2 : 1
        border.color: card.current && card.available ? Theme.accent
                    : card.hover ? Theme.alpha(Theme.accent, 0.6)
                    : Theme.cardEdge
        Behavior on border.color { ColorAnimation { duration: Theme.cardEase } }
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.cardTop }
            GradientStop {
                position: 1.0
                color: card.current && card.available ? Theme.cardActiveBottom : Theme.cardBottom
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
        onClicked: {
            switch (card.actionName) {
            case "connect": card.connect(); break
            case "cancel": card.cancel(); break
            case "choose": card.choose(); break
            }
        }
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

    // ── Bottom: the button with the state and what the click does.
    Rectangle {
        id: button
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        height: 48
        radius: 14
        readonly property bool lit: card.current && card.available
        color: {
            var c = card.statusColor
            var strength = lit ? (Theme.light ? 0.16 : 0.32) : (Theme.light ? 0.09 : 0.16)
            return Theme.alpha(c, strength * (card.hover ? 1.3 : 1.0))
        }
        border.width: 1
        border.color: Theme.alpha(card.statusColor, lit ? 0.45 : 0.22)
        Behavior on color { ColorAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }

        Icon {
            id: stateGlyph
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            name: card.stateIcon
            size: 18
            spinning: card.mood === "busy"
            color: Theme.light ? card.statusColor : Qt.lighter(card.statusColor, 1.25)
        }
        Text {
            anchors.left: stateGlyph.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            lineHeight: 0.95
            text: card.statusText
            color: Theme.light ? Qt.darker(card.statusColor, 1.2) : Qt.lighter(card.statusColor, 1.35)
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
    }
}
