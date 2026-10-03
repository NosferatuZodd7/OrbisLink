// SPDX-License-Identifier: AGPL-3.0-or-later
//
// An on/off setting, drawn as a switch: the label on the left, the switch
// on the right, the way system settings do it.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

CheckBox {
    id: control
    property color labelColor: Theme.text

    implicitHeight: Math.max(28, label.implicitHeight)
    spacing: 12
    leftPadding: 0
    rightPadding: 0
    // A setting row takes the whole width, so every switch lines up on the right.
    Layout.fillWidth: true

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    indicator: Rectangle {
        implicitWidth: 40
        implicitHeight: 24
        x: control.width - width - control.rightPadding
        y: (control.height - height) / 2
        radius: height / 2
        color: control.checked ? Theme.accent
             : Theme.light ? "#D5DCE6" : Qt.rgba(1, 1, 1, 0.14)
        Behavior on color { ColorAnimation { duration: Theme.fast } }

        Rectangle {
            width: 18
            height: 18
            radius: 9
            y: 3
            x: control.checked ? parent.width - width - 3 : 3
            color: "#FFFFFF"
            Behavior on x { NumberAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
        }
    }

    contentItem: Text {
        id: label
        text: control.text
        color: control.enabled ? control.labelColor : Theme.textSecondary
        font.pixelSize: 13
        wrapMode: Text.WordWrap
        rightPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
    }
}
