// SPDX-License-Identifier: AGPL-3.0-or-later
//
// O comando da PS4 do mapa das teclas. A parte em "destaque" acende-se —
// é o que liga uma tecla do desenho do teclado ao botão que ela faz.
//
// A imagem (src/icons/dualshock.svg, desenhada para o OrbisLink) tem os
// botões, o touchpad, o PS e os analógicos recortados
// (transparentes). Por isso o destaque pinta-se POR BAIXO dela: a cor
// enche o recorte e o contorno do comando fica nítido por cima, sem
// precisar de máscaras nem de QtQuick.Effects (que o Qt 6.4 das capturas
// não tem). Os símbolos △ ◯ ✕ ▢ também são recortes, e é assim que
// aparecem nas suas cores.
//
// Duas imagens: o corpo claro nos temas escuros (um comando preto sobre
// fundo preto desapareceria) e o corpo escuro no tema claro.
//
// As posições estão em unidades do SVG (1590×988) e são as mesmas que lá
// estão: mudar uma num sítio obriga a mudá-la no outro.
import QtQuick

Item {
    id: comando

    // "cross", "circle", "square", "triangle", "l1", "l2", "r1", "r2",
    // "lstick", "rstick", "dpad", "options", "share", "touchpad", "ps" ou "".
    property string destaque: ""

    readonly property color corCruz: "#7CB2E8"
    readonly property color corCirculo: "#FF6B6B"
    readonly property color corQuadrado: "#E88BD6"
    readonly property color corTriangulo: "#40E0B0"

    // A imagem e, por cima dela, a faixa dos ombros (L1/L2/R1/R2 não se
    // vêem de frente, vão como etiquetas).
    readonly property real larguraImagem: 1590
    readonly property real alturaImagem: 988
    readonly property real alturaOmbros: 210
    readonly property real alturaTotal: alturaImagem + alturaOmbros

    readonly property real escala: Math.min(width / larguraImagem, height / alturaTotal)
    readonly property real origemX: (width - larguraImagem * escala) / 2
    readonly property real origemY: (height - alturaTotal * escala) / 2

    implicitWidth: 240
    implicitHeight: Math.round(240 * alturaTotal / larguraImagem)

    // As zonas recortadas da imagem, e a cor com que ficam em repouso.
    // Os botões da direita e o PS são círculos; o touchpad e o
    // Share/Options são rectângulos arredondados.
    readonly property var zonas: ({
        "triangle": { c: [1286, 163, 50], cor: corTriangulo },
        "circle":   { c: [1404, 278, 50], cor: corCirculo },
        "cross":    { c: [1286, 393, 50], cor: corCruz },
        "square":   { c: [1171, 278, 50], cor: corQuadrado },
        "ps":       { c: [795, 502, 44] },
        "lstick":   { c: [541, 492, 148] },
        "rstick":   { c: [1045, 492, 148] },
        "dpad":     { c: [299, 278, 186] },
        "touchpad": { r: [533, 70, 524, 256, 30] },
        "share":    { r: [438, 80, 60, 86, 22] },
        "options":  { r: [1087, 80, 60, 86, 22] }
    })

    function redesenhar() { baixo.requestPaint(); cima.requestPaint() }
    onDestaqueChanged: redesenhar()
    onWidthChanged: redesenhar()
    onHeightChanged: redesenhar()
    Connections {
        target: Theme
        function onAccentChanged() { comando.redesenhar() }
        function onNomeChanged() { comando.redesenhar() }
    }

    function prepararContexto(ctx) {
        ctx.reset()
        ctx.translate(origemX, origemY)
        ctx.scale(escala, escala)
    }
    function caminho(ctx, zona) {
        ctx.beginPath()
        if (zona.c)
            ctx.arc(zona.c[0], zona.c[1] + alturaOmbros, zona.c[2], 0, Math.PI * 2)
        else
            ctx.roundedRect(zona.r[0], zona.r[1] + alturaOmbros, zona.r[2], zona.r[3],
                            zona.r[4], zona.r[4])
    }

    // Por baixo: as cores dos símbolos, e o recorte aceso.
    Canvas {
        id: baixo
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            comando.prepararContexto(ctx)
            for (var nome in comando.zonas) {
                var zona = comando.zonas[nome]
                var aceso = comando.destaque === nome
                if (!aceso && !zona.cor)
                    continue
                comando.caminho(ctx, zona)
                ctx.fillStyle = aceso ? Theme.accent : zona.cor
                ctx.fill()
            }
        }
    }

    Image {
        x: comando.origemX
        y: comando.origemY + comando.alturaOmbros * comando.escala
        width: comando.larguraImagem * comando.escala
        height: comando.alturaImagem * comando.escala
        source: Theme.claro ? "qrc:/icons/dualshock-dark.png" : "qrc:/icons/dualshock-light.png"
        sourceSize.width: 720
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }

    // Por cima: um anel à volta do que está aceso (um recorte pequeno,
    // como o símbolo de um botão, sozinho não chama o olho) e as etiquetas
    // dos ombros.
    Canvas {
        id: cima
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            comando.prepararContexto(ctx)

            var zona = comando.zonas[comando.destaque]
            if (zona && zona.c && zona.c[2] < 100) {
                ctx.beginPath()
                ctx.arc(zona.c[0], zona.c[1] + comando.alturaOmbros, zona.c[2] + 16, 0,
                        Math.PI * 2)
                ctx.lineWidth = 12
                ctx.strokeStyle = Theme.accent
                ctx.stroke()
            }

            // L2 por cima de L1, como no comando: o gatilho fica atrás.
            var ombros = [
                ["l2", "L2", 215, 0], ["l1", "L1", 185, 100],
                ["r2", "R2", 1195, 0], ["r1", "R1", 1165, 100]
            ]
            var neutro = Theme.claro ? "#2B2A29" : "#D4D5D6"
            ctx.font = "bold 62px sans-serif"
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            for (var i = 0; i < ombros.length; ++i) {
                var o = ombros[i]
                var aceso = comando.destaque === o[0]
                var largura = o[0] === "l1" || o[0] === "r1" ? 240 : 180
                ctx.beginPath()
                ctx.roundedRect(o[2], o[3], largura, 88, 44, 44)
                if (aceso) {
                    ctx.fillStyle = Theme.accent
                    ctx.fill()
                } else {
                    ctx.lineWidth = 7
                    ctx.strokeStyle = neutro
                    ctx.stroke()
                }
                ctx.fillStyle = aceso ? "#FFFFFF" : neutro
                ctx.fillText(o[1], o[2] + largura / 2, o[3] + 46)
            }
        }
    }
}
