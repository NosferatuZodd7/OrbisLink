// SPDX-License-Identifier: AGPL-3.0-or-later
//
// As cores, as formas e o movimento da aplicação, num sítio só.
//
// A linguagem é "liquid glass": superfícies translúcidas sobrepostas, uma
// aresta de luz em cima, sombras difusas, cantos largos e movimento com
// física. A profundidade vem de camadas, não de linhas — por isso quase não
// há separadores nesta interface.
//
// É um singleton e não um .js porque os temas trocam em tempo de execução:
// mudando estas propriedades, tudo o que lhes está ligado redesenha-se.
pragma Singleton
import QtQuick

QtObject {
    id: tema

    // "escuro", "vidro" ou "claro". O "vidro" é o escuro com mais
    // transparência; mantém-se o nome por causa das definições já gravadas.
    property string nome: "escuro"
    readonly property bool claro: nome === "claro"

    // ── Fundo
    // Um preto profundo, não neutro: leva um toque de azul para o vidro
    // por cima ter de onde tirar cor.
    property color background: "#07070A"
    property color backgroundDeep: "#050507"

    // ── Superfícies de vidro
    // "panel" é a cor base; o que se desenha é o panelFill, que já leva a
    // transparência do tema.
    property color panel: "#16161C"
    property color panelAlt: "#1D1D25"
    property real panelOpacity: 0.55
    // A aresta de luz no topo de cada superfície, que é o que faz o vidro
    // parecer curvo em vez de chapado.
    property real glassHighlight: 0.08
    // A moldura de 1px que separa o vidro do que está por trás.
    property real glassBorder: 0.12

    // ── Diálogos
    // Um modal não é uma superfície de fundo: tem de se ler sem o que está
    // por trás a atravessá-lo. O vidro serve para painéis, não para uma
    // caixa que pede uma decisão.
    property real dialogOpacity: 0.97

    // ── Texto
    property color text: "#FFFFFF"
    property color textMuted: "#FFFFFF"
    property real textMutedOpacity: 0.62
    // A cor já com a opacidade aplicada, para quem só quer pintar.
    readonly property color textSecondary: Qt.rgba(textMuted.r, textMuted.g, textMuted.b,
                                                   textMutedOpacity)

    // ── Acento e estados
    property color accent: "#0A84FF"
    property color accentSoft: "#0A84FF"
    property real accentSoftOpacity: 0.18
    property color ok: "#30D158"
    property color warn: "#FFD60A"
    property color error: "#FF453A"

    // Compatibilidade: há código que pinta bordas com "border".
    readonly property color border: Qt.rgba(1, 1, 1, glassBorder)

    // ── Forma
    // Cantos largos e contínuos. 28 nos cartões, 20 nos controlos, cápsula
    // na navegação.
    property int radius: 28
    property int radiusControl: 20
    property int radiusSmall: 14
    property int spacing: 16
    // Margens generosas: o ar é metade do desenho.
    property int gutter: 28
    property int padding: 24
    // Dentro dos diálogos. Onde o canto é curvo, um texto à distância do
    // raio já está dentro da curva, e isso lê-se como descuido. Por isso a
    // margem é o raio mais uma folga que se veja, e não o raio à tangente.
    readonly property int dialogMargin: radius + 8
    // Dentro das caixas de aviso e de conversão, que já estão elas próprias
    // encostadas à margem de cima.
    readonly property int dialogInner: 18
    // Alturas do cabeçalho e do rodapé. O título está centrado na altura,
    // portanto é isto que decide a folga por cima dele — e é o canto de
    // cima que a torna visível.
    readonly property int dialogHeader: 68
    readonly property int dialogFooter: 74

    // ── Movimento
    // Tudo o que se mexe usa estes números, para a interface inteira ter a
    // mesma física em vez de cada peça inventar a sua.
    readonly property int fast: 180
    readonly property int normal: 260
    readonly property int slow: 380
    readonly property int easeOut: Easing.OutCubic
    // O "spring" do Qt sem molas: um OutBack contido chega para dar a
    // sensação de peso sem o exagero de um salto.
    readonly property int easeSpring: Easing.OutBack
    readonly property real hoverScale: 1.02
    readonly property real pressScale: 0.985

    // ── Texto desenhado por cima do vídeo
    // A área do stream é sempre escura, independentemente do tema, por isso
    // o texto por cima dela é sempre claro.
    readonly property color onStage: "#FFFFFF"
    readonly property color onStageMuted: Qt.rgba(1, 1, 1, 0.65)

    // ── O palco parado, sem imagem da consola
    // Aí já não há vídeo a ditar o escuro: o palco segue o tema, com o
    // fundo de cada um (o preto com a grelha verde, o branco com a
    // ciano). Só quando a imagem chega é que volta ao preto e ao onStage.
    // O branco é branco puro de propósito: a imagem clara tem fundo branco,
    // e num palco tingido ver-se-ia a faixa onde ela acaba.
    readonly property url stageBackdrop: claro ? "qrc:/icons/backdrop-light.png"
                                               : "qrc:/icons/backdrop.png"
    // Ciano claro sobre branco quase não se vê com a opacidade que chega
    // ao verde sobre preto; por isso cada tema tem a sua.
    readonly property real stageBackdropOpacity: claro ? 0.45 : 0.15
    readonly property color stageIdle: claro ? "#FFFFFF" : "#000000"
    readonly property color stageGrid: claro ? "#C9D3E3" : "#1B2130"
    readonly property color onIdleStage: claro ? text : onStage
    readonly property color onIdleStageMuted: claro ? textSecondary : onStageMuted

    // ── Caixa da consola
    // Azul-marinho quase preto nos temas escuros, branco gelo no claro. A cor
    // só aparece nos indicadores de estado.
    readonly property color cardTop: claro ? "#FFFFFF" : "#071426"
    readonly property color cardBottom: claro ? "#EEF4FC" : "#0A1E3A"
    readonly property color cardText: claro ? "#0F172A" : "#FFFFFF"
    readonly property color cardTextMuted: claro ? "#5B6B82" : "#A8B3C7"
    readonly property color cardEdge: claro ? Qt.rgba(15 / 255, 23 / 255, 42 / 255, 0.08)
                                            : Qt.rgba(45 / 255, 140 / 255, 1, 0.28)
    readonly property color cardWave: claro ? Qt.rgba(0, 112 / 255, 243 / 255, 0.07)
                                            : Qt.rgba(45 / 255, 140 / 255, 1, 0.12)
    readonly property color cardBlue: "#0070F3"
    readonly property color cardGlow: "#2D8CFF"
    readonly property color cardAmber: "#F5A524"
    readonly property color cardRed: "#E5484D"
    readonly property int cardEase: 250

    // ── As cores que as superfícies usam de facto
    readonly property color panelFill: Qt.rgba(panel.r, panel.g, panel.b, panelOpacity)
    readonly property color panelAltFill: Qt.rgba(panelAlt.r, panelAlt.g, panelAlt.b,
                                                  panelOpacity)
    readonly property color glassEdge: Qt.rgba(1, 1, 1, glassBorder)
    readonly property color glassSheen: Qt.rgba(1, 1, 1, glassHighlight)
    readonly property color accentFill: Qt.rgba(accent.r, accent.g, accent.b, accentSoftOpacity)
    readonly property color shadow: Qt.rgba(0, 0, 0, claro ? 0.10 : 0.45)
    readonly property color dialogFill: Qt.rgba(panel.r, panel.g, panel.b, dialogOpacity)
    // O escurecimento por trás de um modal: é o que diz que o resto da
    // aplicação está à espera.
    readonly property color scrim: Qt.rgba(0, 0, 0, claro ? 0.32 : 0.58)

    function aplicar(qual) {
        nome = qual === "vidro" || qual === "claro" ? qual : "escuro"
        if (nome === "claro") {
            // Branco gelo, superfícies de vidro branco, texto quase preto.
            background = "#F6F7FB"
            backgroundDeep = "#EDEFF5"
            panel = "#FFFFFF"
            panelAlt = "#FFFFFF"
            panelOpacity = 0.68
            dialogOpacity = 0.98
            glassHighlight = 0.55
            glassBorder = 0.55
            text = "#111111"
            textMuted = "#111111"
            textMutedOpacity = 0.55
            accent = "#007AFF"
            accentSoft = "#007AFF"
            accentSoftOpacity = 0.12
            ok = "#28A745"
            warn = "#B26B00"
            error = "#D7362B"
            radius = 28
            radiusControl = 20
        } else if (nome === "vidro") {
            // O mesmo escuro, mas com mais do que está por trás a passar.
            background = "#050507"
            backgroundDeep = "#030304"
            panel = "#14141B"
            panelAlt = "#1B1B24"
            panelOpacity = 0.38
            dialogOpacity = 0.94
            glassHighlight = 0.12
            glassBorder = 0.16
            text = "#FFFFFF"
            textMuted = "#FFFFFF"
            textMutedOpacity = 0.66
            accent = "#0A84FF"
            accentSoft = "#0A84FF"
            accentSoftOpacity = 0.22
            ok = "#30D158"
            warn = "#FFD60A"
            error = "#FF453A"
            radius = 32
            radiusControl = 22
        } else {
            background = "#07070A"
            backgroundDeep = "#050507"
            panel = "#16161C"
            panelAlt = "#1D1D25"
            panelOpacity = 0.55
            dialogOpacity = 0.97
            glassHighlight = 0.08
            glassBorder = 0.12
            text = "#FFFFFF"
            textMuted = "#FFFFFF"
            textMutedOpacity = 0.62
            accent = "#0A84FF"
            accentSoft = "#0A84FF"
            accentSoftOpacity = 0.18
            ok = "#30D158"
            warn = "#FFD60A"
            error = "#FF453A"
            radius = 28
            radiusControl = 20
        }
    }

    function stateColor(state) {
        if (state === "available") return ok
        if (state === "checking") return warn
        if (state === "unavailable") return error
        return textSecondary
    }

    function taskColor(state) {
        if (state === "completed") return ok
        if (state === "error") return error
        if (state === "cancelled") return textSecondary
        if (state === "pending") return textSecondary
        return accent
    }
}
