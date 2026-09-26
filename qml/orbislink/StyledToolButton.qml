// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The small button of bars and lists. Round, with no frame until the mouse
// gets there — navigation has to disappear when it is not needed.
import QtQuick
import QtQuick.Controls.Basic

ToolButton {
    id: button

    property bool danger: false

    implicitWidth: 38
    implicitHeight: 38
    font.pixelSize: 14

    scale: down ? Theme.pressScale : (hovered ? Theme.hoverScale : 1.0)
    Behavior on scale {
        NumberAnimation { duration: Theme.fast; easing.type: Theme.easeSpring; easing.overshoot: 1.1 }
    }

    background: Rectangle {
        radius: width / 2
        color: button.down
            ? Theme.accentFill
            : (button.hovered
                ? Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b, 0.55)
                : "transparent")
        border.width: button.hovered ? 1 : 0
        border.color: Theme.glassEdge
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
    }

    contentItem: Text {
        text: button.text
        font: button.font
        color: !button.enabled ? Theme.textSecondary
             : button.danger ? Theme.error
             : button.hovered ? Theme.text : Theme.textSecondary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
}
