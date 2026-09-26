// SPDX-License-Identifier: AGPL-3.0-or-later
//
// O botão da aplicação, em vidro líquido.
//
// O QtQuick.Controls.Basic desenha botões cinzentos chapados. Este é uma
// superfície translúcida com aresta de luz, que cresce 2% ao passar o rato
// e encolhe ao carregar — o movimento é o que o faz parecer um corpo e não
// um desenho.
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: botao

    property bool primary: false
    property bool danger: false
    property bool chip: false
    // Largura pedida — e é um mínimo, não um máximo. Os números dos
    // diálogos foram medidos com as etiquetas em português; em inglês a
    // mesma etiqueta pode ser mais comprida. Um botão nunca fica mais
    // estreito do que o seu texto.
    property int larguraMinima: 0

    implicitWidth: Math.max(larguraMinima, rotulo.implicitWidth + leftPadding + rightPadding)
    implicitHeight: chip ? 30 : 38
    // Só folga dos lados. O "padding" do Control aplica-se aos quatro, e
    // 20 em cima mais 20 em baixo não cabem numa altura de 38: o texto
    // ficaria com altura negativa e não apareceria.
    leftPadding: chip ? 14 : 20
    rightPadding: chip ? 14 : 20
    topPadding: 0
    bottomPadding: 0
    font.pixelSize: chip ? 11 : 13
    font.weight: primary ? Font.DemiBold : Font.Medium
    // O tracking ligeiramente fechado é metade da personalidade da
    // tipografia da Apple.
    font.letterSpacing: -0.2

    // Cresce ao passar o rato, encolhe ao carregar. O OutBack dá o peso.
    scale: down ? Theme.pressScale : (hovered ? Theme.hoverScale : 1.0)
    Behavior on scale {
        NumberAnimation { duration: Theme.fast; easing.type: Theme.easeSpring; easing.overshoot: 1.1 }
    }

    readonly property color corDeFundo: {
        if (!enabled)
            return Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b, 0.25)
        if (primary)
            return down ? Qt.darker(Theme.accent, 1.15) : Theme.accent
        if (danger)
            return Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, hovered ? 0.24 : 0.14)
        return Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b,
                       hovered ? Theme.panelOpacity + 0.18 : Theme.panelOpacity)
    }

    readonly property color corDoTexto: {
        if (!enabled)
            return Theme.textSecondary
        if (primary)
            return "#FFFFFF"
        if (danger)
            return Theme.error
        return Theme.text
    }

    background: Item {
        // Halo por baixo do botão principal: é ele que puxa o olho.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -5
            radius: Theme.radiusControl + 5
            visible: botao.primary && botao.enabled
            color: "transparent"
            border.width: 5
            border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                                  botao.hovered ? 0.30 : 0.18)
            Behavior on border.color { ColorAnimation { duration: Theme.fast } }
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: botao.corDeFundo
            border.width: 1
            border.color: botao.primary
                ? Qt.rgba(1, 1, 1, 0.22)
                : (botao.danger
                    ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.35)
                    : Theme.glassEdge)
            Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }

            // Gradiente interno e aresta de luz, como nas outras superfícies.
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    GradientStop {
                        position: 0.0
                        color: Qt.rgba(1, 1, 1, botao.primary ? 0.20 : Theme.glassHighlight)
                    }
                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.06) }
                }
            }
        }
    }

    contentItem: Text {
        id: rotulo
        text: botao.text
        font: botao.font
        color: botao.corDoTexto
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
}
