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
        eyebrow: qsTr("Remote Play")
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
                // A glass row per account: faint glass, brighter under the
                // mouse, and a bright glass pill with an accent edge when picked.
                implicitHeight: 60
                radius: Theme.radius
                color: picked ? (Theme.light ? Qt.rgba(1, 1, 1, 0.92) : Qt.rgba(1, 1, 1, 0.14))
                     : rowArea.containsMouse ? Theme.controlHover
                     : (Theme.light ? Qt.rgba(1, 1, 1, 0.40) : Qt.rgba(1, 1, 1, 0.04))
                border.width: 1
                border.color: picked ? Theme.alpha(Theme.accent, 0.65)
                            : Theme.light ? Qt.rgba(1, 1, 1, 0.9) : Theme.glassEdge
                Behavior on color { ColorAnimation { duration: Theme.fast } }

                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: row.modelData.label
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
                        border.color: row.picked ? Theme.accent : Theme.alpha(Theme.ink, 0.3)
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
                            color: Theme.ink
                            font.family: Theme.fontDisplay
                            font.pixelSize: 15
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                        Row {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                id: idText
                                width: Math.min(implicitWidth, parent.width - pinText.implicitWidth)
                                text: dialog.shown(row.modelData.accountId)
                                color: Theme.textSecondary
                                font.family: Theme.fontMono
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                            Text {
                                id: pinText
                                text: "  ·  " + (row.modelData.remotePlayPin.length > 0 ? qsTr("PIN saved")
                                                                                      : qsTr("no PIN saved"))
                                color: Theme.textSecondary
                                font.pixelSize: 11
                            }
                        }
                    }
                    // Registered here, or registration comes first.
                    Tag {
                        text: row.registeredHere ? qsTr("Registered here") : qsTr("Needs registering")
                        tone: row.registeredHere ? "ok" : "warn"
                        iconName: row.registeredHere ? "check" : "link"
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
            color: Theme.textSecondary
            font.pixelSize: 11
            text: qsTr("An account not registered on this console yet is registered first, with the "
                       + "PIN the console shows in Remote Play Connection Settings → Add Device.")
        }
    }
}
