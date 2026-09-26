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
            color: Theme.accent
            font.bold: true
            font.pixelSize: 12
        }
        StyledButton {
            text: qsTr("Add console…")
            minimumWidth: 130
            onClicked: addDialog.open()
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
            implicitHeight: content.implicitHeight + 20
            radius: Theme.radiusSmall
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
                        implicitWidth: 40
                        implicitHeight: 26
                        radius: 6
                        color: "transparent"
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
                                font.bold: true
                                font.pixelSize: 13
                                elide: Text.ElideRight
                                Layout.maximumWidth: 200
                            }
                            Text {
                                visible: row.modelData.active
                                text: qsTr("in use")
                                color: Theme.accent
                                font.pixelSize: 11
                            }
                        }
                        Text {
                            text: row.modelData.address + "  ·  " + (row.registration
                                ? qsTr("registered for Remote Play")
                                : manager.hostIdOf(row.modelData).length > 0
                                    ? qsTr("not registered")
                                    : qsTr("registration unknown until it answers"))
                            color: row.registration ? Theme.ok : Theme.textMuted
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    StyledButton {
                        visible: !row.modelData.active
                        text: qsTr("Use")
                        minimumWidth: 64
                        onClicked: app.selectConsole(row.modelData.address)
                    }
                    StyledButton {
                        text: qsTr("Edit")
                        minimumWidth: 64
                        onClicked: {
                            nameEdit.text = row.modelData.name
                            addressEdit.text = row.modelData.address
                            manager.editing = row.modelData.address
                        }
                    }
                    StyledButton {
                        visible: row.registration !== null
                        danger: manager.isConfirming(row.modelData.address, "forget")
                        text: manager.isConfirming(row.modelData.address, "forget")
                              ? qsTr("Confirm?") : qsTr("Forget registration")
                        minimumWidth: 90
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Removes this PC's Remote Play registration on this console. "
                                           + "Connecting again will need a new PIN.")
                        onClicked: {
                            if (manager.isConfirming(row.modelData.address, "forget")) {
                                stream.forgetRegistration(row.registration.hostId)
                                manager.confirming = ""
                            } else {
                                manager.askConfirm(row.modelData.address, "forget")
                            }
                        }
                    }
                    StyledButton {
                        danger: true
                        enabled: app.consoles.length > 1
                        text: manager.isConfirming(row.modelData.address, "remove")
                              ? qsTr("Confirm?") : qsTr("Remove")
                        minimumWidth: 70
                        ToolTip.visible: hovered
                        ToolTip.text: app.consoles.length > 1
                            ? qsTr("Removes it from the list. The Remote Play registration stays until you forget it.")
                            : qsTr("The only console in the list cannot be removed.")
                        onClicked: {
                            if (manager.isConfirming(row.modelData.address, "remove")) {
                                app.removeConsole(row.modelData.address)
                                manager.confirming = ""
                            } else {
                                manager.askConfirm(row.modelData.address, "remove")
                            }
                        }
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
                            onClicked: {
                                if (app.updateConsole(row.modelData.address, nameEdit.text, addressEdit.text))
                                    manager.editing = ""
                            }
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
                    onClicked: {
                        if (manager.isConfirming(orphan.modelData.hostId, "forget")) {
                            stream.forgetRegistration(orphan.modelData.hostId)
                            manager.confirming = ""
                        } else {
                            manager.askConfirm(orphan.modelData.hostId, "forget")
                        }
                    }
                }
            }
        }
    }

    AddConsoleDialog {
        id: addDialog
        selectAfterAdd: false
    }
}
