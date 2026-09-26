// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Os três temas como ícones na barra de cima, ao lado das definições.
// Um clique muda logo, sem abrir as definições, e o tema activo vê-se
// aceso.
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: seletor

    readonly property var temas: [
        { nome: "escuro", icone: "☾", dica: qsTr("Tema escuro") },
        { nome: "vidro",  icone: "◐", dica: qsTr("Tema vidro (escuro, mais transparente)") },
        { nome: "claro",  icone: "☀", dica: qsTr("Tema claro") }
    ]

    implicitWidth: linha.implicitWidth + 8
    implicitHeight: 38
    radius: height / 2
    color: Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b, 0.35)
    border.width: 1
    border.color: Theme.glassEdge

    Row {
        id: linha
        anchors.centerIn: parent
        spacing: 2

        Repeater {
            model: seletor.temas

            ToolButton {
                id: botao
                readonly property bool activo: Theme.nome === modelData.nome

                width: 30
                height: 30
                font.pixelSize: 14
                text: modelData.icone
                ToolTip.visible: hovered
                ToolTip.text: modelData.dica
                Accessible.name: modelData.dica

                scale: down ? Theme.pressScale : (hovered ? Theme.hoverScale : 1.0)
                Behavior on scale {
                    NumberAnimation { duration: Theme.fast; easing.type: Theme.easeSpring; easing.overshoot: 1.1 }
                }

                // O Main.qml aplica o tema quando as definições mudam; aqui
                // só se grava.
                onClicked: app.setTheme(modelData.nome)

                background: Rectangle {
                    radius: width / 2
                    color: botao.activo ? Theme.accentFill
                         : botao.hovered ? Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g,
                                                   Theme.panelAlt.b, 0.55)
                         : "transparent"
                    border.width: botao.activo ? 1 : 0
                    border.color: Theme.accent
                    Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
                }

                contentItem: Text {
                    text: botao.text
                    font: botao.font
                    color: botao.activo ? Theme.accent
                         : botao.hovered ? Theme.text : Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    Behavior on color { ColorAnimation { duration: Theme.fast } }
                }
            }
        }
    }
}
