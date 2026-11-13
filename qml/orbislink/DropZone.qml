// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One of the two options of the overlay. It has no DropArea of its own: the
// drag is received by the window's single DropArea, which decides from the
// cursor position which zone is being pointed at (see DropOverlay.qml).
//
// A big target: a pane of glass with the mode's icon on a glass disc, its
// name in bold and one line on what it does. Pointed at, it brightens, takes
// an accent ring and lifts a little; when the mode cannot be used it fades
// and says why.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: zone
    property string title: ""
    property string subtitle: ""
    // Name of the icon (Icons.js).
    property string glyph: ""
    property bool enabledZone: true
    property bool highlighted: false
    property string disabledReason: ""

    readonly property bool active: highlighted && enabledZone

    Accessible.role: Accessible.StaticText
    Accessible.name: title

    // The accent glow around the target pointed at.
    Rectangle {
        anchors.fill: target
        anchors.margins: -5
        radius: target.radius + 5
        color: "transparent"
        border.width: 3
        border.color: Theme.alpha(Theme.accent, 0.22)
        opacity: zone.active ? 1 : 0
        scale: target.scale
        Behavior on opacity { NumberAnimation { duration: 110 } }
    }

    Rectangle {
        id: target
        anchors.fill: parent
        radius: Theme.radius + 4
        color: zone.active ? (Theme.light ? Qt.rgba(1, 1, 1, 0.92) : Qt.rgba(1, 1, 1, 0.14))
             : zone.highlighted ? Theme.alpha(Theme.error, 0.08)
             : Theme.light ? Qt.rgba(1, 1, 1, 0.62) : Qt.rgba(1, 1, 1, 0.05)
        opacity: zone.enabledZone ? 1.0 : 0.6
        border.width: zone.active ? 2 : 1
        border.color: zone.active ? Theme.accent
                    : Theme.light ? Theme.alpha(Theme.ink, 0.10) : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.04)
        scale: zone.active ? 1.02 : 1.0

        Behavior on color { ColorAnimation { duration: 110 } }
        Behavior on border.color { ColorAnimation { duration: 110 } }
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }

        // The light on the glass, from the top.
        Rectangle {
            anchors.fill: parent
            anchors.margins: parent.border.width
            radius: parent.radius - parent.border.width
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.45 : 0.06) }
                GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0) }
            }
        }

        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - 48
            spacing: 8

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 8
                implicitWidth: 72
                implicitHeight: 72
                radius: 36
                color: zone.active ? Theme.accent : Theme.controlFill
                border.width: 1
                border.color: zone.active ? Theme.accent
                            : Theme.light ? Qt.rgba(1, 1, 1, 1) : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.06)
                Behavior on color { ColorAnimation { duration: 110 } }
                Icon {
                    anchors.centerIn: parent
                    name: zone.glyph
                    size: 32
                    strokeWidth: 1.5
                    color: !zone.enabledZone ? Theme.textSecondary
                         : zone.active ? Theme.textOnAccent : Theme.ink
                }
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: zone.title
                color: Theme.ink
                font.pixelSize: 22
                font.family: Theme.fontDisplay
                font.weight: Font.Bold
                font.letterSpacing: -0.3
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: zone.enabledZone ? zone.subtitle : zone.disabledReason
                color: zone.enabledZone ? Theme.textSecondary
                     : Theme.light ? Qt.darker(Theme.error, 1.3) : Theme.error
                font.pixelSize: 13
                lineHeight: 1.15
            }
        }
    }
}
