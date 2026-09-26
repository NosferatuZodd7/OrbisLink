// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Uma das duas opções da sobreposição. Não tem DropArea própria: quem
// recebe o arrasto é o DropArea único da janela, que decide pela posição
// do cursor qual das zonas está a ser apontada (ver DropOverlay.qml).
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: zone
    property string title: ""
    property string subtitle: ""
    property string glyph: ""
    property bool enabledZone: true
    property bool highlighted: false
    property string disabledReason: ""

    readonly property bool active: highlighted && enabledZone

    Rectangle {
        anchors.fill: parent
        radius: 16
        color: zone.active ? Theme.accentSoft : Theme.panel
        opacity: zone.enabledZone ? 1.0 : 0.45
        border.width: 2
        border.color: zone.active ? Theme.accent : Theme.border
        scale: zone.active ? 1.02 : 1.0

        Behavior on color { ColorAnimation { duration: 110 } }
        Behavior on border.color { ColorAnimation { duration: 110 } }
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }

        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - 48
            spacing: 10

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: zone.glyph
                font.pixelSize: 46
                color: zone.enabledZone ? Theme.accent : Theme.textMuted
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: zone.title
                color: Theme.onStage
                font.pixelSize: 18
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: zone.enabledZone ? zone.subtitle : zone.disabledReason
                color: zone.enabledZone ? Theme.textMuted : Theme.error
                font.pixelSize: 12
            }
        }
    }
}
