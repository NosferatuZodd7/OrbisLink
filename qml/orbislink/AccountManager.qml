// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The saved PSN Account IDs, in the settings: each has a name of the user's
// choosing, and each console picks one of them (in the Consoles section) to
// register with. They can be added, renamed, corrected and removed here.
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

    function startEdit(key, label, accountId) {
        labelEdit.text = label
        idEdit.text = accountId
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
            color: Theme.accent
            font.bold: true
            font.pixelSize: 12
        }
        StyledButton {
            text: qsTr("Add Account ID…")
            minimumWidth: 130
            enabled: manager.editing === ""
            onClicked: manager.startEdit("+", "", "")
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("The PSN Account IDs saved on this PC. Each console chooses one in Consoles "
                   + "(Edit); it is what registration sends to the console.")
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
            implicitHeight: visible ? 52 : 0
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
                              + (row.modelData.usedBy.length > 0
                                 ? qsTr("used by %1").arg(row.modelData.usedBy.join(", "))
                                 : qsTr("not used by any console"))
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
                                                 row.modelData.accountId)
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
                    enabled: idEdit.ok
                    onClicked: {
                        var old = manager.editing === "+" ? "" : manager.editing
                        if (app.saveAccount(old, labelEdit.text, idEdit.base64))
                            manager.editing = ""
                    }
                }
            }
        }
    }
}
