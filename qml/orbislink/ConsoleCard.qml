// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A consola encontrada na rede, numa caixa que se clica.
//
// Um clique faz o que faz sentido no estado em que ela está: liga se estiver
// pronta, acorda-a se estiver em repouso, abre o registo se este PC ainda
// não estiver registado, procura outra vez se não respondeu, e cancela se
// estiver a ligar. O que vai acontecer está escrito no botão de baixo.
//
// O desenho é de 360×330 e escala-se inteiro (`fator`) quando há várias
// consolas lado a lado: assim as proporções nunca mudam. Sem QtQuick.Effects
// (o Qt 6.4 das capturas não o tem), o vidro, o brilho e a sombra são camadas
// e gradientes desenhados aqui.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: caixa

    // "ready", "standby", "offline" ou "unknown", como vem do stream.
    property string estado: "unknown"
    property bool registada: false
    property bool aLigar: false
    property bool aProcurar: false
    property bool disponivel: true   // falso numa compilação sem Remote Play
    // "ps4", "ps5", ou vazio quando a consola nunca respondeu — e aí não se
    // inventa um número: aparece só "PS".
    property string tipo: ""
    property string nome: ""
    property string endereco: ""
    // Falso para as outras consolas da lista: aí o clique escolhe-a (passa a
    // ser a consola em uso), e no canto há um ✕ para a tirar da lista.
    property bool ativa: true
    property real fator: 1.0

    signal ligar()
    signal acordar()
    signal registar()
    signal procurar()
    signal cancelar()
    signal editar()
    signal escolher()
    signal remover()

    // O ✕ pede um segundo clique antes de remover.
    property bool confirmarRemocao: false
    Timer {
        running: caixa.confirmarRemocao
        interval: 3500
        onTriggered: caixa.confirmarRemocao = false
    }

    // O que o clique faz agora.
    readonly property string accao: {
        if (!disponivel) return ""
        if (!ativa) return "escolher"
        if (aLigar) return "cancelar"
        if (estado === "offline" || estado === "unknown") return "procurar"
        if (!registada) return "registar"
        if (estado === "standby") return "acordar"
        return "ligar"
    }
    readonly property bool pronta: ativa && accao === "ligar"
    readonly property bool apagada: !disponivel || estado === "offline"
    readonly property bool aVerificar: aProcurar || (estado === "unknown" && disponivel)

    readonly property color corEstado: !disponivel ? Theme.cardTextMuted
                                     : aVerificar ? Theme.cardGlow
                                     : estado === "ready" ? Theme.cardBlue
                                     : estado === "standby" ? Theme.cardAmber
                                     : estado === "offline" ? Theme.cardRed
                                     : Theme.cardTextMuted

    readonly property string estadoTexto: {
        if (!disponivel) return qsTr("Remote Play não incluído nesta versão")
        if (confirmarRemocao) return qsTr("Clica outra vez no ✕ para remover")
        if (!ativa) {
            if (estado === "ready") return qsTr("Pronta — clica para usar")
            if (estado === "standby") return qsTr("Em repouso — clica para usar")
            if (estado === "offline") return qsTr("Não responde — clica para usar")
            return qsTr("A verificar — clica para usar")
        }
        if (aLigar) return qsTr("A ligar… — clica para cancelar")
        if (aProcurar) return qsTr("A procurar a consola…")
        switch (accao) {
        case "procurar": return estado === "offline" ? qsTr("Não responde — clica para procurar")
                                                     : qsTr("Clica para procurar a consola")
        case "registar": return qsTr("Por registar — clica para registar")
        case "acordar": return qsTr("Em repouso — clica para acordar")
        default: return qsTr("Pronta — clica para ligar")
        }
    }

    implicitWidth: 360 * fator
    implicitHeight: 330 * fator
    width: implicitWidth
    height: implicitHeight

    // Sem resposta, a caixa inteira recua um pouco.
    opacity: apagada && !area.containsMouse ? 0.9 : 1.0
    Behavior on opacity { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }

    Item {
        id: desenho
        width: 360
        height: 330
        scale: caixa.fator
        transformOrigin: Item.TopLeft

        // ── Sombra suave: três camadas a alargar e a desaparecer.
        Repeater {
            model: 3
            Rectangle {
                anchors.fill: fundo
                anchors.margins: -(index + 1) * 4
                anchors.topMargin: -(index + 1) * 2
                anchors.bottomMargin: -(index + 1) * 6
                radius: fundo.radius + (index + 1) * 4
                color: "transparent"
                border.width: 4
                border.color: Qt.rgba(0, 0, 0, Theme.claro ? 0.035 - index * 0.01 : 0.12 - index * 0.035)
            }
        }

        // ── Brilho azul à volta quando está pronta a ligar.
        Repeater {
            model: 3
            Rectangle {
                anchors.fill: fundo
                anchors.margins: -(index + 1) * 3
                radius: fundo.radius + (index + 1) * 3
                color: "transparent"
                border.width: 3
                border.color: Theme.cardGlow
                opacity: caixa.ativa && caixa.disponivel
                         ? (0.22 - index * 0.07) * (caixa.pronta ? brilho.nivel : 0.7) : 0
                Behavior on opacity { NumberAnimation { duration: Theme.cardEase } }
            }
        }
        QtObject {
            id: brilho
            property real nivel: 1.0
        }
        SequentialAnimation {
            running: caixa.pronta && caixa.visible
            loops: Animation.Infinite
            NumberAnimation { target: brilho; property: "nivel"; to: 0.45; duration: 1600; easing.type: Easing.InOutSine }
            NumberAnimation { target: brilho; property: "nivel"; to: 1.0; duration: 1600; easing.type: Easing.InOutSine }
        }

        Rectangle {
            id: fundo
            anchors.fill: parent
            radius: 30
            border.width: caixa.ativa && caixa.disponivel ? 1.5 : 1
            border.color: area.containsMouse && caixa.accao.length > 0 ? Theme.cardGlow
                        : caixa.ativa && caixa.disponivel ? Qt.rgba(Theme.cardGlow.r, Theme.cardGlow.g,
                                                                    Theme.cardGlow.b, 0.75)
                        : Theme.cardEdge
            Behavior on border.color { ColorAnimation { duration: Theme.cardEase } }
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.cardTop }
                GradientStop { position: 1.0; color: Theme.cardBottom }
            }
            clip: true

            // Curvas largas e o gradiente radial atrás do logótipo, mais um
            // ruído quase invisível para o azul não ficar chapado.
            Canvas {
                id: ondas
                anchors.fill: parent
                readonly property color corOnda: Theme.cardWave
                readonly property color corLuz: caixa.corEstado
                readonly property bool claro: Theme.claro
                onCorOndaChanged: requestPaint()
                onCorLuzChanged: requestPaint()
                onClaroChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    var w = width, h = height

                    var halo = ctx.createRadialGradient(w / 2, 92, 4, w / 2, 92, 150)
                    halo.addColorStop(0, Qt.rgba(corLuz.r, corLuz.g, corLuz.b, claro ? 0.10 : 0.20))
                    halo.addColorStop(1, Qt.rgba(corLuz.r, corLuz.g, corLuz.b, 0))
                    ctx.fillStyle = halo
                    ctx.fillRect(0, 0, w, h)

                    ctx.fillStyle = corOnda
                    ctx.beginPath()
                    ctx.moveTo(0, h * 0.42)
                    ctx.bezierCurveTo(w * 0.30, h * 0.66, w * 0.62, h * 0.72, w, h * 0.46)
                    ctx.lineTo(w, h * 0.58)
                    ctx.bezierCurveTo(w * 0.62, h * 0.84, w * 0.30, h * 0.80, 0, h * 0.54)
                    ctx.closePath()
                    ctx.fill()
                    ctx.beginPath()
                    ctx.moveTo(0, h * 0.30)
                    ctx.bezierCurveTo(w * 0.35, h * 0.52, w * 0.70, h * 0.58, w, h * 0.34)
                    ctx.lineTo(w, h * 0.37)
                    ctx.bezierCurveTo(w * 0.70, h * 0.62, w * 0.35, h * 0.56, 0, h * 0.33)
                    ctx.closePath()
                    ctx.fill()

                    // Ruído a 2–3%: pontos soltos, sempre os mesmos.
                    var semente = 7
                    function aleatorio() {
                        semente = (semente * 16807) % 2147483647
                        return semente / 2147483647
                    }
                    ctx.fillStyle = claro ? "rgba(15,23,42,0.035)" : "rgba(255,255,255,0.03)"
                    for (var i = 0; i < 900; ++i)
                        ctx.fillRect(aleatorio() * w, aleatorio() * h, 1, 1)
                }
            }

            // A aresta de luz em cima.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 1
                height: 90
                radius: parent.radius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.claro ? 0.7 : 0.06) }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
        }

        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: caixa.accao.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                switch (caixa.accao) {
                case "ligar": caixa.ligar(); break
                case "acordar": caixa.acordar(); break
                case "registar": caixa.registar(); break
                case "procurar": caixa.procurar(); break
                case "cancelar": caixa.cancelar(); break
                case "escolher": caixa.escolher(); break
                }
            }
        }

        // ── Topo: o símbolo da app à esquerda, e à direita o registo (a
        // "ligação" entre este PC e a consola) ou, nas outras consolas, o ✕.
        Image {
            x: 24
            y: 22
            width: 22
            height: 22
            source: "qrc:/icons/mark.png"
            sourceSize.width: 44
            sourceSize.height: 44
            opacity: 0.55
        }

        StyledToolButton {
            x: parent.width - width - 16
            y: 14
            implicitWidth: 36
            implicitHeight: 36
            visible: caixa.disponivel && caixa.ativa
            opacity: 0.7
            text: "🔗"
            ToolTip.visible: hovered
            ToolTip.text: caixa.registada ? qsTr("Registar este PC outra vez")
                                          : qsTr("Registar este PC na consola")
            onClicked: caixa.editar()
        }
        StyledToolButton {
            x: parent.width - width - 16
            y: 14
            implicitWidth: 36
            implicitHeight: 36
            visible: !caixa.ativa
            danger: caixa.confirmarRemocao
            opacity: 0.7
            text: "✕"
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Tirar esta consola da lista")
            onClicked: {
                if (caixa.confirmarRemocao)
                    caixa.remover()
                else
                    caixa.confirmarRemocao = true
            }
        }

        // ── Centro: PS4/PS5 em traço fino, a linha azul, o nome e o IP.
        Column {
            x: 0
            y: 58
            width: parent.width
            spacing: 0

            // A wordmark PS4/PS5 (ou só "PS", quando o tipo não se sabe),
            // gerada por scripts/gerar-wordmarks.py: branca nos temas
            // escuros, preta no claro.
            Image {
                id: letras
                anchors.horizontalCenter: parent.horizontalCenter
                readonly property string nome: caixa.tipo === "ps4" ? "ps4"
                                             : caixa.tipo === "ps5" ? "ps5" : "ps"
                source: "qrc:/icons/wordmark-" + nome + (Theme.claro ? "-preto" : "-branco") + ".png"
                sourceSize.height: 186
                height: 62
                width: implicitWidth / 3
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }

            Item { width: 1; height: 6 }

            Item { width: 1; height: 8 }

            // A linha fina por baixo do logótipo: azul na consola em uso,
            // cinzenta nas outras.
            Rectangle {
                id: divisor
                anchors.horizontalCenter: parent.horizontalCenter
                width: 118
                height: 3
                radius: 1.5
                readonly property color cor: caixa.ativa ? Theme.cardBlue : Theme.cardTextMuted
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.rgba(divisor.cor.r, divisor.cor.g, divisor.cor.b, 0.35) }
                    GradientStop { position: 0.5; color: divisor.cor }
                    GradientStop { position: 1.0; color: Qt.rgba(divisor.cor.r, divisor.cor.g, divisor.cor.b, 0.35) }
                }
                opacity: caixa.ativa ? 1.0 : 0.6
            }

            Item { width: 1; height: 22 }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 48
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: caixa.nome.length > 0 ? caixa.nome : qsTr("Consola")
                color: Theme.cardText
                font.pixelSize: 26
                font.weight: Font.DemiBold
            }

            Item { width: 1; height: 2 }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: caixa.endereco.length > 0
                text: caixa.endereco
                color: Theme.cardTextMuted
                font.pixelSize: 14
            }
        }

        // ── Rodapé: o botão com o estado e o que o clique faz.
        Rectangle {
            id: botao
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            width: parent.width * 0.82
            height: 58
            radius: height / 2
            clip: true
            readonly property real alfa: area.containsMouse && caixa.accao.length > 0 ? 1.4 : 1.0
            color: {
                var c = caixa.corEstado
                if (!caixa.disponivel)
                    return Qt.rgba(c.r, c.g, c.b, 0.12)
                if (caixa.estado === "standby" && !caixa.aVerificar)
                    return Qt.rgba(c.r, c.g, c.b, (Theme.claro ? 0.13 : 0.16) * alfa)
                if (caixa.estado === "offline" && !caixa.aVerificar)
                    return Qt.rgba(c.r, c.g, c.b, (Theme.claro ? 0.10 : 0.16) * alfa)
                return Qt.rgba(c.r, c.g, c.b, (Theme.claro ? 0.10 : 0.20) * alfa)
            }
            border.width: 1
            border.color: Qt.rgba(caixa.corEstado.r, caixa.corEstado.g, caixa.corEstado.b,
                                  Theme.claro ? 0.10 : 0.28)
            Behavior on color { ColorAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }

            // O ponto à esquerda, o texto (até duas linhas) e o chevron à
            // direita, na cor do estado.
            Rectangle {
                id: ponto
                x: 24
                anchors.verticalCenter: parent.verticalCenter
                width: 14; height: 14; radius: 7
                color: caixa.corEstado
                SequentialAnimation on opacity {
                    running: caixa.aLigar || caixa.aVerificar
                    loops: Animation.Infinite
                    onStopped: ponto.opacity = 1
                    NumberAnimation { to: 0.25; duration: 500 }
                    NumberAnimation { to: 1.0; duration: 500 }
                }
            }
            Text {
                anchors.left: ponto.right
                anchors.leftMargin: 16
                anchors.right: chevron.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeight: 0.95
                text: caixa.estadoTexto
                color: Theme.cardText
                font.pixelSize: 15
                font.weight: Font.Medium
            }
            Text {
                id: chevron
                anchors.right: parent.right
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                visible: caixa.accao.length > 0
                text: "›"
                color: caixa.corEstado
                font.pixelSize: 28
            }

            // A procurar: um brilho que corre ao longo do fundo do botão.
            Rectangle {
                id: brilhoBarra
                visible: caixa.aVerificar
                anchors.bottom: parent.bottom
                height: 2
                width: parent.width * 0.35
                radius: 1
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.5; color: Theme.cardGlow }
                    GradientStop { position: 1.0; color: "transparent" }
                }
                NumberAnimation on x {
                    running: caixa.aVerificar
                    loops: Animation.Infinite
                    from: -brilhoBarra.width
                    to: botao.width
                    duration: 1300
                    easing.type: Easing.InOutQuad
                }
            }
        }
    }
}
