// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A caixa ao lado das consolas para juntar mais uma à lista. Mais estreita e
// mais leve do que as outras: é um convite, não uma consola.
import QtQuick

Rectangle {
    id: caixa

    signal adicionar()

    implicitWidth: 170
    implicitHeight: 250
    radius: Theme.radius
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
            width: 56
            height: 56
            radius: 28
            color: "transparent"
            border.width: 2
            border.color: area.containsMouse ? Theme.accent : Theme.onIdleStageMuted
            Rectangle {
                anchors.centerIn: parent
                width: 22; height: 2; radius: 1
                color: parent.border.color
            }
            Rectangle {
                anchors.centerIn: parent
                width: 2; height: 22; radius: 1
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
            font.pixelSize: 13
            font.bold: true
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
