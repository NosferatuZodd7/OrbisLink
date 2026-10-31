// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One of the two options of the overlay. It has no DropArea of its own: the
// drag is received by the window's single DropArea, which decides from the
// cursor position which zone is being pointed at (see DropOverlay.qml).
//
// A big target: a plate with the mode's icon in a round seal, its name in
// the serif and one line on what it does. Pointed at, it lifts and takes the
// accent; when the mode cannot be used it fades and says why.
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

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: zone.active ? Theme.accentFill
             : zone.highlighted ? Theme.alpha(Theme.error, 0.06) : Theme.panelFill
        opacity: zone.enabledZone ? 1.0 : 0.6
        border.width: zone.active ? 2 : 1
        border.color: zone.active ? Theme.accent : Theme.glassEdge
        scale: zone.active ? 1.02 : 1.0

        Behavior on color { ColorAnimation { duration: 110 } }
        Behavior on border.color { ColorAnimation { duration: 110 } }
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }

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
                border.color: zone.active ? Theme.accent : Theme.alpha(Theme.brass, 0.5)
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
                font.weight: Font.DemiBold
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
