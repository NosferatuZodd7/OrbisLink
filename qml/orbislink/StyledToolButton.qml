// SPDX-License-Identifier: AGPL-3.0-or-later
//
// O botão pequeno das barras e das listas. Redondo, sem moldura até o rato
// lá chegar — a navegação tem de desaparecer quando não se precisa dela.
import QtQuick
import QtQuick.Controls.Basic

ToolButton {
    id: botao

    property bool danger: false

    implicitWidth: 38
    implicitHeight: 38
    font.pixelSize: 14

    scale: down ? Theme.pressScale : (hovered ? Theme.hoverScale : 1.0)
    Behavior on scale {
        NumberAnimation { duration: Theme.fast; easing.type: Theme.easeSpring; easing.overshoot: 1.1 }
    }

    background: Rectangle {
        radius: width / 2
        color: botao.down
            ? Theme.accentFill
            : (botao.hovered
                ? Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b, 0.55)
                : "transparent")
        border.width: botao.hovered ? 1 : 0
        border.color: Theme.glassEdge
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
    }

    contentItem: Text {
        text: botao.text
        font: botao.font
        color: !botao.enabled ? Theme.textSecondary
             : botao.danger ? Theme.error
             : botao.hovered ? Theme.text : Theme.textSecondary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
}
