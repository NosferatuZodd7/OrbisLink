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
    spacing: 8

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

    function saveEdit(oldAddress, name, address, accountId, startMode) {
        var target = address.trim()
        if (!app.updateConsole(oldAddress, name, target))
            return
        editing = ""
        app.setConsoleAccount(target, accountId)
        app.setConsoleStartMode(target, startMode)
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

    RowLayout {
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: qsTr("Consoles")
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
        }
        StyledButton {
            text: qsTr("Add console…")
            primary: true
            iconName: "plus"
            minimumWidth: 130
            onClicked: addDialog.open()
        }
    }

    // While a session runs, say why the console in use cannot change.
    Rectangle {
        visible: manager.sessionActive
        Layout.fillWidth: true
        implicitHeight: lockRow.implicitHeight + 20
        radius: 12
        color: Theme.alpha(Theme.warn, 0.12)
        border.color: Theme.alpha(Theme.warn, 0.35)
        Row {
            id: lockRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "info"
                size: 16
                color: Theme.warn
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 26
                wrapMode: Text.WordWrap
                text: qsTr("A Remote Play session is running. The console in use cannot be switched, "
                           + "edited or removed until it ends.")
                color: Theme.text
                font.pixelSize: 12
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Every console saved on this PC. The one in use is the one FTP, the installer and "
                   + "Remote Play talk to; its ports are edited below.")
        color: Theme.textMuted
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }

    Repeater {
        model: app.consoles

        delegate: Rectangle {
            id: row
            required property var modelData
            readonly property bool isEditing: manager.editing === modelData.address
            readonly property var registration: manager.registrationOf(modelData)
            readonly property string kindText: modelData.type === "ps5" ? "PS5"
                : modelData.type === "ps4" ? "PS4" : "?"

            Layout.fillWidth: true
            implicitHeight: content.implicitHeight + 28
            radius: 16
            color: Theme.panelAltFill
            border.color: modelData.active ? Theme.accent : Theme.border
            border.width: 1

            ColumnLayout {
                id: content
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: !row.isEditing

                    Rectangle {
                        implicitWidth: 46
                        implicitHeight: 34
                        radius: 10
                        color: Theme.controlFill
                        border.color: Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: row.kindText
                            color: Theme.text
                            font.bold: true
                            font.pixelSize: 11
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            spacing: 6
                            Text {
                                text: row.modelData.name
                                color: Theme.text
                                font.weight: Font.DemiBold
                                font.pixelSize: 14
                                elide: Text.ElideRight
                                Layout.maximumWidth: 220
                            }
                            Rectangle {
                                visible: row.modelData.active
                                implicitHeight: 20
                                implicitWidth: inUseText.implicitWidth + 18
                                radius: 10
                                color: Theme.alpha(Theme.ok, 0.14)
                                border.width: 1
                                border.color: Theme.alpha(Theme.ok, 0.3)
                                Text {
                                    id: inUseText
                                    anchors.centerIn: parent
                                    text: qsTr("in use")
                                    color: Theme.ok
                                    font.pixelSize: 11
                                    font.weight: Font.Medium
                                }
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
                        Text {
                            text: row.modelData.address + "  ·  " + (row.registration
                                ? qsTr("registered for Remote Play")
                                : manager.hostIdOf(row.modelData).length > 0
                                    ? qsTr("not registered")
                                    : qsTr("registration unknown until it answers"))
                            color: row.registration ? Theme.ok : Theme.textSecondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
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
                        ToolTip.text: qsTr("Removes this PC's Remote Play registration on this console. "
                                           + "Connecting again will need a new PIN.")
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

                    Text { text: qsTr("Name"); color: Theme.textMuted; font.pixelSize: 12 }
                    StyledField { id: nameEdit; Layout.fillWidth: true }

                    Text { text: qsTr("IP address"); color: Theme.textMuted; font.pixelSize: 12 }
                    StyledField {
                        id: addressEdit
                        Layout.fillWidth: true
                        placeholderText: "192.168.1.42"
                    }

                    Text { text: qsTr("Account ID"); color: Theme.textMuted; font.pixelSize: 12 }
                    StyledCombo {
                        id: accountBox
                        Layout.fillWidth: true
                        model: manager.accountChoices.map(function (a) { return a.label })
                    }

                    // What a click on the console's card starts.
                    Text { text: qsTr("Start with"); color: Theme.textMuted; font.pixelSize: 12 }
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
                                                 : row.modelData.startMode)
                        }
                    }
                }
            }
        }
    }

    // Registrations left behind by consoles that are no longer in the list.
    Text {
        visible: manager.orphans.length > 0
        Layout.fillWidth: true
        Layout.topMargin: 4
        text: qsTr("Registered on this PC, but not in the list:")
        color: Theme.textMuted
        font.pixelSize: 11
    }

    Repeater {
        model: manager.orphans

        delegate: Rectangle {
            id: orphan
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: 44
            radius: Theme.radiusSmall
            color: Theme.panelAltFill
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    text: (orphan.modelData.name.length > 0 ? orphan.modelData.name : qsTr("Console"))
                          + "  ·  " + (orphan.modelData.ps5 ? "PS5" : "PS4")
                          + "  ·  " + orphan.modelData.hostId
                    color: Theme.text
                    font.pixelSize: 12
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
