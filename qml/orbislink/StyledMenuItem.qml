// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Uma linha do menu de contexto.
//
// O realce não ocupa a largura toda: fica encolhido dos lados e com cantos
// próprios. Assim nunca toca nas arestas arredondadas do menu — que é o
// que faria o realce parecer um rectângulo colado por cima do vidro em vez
// de fazer parte dele.
import QtQuick
import QtQuick.Controls.Basic

MenuItem {
    id: control
    implicitHeight: 36

    contentItem: Text {
        leftPadding: 20
        rightPadding: 20
        text: control.text
        color: control.enabled ? Theme.text : Theme.textMuted
        opacity: control.enabled ? 1.0 : 0.55
        font.pixelSize: 12
        font.letterSpacing: -0.2
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        radius: 10
        color: control.highlighted ? Theme.accentFill : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
}
