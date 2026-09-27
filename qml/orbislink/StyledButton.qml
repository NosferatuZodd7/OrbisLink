// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The application's button.
//
// Four kinds: primary (solid accent, one per screen at most), secondary (the
// default: a quiet fill with a thin border), danger (soft red) and chip (a
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

    readonly property real iconSpace: iconName.length > 0 ? (chip ? 14 : 16) + 8 : 0
    implicitWidth: Math.max(minimumWidth, keyLabel.implicitWidth + iconSpace + leftPadding + rightPadding)
    implicitHeight: chip ? 30 : Theme.controlHeight
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
            return primary ? Theme.alpha(Theme.accent, 0.35) : Theme.controlFill
        if (primary)
            return down ? Qt.darker(Theme.accent, 1.12) : hovered ? Theme.accentHover : Theme.accent
        if (danger && solid)
            return down ? Qt.darker(Theme.error, 1.12) : hovered ? Qt.lighter(Theme.error, 1.08) : Theme.error
        if (danger)
            return Theme.alpha(Theme.error, hovered ? 0.22 : 0.13)
        return hovered ? Theme.controlHover : Theme.controlFill
    }

    readonly property color textColor: {
        if (!enabled)
            return primary ? Theme.alpha(Theme.textOnAccent, 0.7) : Theme.textSecondary
        if (primary || (danger && solid))
            return Theme.textOnAccent
        if (danger)
            return Theme.error
        return Theme.text
    }

    background: Rectangle {
        radius: Theme.radiusControl
        color: button.backgroundColor
        border.width: button.primary || button.solid ? 0 : 1
        border.color: button.danger ? Theme.alpha(Theme.error, 0.28) : Theme.glassEdge
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }

        // A faint light from above, only on the primary one.
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            visible: button.primary && button.enabled
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.14) }
                GradientStop { position: 1.0; color: "transparent" }
            }
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
            }
            Text {
                id: keyLabel
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
