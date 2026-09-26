// SPDX-License-Identifier: AGPL-3.0-or-later
//
// O mapa do teclado desenhado como um teclado: cada tecla que faz alguma
// coisa mostra a letra dela e, dentro da própria tecla, o botão da
// PlayStation que faz (o P leva o "PS", o V leva o △…). As outras teclas
// ficam apagadas, só para o desenho se reconhecer como um teclado.
//
// As teclas vêm do stream (stream.keyBindings), por isso o desenho mostra
// sempre o mapa em uso. Em modo de edição, clicar numa tecla escolhe a
// acção dela; a seguir, carregar numa tecla do teclado (ou clicar noutra
// do desenho) muda-a para lá.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: mapa

    // O botão por baixo do rato, para o desenho do comando o acender.
    property string destaque: escolhida.length > 0 ? acoes[escolhida].alvo : destaqueRato
    property string destaqueRato: ""
    property string descricao: ""

    property bool editando: false
    // A acção à espera de uma tecla nova ("" quando nenhuma).
    property string escolhida: ""
    property string aviso: ""

    readonly property bool temStream: typeof stream !== "undefined" && stream !== null
    readonly property var ligacoes: temStream ? stream.keyBindings : ({})

    // tecla → acção, ao contrário do que vem do stream.
    readonly property var porTecla: {
        var inverso = {}
        for (var acao in ligacoes)
            inverso[ligacoes[acao]] = acao
        return inverso
    }

    // O texto da caixa por baixo do comando.
    readonly property string texto: {
        if (escolhida.length > 0)
            return qsTr("A mudar %1: carrega na tecla nova, ou clica numa tecla do desenho. "
                        + "Esc cancela.").arg(acoes[escolhida].nome)
        if (aviso.length > 0)
            return aviso
        return descricao
    }

    readonly property color corCruz: "#7CB2E8"
    readonly property color corCirculo: "#FF6B6B"
    readonly property color corQuadrado: "#E88BD6"
    readonly property color corTriangulo: "#40E0B0"

    // O que cada acção mostra dentro da tecla. "alvo" é a parte do comando
    // que se acende; "tipo" diz como se desenha o sinal.
    readonly property var acoes: ({
        "cross":        { sinal: "✕", tipo: "simbolo", cor: corCruz, alvo: "cross", nome: qsTr("Cruz") },
        "circle":       { sinal: "◯", tipo: "simbolo", cor: corCirculo, alvo: "circle", nome: qsTr("Círculo") },
        "square":       { sinal: "▢", tipo: "simbolo", cor: corQuadrado, alvo: "square", nome: qsTr("Quadrado") },
        "triangle":     { sinal: "△", tipo: "simbolo", cor: corTriangulo, alvo: "triangle", nome: qsTr("Triângulo") },
        "dpad_up":      { sinal: "✚↑", tipo: "botao", alvo: "dpad", nome: qsTr("cruzeta para cima") },
        "dpad_down":    { sinal: "✚↓", tipo: "botao", alvo: "dpad", nome: qsTr("cruzeta para baixo") },
        "dpad_left":    { sinal: "✚←", tipo: "botao", alvo: "dpad", nome: qsTr("cruzeta para a esquerda") },
        "dpad_right":   { sinal: "✚→", tipo: "botao", alvo: "dpad", nome: qsTr("cruzeta para a direita") },
        "l1":           { sinal: "L1", tipo: "botao", alvo: "l1", nome: qsTr("L1") },
        "l2":           { sinal: "L2", tipo: "botao", alvo: "l2", nome: qsTr("L2 (gatilho)") },
        "r1":           { sinal: "R1", tipo: "botao", alvo: "r1", nome: qsTr("R1") },
        "r2":           { sinal: "R2", tipo: "botao", alvo: "r2", nome: qsTr("R2 (gatilho)") },
        "l3":           { sinal: "L3", tipo: "botao", alvo: "lstick", nome: qsTr("L3 (carregar no analógico esquerdo)") },
        "r3":           { sinal: "R3", tipo: "botao", alvo: "rstick", nome: qsTr("R3 (carregar no analógico direito)") },
        "lstick_up":    { sinal: "L↑", tipo: "botao", alvo: "lstick", nome: qsTr("analógico esquerdo para cima") },
        "lstick_left":  { sinal: "L←", tipo: "botao", alvo: "lstick", nome: qsTr("analógico esquerdo para a esquerda") },
        "lstick_down":  { sinal: "L↓", tipo: "botao", alvo: "lstick", nome: qsTr("analógico esquerdo para baixo") },
        "lstick_right": { sinal: "L→", tipo: "botao", alvo: "lstick", nome: qsTr("analógico esquerdo para a direita") },
        "rstick_up":    { sinal: "R↑", tipo: "botao", alvo: "rstick", nome: qsTr("analógico direito para cima") },
        "rstick_left":  { sinal: "R←", tipo: "botao", alvo: "rstick", nome: qsTr("analógico direito para a esquerda") },
        "rstick_down":  { sinal: "R↓", tipo: "botao", alvo: "rstick", nome: qsTr("analógico direito para baixo") },
        "rstick_right": { sinal: "R→", tipo: "botao", alvo: "rstick", nome: qsTr("analógico direito para a direita") },
        "options":      { sinal: "OPT", tipo: "botao", alvo: "options", nome: qsTr("Options") },
        "share":        { sinal: "SHR", tipo: "botao", alvo: "share", nome: qsTr("Share") },
        "touchpad":     { sinal: "PAD", tipo: "botao", alvo: "touchpad", nome: qsTr("carregar no touchpad") },
        "ps":           { sinal: "PS", tipo: "ps", alvo: "ps", nome: qsTr("botão PS") }
    })

    // As teclas, em unidades de uma tecla: [rótulo, código, x, linha, largura].
    // Um teclado a sério, reduzido às cinco filas que interessam e às setas.
    readonly property var teclas: [
        ["Esc", Qt.Key_Escape, 0, 0, 1],
        ["1", Qt.Key_1, 1.5, 0, 1], ["2", Qt.Key_2, 2.5, 0, 1], ["3", Qt.Key_3, 3.5, 0, 1],
        ["4", Qt.Key_4, 4.5, 0, 1], ["5", Qt.Key_5, 5.5, 0, 1], ["6", Qt.Key_6, 6.5, 0, 1],
        ["7", Qt.Key_7, 7.5, 0, 1], ["8", Qt.Key_8, 8.5, 0, 1], ["9", Qt.Key_9, 9.5, 0, 1],
        ["0", Qt.Key_0, 10.5, 0, 1], ["⌫", Qt.Key_Backspace, 11.5, 0, 2],
        ["Tab", Qt.Key_Tab, 0, 1, 1.5],
        ["Q", Qt.Key_Q, 1.5, 1, 1], ["W", Qt.Key_W, 2.5, 1, 1], ["E", Qt.Key_E, 3.5, 1, 1],
        ["R", Qt.Key_R, 4.5, 1, 1], ["T", Qt.Key_T, 5.5, 1, 1], ["Y", Qt.Key_Y, 6.5, 1, 1],
        ["U", Qt.Key_U, 7.5, 1, 1], ["I", Qt.Key_I, 8.5, 1, 1], ["O", Qt.Key_O, 9.5, 1, 1],
        ["P", Qt.Key_P, 10.5, 1, 1],
        ["Caps", Qt.Key_CapsLock, 0, 2, 1.75],
        ["A", Qt.Key_A, 1.75, 2, 1], ["S", Qt.Key_S, 2.75, 2, 1], ["D", Qt.Key_D, 3.75, 2, 1],
        ["F", Qt.Key_F, 4.75, 2, 1], ["G", Qt.Key_G, 5.75, 2, 1], ["H", Qt.Key_H, 6.75, 2, 1],
        ["J", Qt.Key_J, 7.75, 2, 1], ["K", Qt.Key_K, 8.75, 2, 1], ["L", Qt.Key_L, 9.75, 2, 1],
        ["Enter", Qt.Key_Return, 10.75, 2, 2.75],
        ["Shift", Qt.Key_Shift, 0, 3, 2.25],
        ["Z", Qt.Key_Z, 2.25, 3, 1], ["X", Qt.Key_X, 3.25, 3, 1], ["C", Qt.Key_C, 4.25, 3, 1],
        ["V", Qt.Key_V, 5.25, 3, 1], ["B", Qt.Key_B, 6.25, 3, 1], ["N", Qt.Key_N, 7.25, 3, 1],
        ["M", Qt.Key_M, 8.25, 3, 1],
        ["Ctrl", Qt.Key_Control, 0, 4, 1.5], ["Alt", Qt.Key_Alt, 1.5, 4, 1.25],
        [qsTr("Espaço"), Qt.Key_Space, 2.75, 4, 6.5], ["AltGr", Qt.Key_AltGr, 9.25, 4, 1.25],
        ["↑", Qt.Key_Up, 15, 3, 1],
        ["←", Qt.Key_Left, 14, 4, 1], ["↓", Qt.Key_Down, 15, 4, 1], ["→", Qt.Key_Right, 16, 4, 1]
    ]

    // Acções cujas teclas não estão no desenho (F1, teclado numérico…):
    // ficam escritas por baixo, para nenhuma acção desaparecer do mapa.
    readonly property string foraDoDesenho: {
        var desenhadas = {}
        for (var i = 0; i < teclas.length; ++i)
            desenhadas[teclas[i][1]] = true
        var linhas = []
        for (var acao in ligacoes) {
            var codigo = ligacoes[acao]
            if (!desenhadas[codigo] && acoes[acao])
                linhas.push((temStream ? stream.keyName(codigo) : codigo) + " → "
                            + acoes[acao].sinal + " " + acoes[acao].nome)
        }
        return linhas.join("   ·   ")
    }

    // 17 unidades de largura; cada unidade encolhe se a janela for estreita.
    readonly property real unidade: Math.min(40, width / 17)
    readonly property real folga: Math.max(3, unidade * 0.1)

    implicitWidth: 17 * 40
    implicitHeight: 5 * unidade + (foraDoDesenho.length > 0 ? 22 : 0)

    function atribuir(codigo) {
        if (!temStream || escolhida.length === 0)
            return
        if (codigo === Qt.Key_Escape) {
            escolhida = ""
            return
        }
        if (stream.setKeyBinding(escolhida, codigo)) {
            aviso = ""
            escolhida = ""
        } else {
            aviso = qsTr("Essa tecla não se pode usar: o Esc sai do stream e o F11 muda o "
                         + "ecrã inteiro.")
            escolhida = ""
        }
    }

    onEditandoChanged: { escolhida = ""; aviso = "" }
    onEscolhidaChanged: if (escolhida.length > 0) forceActiveFocus()

    // A tecla nova vem daqui. Aceita-se o evento para o diálogo não o usar
    // (o Enter não carrega num botão, o Esc não fecha a janela).
    Keys.onPressed: function (event) {
        if (!editando || escolhida.length === 0)
            return
        if (event.isAutoRepeat) {
            event.accepted = true
            return
        }
        atribuir(event.key)
        event.accepted = true
    }

    Repeater {
        model: mapa.teclas

        Rectangle {
            id: tecla
            readonly property string rotulo: modelData[0]
            readonly property int codigo: modelData[1]
            readonly property bool fixa: codigo === Qt.Key_Escape
            readonly property string acao: mapa.porTecla[codigo] !== undefined
                                           ? mapa.porTecla[codigo] : ""
            readonly property var funcao: fixa
                ? { sinal: qsTr("sair"), tipo: "aviso", alvo: "", nome: qsTr("sai do stream") }
                : (acao.length > 0 ? mapa.acoes[acao] : undefined)
            readonly property bool util: funcao !== undefined
            readonly property bool escolhidaAqui: acao.length > 0 && mapa.escolhida === acao
            // Em edição, qualquer tecla do desenho responde ao rato: é um
            // destino possível para a acção escolhida.
            readonly property bool sobre: area.containsMouse
                                          && (util || (mapa.editando && mapa.escolhida.length > 0))

            x: modelData[2] * mapa.unidade
            y: modelData[3] * mapa.unidade
            width: modelData[4] * mapa.unidade - mapa.folga
            height: mapa.unidade - mapa.folga
            radius: Math.round(mapa.unidade * 0.2)

            // No claro o painel é branco, como o diálogo: as teclas úteis
            // levam um cinzento-gelo e uma aresta escura, senão seriam as
            // apagadas a ver-se.
            color: escolhidaAqui || sobre ? Theme.accentFill
                 : !util ? "transparent"
                 : Theme.claro ? "#EEF1F6"
                 : Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b, 0.9)
            border.width: escolhidaAqui ? 2 : 1
            border.color: escolhidaAqui || sobre ? Theme.accent
                        : !util ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.10)
                        : Theme.claro ? Qt.rgba(0, 0, 0, 0.22)
                        : Theme.glassEdge
            scale: sobre || escolhidaAqui ? 1.06 : 1.0
            z: sobre || escolhidaAqui ? 1 : 0
            Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
            Behavior on color { ColorAnimation { duration: Theme.fast } }

            // A tecla à espera pisca devagar, para se ver qual está a mudar.
            SequentialAnimation on opacity {
                running: tecla.escolhidaAqui
                loops: Animation.Infinite
                onStopped: tecla.opacity = 1
                NumberAnimation { to: 0.55; duration: 520; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 520; easing.type: Easing.InOutSine }
            }

            // A tecla como está impressa no teclado: em cima, à esquerda.
            Text {
                x: 5
                y: 3
                text: tecla.rotulo
                color: tecla.util ? Theme.text
                                  : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.28)
                font.pixelSize: Math.max(8, Math.round(mapa.unidade * (tecla.rotulo.length > 1 ? 0.24 : 0.3)))
                font.bold: tecla.util
            }

            // E o botão da PlayStation, dentro da mesma tecla, em baixo à
            // direita. É isto que se procura quando se olha para o mapa.
            Rectangle {
                visible: tecla.util
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 3
                readonly property string tipo: tecla.util ? tecla.funcao.tipo : ""
                width: Math.max(height, sinal.implicitWidth + 6)
                height: Math.round(mapa.unidade * 0.42)
                radius: height / 2
                color: tipo === "ps" ? Theme.accent
                     : tipo === "aviso" ? Qt.rgba(Theme.warn.r, Theme.warn.g, Theme.warn.b, 0.20)
                     : tipo === "simbolo" ? "transparent"
                     : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.10)
                Text {
                    id: sinal
                    anchors.centerIn: parent
                    text: tecla.util ? tecla.funcao.sinal : ""
                    color: parent.tipo === "ps" ? "#FFFFFF"
                         : parent.tipo === "simbolo" ? tecla.funcao.cor
                         : parent.tipo === "aviso" ? Theme.warn
                         : Theme.text
                    font.pixelSize: parent.tipo === "simbolo"
                                    ? Math.round(mapa.unidade * 0.36)
                                    : Math.max(7, Math.round(mapa.unidade * 0.22))
                    font.bold: true
                }
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: mapa.editando && (tecla.util || mapa.escolhida.length > 0)
                             && !tecla.fixa ? Qt.PointingHandCursor : Qt.ArrowCursor
                onContainsMouseChanged: {
                    if (containsMouse && tecla.util) {
                        mapa.destaqueRato = tecla.funcao.alvo
                        mapa.descricao = tecla.rotulo + " — " + tecla.funcao.nome
                    } else if (!containsMouse && tecla.util
                               && mapa.descricao === tecla.rotulo + " — " + tecla.funcao.nome) {
                        mapa.destaqueRato = ""
                        mapa.descricao = ""
                    }
                }
                onClicked: {
                    if (!mapa.editando)
                        return
                    if (mapa.escolhida.length > 0) {
                        if (tecla.escolhidaAqui)
                            mapa.escolhida = ""
                        else
                            mapa.atribuir(tecla.codigo)
                        return
                    }
                    if (tecla.acao.length > 0) {
                        mapa.aviso = ""
                        mapa.escolhida = tecla.acao
                    }
                }
            }
        }
    }

    Text {
        visible: mapa.foraDoDesenho.length > 0
        y: 5 * mapa.unidade + 6
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        text: qsTr("Fora do desenho: %1").arg(mapa.foraDoDesenho)
        color: Theme.textSecondary
        font.pixelSize: 11
    }
}
