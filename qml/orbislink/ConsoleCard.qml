// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A saved console, as a plate of the atlas you click.
//
// Two ways in, one button each at the foot of the plate:
//
// * Remote Play: the app asks the console how it is, wakes it if it is in
//   rest mode, waits for it to be ready and connects — or opens registration,
//   if this PC is not registered on it yet. While that is going on, the
//   button cancels. FTP and the installer work alongside.
// * FTP: only the file browser, as an alternative to FileZilla.
//
// A click anywhere else on the plate starts the console's preferred one
// (chosen when adding or editing it). On another console (not the one in
// use), either makes it the console in use first; the ✕ in its corner
// removes it from the list.
//
// The console in use gets the ornate plate (the brass double edge): it is
// the one thing on the page the rest is about. A console with a jailbreak
// is printed on warmer, brass-tinted paper, so it is told apart at a glance.
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
    // Lit for a moment, to show where a console that was just registered is.
    property bool spotlight: false
    // What a click on the card starts: "remoteplay" or "ftp".
    property string startMode: "remoteplay"
    readonly property bool prefersFtp: startMode === "ftp" && ftpAvailable
    // Which homebrew enabler it runs ("GoldHEN", "etaHEN", "HEN"), once known.
    property string jailbreak: ""
    // The card's colour: brass when the console has a jailbreak (its FTP
    // answers), the accent otherwise.
    readonly property color tone: ftpAvailable ? Theme.hen : Theme.accent

    // The saved accounts ([{ label, accountId }]), the one Remote Play uses
    // on this console (base64) and the ones registered on it. Only the
    // console in use offers the choice.
    property var accounts: []
    property string account: ""
    property var registeredAccounts: []
    readonly property bool showsAccount: current && available && accounts.length > 0
    readonly property string accountLabel: {
        for (var i = 0; i < accounts.length; ++i)
            if (accounts[i].accountId === account)
                return accounts[i].label.length > 0 ? accounts[i].label : qsTr("Account %1").arg(i + 1)
        return qsTr("Choose an account")
    }

    signal connect()
    signal cancel()
    signal edit()
    signal choose()
    signal remove()
    signal openFtp()
    signal chooseAccount(string accountId)
    signal manageAccounts()

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

    // One click, one request: after a click the card takes no other until
    // the console has answered (Remote Play connected or failed, FTP found
    // or not), and at least for a moment, so a double click is one click.
    property bool clickLocked: false
    readonly property bool waitingOnConsole: busy
        || (current && (app.ftpState === "checking" || app.remotePlayState === "checking"))
    readonly property bool cancelReady: !minimumLock.running && current && busy
    function releaseLock() {
        if (!minimumLock.running && !waitingOnConsole)
            clickLocked = false
    }
    onWaitingOnConsoleChanged: releaseLock()
    Timer {
        id: minimumLock
        interval: 1500
        onTriggered: card.releaseLock()
    }
    // Never stuck: whatever happens, the card listens again after a while.
    Timer {
        running: card.clickLocked
        interval: 30000
        onTriggered: card.clickLocked = false
    }
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

    readonly property color statusColor: mood === "ready" ? Theme.ok
                                       : mood === "unregistered" || mood === "busy" ? Theme.accent
                                       : mood === "standby" ? Theme.warn
                                       : mood === "offline" || mood === "remove" ? Theme.error
                                       : Theme.textSecondary
    // As text: on paper the tones need a darker shade to be read.
    readonly property color statusInk: Theme.light ? Qt.darker(statusColor, 1.35)
                                                   : Qt.lighter(statusColor, 1.2)

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
        if (name === "cancel") {
            // Only from the button, and never as the second half of a
            // double click on the card.
            if (!card.cancelReady)
                return
        } else {
            if (card.clickLocked)
                return
            card.clickLocked = true
            minimumLock.restart()
        }
        switch (name) {
        case "connect": card.connect(); break
        case "cancel": card.cancel(); break
        case "choose": card.choose(); break
        case "ftp": card.openFtp(); break
        }
    }

    // The page sets the width (its grid reflows with the window); this is
    // the plate's natural size.
    implicitWidth: 280
    implicitHeight: 240

    readonly property bool hover: area.containsMouse && actionName.length > 0 && actionName !== "cancel"
                                  && !clickLocked

    opacity: !available || (status === "offline" && !area.containsMouse) ? 0.85 : 1.0
    Behavior on opacity { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }

    // With the mouse over it, it lifts a little; when pressed, it sinks.
    scale: area.pressed && hover ? 0.99 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
    transform: Translate {
        y: card.hover && !area.pressed ? -3 : 0
        Behavior on y { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
    }

    // ── The spotlight: an accent ring that breathes a few times.
    Rectangle {
        id: spotlightRing
        anchors.fill: plate
        anchors.margins: -6
        radius: plate.radius + 6
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        opacity: 0
        SequentialAnimation {
            running: card.spotlight
            loops: 3
            onStopped: spotlightRing.opacity = 0
            NumberAnimation {
                target: spotlightRing; property: "opacity"
                from: 0; to: 0.9; duration: 450; easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: spotlightRing; property: "opacity"
                to: 0.2; duration: 650; easing.type: Easing.InOutCubic
            }
        }
    }

    // A soft shadow under the plate, a hint of the lift on hover.
    Rectangle {
        anchors.fill: plate
        anchors.topMargin: card.hover ? 6 : 3
        anchors.bottomMargin: card.hover ? -6 : -3
        radius: plate.radius
        color: Qt.rgba(0, 0, 0, Theme.light ? 0.06 : 0.22)
        Behavior on anchors.topMargin { NumberAnimation { duration: Theme.normal } }
    }

    Plate {
        id: plate
        anchors.fill: parent
        ornate: card.current && card.available
        // Brass-tinted paper for a console with a jailbreak.
        color: card.ftpAvailable
               ? Theme.mix(Theme.panelFill, Theme.hen, Theme.light ? 0.13 : 0.10)
               : Theme.panelFill
        border.color: plate.ornate ? Theme.alpha(Theme.brass, 0.7)
                    : card.hover ? Theme.alpha(card.tone, 0.6)
                    : card.ftpAvailable ? Theme.alpha(Theme.hen, 0.4)
                    : Theme.glassEdge
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: card.actionName.length > 0 && card.actionName !== "cancel" && !card.clickLocked
                     ? Qt.PointingHandCursor : Qt.ArrowCursor
        // The card itself only starts things; cancelling is the button's.
        onClicked: if (card.actionName !== "cancel") card.runAction(card.actionName)
        Accessible.role: Accessible.Button
        Accessible.name: (card.name.length > 0 ? card.name : qsTr("Console")) + " — " + card.statusText
    }

    readonly property int pad: 20

    // ── Head: PS4/PS5 in the serif, large; on the right the account (on the
    // console in use) and the registration button, or the ✕ on the others.
    Text {
        id: kindLabel
        x: card.pad
        y: 16
        text: card.known ? card.kind.toUpperCase() : qsTr("Console")
        color: Theme.ink
        font.family: Theme.fontDisplay
        font.pixelSize: card.known ? 36 : 26
        font.weight: Font.DemiBold
        font.letterSpacing: 0.5
    }

    StyledToolButton {
        id: cornerButton
        x: parent.width - width - 14
        y: 16
        width: 32
        height: 32
        visible: card.available && card.current
        iconName: "link"
        iconSize: 16
        Accessible.name: ToolTip.text
        ToolTip.visible: hovered
        ToolTip.text: card.registered ? qsTr("Register this PC again")
                                      : qsTr("Register this PC on the console")
        onClicked: card.edit()
    }
    StyledToolButton {
        x: parent.width - width - 14
        y: 16
        width: 32
        height: 32
        visible: !card.current
        danger: card.confirmRemoval
        iconName: "close"
        iconSize: 16
        Accessible.name: ToolTip.text
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Remove this console from the list")
        onClicked: {
            if (card.confirmRemoval)
                card.remove()
            else
                card.confirmRemoval = true
        }
    }

    // The account Remote Play goes in with, on the console in use: level
    // with PS4/PS5, beside the registration button. A click opens the list.
    Rectangle {
        id: accountChip
        anchors.right: cornerButton.left
        anchors.rightMargin: 6
        anchors.verticalCenter: cornerButton.verticalCenter
        visible: card.showsAccount
        height: 26
        // Measured apart: the label's width must not depend on the
        // chip's, which depends on the label.
        readonly property real chrome: 10 + 12 + 10 + 2 * accountRow.spacing + 10
        width: Math.max(0, Math.min(chrome + Math.ceil(labelMetrics.advanceWidth),
                                    cornerButton.x - 6 - kindLabel.x - kindLabel.implicitWidth - 12))
        radius: height / 2
        color: chipArea.containsMouse || accountMenu.visible ? Theme.alpha(Theme.ink, 0.08)
                                                             : Theme.controlFill
        border.width: 1
        border.color: chipArea.containsMouse || accountMenu.visible ? Theme.alpha(card.tone, 0.55)
                                                                    : Theme.glassEdge
        Behavior on color { ColorAnimation { duration: Theme.fast } }

        Row {
            id: accountRow
            anchors.verticalCenter: parent.verticalCenter
            x: 10
            spacing: 5
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "user"
                size: 12
                color: Theme.textSecondary
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, accountChip.width - accountChip.chrome)
                elide: Text.ElideRight
                text: card.accountLabel
                color: Theme.text
                font.pixelSize: 11
                font.weight: Font.Medium
            }
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-down"
                size: 10
                color: Theme.textSecondary
            }
        }

        TextMetrics {
            id: labelMetrics
            font.pixelSize: 11
            font.weight: Font.Medium
            text: card.accountLabel
        }

        MouseArea {
            id: chipArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: accountMenu.popup(accountChip, 0, accountChip.height + 4)
            Accessible.role: Accessible.ButtonMenu
            Accessible.name: card.accountLabel
        }
        ToolTip.visible: chipArea.containsMouse && !accountMenu.visible
        ToolTip.text: qsTr("The account (PSID) Remote Play uses on this console")

        Menu {
            id: accountMenu
            topPadding: 8
            bottomPadding: 8
            background: Rectangle {
                implicitWidth: 280
                color: Theme.menuFill
                border.color: Theme.glassEdge
                border.width: 1
                radius: 14
            }

            Instantiator {
                model: card.accounts
                delegate: StyledMenuItem {
                    readonly property bool chosen: modelData.accountId === card.account
                    readonly property bool registeredHere:
                        card.registeredAccounts.indexOf(modelData.accountId) >= 0
                    iconName: chosen ? "check" : "user"
                    text: (modelData.label.length > 0 ? modelData.label : qsTr("Account %1").arg(index + 1))
                          + (registeredHere ? "" : "  ·  " + qsTr("not registered here"))
                    onTriggered: if (!chosen) card.chooseAccount(modelData.accountId)
                }
                onObjectAdded: function (index, object) { accountMenu.insertItem(index, object) }
                onObjectRemoved: function (index, object) { accountMenu.removeItem(object) }
            }
            MenuSeparator {
                padding: 6
                leftPadding: 16
                rightPadding: 16
                contentItem: Rectangle { implicitHeight: 1; color: Theme.glassEdge }
            }
            StyledMenuItem {
                iconName: "users"
                text: qsTr("Manage accounts…")
                onTriggered: card.manageAccounts()
            }
        }
    }

    // ── The name in the serif, the address in mono, then the tags.
    Column {
        id: identity
        x: card.pad
        y: kindLabel.y + kindLabel.height - 2
        width: parent.width - 2 * card.pad
        spacing: 2

        Text {
            width: parent.width
            elide: Text.ElideRight
            text: card.name.length > 0 ? card.name : qsTr("Console")
            color: Theme.ink
            font.pixelSize: Theme.fontCardTitle
            font.family: Theme.fontDisplay
            font.weight: Font.DemiBold
        }
        Text {
            visible: card.address.length > 0
            text: card.address
            color: Theme.textSecondary
            font.pixelSize: 12
            font.family: Theme.fontMono
        }
    }

    Flow {
        id: tags
        x: card.pad
        y: identity.y + identity.height + 10
        width: parent.width - 2 * card.pad
        spacing: 6

        Tag {
            visible: card.current && card.available
            text: qsTr("In use")
            tone: "ok"
            iconName: "check"
        }
        // The jailbreak, in solid brass: the first fact the eye finds.
        Tag {
            visible: card.ftpAvailable
            text: card.jailbreak.length > 0 ? card.jailbreak : "HEN"
            tone: "brass"
            solid: true
            iconName: "unlock"
            HoverHandler { id: henHover }
            ToolTip.visible: henHover.hovered
            ToolTip.text: qsTr("This console has a jailbreak: its FTP server answers, so files and "
                               + "packages can be sent to it.")
        }
        Tag {
            visible: card.registered
            text: qsTr("Registered")
            iconName: "link"
        }
    }

    // ── Foot: a hairline, the state in one line, and the two ways in.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: stateLine.top
        anchors.leftMargin: card.pad
        anchors.rightMargin: card.pad
        anchors.bottomMargin: 12
        height: 1
        color: Theme.glassEdge
    }

    Row {
        id: stateLine
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: buttons.top
        anchors.leftMargin: card.pad
        anchors.rightMargin: card.pad
        anchors.bottomMargin: 14
        spacing: 8
        // The state's sign, pulsing while the console is being asked.
        Icon {
            id: stateGlyph
            anchors.verticalCenter: parent.verticalCenter
            name: card.stateIcon
            size: 14
            spinning: card.mood === "busy"
            color: card.statusInk
            SequentialAnimation on opacity {
                running: card.checking && card.mood !== "busy"
                loops: Animation.Infinite
                onStopped: stateGlyph.opacity = 1
                NumberAnimation { to: 0.35; duration: 550 }
                NumberAnimation { to: 1.0; duration: 550 }
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - stateGlyph.width - parent.spacing
            elide: Text.ElideRight
            text: card.statusText
            color: card.statusInk
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
    }

    Row {
        id: buttons
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: card.pad - 4
        anchors.rightMargin: card.pad - 4
        anchors.bottomMargin: 18
        spacing: 8
        readonly property real half: (width - spacing) / 2
        // Room enough for an icon beside the words.
        readonly property bool roomy: !card.ftpAvailable || half >= 140

        // Remote Play: connects (or wakes, or registers); on the console
        // being connected, cancels.
        StyledButton {
            // Alone, it takes the whole width.
            width: card.ftpAvailable ? buttons.half : buttons.width
            leftPadding: 10
            rightPadding: 10
            readonly property bool cancelling: card.current && card.busy
            text: cancelling ? qsTr("Cancel") : qsTr("Remote Play")
            iconName: !buttons.roomy ? "" : cancelling ? "loader" : "play"
            primary: !card.prefersFtp
            enabled: card.available && (cancelling ? card.cancelReady : !card.clickLocked)
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
            iconName: buttons.roomy ? "folder-open" : ""
            primary: card.prefersFtp
            enabled: !card.clickLocked
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Browse the console's files over FTP, without Remote Play")
            onClicked: card.runAction("ftp")
        }
    }
}
