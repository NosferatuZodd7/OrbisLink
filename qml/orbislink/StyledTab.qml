// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One segment of a segmented control: the TabBar draws the track, and the
// selected segment is a raised surface inside it.
import QtQuick
import QtQuick.Controls.Basic

TabButton {
    id: control
    implicitHeight: 36

    contentItem: Text {
        text: control.text
        color: control.checked ? Theme.text : Theme.textSecondary
        font.pixelSize: 13
        font.weight: control.checked ? Font.DemiBold : Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Behavior on color { ColorAnimation { duration: Theme.normal } }
    }

    background: Rectangle {
        radius: 9
        color: control.checked ? (Theme.light ? "#FFFFFF" : Theme.alpha(Theme.panelAlt, 1.0))
             : control.hovered ? Theme.alpha(Theme.controlHover, 0.6)
             : "transparent"
        border.width: control.checked ? 1 : 0
        border.color: Theme.glassEdge
        Behavior on color { ColorAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
    }
}
