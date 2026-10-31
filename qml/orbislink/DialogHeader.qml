// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The top of a dialog, as every dialog of the atlas has it: an optional
// brass eyebrow (what kind of dialog this is), the title in the serif and, on
// the right, the round ✕ that closes it.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: header

    property string title: ""
    // A line of small capitals over the title ("Settings", "Console"...);
    // without it the title stands alone, as before.
    property string eyebrow: ""
    // The dialog the ✕ closes; without one there is no ✕.
    property var dialog: null
    property bool closable: dialog !== null
    // A ‹ before the title, back to where the dialog was opened from.
    property bool backable: false
    property string backText: qsTr("Back")
    signal back()

    // The eyebrow takes its own line, so the header grows by it.
    implicitHeight: Theme.dialogHeader + (eyebrow.length > 0 ? 14 : 0)

    StyledToolButton {
        id: backButton
        visible: header.backable
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: titleColumn.verticalCenter
        iconName: "chevron-left"
        Accessible.name: header.backText
        ToolTip.visible: hovered
        ToolTip.text: header.backText
        onClicked: header.back()
    }

    Column {
        id: titleColumn
        anchors.left: backButton.visible ? backButton.right : parent.left
        anchors.leftMargin: backButton.visible ? 6 : Theme.dialogMargin
        anchors.right: closeButton.visible ? closeButton.left : parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 4
        spacing: 2

        Eyebrow {
            width: parent.width
            visible: header.eyebrow.length > 0
            text: header.eyebrow
        }
        Text {
            width: parent.width
            text: header.title
            color: Theme.ink
            font.pixelSize: 22
            font.family: Theme.fontDisplay
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
    }

    StyledToolButton {
        id: closeButton
        visible: header.closable
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: titleColumn.verticalCenter
        iconName: "close"
        Accessible.name: qsTr("Close")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Close")
        onClicked: header.dialog.close()
    }
}
