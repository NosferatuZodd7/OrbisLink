// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The application's button.
//
// Four kinds, all round-ended: primary (solid accent, one per screen at
// most), secondary (the default: drawn in outline, filled on hover), danger
// (red outline; solid for the one action that must stand out) and chip (a
// smaller secondary). An optional line icon sits before the text.
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: button

    property bool primary: false
    property bool danger: false
    // With danger: a solid red button, for the one destructive action that
    // must stand out (ending a session).
    property bool solid: false
    property bool chip: false
    // Name of an icon from Icons.js, drawn before the text.
    property string iconName: ""
    // Requested width — and it is a minimum, not a maximum: in another
    // language the same label may be longer, and a button is never narrower
    // than its text.
    property int minimumWidth: 0

    // Icon and text, or the icon alone when there is no text.
    readonly property real iconSpace: iconName.length > 0 ? (chip ? 14 : 16) + (text.length > 0 ? 8 : 0) : 0
    implicitWidth: Math.max(minimumWidth, keyLabel.implicitWidth + iconSpace + leftPadding + rightPadding)
    implicitHeight: chip ? 28 : Theme.controlHeight
    // Side padding only: 20 above plus 20 below would not fit in the height.
    leftPadding: chip ? 12 : 18
    rightPadding: chip ? 12 : 18
    topPadding: 0
    bottomPadding: 0
    font.pixelSize: chip ? 12 : 13
    font.weight: primary ? Font.DemiBold : Font.Medium

    scale: down ? Theme.pressScale : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }

    readonly property color backgroundColor: {
        if (!enabled)
            return primary ? Theme.alpha(Theme.accent, 0.35) : "transparent"
        if (primary)
            return down ? Qt.darker(Theme.accent, 1.12) : hovered ? Theme.accentHover : Theme.accent
        if (danger && solid)
            return down ? Qt.darker(Theme.error, 1.12) : hovered ? Qt.lighter(Theme.error, 1.08) : Theme.error
        if (danger)
            return Theme.alpha(Theme.error, down ? 0.22 : hovered ? 0.14 : 0.0)
        return down ? Theme.controlHover : hovered ? Theme.controlFill : "transparent"
    }

    readonly property color textColor: {
        if (!enabled)
            return primary ? Theme.alpha(Theme.textOnAccent, 0.7) : Theme.alpha(Theme.textSecondary, 0.7)
        if (primary || (danger && solid))
            return Theme.textOnAccent
        if (danger)
            return Theme.error
        return Theme.ink
    }

    // The hand over anything that can be clicked.
    HoverHandler {
        cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    readonly property color hoverTone: danger ? Theme.error : Theme.accent

    // Round-ended: the primary one solid, the others drawn in outline, as
    // an engraver would — a line, not a block.
    background: Rectangle {
        radius: height / 2
        color: button.backgroundColor
        border.width: button.primary || button.solid ? 0 : 1
        border.color: !button.enabled ? Theme.alpha(Theme.glassEdge, 0.6)
                    : button.danger ? Theme.alpha(Theme.error, button.hovered ? 0.75 : 0.45)
                    : button.hovered ? Theme.alpha(Theme.ink, Theme.light ? 0.55 : 0.45)
                    : Theme.alpha(Theme.ink, Theme.light ? 0.30 : 0.22)
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
        Behavior on border.color { ColorAnimation { duration: Theme.fast } }

        // Keyboard focus: a ring in the accent, a little outside.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: height / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.alpha(button.hoverTone, 0.55)
            visible: button.visualFocus
        }
    }

    contentItem: Item {
        Row {
            anchors.centerIn: parent
            spacing: 8
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: button.iconName
                size: button.chip ? 14 : 16
                color: button.textColor
                // The waiting icon turns on its own.
                spinning: button.iconName === "loader"
            }
            Text {
                id: keyLabel
                visible: text.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, Math.max(0, button.availableWidth - button.iconSpace))
                text: button.text
                font: button.font
                color: button.textColor
                elide: Text.ElideRight
                Behavior on color { ColorAnimation { duration: Theme.fast } }
            }
        }
    }
}
