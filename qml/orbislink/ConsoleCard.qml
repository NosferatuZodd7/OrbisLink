// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The console found on the network, in a card you click.
//
// One click connects: the app asks the console how it is, wakes it if it is
// in rest mode, waits for it to be ready and connects — or opens registration,
// if this PC is not registered on it yet. While that is going on, another
// click cancels. What is happening is written on the bottom button.
//
// The design is 360×330 and scales as a whole (`scaleFactor`) when there are
// several consoles side by side: that way the proportions never change.
// Without QtQuick.Effects (the screenshots' Qt 6.4 lacks it), the glass,
// glow and shadow are layers and gradients drawn here.
//
// A console of unknown type gets a different card, in a cartoon style:
// flat colour, thick outline and a hard offset shadow, with no glass,
// waves or glow.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: caixa

    // "ready", "standby", "offline" or "unknown", as it comes from the stream.
    property string estado: "unknown"
    property bool registada: false
    property bool aLigar: false
    property bool aProcurar: false
    // The one-click connection step: "", "checking" or "waking".
    property string etapa: ""
    property bool disponivel: true   // false in a build without Remote Play
    // "ps4", "ps5", or empty when the console never answered — and then no
    // number is made up: "Unknown PlayStation" appears, on a simpler card,
    // so it does not pass for a known console.
    property string tipo: ""
    readonly property bool conhecida: tipo === "ps4" || tipo === "ps5"
    property string nome: ""
    property string endereco: ""
    // False for the other consoles in the list: there the click selects it
    // (it becomes the console in use), and the corner has a ✕ to remove it.
    property bool ativa: true
    property real fator: 1.0

    signal ligar()
    signal cancelar()
    signal editar()
    signal escolher()
    signal remover()

    // The ✕ asks for a second click before removing.
    property bool confirmarRemocao: false
    Timer {
        running: caixa.confirmarRemocao
        interval: 3500
        onTriggered: caixa.confirmarRemocao = false
    }

    // What the click does now.
    readonly property string accao: {
        if (!disponivel) return ""
        if (!ativa) return "choose"
        if (aLigar || etapa.length > 0) return "cancel"
        return "connect"
    }
    readonly property bool apagada: !disponivel || estado === "offline"
    readonly property bool aVerificar: aProcurar || etapa.length > 0
                                       || (estado === "unknown" && disponivel)

    readonly property color corEstado: !disponivel ? Theme.cardTextMuted
                                     : aVerificar ? Theme.cardGlow
                                     : estado === "ready" ? Theme.cardBlue
                                     : estado === "standby" ? Theme.cardAmber
                                     : estado === "offline" ? Theme.cardRed
                                     : Theme.cardTextMuted

    readonly property string estadoTexto: {
        if (!disponivel) return qsTr("Remote Play is not in this build")
        if (confirmarRemocao) return qsTr("Click ✕ again to remove")
        if (!ativa) {
            if (estado === "ready") return qsTr("Ready — click to connect")
            if (estado === "standby") return qsTr("In rest mode — click to wake and connect")
            if (estado === "offline") return qsTr("Not responding — click to try to connect")
            return qsTr("Checking — click to connect")
        }
        if (aLigar) return qsTr("Connecting… — click to cancel")
        if (etapa === "waking") return qsTr("Waking the console… — click to cancel")
        if (etapa === "checking") return qsTr("Checking the console… — click to cancel")
        if (aProcurar) return qsTr("Searching for the console…")
        if (estado === "offline") return qsTr("Not responding — click to try to connect")
        if (estado === "unknown") return qsTr("Click to connect")
        if (!registada) return qsTr("Not registered — click to register")
        if (estado === "standby") return qsTr("In rest mode — click to wake and connect")
        return qsTr("Ready — click to connect")
    }

    implicitWidth: 360 * fator
    implicitHeight: 330 * fator
    width: implicitWidth
    height: implicitHeight

    // The mouse over a card that does something when clicked.
    readonly property bool sobre: area.containsMouse && accao.length > 0

    // With no action, the whole card fades back a little.
    opacity: apagada && !area.containsMouse ? 0.9 : 1.0
    Behavior on opacity { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }

    // With the mouse over it, it grows a little and lifts; when pressed, it sinks.
    scale: area.pressed && sobre ? Theme.pressScale : (sobre ? 1.03 : 1.0)
    Behavior on scale { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
    transform: Translate {
        y: caixa.sobre && !area.pressed ? -4 : 0
        Behavior on y { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
    }

    Item {
        id: desenho
        width: 360
        height: 330
        scale: caixa.fator
        transformOrigin: Item.TopLeft

        // ── Soft shadow: three layers widening and fading.
        Repeater {
            model: caixa.conhecida ? 3 : 0
            Rectangle {
                anchors.fill: fundo
                anchors.margins: -(index + 1) * 4
                anchors.topMargin: -(index + 1) * 2
                anchors.bottomMargin: -(index + 1) * 6
                radius: fundo.radius + (index + 1) * 4
                color: "transparent"
                border.width: 4
                border.color: Qt.rgba(0, 0, 0, (Theme.claro ? 0.03 - index * 0.008 : 0.09 - index * 0.025)
                                               * (caixa.sobre ? 1.6 : 1.0))
            }
        }

        // ── Blue glow around it, only with the mouse over it: at rest, the
        // card stays discreet.
        Repeater {
            model: caixa.conhecida ? 3 : 0
            Rectangle {
                anchors.fill: fundo
                anchors.margins: -(index + 1) * 3
                radius: fundo.radius + (index + 1) * 3
                color: "transparent"
                border.width: 3
                border.color: Theme.cardGlow
                opacity: caixa.sobre ? 0.2 - index * 0.06 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.cardEase } }
            }
        }

        // ── Cartoon: the hard shadow, the same shape offset and without blur.
        Rectangle {
            visible: !caixa.conhecida
            x: area.pressed ? 3 : caixa.sobre ? 10 : 7
            y: area.pressed ? 3 : caixa.sobre ? 10 : 7
            Behavior on x { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
            Behavior on y { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
            width: fundo.width
            height: fundo.height
            radius: fundo.radius
            color: Theme.claro ? Theme.cardText : Qt.rgba(Theme.cardGlow.r, Theme.cardGlow.g,
                                                         Theme.cardGlow.b, 0.5)
        }

        Rectangle {
            id: fundo
            anchors.fill: parent
            radius: 30
            border.width: !caixa.conhecida ? 4 : 1
            border.color: caixa.sobre ? Theme.cardGlow
                        : !caixa.conhecida ? (Theme.claro ? Theme.cardText : Theme.cardTextMuted)
                        : caixa.ativa && caixa.disponivel ? Qt.rgba(Theme.cardGlow.r, Theme.cardGlow.g,
                                                                    Theme.cardGlow.b, 0.4)
                        : Theme.cardEdge
            Behavior on border.color { ColorAnimation { duration: Theme.cardEase } }
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.cardTop }
                GradientStop { position: 1.0; color: Theme.cardBottom }
            }
            clip: true

            // Wide curves and the radial gradient behind the logo, plus an
            // almost invisible noise so the blue does not look flat.
            Canvas {
                id: ondas
                anchors.fill: parent
                visible: caixa.conhecida
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
                    halo.addColorStop(0, Qt.rgba(corLuz.r, corLuz.g, corLuz.b, claro ? 0.06 : 0.12))
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

                    // Noise at 2–3%: scattered dots, always the same ones.
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

            // Cartoon: the flat colour over the gradient, inside the outline.
            Rectangle {
                visible: !caixa.conhecida
                anchors.fill: parent
                anchors.margins: parent.border.width
                radius: parent.radius - parent.border.width
                color: Theme.cardTop
            }

            // The edge of light at the top.
            Rectangle {
                visible: caixa.conhecida
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
                case "connect": caixa.ligar(); break
                case "cancel": caixa.cancelar(); break
                case "choose": caixa.escolher(); break
                }
            }
        }

        // ── Top: the app symbol on the left, and on the right the registration
        // (the "link" between this PC and the console) or, on other consoles, the ✕.
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
            ToolTip.text: caixa.registada ? qsTr("Register this PC again")
                                          : qsTr("Register this PC on the console")
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
            ToolTip.text: qsTr("Remove this console from the list")
            onClicked: {
                if (caixa.confirmarRemocao)
                    caixa.remover()
                else
                    caixa.confirmarRemocao = true
            }
        }

        // ── Centre: PS4/PS5 in a thin stroke, the blue line, the name and the IP.
        Column {
            x: 0
            y: 58
            width: parent.width
            spacing: 0

            // The PS4/PS5 wordmark (or "Unknown PlayStation", when the type is
            // not known), generated by scripts/gerar-wordmarks.py: white on the
            // dark themes, black on the light one.
            Image {
                id: letras
                anchors.horizontalCenter: parent.horizontalCenter
                source: "qrc:/icons/wordmark-" + (caixa.conhecida ? caixa.tipo : "desconhecida")
                        + (Theme.claro ? "-preto" : "-branco") + ".png"
                sourceSize.height: 186
                height: 62
                width: implicitWidth / 3
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }

            Item { width: 1; height: 6 }

            Item { width: 1; height: 8 }

            // The thin line under the logo: blue on the console in use,
            // grey on the others.
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
                text: caixa.nome.length > 0 ? caixa.nome : qsTr("Console")
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

        // ── Footer: the button with the state and what the click does.
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
            // On the cartoon card, a thick outline in the state colour.
            border.width: caixa.conhecida ? 1 : 3
            border.color: caixa.conhecida
                          ? Qt.rgba(caixa.corEstado.r, caixa.corEstado.g, caixa.corEstado.b,
                                    Theme.claro ? 0.10 : 0.28)
                          : caixa.corEstado
            Behavior on color { ColorAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }

            // The dot on the left, the text (up to two lines) and the chevron
            // on the right, in the state's colour.
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

            // Searching: a shine running along the bottom of the button.
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
