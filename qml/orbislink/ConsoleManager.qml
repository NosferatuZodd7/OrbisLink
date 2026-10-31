// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every console saved on this PC, in the settings: switch to one, rename
// it, change its IP, remove it, forget this PC's Remote Play registration
// on it, or add a new one.
//
// The registration is tied to the console by its host-id (the MAC it
// reports in discovery), which is learned the first time the console
// answers. Registrations whose console is not in the list any more are
// shown at the end, so they can still be forgotten.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: manager
    spacing: 10

    readonly property bool hasStream: typeof stream !== "undefined" && stream !== null
    readonly property var registrations: hasStream ? stream.registrations : []
    // A Remote Play session is running (or starting) on the console in use.
    // Until it ends, that console stays the one in use: switching would move
    // FTP and the installer to another console while the picture still comes
    // from this one.
    readonly property bool sessionActive: hasStream
        && (stream.streaming || stream.sessionState === "connecting")
    readonly property string sessionLock: qsTr("End the Remote Play session first: it is running on "
                                               + "the console in use.")

    // The address of the row being edited ("" when none), and the one
    // waiting for a second click to confirm a removal or a forget.
    property string editing: ""
    property string confirming: ""
    property string confirmingAction: ""

    function sameId(a, b) {
        return a !== undefined && b !== undefined && a.length > 0
            && a.toUpperCase() === b.toUpperCase()
    }

    // The host-id of a saved console: the stored one or, for the console in
    // use, the one it has just reported.
    function hostIdOf(entry) {
        if (entry.hostId && entry.hostId.length > 0)
            return entry.hostId
        if (entry.active && hasStream)
            return stream.hostId
        return ""
    }

    function registrationOf(entry) {
        var id = hostIdOf(entry)
        for (var i = 0; i < registrations.length; ++i)
            if (sameId(registrations[i].hostId, id))
                return registrations[i]
        return null
    }

    // Registrations with no console in the list to show them against.
    readonly property var orphans: {
        var out = []
        var items = app.consoles
        for (var i = 0; i < registrations.length; ++i) {
            var matched = false
            for (var j = 0; j < items.length; ++j)
                if (sameId(registrations[i].hostId, hostIdOf(items[j])))
                    matched = true
            if (!matched)
                out.push(registrations[i])
        }
        return out
    }

    // The saved Account IDs as the dropdown lists them: "none" first.
    readonly property var accountChoices: {
        var out = [{ label: qsTr("No Account ID"), accountId: "" }]
        var items = app.accounts
        for (var i = 0; i < items.length; ++i)
            out.push(items[i])
        return out
    }

    function accountIndex(accountId) {
        for (var i = 0; i < accountChoices.length; ++i)
            if (accountChoices[i].accountId === accountId)
                return i
        return 0
    }

    // The actions live here and not in the rows: changing the list rebuilds
    // the rows, and a handler whose row has just been destroyed stops
    // halfway (the edit form would stay open, empty).
    // Whether a saved console answers on FTP (see StreamArea.ftpOk).
    function ftpOk(entry) {
        return entry.active ? app.ftpState === "available"
                            : app.ftpReachable[entry.address] === true
    }

    // The PIN is checked before anything is saved, so a mistyped one never
    // leaves the console half edited.
    property bool pinInvalid: false
    function saveEdit(oldAddress, name, address, accountId, startMode, pin) {
        var target = address.trim()
        var digits = pin.trim()
        pinInvalid = digits.length > 0 && !/^[0-9]{4}$/.test(digits)
        if (pinInvalid)
            return
        if (!app.updateConsole(oldAddress, name, target))
            return
        editing = ""
        app.setConsoleAccount(target, accountId)
        app.setConsoleStartMode(target, startMode)
        app.setConsolePin(target, digits)
    }

    function confirmForget(key, hostId) {
        if (isConfirming(key, "forget")) {
            confirming = ""
            stream.forgetRegistration(hostId)
        } else {
            askConfirm(key, "forget")
        }
    }

    function confirmRemove(address) {
        if (isConfirming(address, "remove")) {
            confirming = ""
            app.removeConsole(address)
        } else {
            askConfirm(address, "remove")
        }
    }

    function askConfirm(key, action) {
        confirming = key
        confirmingAction = action
        confirmTimer.restart()
    }

    function isConfirming(key, action) {
        return confirming === key && confirmingAction === action
    }

    Timer {
        id: confirmTimer
        interval: 4000
        onTriggered: { manager.confirming = ""; manager.confirmingAction = "" }
    }

    PageHeader {
        // Room on the right for the settings dialog's round ✕.
        Layout.rightMargin: 40
        Layout.bottomMargin: 8
        eyebrow: qsTr("Network")
        title: qsTr("Consoles")
        subtitle: qsTr("Every console saved on this PC. The one in use is the one FTP, the installer and "
                       + "Remote Play talk to; its ports are edited below.")
        StyledButton {
            text: qsTr("Add console…")
            primary: true
            iconName: "plus"
            minimumWidth: 130
            onClicked: addDialog.open()
        }
    }

    // While a session runs, say why the console in use cannot change.
    Plate {
        visible: manager.sessionActive
        Layout.fillWidth: true
        implicitHeight: lockRow.implicitHeight + 24
        radius: Theme.radiusSmall
        color: Theme.alpha(Theme.warn, 0.10)
        border.color: Theme.alpha(Theme.warn, 0.40)
        RowLayout {
            id: lockRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 10
            Icon {
                name: "info"
                size: 16
                color: Theme.light ? Qt.darker(Theme.warn, 1.3) : Theme.warn
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("A Remote Play session is running. The console in use cannot be switched, "
                           + "edited or removed until it ends.")
                color: Theme.text
                font.pixelSize: Theme.fontSmall
            }
        }
    }

    Repeater {
        model: app.consoles

        // One plate per console; the one in use is edged in the accent, and a
        // console that answers on FTP (a jailbreak) takes the brass of its card.
        delegate: Plate {
            id: row
            required property var modelData
            readonly property bool isEditing: manager.editing === modelData.address
            readonly property var registration: manager.registrationOf(modelData)
            readonly property string kindText: modelData.type === "ps5" ? "PS5"
                : modelData.type === "ps4" ? "PS4" : "?"

            Layout.fillWidth: true
            implicitHeight: content.implicitHeight + 32
            readonly property bool ftpHere: manager.ftpOk(modelData)
            color: modelData.active ? Qt.tint(Theme.panelFill, Theme.alpha(Theme.accent, 0.07)) : Theme.panelFill
            border.color: modelData.active ? (ftpHere ? Theme.alpha(Theme.henFill, 0.85) : Theme.alpha(Theme.accent, 0.8))
                        : ftpHere ? Theme.alpha(Theme.henFill, 0.45) : Theme.glassEdge

            ColumnLayout {
                id: content
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 18
                anchors.rightMargin: 16
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    visible: !row.isEditing

                    // The kind, in a round seal like the page's.
                    Rectangle {
                        implicitWidth: 46
                        implicitHeight: 46
                        radius: 23
                        color: Theme.alpha(Theme.ink, Theme.light ? 0.05 : 0.07)
                        border.width: 1
                        border.color: row.modelData.active ? Theme.alpha(Theme.brass, 0.6) : Theme.glassEdge
                        Text {
                            anchors.centerIn: parent
                            text: row.kindText
                            color: Theme.ink
                            font.family: Theme.fontMono
                            font.weight: Font.Bold
                            font.pixelSize: Theme.fontCaption
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        RowLayout {
                            spacing: 8
                            Text {
                                text: row.modelData.name
                                color: Theme.ink
                                font.family: Theme.fontDisplay
                                font.weight: Font.DemiBold
                                font.pixelSize: Theme.fontCardTitle
                                elide: Text.ElideRight
                                Layout.maximumWidth: 220
                            }
                            Tag {
                                visible: row.modelData.active
                                tone: "ok"
                                text: qsTr("in use")
                            }
                            // The jailbreak, in the same gold as the card.
                            Tag {
                                visible: row.ftpHere
                                tone: "brass"
                                solid: true
                                text: app.jailbreaks[row.modelData.address] || "HEN"
                            }
                        }
                        Text {
                            text: (row.modelData.accountLabel.length > 0
                                   ? qsTr("Account ID: %1").arg(row.modelData.accountLabel)
                                   : qsTr("No Account ID chosen"))
                                  + "  ·  "
                                  + (row.modelData.startMode === "ftp" && manager.ftpOk(row.modelData)
                                     ? qsTr("Starts with FTP") : qsTr("Starts with Remote Play"))
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: row.modelData.address
                                color: Theme.text
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSmall
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "·  " + (row.registration
                                    ? qsTr("registered for Remote Play")
                                    : manager.hostIdOf(row.modelData).length > 0
                                        ? qsTr("not registered")
                                        : qsTr("registration unknown until it answers"))
                                color: row.registration ? (Theme.light ? Qt.darker(Theme.ok, 1.3) : Theme.ok)
                                                        : Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                            }
                        }
                    }

                    StyledButton {
                        visible: !row.modelData.active
                        enabled: !manager.sessionActive
                        text: qsTr("Use")
                        minimumWidth: 64
                        ToolTip.visible: hovered && manager.sessionActive
                        ToolTip.text: manager.sessionLock
                        onClicked: app.selectConsole(row.modelData.address)
                    }
                    StyledButton {
                        readonly property bool locked: row.modelData.active && manager.sessionActive
                        enabled: !locked
                        text: qsTr("Edit")
                        minimumWidth: 64
                        ToolTip.visible: hovered && locked
                        ToolTip.text: manager.sessionLock
                        onClicked: {
                            nameEdit.text = row.modelData.name
                            addressEdit.text = row.modelData.address
                            accountBox.currentIndex = manager.accountIndex(row.modelData.accountId)
                            startBox.currentIndex = row.modelData.startMode === "ftp"
                                                    && manager.ftpOk(row.modelData) ? 1 : 0
                            pinEdit.text = row.modelData.remotePlayPin || ""
                            manager.pinInvalid = false
                            manager.editing = row.modelData.address
                        }
                    }
                    StyledButton {
                        visible: row.registration !== null
                        enabled: !(row.modelData.active && manager.sessionActive)
                        danger: manager.isConfirming(row.modelData.address, "forget")
                        text: manager.isConfirming(row.modelData.address, "forget")
                              ? qsTr("Confirm?") : qsTr("Forget registration")
                        minimumWidth: 90
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Removes this PC's Remote Play registrations on this console, "
                                           + "every account's. Connecting again will need a new PIN.")
                        onClicked: manager.confirmForget(row.modelData.address, row.registration.hostId)
                    }
                    StyledButton {
                        readonly property bool locked: row.modelData.active && manager.sessionActive
                        danger: true
                        enabled: app.consoles.length > 1 && !locked
                        text: manager.isConfirming(row.modelData.address, "remove")
                              ? qsTr("Confirm?") : qsTr("Remove")
                        minimumWidth: 70
                        ToolTip.visible: hovered
                        ToolTip.text: locked ? manager.sessionLock
                            : app.consoles.length > 1
                            ? qsTr("Removes it from the list. The Remote Play registration stays until you forget it.")
                            : qsTr("The only console in the list cannot be removed.")
                        onClicked: manager.confirmRemove(row.modelData.address)
                    }
                }

                // Editing: name and IP of this console.
                GridLayout {
                    Layout.fillWidth: true
                    visible: row.isEditing
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    Eyebrow {
                        Layout.columnSpan: 2
                        Layout.bottomMargin: 2
                        text: qsTr("Edit console")
                    }

                    Text { text: qsTr("Name"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; Layout.preferredWidth: 110 }
                    StyledField { id: nameEdit; Layout.fillWidth: true }

                    Text { text: qsTr("IP address"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    StyledField {
                        id: addressEdit
                        Layout.fillWidth: true
                        font.family: Theme.fontMono
                        placeholderText: "192.168.1.42"
                    }

                    Text { text: qsTr("Account ID"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    StyledCombo {
                        id: accountBox
                        Layout.fillWidth: true
                        model: manager.accountChoices.map(function (a) { return a.label })
                    }

                    // The passcode this console asks for when Remote Play
                    // connects: it is the console's, not shared with the others.
                    Text { text: qsTr("Remote Play PIN"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    StyledField {
                        id: pinEdit
                        Layout.fillWidth: true
                        font.family: Theme.fontMono
                        echoMode: TextInput.Password
                        maximumLength: 4
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: RegularExpressionValidator { regularExpression: /[0-9]{0,4}/ }
                        placeholderText: qsTr("4 digits — empty: the account's PIN")
                        onTextChanged: manager.pinInvalid = false
                    }
                    Item { implicitWidth: 1 }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: manager.pinInvalid
                              ? qsTr("The console's Remote Play PIN is 4 digits.")
                              : qsTr("Only for this console: sent when it asks for the passcode while Remote Play "
                                     + "connects. Each console keeps its own.")
                        color: manager.pinInvalid ? Theme.error : Theme.textSecondary
                        font.pixelSize: 11
                    }

                    // What a click on the console's card starts.
                    Text { text: qsTr("Start with"); color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                    StyledCombo {
                        id: startBox
                        Layout.fillWidth: true
                        // Without FTP on the console there is nothing to choose.
                        readonly property bool ftpHere: manager.ftpOk(row.modelData)
                        enabled: ftpHere
                        model: [qsTr("Remote Play (with FTP and the installer)"), qsTr("FTP only")]
                        onFtpHereChanged: if (!ftpHere) currentIndex = 0
                    }

                    Item { implicitWidth: 1; visible: !startBox.ftpHere }
                    Text {
                        visible: !startBox.ftpHere
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("This console is not answering on FTP, so it starts with Remote Play. "
                                   + "FTP needs a jailbreak: GoldHEN on the PS4, etaHEN on the PS5.")
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }

                    Item { implicitWidth: 1 }
                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        StyledButton {
                            text: qsTr("Cancel")
                            minimumWidth: 90
                            onClicked: manager.editing = ""
                        }
                        StyledButton {
                            text: qsTr("Save")
                            primary: true
                            minimumWidth: 90
                            onClicked: manager.saveEdit(row.modelData.address, nameEdit.text, addressEdit.text,
                                manager.accountChoices[accountBox.currentIndex].accountId,
                                // Locked: what was chosen before stays stored, for when FTP appears.
                                startBox.enabled ? (startBox.currentIndex === 1 ? "ftp" : "remoteplay")
                                                 : row.modelData.startMode,
                                pinEdit.text)
                        }
                    }
                }
            }
        }
    }

    // Registrations left behind by consoles that are no longer in the list.
    Eyebrow {
        visible: manager.orphans.length > 0
        Layout.fillWidth: true
        Layout.topMargin: 12
        Layout.leftMargin: 4
        text: qsTr("Registered on this PC, but not in the list:")
    }

    Repeater {
        model: manager.orphans

        delegate: Plate {
            id: orphan
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: 52
            radius: Theme.radiusSmall

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    text: (orphan.modelData.name.length > 0 ? orphan.modelData.name : qsTr("Console"))
                          + "  ·  " + (orphan.modelData.ps5 ? "PS5" : "PS4")
                          + "  ·  " + orphan.modelData.hostId
                    color: Theme.text
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }
                StyledButton {
                    danger: manager.isConfirming(orphan.modelData.hostId, "forget")
                    text: manager.isConfirming(orphan.modelData.hostId, "forget")
                          ? qsTr("Confirm?") : qsTr("Forget registration")
                    minimumWidth: 90
                    onClicked: manager.confirmForget(orphan.modelData.hostId, orphan.modelData.hostId)
                }
            }
        }
    }

    AddConsoleDialog {
        id: addDialog
        selectAfterAdd: false
    }
}
