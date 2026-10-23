// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The saved PSN Account IDs, in the settings: each has a name of the user's
// choosing and its own Remote Play PIN, and registers on each console by
// itself. They can be added, renamed, corrected and removed here.
//
// The ID is stored in base64, the only form Remote Play accepts, but it is
// typed and shown in whatever form is at hand: AccountIdField converts it.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: manager
    spacing: 8

    readonly property bool hasStream: typeof stream !== "undefined" && stream !== null
    readonly property var registrations: hasStream ? stream.registrations : []

    // The consoles an account is registered on, by name.
    function registeredOn(accountId) {
        var names = []
        for (var i = 0; i < registrations.length; ++i)
            if (registrations[i].accountId === accountId) {
                var name = registrations[i].name.length > 0 ? registrations[i].name
                                                             : (registrations[i].ps5 ? "PS5" : "PS4")
                if (names.indexOf(name) < 0)
                    names.push(name)
            }
        return names
    }

    // The ID being edited ("" when none; "+" for a new one), and the one
    // waiting for a second click to confirm its removal.
    property string editing: ""
    property string confirming: ""

    // How the ID is shown in the list: the decimal the PSN gives, when it
    // can be converted.
    function shown(accountId) {
        if (hasStream) {
            var forms = stream.accountIdForms(accountId)
            if (forms.valid)
                return forms.decimal
        }
        return accountId
    }

    // Here and not in the row: removing rebuilds the rows, and a handler
    // whose row has just been destroyed stops halfway.
    function confirmRemove(accountId) {
        if (confirming === accountId) {
            confirming = ""
            app.removeAccount(accountId)
        } else {
            confirming = accountId
            confirmTimer.restart()
        }
    }

    function startEdit(key, label, accountId, pin) {
        labelEdit.text = label
        idEdit.text = accountId
        pinEdit.text = pin
        editing = key
    }

    Timer {
        id: confirmTimer
        interval: 4000
        onTriggered: manager.confirming = ""
    }

    RowLayout {
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: qsTr("Account IDs (PSID)")
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
        }
        StyledButton {
            text: qsTr("Add Account ID…")
            primary: true
            iconName: "plus"
            minimumWidth: 130
            enabled: manager.editing === ""
            onClicked: manager.startEdit("+", "", "", "")
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("The PSN Account IDs saved on this PC. Each one registers on a console by "
                   + "itself and has its own Remote Play PIN; with two or more, connecting asks "
                   + "which one to use.")
        color: Theme.textMuted
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }

    Text {
        visible: app.accounts.length === 0 && manager.editing === ""
        Layout.fillWidth: true
        text: qsTr("No Account ID saved yet.")
        color: Theme.textMuted
        font.pixelSize: 12
    }

    Repeater {
        model: app.accounts

        delegate: Rectangle {
            id: row
            required property var modelData
            Layout.fillWidth: true
            visible: manager.editing !== modelData.accountId
            implicitHeight: visible ? 58 : 0
            readonly property var registeredOn: manager.registeredOn(modelData.accountId)
            radius: Theme.radiusSmall
            color: Theme.panelAltFill
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: row.modelData.label
                        color: Theme.text
                        font.bold: true
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: manager.shown(row.modelData.accountId) + "  ·  "
                              + (row.registeredOn.length > 0
                                 ? qsTr("registered on %1").arg(row.registeredOn.join(", "))
                                 : qsTr("not registered on any console yet"))
                              + "  ·  "
                              + (row.modelData.remotePlayPin.length > 0 ? qsTr("PIN saved")
                                                                        : qsTr("no PIN saved"))
                        color: Theme.textMuted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                StyledButton {
                    text: qsTr("Edit")
                    minimumWidth: 64
                    enabled: manager.editing === ""
                    onClicked: manager.startEdit(row.modelData.accountId, row.modelData.label,
                                                 row.modelData.accountId, row.modelData.remotePlayPin)
                }
                StyledButton {
                    danger: true
                    text: manager.confirming === row.modelData.accountId ? qsTr("Confirm?") : qsTr("Remove")
                    minimumWidth: 70
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Removes it from the list. Consoles that used it are left "
                                       + "without one; registrations already made stay.")
                    onClicked: manager.confirmRemove(row.modelData.accountId)
                }
            }
        }
    }

    // Adding or editing: a name and the ID in any form.
    Rectangle {
        Layout.fillWidth: true
        visible: manager.editing !== ""
        implicitHeight: editForm.implicitHeight + 24
        radius: Theme.radiusSmall
        color: Theme.panelAltFill
        border.color: Theme.accent

        GridLayout {
            id: editForm
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            columns: 2
            columnSpacing: 12
            rowSpacing: 8

            // The username, so the ID can be told apart at a glance: the ID
            // itself is just a long number.
            Text { text: qsTr("Username"); color: Theme.textMuted; font.pixelSize: 12 }
            StyledField {
                id: labelEdit
                Layout.fillWidth: true
                placeholderText: qsTr("PSN username, to tell it apart")
            }

            Text {
                text: qsTr("Account ID")
                color: Theme.textMuted
                font.pixelSize: 12
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 8
            }
            AccountIdField {
                id: idEdit
                Layout.fillWidth: true
            }

            // This account's own PIN: never used for another account.
            Text {
                text: qsTr("Remote Play PIN")
                color: Theme.textMuted
                font.pixelSize: 12
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 8
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                StyledField {
                    id: pinEdit
                    Layout.fillWidth: true
                    placeholderText: qsTr("Optional — 4 or 8 digits")
                    maximumLength: 8
                    echoMode: activeFocus ? TextInput.Normal : TextInput.Password
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: RegularExpressionValidator { regularExpression: /[0-9]{0,8}/ }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: pinEdit.text.length > 0 && pinEdit.text.length !== 4 && pinEdit.text.length !== 8
                           ? Theme.error : Theme.textMuted
                    font.pixelSize: 11
                    text: qsTr("4 digits: the account's passcode, sent when the console asks for it "
                               + "while connecting. 8 digits: the pairing PIN, filled in when this "
                               + "account registers (the console shows a new one each time).")
                }
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
                    enabled: idEdit.ok && (pinEdit.text.length === 0 || pinEdit.text.length === 4
                                           || pinEdit.text.length === 8)
                    onClicked: {
                        var old = manager.editing === "+" ? "" : manager.editing
                        if (app.saveAccount(old, labelEdit.text, idEdit.base64, pinEdit.text))
                            manager.editing = ""
                    }
                }
            }
        }
    }
}
