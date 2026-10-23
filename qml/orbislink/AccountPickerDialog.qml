// SPDX-License-Identifier: AGPL-3.0-or-later
//
// With two or more saved accounts, connecting first asks which one: the
// session then uses that account's own registration on the console and its
// own PIN, never another account's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: dialog

    // The account chosen, in base64.
    signal chosen(string accountId)

    readonly property bool ready: typeof stream !== "undefined" && stream !== null
    property string selected: ""

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(520, parent.width - 32)
    modal: true
    padding: 0

    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radiusDialog
    }

    // The account offered first: the one this console used last, else the
    // first saved.
    function ask() {
        var accounts = app.accounts
        var last = ready ? stream.savedAccountId : ""
        selected = accounts.length > 0 ? accounts[0].accountId : ""
        for (var i = 0; i < accounts.length; ++i)
            if (accounts[i].accountId === last)
                selected = last
        open()
    }

    function shown(accountId) {
        if (ready) {
            var forms = stream.accountIdForms(accountId)
            if (forms.valid)
                return forms.decimal
        }
        return accountId
    }

    header: DialogHeader {
        title: qsTr("Which account?")
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
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Cancel")
                minimumWidth: 100
                onClicked: dialog.close()
            }
            StyledButton {
                text: qsTr("Continue")
                primary: true
                minimumWidth: 110
                enabled: dialog.selected.length > 0
                onClicked: {
                    var id = dialog.selected
                    dialog.close()
                    dialog.chosen(id)
                }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: Theme.dialogInner
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: 12
            text: qsTr("Remote Play uses this account's own registration on the console and its own PIN.")
        }

        Repeater {
            model: app.accounts
            delegate: Rectangle {
                id: row
                required property var modelData
                readonly property bool picked: dialog.selected === modelData.accountId
                readonly property bool registeredHere: dialog.ready
                    && stream.registeredAccounts.indexOf(modelData.accountId) >= 0
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                implicitHeight: 58
                radius: 12
                color: picked ? Theme.accentFill : rowArea.containsMouse ? Theme.controlHover : Theme.panelAltFill
                border.width: 1
                border.color: picked ? Theme.alpha(Theme.accent, 0.6) : Theme.border
                Behavior on color { ColorAnimation { duration: Theme.fast } }

                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: dialog.selected = row.modelData.accountId
                    onDoubleClicked: {
                        dialog.selected = row.modelData.accountId
                        dialog.close()
                        dialog.chosen(row.modelData.accountId)
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 12
                    // A radio mark.
                    Rectangle {
                        implicitWidth: 18
                        implicitHeight: 18
                        radius: 9
                        color: "transparent"
                        border.width: 2
                        border.color: row.picked ? Theme.accent : Theme.textMuted
                        Rectangle {
                            anchors.centerIn: parent
                            width: 8
                            height: 8
                            radius: 4
                            color: Theme.accent
                            visible: row.picked
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.label
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: dialog.shown(row.modelData.accountId) + "  ·  "
                                  + (row.modelData.remotePlayPin.length > 0 ? qsTr("PIN saved")
                                                                            : qsTr("no PIN saved"))
                            color: Theme.textMuted
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                    // Registered here, or registration comes first.
                    Rectangle {
                        implicitHeight: 22
                        implicitWidth: stateText.implicitWidth + 16
                        radius: 11
                        color: Theme.alpha(row.registeredHere ? Theme.ok : Theme.warn, 0.14)
                        border.width: 1
                        border.color: Theme.alpha(row.registeredHere ? Theme.ok : Theme.warn, 0.4)
                        Text {
                            id: stateText
                            anchors.centerIn: parent
                            text: row.registeredHere ? qsTr("Registered here") : qsTr("Needs registering")
                            color: row.registeredHere ? Theme.ok : Theme.warn
                            font.pixelSize: 11
                            font.weight: Font.Medium
                        }
                    }
                }
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
            text: qsTr("An account not registered on this console yet is registered first, with the "
                       + "PIN the console shows in Remote Play Connection Settings → Add Device.")
        }
    }
}
