// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A tab: its name, and behind the chosen one a pill of glass that slides
// in, as a segmented control does. The TabBar it sits in draws no track — at
// most a hairline under all of them.
import QtQuick
import QtQuick.Controls.Basic

TabButton {
    id: control
    implicitHeight: 34

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    contentItem: Text {
        text: control.text
        color: control.checked ? Theme.ink : control.hovered ? Theme.text : Theme.textSecondary
        font.pixelSize: 13
        font.weight: control.checked ? Font.DemiBold : Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Behavior on color { ColorAnimation { duration: Theme.normal } }
    }

    background: Item {
        Rectangle {
            anchors.centerIn: parent
            height: parent.height - 4
            width: control.checked ? Math.min(parent.width - 4, control.contentItem.implicitWidth + 32) : 0
            radius: height / 2
            opacity: control.checked ? 1 : 0
            color: Theme.light ? Qt.rgba(1, 1, 1, 0.92) : Qt.rgba(1, 1, 1, 0.13)
            border.width: 1
            border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.06)
            Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
            Behavior on opacity { NumberAnimation { duration: Theme.normal } }
        }
        Rectangle {
            anchors.centerIn: parent
            height: parent.height - 4
            width: Math.min(parent.width - 4, control.contentItem.implicitWidth + 32)
            radius: height / 2
            visible: !control.checked && control.hovered
            color: Theme.controlFill
        }
    }
}
