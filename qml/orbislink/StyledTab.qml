// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Separador com indicador em cápsula de vidro, que desliza de um para o
// outro em vez de saltar. Sem sublinhado: a separação faz-se por
// profundidade, não por linhas.
import QtQuick
import QtQuick.Controls.Basic

TabButton {
    id: control
    implicitHeight: 40

    scale: down ? 0.99 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }

    contentItem: Text {
        text: control.text
        color: control.checked ? Theme.text : Theme.textSecondary
        font.pixelSize: 13
        font.weight: control.checked ? Font.DemiBold : Font.Medium
        font.letterSpacing: -0.2
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Behavior on color { ColorAnimation { duration: Theme.normal } }
    }

    background: Rectangle {
        radius: height / 2
        color: control.checked
            ? Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b,
                      Theme.panelOpacity + 0.2)
            : (control.hovered
                ? Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b, 0.25)
                : "transparent")
        border.width: control.checked ? 1 : 0
        border.color: Theme.glassEdge
        Behavior on color { ColorAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            visible: control.checked
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.glassHighlight) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
    }
}
