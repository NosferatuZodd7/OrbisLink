// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Uma superfície de vidro líquido: o bloco de construção de toda a
// interface.
//
// O que faz o vidro parecer vidro não é o desfoque — é a sobreposição de
// camadas translúcidas, a aresta de luz no topo e a sombra difusa por
// baixo. Isso está tudo aqui e corre em qualquer máquina, incluindo as que
// caem para desenho por software.
//
// Um desfoque verdadeiro do que está por trás precisa do QtQuick.Effects,
// que só existe no Qt 6.5+. Fica de fora de propósito: as capturas de ecrã
// e os testes correm num Qt 6.4, e um import que lá não exista impede a
// janela inteira de abrir. O fundo desta aplicação é um gradiente suave, e
// sobre gradientes a diferença entre desfocar e sobrepor translucidez não
// se vê.
import QtQuick

Item {
    id: superficie

    // O conteúdo vai para aqui dentro, já com o respiro do tema.
    default property alias content: area.data
    property int radius: Theme.radius
    property real fill: Theme.panelOpacity
    property color tint: Theme.panel
    // Um halo da cor de acento por trás, para o que está activo.
    property bool glowing: false
    property color glowColor: Theme.accent
    // O respiro interior. 24–32 é o que a linguagem pede.
    property int padding: Theme.padding
    property alias contentItem: area

    implicitWidth: area.implicitWidth + padding * 2
    implicitHeight: area.implicitHeight + padding * 2

    // ── sombra difusa
    // Três camadas cada vez maiores e mais fracas: sem desfoque, é assim
    // que se faz uma sombra que não tem aresta.
    Repeater {
        model: 3
        delegate: Rectangle {
            z: -2
            anchors.fill: parent
            anchors.margins: -(index + 1) * 3
            radius: superficie.radius + (index + 1) * 3
            color: "transparent"
            border.width: 3
            border.color: Qt.rgba(Theme.shadow.r, Theme.shadow.g, Theme.shadow.b,
                                  Theme.shadow.a * (0.22 - index * 0.06))
        }
    }

    // ── halo do acento, quando está activo
    Rectangle {
        z: -1
        anchors.fill: parent
        anchors.margins: -6
        radius: superficie.radius + 6
        visible: superficie.glowing
        color: "transparent"
        border.width: 6
        border.color: Qt.rgba(superficie.glowColor.r, superficie.glowColor.g,
                              superficie.glowColor.b, 0.16)
    }

    // ── o vidro
    Rectangle {
        id: vidro
        anchors.fill: parent
        radius: superficie.radius
        color: Qt.rgba(superficie.tint.r, superficie.tint.g, superficie.tint.b, superficie.fill)
        border.width: 1
        border.color: Theme.glassEdge

        // Gradiente interno quase imperceptível: sem ele a superfície fica
        // chapada e deixa de parecer um corpo.
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.glassHighlight * 0.9) }
                GradientStop { position: 0.35; color: Qt.rgba(1, 1, 1, Theme.glassHighlight * 0.15) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, Theme.claro ? 0.02 : 0.10) }
            }
        }

        // A aresta de luz em cima, que é o reflexo especular do vidro
        // curvo. É uma linha, mas é o que mais faz pela ilusão.
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 1
            anchors.leftMargin: parent.radius * 0.5
            anchors.rightMargin: parent.radius * 0.5
            height: 1
            radius: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, Theme.claro ? 0.9 : 0.35) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        Behavior on color { ColorAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
    }

    Item {
        id: area
        anchors.fill: parent
        anchors.margins: superficie.padding
    }
}
