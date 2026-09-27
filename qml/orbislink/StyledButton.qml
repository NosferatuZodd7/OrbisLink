// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The application's button, in liquid glass.
//
// QtQuick.Controls.Basic draws flat grey buttons. This one is a
// translucent surface with an edge of light, which grows 2% on hover
// and shrinks when pressed — the movement is what makes it feel like a body
// and not a drawing.
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: button

    property bool primary: false
    property bool danger: false
    property bool chip: false
    // Requested width — and it is a minimum, not a maximum. The dialogs'
    // numbers were measured with one language's labels; in another the
    // same label may be longer. A button is never narrower than its
    // text.
    property int minimumWidth: 0

    implicitWidth: Math.max(minimumWidth, keyLabel.implicitWidth + leftPadding + rightPadding)
    implicitHeight: chip ? 30 : 38
    // Side padding only. Control's "padding" applies to all four, and
    // 20 above plus 20 below do not fit in a height of 38: the text
    // would get a negative height and not appear.
    leftPadding: chip ? 14 : 20
    rightPadding: chip ? 14 : 20
    topPadding: 0
    bottomPadding: 0
    font.pixelSize: chip ? 11 : 13
    font.weight: primary ? Font.DemiBold : Font.Medium
    // The slightly tight tracking is half the personality of Apple's
    // typography.
    font.letterSpacing: -0.2

    // Grows on hover, shrinks when pressed. OutBack gives it weight.
    scale: down ? Theme.pressScale : (hovered ? Theme.hoverScale : 1.0)
    Behavior on scale {
        NumberAnimation { duration: Theme.fast; easing.type: Theme.easeSpring; easing.overshoot: 1.1 }
    }

    readonly property color backgroundColor: {
        if (!enabled)
            return Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b, 0.25)
        if (primary)
            return down ? Qt.darker(Theme.accent, 1.15) : Theme.accent
        if (danger)
            return Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, hovered ? 0.24 : 0.14)
        return Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b,
                       hovered ? Theme.panelOpacity + 0.18 : Theme.panelOpacity)
    }

    readonly property color textColor: {
        if (!enabled)
            return Theme.textSecondary
        if (primary)
            return "#FFFFFF"
        if (danger)
            return Theme.error
        return Theme.text
    }

    background: Item {
        // Halo under the primary button: it is what draws the eye.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -5
            radius: Theme.radiusControl + 5
            visible: button.primary && button.enabled
            color: "transparent"
            border.width: 5
            border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                                  button.hovered ? 0.30 : 0.18)
            Behavior on border.color { ColorAnimation { duration: Theme.fast } }
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: button.backgroundColor
            border.width: 1
            border.color: button.primary
                ? Qt.rgba(1, 1, 1, 0.22)
                : (button.danger
                    ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.35)
                    : Theme.glassEdge)
            Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }

            // Inner gradient and edge of light, as on the other surfaces.
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    GradientStop {
                        position: 0.0
                        color: Qt.rgba(1, 1, 1, button.primary ? 0.20 : Theme.glassHighlight)
                    }
                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.06) }
                }
            }
        }
    }

    contentItem: Text {
        id: keyLabel
        text: button.text
        font: button.font
        color: button.textColor
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
}
