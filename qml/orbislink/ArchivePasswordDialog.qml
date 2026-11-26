// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The password of an archive, asked for inside the app. It goes to 7-Zip
// through its standard input for that one job and is not kept anywhere:
// the field is emptied as soon as the dialog closes.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: dialog
    property string name: ""
    // The password given before was wrong.
    property bool wrong: false
    property var onGiven: null

    // `given` gets the password typed.
    function ask(archiveName, wasWrong, given) {
        name = archiveName || ""
        wrong = !!wasWrong
        onGiven = given
        field.text = ""
        open()
    }
    function give() {
        if (field.text.length === 0)
            return
        var given = onGiven
        var password = field.text
        field.text = ""
        onGiven = null
        close()
        if (given)
            given(password)
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(440, parent.width - 32)
    modal: true
    padding: 0
    Overlay.modal: Rectangle { color: Theme.scrim }
    background: ShelfTile.DialogGlass {}
    // Typed into at once (once it is on screen: before, the focus is lost).
    onOpened: field.forceActiveFocus()
    onClosed: {
        field.text = ""
        onGiven = null
    }

    header: ShelfTile.DialogHead {
        eyebrow: qsTr("Archive")
        title: qsTr("Password")
        dialog: dialog
    }

    contentItem: ColumnLayout {
        spacing: 10
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            wrapMode: Text.WordWrap
            color: dialog.wrong ? Theme.error : Theme.textSecondary
            font.pixelSize: 13
            text: dialog.wrong
                  ? qsTr("That password did not open %1. Type it again.").arg(dialog.name)
                  : qsTr("%1 is protected by a password (often on the page it was downloaded from).").arg(dialog.name)
        }
        StyledField {
            id: field
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            echoMode: reveal.checked ? TextInput.Normal : TextInput.Password
            placeholderText: qsTr("Password")
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            onAccepted: dialog.give()
        }
        StyledCheck {
            id: reveal
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            text: qsTr("Show it")
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.bottomMargin: Theme.dialogInner
            wrapMode: Text.WordWrap
            color: Theme.alpha(Theme.textSecondary, 0.8)
            font.pixelSize: 11
            text: qsTr("Used for this extraction only: it is not saved or written to the log.")
        }
    }

    footer: Item {
        implicitHeight: Theme.dialogFooter
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            height: 1
            color: Theme.glassEdge
        }
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogInner
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 8
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Cancel")
                onClicked: dialog.close()
            }
            StyledButton {
                text: qsTr("Extract")
                iconName: "package"
                solid: true
                enabled: field.text.length > 0
                onClicked: dialog.give()
            }
        }
    }
}
