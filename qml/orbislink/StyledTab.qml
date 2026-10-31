// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A tab: its name, and under the chosen one a short brass-ended line in the
// ink, as an atlas marks the plate open. The TabBar it sits in draws no
// track — at most a hairline under all of them.
import QtQuick
import QtQuick.Controls.Basic

TabButton {
    id: control
    implicitHeight: 38

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    contentItem: Text {
        text: control.text
        color: control.checked ? Theme.ink : control.hovered ? Theme.text : Theme.textSecondary
        font.pixelSize: 13
        font.weight: control.checked ? Font.Bold : Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Behavior on color { ColorAnimation { duration: Theme.normal } }
    }

    background: Item {
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            height: 2
            radius: 1
            width: control.checked ? Math.min(parent.width - 12, control.contentItem.implicitWidth + 18) : 0
            color: Theme.ink
            Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
            // The brass dot at its middle.
            Rectangle {
                anchors.centerIn: parent
                width: 4
                height: 4
                radius: 2
                color: Theme.brass
                visible: control.checked
            }
        }
    }
}
