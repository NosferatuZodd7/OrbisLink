// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The top of a dialog, as every dialog has it: an optional champagne eyebrow
// (what kind of dialog this is), the title in bold and, on the right, the
// round glass button that closes it.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: header

    property string title: ""
    // A line of small capitals over the title ("Settings", "Console"...);
    // without it the title stands alone, as before.
    property string eyebrow: ""
    // The dialog the close button closes; without one there is no button.
    property var dialog: null
    property bool closable: dialog !== null
    // A ‹ before the title, back to where the dialog was opened from.
    property bool backable: false
    property string backText: qsTr("Back")
    signal back()

    // The eyebrow takes its own line, so the header grows by it.
    implicitHeight: Theme.dialogHeader + (eyebrow.length > 0 ? 14 : 0)

    // A round glass disc: the back and close buttons, always visible as
    // glass (not only on hover), brightening under the mouse.
    component GlassDisc: StyledToolButton {
        id: disc
        implicitWidth: 34
        implicitHeight: 34
        background: Rectangle {
            radius: width / 2
            color: disc.hovered || disc.down ? Theme.controlHover : Theme.controlFill
            border.width: 1
            border.color: disc.visualFocus ? Theme.accent
                        : Theme.light ? Qt.rgba(1, 1, 1, disc.hovered ? 1 : 0.9)
                        : Qt.rgba(1, 1, 1, Theme.glassBorder + (disc.hovered ? 0.14 : 0.04))
            Behavior on color { ColorAnimation { duration: Theme.fast } }
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: width / 2
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.55 : 0.10) }
                    GradientStop { position: 0.6; color: Qt.rgba(1, 1, 1, 0) }
                }
            }
        }
    }

    GlassDisc {
        id: backButton
        visible: header.backable
        anchors.left: parent.left
        anchors.leftMargin: 16
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
        anchors.leftMargin: backButton.visible ? 10 : Theme.dialogMargin
        anchors.right: closeButton.visible ? closeButton.left : parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 4
        spacing: 3

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
            font.weight: Font.Bold
            font.letterSpacing: -0.3
            elide: Text.ElideRight
        }
    }

    GlassDisc {
        id: closeButton
        visible: header.closable
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.verticalCenter: titleColumn.verticalCenter
        iconName: "close"
        Accessible.name: qsTr("Close")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Close")
        onClicked: header.dialog.close()
    }
}
