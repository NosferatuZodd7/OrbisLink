// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The small square button of bars and lists: an icon with no frame until
// the mouse gets there. Takes an icon from Icons.js (iconName) or, failing
// that, its text.
import QtQuick
import QtQuick.Controls.Basic

ToolButton {
    id: button

    property bool danger: false
    // Shown lit, as a toggle that is on.
    property bool active: false
    property string iconName: ""
    property real iconSize: 18
    // The icon turns: something it started is under way.
    property bool spinning: false

    implicitWidth: 36
    implicitHeight: 36
    font.pixelSize: 14

    scale: down ? Theme.pressScale : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }

    readonly property color tone: !enabled ? Theme.alpha(Theme.textSecondary, 0.5)
                                : danger ? Theme.error
                                : active ? Theme.accent
                                : hovered ? Theme.ink : Theme.textSecondary

    HoverHandler {
        cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    // Round, like a button on an instrument; no frame until it is wanted.
    background: Rectangle {
        radius: Math.min(width, height) / 2
        color: button.active ? Theme.accentFill
             : button.down ? Theme.controlHover
             : button.hovered ? Theme.controlFill
             : "transparent"
        border.width: button.active || button.visualFocus ? 1 : 0
        border.color: button.visualFocus ? Theme.accent : Theme.alpha(Theme.accent, 0.4)
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            visible: button.iconName.length > 0
            name: button.iconName
            size: button.iconSize
            color: button.tone
            spinning: button.spinning
        }
        Text {
            anchors.centerIn: parent
            visible: button.iconName.length === 0
            text: button.text
            font: button.font
            color: button.tone
        }
    }
}
