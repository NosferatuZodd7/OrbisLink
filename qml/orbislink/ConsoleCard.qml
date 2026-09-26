// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A consola encontrada na rede, numa caixa que se clica.
//
// Um clique faz o que faz sentido no estado em que ela está: liga se estiver
// pronta, acorda-a se estiver em repouso, abre o registo se este PC ainda
// não estiver registado, procura outra vez se não respondeu, e cancela se
// estiver a ligar. O que vai acontecer está escrito na própria caixa.
//
// A consola aparece como "PS4" ou "PS5", em letras desenhadas aqui na cor do
// texto do tema, com uma linha por baixo na cor do estado: azul pronta,
// laranja em repouso, vermelho quando não responde.
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: caixa

    // "ready", "standby", "offline" ou "unknown", como vem do stream.
    property string estado: "unknown"
    property bool registada: false
    property bool aLigar: false
    property bool aProcurar: false
    property bool disponivel: true   // falso numa compilação sem Remote Play
    property bool ps5: false
    property string nome: ""
    property string endereco: ""

    signal ligar()
    signal acordar()
    signal registar()
    signal procurar()
    signal cancelar()
    signal editar()
    // Só nas consolas que não estão em uso.
    signal escolher()
    signal remover()

    // Falso para as outras consolas da lista: aí o clique escolhe-a (passa a
    // ser a consola em uso), e no canto há um ✕ para a tirar da lista.
    property bool ativa: true
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

    readonly property color corLuz: !disponivel ? Theme.textSecondary
                                  : estado === "ready" ? Theme.accent
                                  : estado === "standby" ? "#FF9F0A"
                                  : estado === "offline" ? Theme.error
                                  : Theme.textSecondary

    readonly property string estadoTexto: {
        if (!disponivel) return qsTr("Remote Play não incluído nesta versão")
        if (confirmarRemocao) return qsTr("Clica outra vez no ✕ para a tirar da lista")
        if (!ativa) {
            if (estado === "ready") return qsTr("Pronta — clica para usar esta")
            if (estado === "standby") return qsTr("Em repouso — clica para usar esta")
            if (estado === "offline") return qsTr("Não responde — clica para usar esta")
            return qsTr("A verificar… — clica para usar esta")
        }
        if (aLigar) return qsTr("A ligar… — clica para cancelar")
        if (aProcurar) return qsTr("A procurar…")
        switch (accao) {
        case "procurar": return estado === "offline" ? qsTr("Não responde — clica para procurar")
                                                     : qsTr("Clica para procurar a consola")
        case "registar": return qsTr("Por registar — clica para registar este PC")
        case "acordar": return qsTr("Em repouso — clica para acordar")
        default: return qsTr("Pronta — clica para ligar")
        }
    }

    implicitWidth: 320
    implicitHeight: 250
    radius: Theme.radius
    // Quase opaca: por trás está a grelha e a imagem do palco, e o nome da
    // consola tem de se ler por cima delas.
    color: Qt.rgba(area.containsMouse && accao.length > 0 ? Theme.panelAlt.r : Theme.panel.r,
                   area.containsMouse && accao.length > 0 ? Theme.panelAlt.g : Theme.panel.g,
                   area.containsMouse && accao.length > 0 ? Theme.panelAlt.b : Theme.panel.b,
                   0.92)
    border.width: 1
    // No claro a caixa é branca sobre o palco branco: a aresta escura é o
    // que a separa dele.
    border.color: area.containsMouse && accao.length > 0 ? Theme.accent
                : Theme.claro ? Qt.rgba(0, 0, 0, 0.14) : Theme.glassEdge
    opacity: apagada && !area.containsMouse ? 0.6 : 1.0
    scale: area.pressed ? Theme.pressScale : (area.containsMouse && accao.length > 0 ? Theme.hoverScale : 1.0)

    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Theme.easeSpring; easing.overshoot: 1.1 } }
    Behavior on opacity { NumberAnimation { duration: Theme.normal } }
    Behavior on color { ColorAnimation { duration: Theme.fast } }
    Behavior on border.color { ColorAnimation { duration: Theme.fast } }

    // Pronta a ligar, a aresta respira devagar na cor de destaque: é o
    // sítio onde se carrega.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -4
        radius: parent.radius + 4
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        opacity: 0
        visible: caixa.pronta
        SequentialAnimation on opacity {
            running: caixa.pronta && caixa.visible
            loops: Animation.Infinite
            NumberAnimation { to: 0.55; duration: 1400; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.0; duration: 1400; easing.type: Easing.InOutSine }
        }
    }

    // A aresta de luz em cima, como nas outras superfícies de vidro.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 1
        height: parent.height / 2
        radius: parent.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.glassSheen }
            GradientStop { position: 1.0; color: "transparent" }
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

    // Os dois extras, pequenos e nos cantos: procurar outra vez e mudar o
    // registo. O clique principal é a caixa inteira.
    StyledToolButton {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        implicitWidth: 32
        implicitHeight: 32
        visible: caixa.disponivel && caixa.ativa
        enabled: !caixa.aProcurar
        text: "⟳"
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Procurar a consola outra vez")
        onClicked: caixa.procurar()
    }
    StyledToolButton {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        implicitWidth: 32
        implicitHeight: 32
        visible: caixa.disponivel && caixa.ativa && caixa.estado !== "offline"
        text: "✎"
        ToolTip.visible: hovered
        ToolTip.text: caixa.registada ? qsTr("Registar este PC outra vez")
                                      : qsTr("Registar este PC na consola")
        onClicked: caixa.editar()
    }
    StyledToolButton {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        implicitWidth: 32
        implicitHeight: 32
        visible: !caixa.ativa
        danger: caixa.confirmarRemocao
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

    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 6
        spacing: 10

        // "PS4" ou "PS5" em letras desenhadas aqui: traço fino, largo e
        // geométrico, na cor do texto do tema. Por baixo, uma linha fina na
        // cor do estado.
        Canvas {
            id: desenho
            anchors.horizontalCenter: parent.horizontalCenter
            width: 176
            height: 78
            readonly property color corLetras: Theme.text
            readonly property color corLuz: caixa.corLuz
            readonly property bool ps5: caixa.ps5
            onCorLetrasChanged: requestPaint()
            onCorLuzChanged: requestPaint()
            onPs5Changed: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var t = 6          // espessura do traço
                var a = 46         // altura das letras
                var l = 40         // largura de cada letra
                var e = 16         // espaço entre letras
                var y0 = 6, y1 = y0 + a, ym = y0 + a / 2
                var x = (width - (3 * l + 2 * e)) / 2
                ctx.strokeStyle = corLetras
                ctx.lineWidth = t
                ctx.lineJoin = "round"
                ctx.lineCap = "round"
                // Uma linha pelos pontos, com os cantos arredondados em `raio`.
                function linha(pontos, raio) {
                    ctx.beginPath()
                    ctx.moveTo(pontos[0][0], pontos[0][1])
                    for (var i = 1; i < pontos.length - 1; ++i)
                        ctx.arcTo(pontos[i][0], pontos[i][1],
                                  pontos[i + 1][0], pontos[i + 1][1], raio || 0.01)
                    var ultimo = pontos[pontos.length - 1]
                    ctx.lineTo(ultimo[0], ultimo[1])
                    ctx.stroke()
                }
                // P
                linha([[x, y1], [x, y0], [x + l, y0], [x + l, ym], [x + 8, ym]], 12)
                x += l + e
                // S
                linha([[x + l, y0], [x, y0], [x, ym], [x + l, ym], [x + l, y1], [x, y1]], 12)
                x += l + e
                if (!ps5) {
                    // 4
                    linha([[x + l - 8, y0], [x, ym + 8], [x + l, ym + 8]])
                    linha([[x + l - 8, y0], [x + l - 8, y1]])
                } else {
                    // 5: o mesmo percurso do S, mas de cantos vivos e com a
                    // barriga redonda, que é o que o distingue.
                    linha([[x + l, y0], [x, y0], [x, ym], [x + l - 12, ym]])
                    ctx.beginPath()
                    ctx.moveTo(x + l - 12, ym)
                    ctx.arcTo(x + l, ym, x + l, y1, 12)
                    ctx.arcTo(x + l, y1, x, y1, 12)
                    ctx.lineTo(x, y1)
                    ctx.stroke()
                }
                // A linha do estado.
                ctx.strokeStyle = corLuz
                ctx.lineWidth = 3
                linha([[width / 2 - 34, y1 + 16], [width / 2 + 34, y1 + 16]])
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: caixa.nome.length > 0 ? caixa.nome : qsTr("Consola")
            color: Theme.text
            font.pixelSize: 17
            font.bold: true
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: caixa.endereco.length > 0
            text: caixa.endereco
            color: Theme.textSecondary
            font.pixelSize: 11
        }

        // O estado, e o que o clique vai fazer.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            implicitWidth: linhaEstado.implicitWidth + 24
            implicitHeight: Math.max(28, linhaEstado.implicitHeight + 10)
            radius: Math.min(height / 2, 14)
            color: Qt.rgba(caixa.corLuz.r, caixa.corLuz.g, caixa.corLuz.b, 0.14)
            Row {
                id: linhaEstado
                anchors.centerIn: parent
                spacing: 8
                Rectangle {
                    id: ponto
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8; height: 8; radius: 4
                    color: caixa.corLuz
                    SequentialAnimation on opacity {
                        running: caixa.aLigar || caixa.aProcurar
                        loops: Animation.Infinite
                        onStopped: ponto.opacity = 1
                        NumberAnimation { to: 0.2; duration: 450 }
                        NumberAnimation { to: 1.0; duration: 450 }
                    }
                }
                Text {
                    id: textoEstado
                    anchors.verticalCenter: parent.verticalCenter
                    // Numa caixa estreita parte em duas linhas em vez de
                    // sair para fora dela.
                    width: Math.min(implicitWidth, caixa.width - 68)
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    text: caixa.estadoTexto
                    color: Theme.text
                    font.pixelSize: 12
                }
            }
        }
    }
}
