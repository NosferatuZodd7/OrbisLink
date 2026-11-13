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

    // The primary button is the one solid thing on a screen: frosted white on
    // graphite, graphite on daylight.
    readonly property color primaryFill: Theme.light ? "#1E1D1B" : "#F4F1EA"
    readonly property color primaryText: Theme.light ? "#FFFFFF" : "#16161A"

    readonly property color backgroundColor: {
        if (!enabled)
            return primary ? Theme.alpha(primaryFill, 0.30) : Theme.alpha(Theme.controlFill, Theme.light ? 0.35 : 0.04)
        if (primary)
            return down ? Theme.alpha(primaryFill, 0.82) : hovered ? Theme.alpha(primaryFill, 1.0) : Theme.alpha(primaryFill, 0.92)
        if (danger && solid)
            return down ? Qt.darker(Theme.error, 1.12) : hovered ? Qt.lighter(Theme.error, 1.06) : Theme.error
        if (danger)
            return Theme.alpha(Theme.error, down ? 0.26 : hovered ? 0.18 : 0.10)
        return down ? Theme.alpha(Theme.controlHover, Theme.light ? 1.0 : 0.22)
             : hovered ? Theme.controlHover : Theme.controlFill
    }

    readonly property color textColor: {
        if (!enabled)
            return primary ? Theme.alpha(primaryText, 0.6) : Theme.alpha(Theme.textSecondary, 0.6)
        if (primary)
            return primaryText
        if (danger && solid)
            return "#FFFFFF"
        if (danger)
            return Theme.light ? Qt.darker(Theme.error, 1.1) : Theme.error
        return Theme.ink
    }

    // The hand over anything that can be clicked.
    HoverHandler {
        cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    readonly property color hoverTone: danger ? Theme.error : Theme.accent

    // A pill of glass: a translucent fill, an edge lit from above, and a
    // sheen that brightens under the mouse.
    background: Rectangle {
        radius: height / 2
        color: button.backgroundColor
        border.width: button.primary || button.solid ? 0 : 1
        border.color: button.danger ? Theme.alpha(Theme.error, button.hovered ? 0.55 : 0.32)
                    : Theme.light ? (button.hovered ? Theme.lightHairlineStrong : Theme.lightHairline)
                    : Qt.rgba(1, 1, 1, button.hovered ? Theme.glassBorder + 0.12 : Theme.glassBorder)
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
        Behavior on border.color { ColorAnimation { duration: Theme.fast } }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: height / 2
            visible: button.enabled && !button.solid
            opacity: button.hovered ? 1.0 : 0.6
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, button.primary ? 0.30 : Theme.light ? 0.6 : 0.10) }
                GradientStop { position: 0.55; color: Qt.rgba(1, 1, 1, 0) }
            }
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        }

        // Keyboard focus: a ring a little outside.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: height / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.alpha(button.hoverTone, 0.6)
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
