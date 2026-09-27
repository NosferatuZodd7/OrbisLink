// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A row of the context menu.
//
// The highlight does not take the full width: it is inset at the sides and
// has its own corners. That way it never touches the menu's rounded edges —
// which is what would make the highlight look like a rectangle stuck on top
// of the glass instead of part of it.
import QtQuick
import QtQuick.Controls.Basic

MenuItem {
    id: control
    implicitHeight: 36

    contentItem: Text {
        leftPadding: 20
        rightPadding: 20
        text: control.text
        color: control.enabled ? Theme.text : Theme.textMuted
        opacity: control.enabled ? 1.0 : 0.55
        font.pixelSize: 12
        font.letterSpacing: -0.2
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        radius: 10
        color: control.highlighted ? Theme.accentFill : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
}
