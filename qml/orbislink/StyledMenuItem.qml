// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A row of the context menu: a line icon and the text, and red for the one
// that destroys something.
//
// The highlight does not take the full width: it is inset at the sides and
// has its own corners, so it never touches the menu's rounded edges.
import QtQuick
import QtQuick.Controls.Basic

MenuItem {
    id: control
    property string iconName: ""
    property bool danger: false
    implicitHeight: 38

    readonly property color tone: !control.enabled ? Theme.textSecondary
                                : control.danger ? Theme.error
                                : Theme.text

    contentItem: Row {
        leftPadding: 16
        rightPadding: 16
        spacing: 12
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            size: 16
            color: control.danger ? Theme.error : Theme.textSecondary
            opacity: control.enabled ? 1.0 : 0.5
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            color: control.tone
            opacity: control.enabled ? 1.0 : 0.6
            font.pixelSize: 13
            elide: Text.ElideRight
        }
    }

    background: Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        radius: 10
        color: !control.highlighted ? "transparent"
             : control.danger ? Theme.alpha(Theme.error, 0.14) : Theme.accentFill
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
}
