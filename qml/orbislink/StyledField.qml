// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Campo de texto em vidro. Sem contorno agressivo: ao ganhar o foco,
// acende-se um halo azul suave e a moldura ilumina-se.
import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: campo
    color: Theme.text
    font.pixelSize: 13
    font.letterSpacing: -0.2
    selectByMouse: true
    placeholderTextColor: Theme.textSecondary
    implicitHeight: 42
    leftPadding: 16
    rightPadding: 16
    selectionColor: Theme.accentFill
    selectedTextColor: Theme.text

    background: Item {
        // O halo do foco, por baixo.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: Theme.radiusControl + 4
            visible: campo.activeFocus
            color: "transparent"
            border.width: 4
            border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b,
                           campo.activeFocus ? Theme.panelOpacity + 0.12 : Theme.panelOpacity)
            border.width: 1
            border.color: campo.activeFocus
                ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.7)
                : Theme.glassEdge
            Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
            Behavior on border.color { ColorAnimation { duration: Theme.fast } }

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.glassHighlight) }
                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.05) }
                }
            }
        }
    }
}
