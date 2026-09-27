// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Registering this PC on the console. Without it there is no Remote Play:
// the console only accepts sessions from devices it has authorised itself.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: dialog

    // See the note in StreamArea.qml: when closing the window the controller
    // goes away before the bindings.
    readonly property bool ready: typeof stream !== "undefined" && stream !== null
    readonly property bool busy: ready && stream.registering
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 610
    modal: true
    padding: 0

    // A nearly opaque modal: with the panels' transparency, what is behind
    // would show through the box, and a box asking for a decision must not
    // be a window.
    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radius
    }

    onOpened: {
        pinField.text = ""
        savedBox.currentIndex = 0
        pinField.forceActiveFocus()
    }

    header: Rectangle {
        implicitHeight: Theme.dialogHeader
        color: "transparent"
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.dialogMargin
            text: qsTr("Register this PC on the console")
            color: Theme.text
            font.pixelSize: 15
            font.bold: true
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
    }

    footer: Rectangle {
        implicitHeight: Theme.dialogFooter
        color: "transparent"
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogInner
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 10
            BusyIndicator {
                running: dialog.busy
                visible: dialog.busy
                implicitWidth: 22
                implicitHeight: 22
            }
            Text {
                visible: dialog.busy
                text: qsTr("Talking to the console…")
                color: Theme.textMuted
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Close")
                minimumWidth: 100
                onClicked: dialog.close()
            }
            StyledButton {
                text: qsTr("Register")
                minimumWidth: 110
                // Only enabled once the Account ID gives a valid value: a
                // button that can be pressed and fails on the console is worse
                // than a greyed-out button.
                enabled: pinField.text.length >= 8 && accountField.ok
                         && dialog.ready && !dialog.busy
                primary: true
                onClicked: if (dialog.ready) stream.registerConsole(pinField.text, accountField.base64)
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        // Step by step, because this always fails for the same reasons: an
        // expired PIN or a swapped Account ID.
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: Theme.dialogInner
            color: Theme.panelAltFill
            border.color: Theme.border
            radius: 8
            implicitHeight: steps.implicitHeight + 28

            ColumnLayout {
                id: steps
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.dialogInner
                anchors.rightMargin: Theme.dialogInner
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: 12
                    text: (typeof stream !== "undefined" && stream && stream.consolePs5)
                          ? qsTr("On the PS5: Settings → System → Remote Play → Link Device, "
                                 + "signed in with the account you will use.")
                          : qsTr("On the console: Settings → Remote Play Connection Settings → "
                                 + "Add Device.")
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: 11
                    text: qsTr("The 8-digit PIN it shows lasts only a few minutes. If it fails, "
                               + "ask the console for another.")
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            columns: 2
            columnSpacing: 12
            rowSpacing: 8

            Text { text: qsTr("PIN"); color: Theme.textMuted; font.pixelSize: 12 }
            StyledField {
                id: pinField
                Layout.fillWidth: true
                placeholderText: "12345678"
                maximumLength: 8
                inputMethodHints: Qt.ImhDigitsOnly
                validator: RegularExpressionValidator { regularExpression: /[0-9]{0,8}/ }
            }

            // A saved Account ID fills the field below in one click.
            Text {
                visible: app.accounts.length > 0
                text: qsTr("Saved Account ID")
                color: Theme.textMuted
                font.pixelSize: 12
            }
            StyledCombo {
                id: savedBox
                visible: app.accounts.length > 0
                Layout.fillWidth: true
                model: [qsTr("Choose…")].concat(app.accounts.map(function (a) { return a.label }))
                onActivated: function (index) {
                    if (index > 0)
                        accountField.text = app.accounts[index - 1].accountId
                }
            }

            Text {
                text: qsTr("Account ID (PSN)")
                color: Theme.textMuted
                font.pixelSize: 12
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 8
            }
            AccountIdField {
                id: accountField
                Layout.fillWidth: true
                // Pre-filled with the last one the console accepted. The PIN
                // changes every time; the Account ID does not.
                //
                // The guard is not fussiness: in a build without Remote Play
                // "stream" does not exist and the binding would blow up with
                // "Cannot read property of null".
                text: (typeof stream !== "undefined" && stream) ? stream.savedAccountId : ""
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.bottomMargin: Theme.dialogInner
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.pixelSize: 11
            text: qsTr("The Account ID is a 64-bit number belonging to the PSN account that uses "
                       + "the console — it is not the username. Paste it however you have it: "
                       + "hexadecimal, decimal or already in base64. The console only accepts the "
                       + "base64 form, and that conversion now happens here.")
        }
    }
}
