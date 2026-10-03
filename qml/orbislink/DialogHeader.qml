// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The top of a dialog: its title and, on the right, the ✕ that closes it.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: header

    property string title: ""
    // The dialog the ✕ closes; without one there is no ✕.
    property var dialog: null
    property bool closable: dialog !== null

    implicitHeight: Theme.dialogHeader

    Text {
        anchors.left: parent.left
        anchors.leftMargin: Theme.dialogMargin
        anchors.right: closeButton.visible ? closeButton.left : parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 4
        text: header.title
        color: Theme.text
        font.pixelSize: 18
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    StyledToolButton {
        id: closeButton
        visible: header.closable
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 4
        iconName: "close"
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Close")
        onClicked: header.dialog.close()
    }
}
