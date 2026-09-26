// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A caixa ao lado das consolas para juntar mais uma à lista. Mais estreita e
// mais leve do que as outras: é um convite, não uma consola.
import QtQuick

Rectangle {
    id: caixa

    signal adicionar()

    // Acompanha a escala das caixas das consolas (desenhadas a 280 de altura).
    readonly property real escala: height / 280
    implicitWidth: 180
    implicitHeight: 280
    radius: 30 * escala
    color: area.containsMouse ? Theme.accentFill : "transparent"
    border.width: area.containsMouse ? 2 : 1
    border.color: area.containsMouse ? Theme.accent
                : Qt.rgba(Theme.onIdleStage.r, Theme.onIdleStage.g, Theme.onIdleStage.b, 0.28)
    scale: area.pressed ? Theme.pressScale : (area.containsMouse ? Theme.hoverScale : 1.0)
    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Theme.easeSpring; easing.overshoot: 1.1 } }
    Behavior on color { ColorAnimation { duration: Theme.fast } }

    Column {
        anchors.centerIn: parent
        spacing: 12

        // O "+" dentro de um círculo fino.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 56 * caixa.escala
            height: 56 * caixa.escala
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: area.containsMouse ? Theme.accent : Theme.onIdleStageMuted
            Rectangle {
                anchors.centerIn: parent
                width: 22 * caixa.escala; height: 2; radius: 1
                color: parent.border.color
            }
            Rectangle {
                anchors.centerIn: parent
                width: 2; height: 22 * caixa.escala; radius: 1
                color: parent.border.color
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: caixa.width - 32
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Adicionar consola")
            color: area.containsMouse ? Theme.accent : Theme.onIdleStage
            font.pixelSize: Math.max(10, Math.round(15 * caixa.escala))
            font.weight: Font.DemiBold
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: caixa.adicionar()
    }
}
