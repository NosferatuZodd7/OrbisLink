// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Registering this PC on the console. Without it there is no Remote Play:
// the console only accepts sessions from devices it has authorised itself.
// Each account registers by itself: this registration belongs to the
// account chosen here, and only that account's sessions use it.
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

    // A sheet of glass floating over the window, lit along its upper edge;
    // it fades in and settles, without bouncing.
    background: Plate {
        level: "secondary"
        radius: Theme.radiusDialog
        color: Theme.dialogFill
        border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Theme.glassEdge
    }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
        NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast; easing.type: Theme.easeOut }
    }

    // The account being registered starts as the one chosen to connect, with
    // the pairing PIN (eight digits) saved for this console — or, when the
    // console has none, the one saved for the account.
    function pinOf(accountId) {
        var consoles = app.consoles
        for (var c = 0; c < consoles.length; ++c)
            if (consoles[c].address === app.consoleAddress && consoles[c].remotePlayPin.length === 8)
                return consoles[c].remotePlayPin
        var accounts = app.accounts
        for (var i = 0; i < accounts.length; ++i)
            if (accounts[i].accountId === accountId)
                return accounts[i].remotePlayPin.length === 8 ? accounts[i].remotePlayPin : ""
        return ""
    }
    onOpened: {
        var chosen = ready ? stream.savedAccountId : ""
        savedBox.currentIndex = 0
        var accounts = app.accounts
        for (var i = 0; i < accounts.length; ++i)
            if (accounts[i].accountId === chosen)
                savedBox.currentIndex = i + 1
        accountField.text = chosen
        pinField.text = pinOf(chosen)
        pinField.forceActiveFocus()
    }

    header: DialogHeader {
        eyebrow: qsTr("Remote Play")
        title: qsTr("Register this PC on the console")
        dialog: dialog
    }

    footer: Rectangle {
        implicitHeight: Theme.dialogFooter
        color: "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogInner
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 10
            Spinner {
                visible: dialog.busy
                size: 20
            }
            Text {
                visible: dialog.busy
                text: qsTr("Talking to the console…")
                color: Theme.textSecondary
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
        spacing: 16

        // Step by step, because this always fails for the same reasons: an
        // expired PIN or a swapped Account ID. Numbered as an atlas numbers
        // its plates, with no box around them (the dialog is the box).
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: 6
            spacing: 10

            Repeater {
                model: [
                    { text: (typeof stream !== "undefined" && stream && stream.consolePs5)
                            ? qsTr("On the PS5: Settings → System → Remote Play → Link Device, "
                                   + "signed in with the account you will use.")
                            : qsTr("On the console: Settings → Remote Play Connection Settings → "
                                   + "Add Device."),
                      strong: true },
                    { text: qsTr("The 8-digit PIN it shows lasts only a few minutes. If it fails, "
                                 + "ask the console for another."),
                      strong: false },
                    { text: app.accounts.length > 1
                            ? qsTr("This registration belongs to the account below only; each account "
                                   + "registers by itself.") : "",
                      strong: false }
                ]
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    visible: modelData.text.length > 0
                    Layout.fillWidth: true
                    spacing: 12
                    // The step's numeral in a small brass ring.
                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: 24
                        implicitHeight: 24
                        radius: 12
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.alpha(Theme.brass, 0.7)
                        Text {
                            anchors.centerIn: parent
                            text: index + 1
                            color: Theme.brass
                            font.family: Theme.fontDisplay
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: 3
                        wrapMode: Text.WordWrap
                        color: modelData.strong ? Theme.text : Theme.textSecondary
                        font.pixelSize: modelData.strong ? 13 : 12
                        lineHeight: 1.15
                        text: modelData.text
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            implicitHeight: 1
            color: Theme.glassEdge
        }

        // The fields, each under its own small-caps label.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 6

            Eyebrow { text: qsTr("PIN"); color: Theme.textSecondary }
            StyledField {
                id: pinField
                Layout.fillWidth: true
                placeholderText: "12345678"
                font.family: Theme.fontMono
                maximumLength: 8
                inputMethodHints: Qt.ImhDigitsOnly
                validator: RegularExpressionValidator { regularExpression: /[0-9]{0,8}/ }
            }

            // A saved Account ID fills the field below in one click.
            Eyebrow {
                visible: app.accounts.length > 0
                Layout.topMargin: 8
                text: qsTr("Saved Account ID")
                color: Theme.textSecondary
            }
            StyledCombo {
                id: savedBox
                visible: app.accounts.length > 0
                Layout.fillWidth: true
                model: [qsTr("Choose…")].concat(app.accounts.map(function (a) { return a.label }))
                onActivated: function (index) {
                    if (index > 0) {
                        accountField.text = app.accounts[index - 1].accountId
                        // That account's own PIN, never the previous one's.
                        pinField.text = dialog.pinOf(app.accounts[index - 1].accountId)
                    }
                }
            }

            Eyebrow {
                Layout.topMargin: 8
                text: qsTr("Account ID (PSN)")
                color: Theme.textSecondary
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
            color: Theme.textSecondary
            font.pixelSize: 11
            lineHeight: 1.15
            text: qsTr("The Account ID is a 64-bit number belonging to the PSN account that uses "
                       + "the console — it is not the username. Paste it however you have it: "
                       + "hexadecimal, decimal or already in base64. The console only accepts the "
                       + "base64 form, and that conversion now happens here.")
        }
    }
}
