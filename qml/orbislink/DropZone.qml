// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One of the two options of the overlay. It has no DropArea of its own: the
// drag is received by the window's single DropArea, which decides from the
// cursor position which zone is being pointed at (see DropOverlay.qml).
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

    Rectangle {
        anchors.fill: parent
        radius: 18
        color: zone.active ? Theme.alpha(Theme.accent, 0.18) : Qt.rgba(1, 1, 1, 0.05)
        opacity: zone.enabledZone ? 1.0 : 0.5
        border.width: zone.active ? 2 : 1
        border.color: zone.active ? Theme.accent : Qt.rgba(1, 1, 1, 0.14)
        scale: zone.active ? 1.02 : 1.0

        Behavior on color { ColorAnimation { duration: 110 } }
        Behavior on border.color { ColorAnimation { duration: 110 } }
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }

        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - 40
            spacing: 10

            Icon {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 6
                name: zone.glyph
                size: 40
                strokeWidth: 1.5
                color: !zone.enabledZone ? Theme.onStageMuted
                     : zone.active ? Theme.accentHover : Theme.onStage
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: zone.title
                color: Theme.onStage
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: zone.enabledZone ? zone.subtitle : zone.disabledReason
                color: zone.enabledZone ? Theme.onStageMuted : Theme.error
                font.pixelSize: 13
            }
        }
    }
}
