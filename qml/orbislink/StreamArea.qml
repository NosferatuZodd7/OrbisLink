// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Área central da janela: o vídeo do Remote Play, e o que se pode fazer
// antes de o haver (registar o PC na consola, acordá-la, ligar).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    // O palco do vídeo flutua como o resto: margens, cantos largos e uma
    // moldura de vidro. Em ecrã inteiro tudo isso desaparece — aí a
    // imagem é que manda.
    readonly property bool solto: typeof window !== "undefined" && window
                                  && window.streamFullscreen

    Rectangle {
        id: palco
        anchors.fill: parent
        anchors.leftMargin: root.solto ? 0 : Theme.gutter
        anchors.topMargin: root.solto ? 0 : 6
        anchors.bottomMargin: root.solto ? 0 : Theme.gutter
        anchors.rightMargin: root.solto ? 0 : 6
        radius: root.solto ? 0 : Theme.radius
        // Preto sempre que há (ou vai haver) imagem; parado, segue o tema.
        color: root.streaming ? "#000000" : Theme.stageIdle
        Behavior on color { ColorAnimation { duration: Theme.normal } }
        border.width: root.solto ? 0 : 1
        border.color: Theme.glassEdge
        clip: true

        Behavior on anchors.leftMargin { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
        Behavior on radius { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
    }

    // Testa o próprio objecto e não só a bandeira: ao fechar a janela o
    // controlador morre antes das bindings, e sem isto o registo enche-se
    // de "Cannot read property of null".
    readonly property bool built: typeof stream !== "undefined" && stream !== null
    readonly property bool streaming: built && stream.streaming
    readonly property string consoleState: built ? stream.consoleState : "unknown"
    readonly property bool registered: built && stream.registered
    readonly property string sessionState: built ? stream.sessionState : "idle"

    // ───────────────────────────── vídeo
    //
    // Carregado à parte porque é o único sítio com "import QtMultimedia":
    // onde esse módulo não existir, esta parte falha sozinha e o resto da
    // janela abre na mesma.
    Loader {
        id: videoLoader
        anchors.fill: palco
        anchors.margins: 1
        visible: root.streaming
        active: root.built
        source: root.built ? "qrc:/qml/StreamVideo.qml" : ""

        onStatusChanged: {
            if (status === Loader.Error)
                console.warn("Sem QtMultimedia: o Remote Play não tem onde desenhar.")
        }
    }

    readonly property bool videoReady: videoLoader.status === Loader.Ready

    onStreamingChanged: {
        if (streaming) {
            if (videoReady)
                videoLoader.item.forceActiveFocus()
            if (built && stream.fullscreenOnConnect)
                window.setStreamFullscreen(true)
        } else {
            if (built)
                stream.releaseAllKeys()
            window.setStreamFullscreen(false)
        }
    }

    // ───────────────────────────── fundo, quando não há imagem
    Image {
        anchors.fill: palco
        anchors.margins: 1
        source: Theme.stageBackdrop
        fillMode: Image.PreserveAspectFit
        opacity: root.streaming ? 0.0 : Theme.stageBackdropOpacity
        visible: opacity > 0
        asynchronous: true
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }

    Canvas {
        id: grelha
        anchors.fill: palco
        anchors.margins: 1
        visible: !root.streaming
        opacity: 0.25
        // O Canvas não se redesenha sozinho quando a cor muda.
        readonly property color cor: Theme.stageGrid
        onCorChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = cor
            ctx.lineWidth = 1
            for (var x = 0; x < width; x += 40) {
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
            }
            for (var y = 0; y < height; y += 40) {
                ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
            }
        }
    }

    // A consola numa caixa ao centro: um clique liga, acorda, regista ou
    // procura, conforme o estado. Por baixo, só a explicação que o estado
    // pedir (o PIN do registo, o motivo de uma falha, o jogo a correr).
    Column {
        anchors.centerIn: palco
        spacing: 18
        visible: !root.streaming

        // As consolas guardadas lado a lado, e a caixa para juntar mais uma.
        // A que está em uso é a de sempre; as outras mostram o estado delas
        // e um clique passa a usá-las.
        Row {
            id: filaConsolas
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 18
            readonly property var lista: app.consoles
            // Encolhem juntas, sem mudar de proporções, quando não cabem.
            readonly property real fator: Math.max(0.42, Math.min(1.0,
                (palco.width - 60 - spacing * lista.length) / (360 * lista.length + 180)))

            Repeater {
                model: filaConsolas.lista

                ConsoleCard {
                    readonly property var outra: root.built && !modelData.active
                                                 ? stream.consoleStates[modelData.address] : undefined
                    fator: filaConsolas.fator
                    ativa: modelData.active
                    disponivel: root.built
                    endereco: modelData.address
                    nome: modelData.active && root.built && stream.consoleName.length > 0
                          ? stream.consoleName : modelData.name
                    estado: modelData.active ? root.consoleState
                          : (outra ? outra.state : "unknown")
                    registada: modelData.active ? root.registered : (outra ? outra.registered : false)
                    ps5: modelData.active ? (root.built && stream.consolePs5)
                                          : (outra ? outra.ps5 : false)
                    aLigar: modelData.active && root.sessionState === "connecting"
                    aProcurar: modelData.active && root.built && stream.searching
                    onLigar: stream.startStream()
                    onAcordar: stream.wakeUp()
                    onRegistar: registerDialog.open()
                    onProcurar: stream.refreshConsole()
                    onCancelar: stream.stopStream()
                    onEditar: registerDialog.open()
                    onEscolher: app.selectConsole(modelData.address)
                    onRemover: app.removeConsole(modelData.address)
                }
            }

            AddConsoleCard {
                width: 180 * filaConsolas.fator
                height: 280 * filaConsolas.fator
                onAdicionar: addConsoleDialog.open()
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(palco.width - 80, 440)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: text.length > 0
            color: Theme.onIdleStageMuted
            font.pixelSize: 12
            text: {
                if (!root.built)
                    return qsTr("Este pacote foi compilado sem o chiaki-ng. Tudo o resto — "
                                + "instalar pkg e FTP — funciona na mesma.")
                if (root.sessionState === "failed" && stream.sessionDetail.length > 0)
                    return stream.sessionDetail
                if (root.consoleState === "offline")
                    return qsTr("Confirma o IP nas definições e que a consola está ligada "
                                + "na mesma rede.")
                if (!root.registered && root.consoleState !== "unknown")
                    return qsTr("Na consola: Definições → Definições de Ligação do Remote Play "
                                + "→ Adicionar Dispositivo. Aparece um PIN de 8 dígitos.")
                if (stream.runningApp.length > 0)
                    return qsTr("A correr: %1").arg(stream.runningApp)
                return ""
            }
        }
    }

    // ───────────────────────────── barra durante o stream
    //
    // Flutua sobre a imagem, em cápsula de vidro, e aparece com um fade
    // em vez de saltar para o ecrã.
    Rectangle {
        id: barraStream
        anchors.top: palco.top
        anchors.horizontalCenter: palco.horizontalCenter
        anchors.topMargin: 16
        readonly property bool mostrar: root.streaming
                                        && (streamBar.containsMouse || streamHover.hovered)
        visible: opacity > 0.01
        opacity: mostrar ? 1.0 : 0.0
        scale: mostrar ? 1.0 : 0.96
        Behavior on opacity { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
        Behavior on scale {
            NumberAnimation { duration: Theme.normal; easing.type: Theme.easeSpring; easing.overshoot: 1.05 }
        }
        color: Qt.rgba(0.04, 0.04, 0.06, 0.72)
        radius: height / 2
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.14)
        implicitWidth: streamRow.implicitWidth + 32
        implicitHeight: 44

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.10) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        RowLayout {
            id: streamRow
            anchors.centerIn: parent
            spacing: 12
            // O que está mesmo a chegar: o tamanho da imagem e os fps
            // contados. Não é o que foi pedido nas definições — a consola
            // pode mandar menos e não avisar.
            Text {
                text: {
                    if (!root.built || stream.frameWidth <= 0)
                        return ""
                    var texto = qsTr("%1×%2").arg(stream.frameWidth).arg(stream.frameHeight)
                    if (stream.measuredFps > 0)
                        texto += qsTr(" · %1 fps").arg(stream.measuredFps)
                    return texto
                }
                color: Theme.onStageMuted
                font.pixelSize: 11
            }
            Text {
                visible: root.built && stream.gamepadName.length > 0
                text: root.built ? stream.gamepadName : ""
                color: Theme.ok
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.maximumWidth: 160
            }
            StyledButton {
                readonly property string somEstado: root.built ? stream.audioState : "parado"
                text: !root.built ? qsTr("Som")
                     : somEstado === "erro" || somEstado === "sem-dispositivo" ? qsTr("Sem som")
                     : stream.muted ? qsTr("Som: desligado")
                     : qsTr("Som: ligado")
                danger: somEstado === "erro" || somEstado === "sem-dispositivo"
                implicitHeight: 30
                font.pixelSize: 11
                ToolTip.visible: hovered
                ToolTip.text: somEstado === "sem-dispositivo"
                        ? qsTr("Este PC não tem saída de som activa.")
                    : somEstado === "erro"
                        ? qsTr("A placa de som recusou o stream — vê o Ctrl+L.")
                    : somEstado === "a-tocar"
                        ? qsTr("A sair por %1").arg(stream.audioDevice)
                        : qsTr("Ainda não chegou som da consola.")
                onClicked: stream.muted = !stream.muted
            }
            Text {
                text: root.built && stream.hardwareDecoder
                      ? qsTr("placa gráfica") : qsTr("processador")
                color: Theme.onStageMuted
                font.pixelSize: 10
            }
            // O microfone tem de se ver. Quando está a captar, o botão fica
            // aceso — ninguém pode estar a ser ouvido sem dar por isso.
            StyledButton {
                readonly property string micEstado: root.built ? stream.microphoneState
                                                               : "desligado"
                text: micEstado === "a-falar" ? qsTr("🎤 A falar")
                     : micEstado === "em-silencio" ? qsTr("🎤 Em silêncio")
                     : qsTr("Microfone")
                implicitHeight: 30
                font.pixelSize: 11
                danger: micEstado === "a-falar"
                ToolTip.visible: hovered
                ToolTip.text: micEstado === "desligado"
                    ? qsTr("Enviar o teu microfone para a consola")
                    : qsTr("A captar de %1. Clica para calar, ou usa o botão direito para "
                           + "desligar.").arg(stream.microphoneDevice)
                onClicked: {
                    if (micEstado === "desligado")
                        stream.setMicrophoneEnabled(true)
                    else
                        stream.setMicrophoneMuted(micEstado === "a-falar")
                }
                // Botão direito desliga de vez, em vez de só calar.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: stream.setMicrophoneEnabled(false)
                }
            }
            StyledButton {
                text: window.streamFullscreen ? qsTr("Sair do ecrã inteiro")
                                              : qsTr("Ecrã inteiro")
                implicitHeight: 30
                font.pixelSize: 11
                onClicked: window.setStreamFullscreen(!window.streamFullscreen)
            }
            StyledButton {
                text: qsTr("Teclas")
                implicitHeight: 30
                font.pixelSize: 11
                onClicked: keysDialog.open()
            }
            StyledButton {
                text: qsTr("Terminar sessão")
                implicitHeight: 30
                font.pixelSize: 11
                onClicked: stream.stopStream()
            }
        }
    }

    HoverHandler { id: streamHover }
    MouseArea { id: streamBar; anchors.fill: palco; hoverEnabled: true; acceptedButtons: Qt.NoButton }

    // Mapa do teclado, porque ninguém adivinha que V é o triângulo.
    //
    // ATENÇÃO: um Dialog sem "background" próprio usa o do estilo Basic, que
    // é branco, e o texto do tema por cima é claro. Por isso tem fundo
    // próprio, como todos os diálogos (o scripts/check-qml.py verifica).
    Dialog {
        id: keysDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 60 : 780, 780)
        height: Math.min(parent ? parent.height - 60 : 680, 680)
        modal: true
        padding: 0
        // À espera de uma tecla nova, o Esc cancela a escolha e não fecha a
        // janela.
        closePolicy: keyboardMap.escolhida.length > 0
                     ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: keyboardMap.editando = false

        Overlay.modal: Rectangle { color: Theme.scrim }

        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radius
        }

        header: Rectangle {
            implicitHeight: Theme.dialogHeader
            color: "transparent"
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: Theme.dialogMargin
                text: qsTr("O teclado como comando")
                color: Theme.text
                font.pixelSize: 15
                font.bold: true
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        footer: Rectangle {
            implicitHeight: Theme.dialogFooter
            color: "transparent"
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 10
                StyledButton {
                    visible: root.built
                    text: keyboardMap.editando ? qsTr("Concluir") : qsTr("Mudar teclas")
                    larguraMinima: 130
                    onClicked: keyboardMap.editando = !keyboardMap.editando
                }
                StyledButton {
                    visible: root.built && keyboardMap.editando
                    text: qsTr("Repor")
                    larguraMinima: 100
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Voltar às teclas por omissão")
                    onClicked: {
                        keyboardMap.escolhida = ""
                        stream.resetKeyBindings()
                    }
                }
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Fechar")
                    larguraMinima: 110
                    primary: true
                    onClicked: keysDialog.close()
                }
            }
        }

        contentItem: ScrollView {
            id: keysScroll
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth

            ColumnLayout {
                width: keysScroll.availableWidth - Theme.dialogMargin * 2
                x: Theme.dialogMargin
                spacing: 18

                Item { Layout.preferredHeight: Theme.dialogInner - 18 }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    text: {
                        if (!root.built)
                            return ""
                        if (stream.gamepadName.length > 0)
                            return qsTr("Comando ligado: %1. O teclado também funciona:")
                                .arg(stream.gamepadName)
                        return qsTr("Nenhum comando ligado — liga um por USB e é reconhecido "
                                    + "sozinho. Entretanto, o teclado:")
                    }
                }

                // Os dois ao centro, um por cima do outro, e a explicação numa
                // caixa por baixo: o olho desce da tecla para o botão e dele
                // para o texto.
                KeyboardMap {
                    id: keyboardMap
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Math.min(parent.width, implicitWidth)
                    Layout.preferredHeight: implicitHeight
                }

                ControllerSketch {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 236
                    Layout.preferredHeight: 178
                    destaque: keyboardMap.destaque
                }

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Math.min(parent.width, 520)
                    // Altura fixa para duas linhas: a caixa não pode saltar
                    // de tamanho cada vez que o rato passa de tecla em tecla.
                    Layout.preferredHeight: 56
                    radius: Theme.radiusSmall
                    readonly property bool cheia: keyboardMap.texto.length > 0
                    color: cheia ? Theme.accentFill
                         : Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b,
                                   Theme.claro ? 1.0 : 0.6)
                    border.width: 1
                    border.color: cheia ? Theme.accent
                                : Theme.claro ? Qt.rgba(0, 0, 0, 0.12) : Theme.glassEdge
                    Behavior on color { ColorAnimation { duration: Theme.fast } }

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                        color: parent.cheia ? Theme.text : Theme.textSecondary
                        font.pixelSize: parent.cheia ? 14 : 12
                        font.bold: parent.cheia
                        text: parent.cheia ? keyboardMap.texto
                            : keyboardMap.editando
                              ? qsTr("Clica na tecla que queres mudar e depois carrega na tecla "
                                     + "nova. Se ela já tiver uma função, as duas trocam.")
                              : qsTr("Passa o rato por cima de uma tecla para ver no comando o "
                                     + "botão que ela faz. As teclas apagadas não fazem nada.")
                    }
                }

                Item { Layout.preferredHeight: 4 }
            }
        }
    }

    // Só para as capturas de ecrã.
    Timer {
        running: typeof demoKeys !== "undefined" && demoKeys
        interval: 1500
        // Como se o rato estivesse em cima do P: a captura mostra a ligação
        // entre a tecla e o botão aceso no comando.
        onTriggered: {
            keysDialog.open()
            keyboardMap.destaqueRato = "ps"
            keyboardMap.descricao = "P — " + keyboardMap.acoes["ps"].nome
        }
    }

    // As outras consolas da lista: pergunta-se como estão de tempos a tempos,
    // enquanto não há sessão (a que está em uso já é vigiada pelo stream).
    Timer {
        running: root.built && !root.streaming && app.consoles.length > 1
        interval: 8000
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            var outras = []
            var lista = app.consoles
            for (var i = 0; i < lista.length; ++i)
                if (!lista[i].active)
                    outras.push(lista[i].address)
            stream.probeConsoles(outras)
        }
    }

    AddConsoleDialog { id: addConsoleDialog }

    // ───────────────────────────── registo
    StreamRegisterDialog { id: registerDialog }

    // Só para as capturas de ecrã.
    Timer {
        running: typeof demoRegister !== "undefined" && demoRegister
        interval: 1500
        onTriggered: registerDialog.open()
    }

    // O PIN que a consola pede para iniciar sessão na conta — não é o do
    // registo. Tem fundo próprio pela mesma razão do mapa das teclas: num
    // diálogo que bloqueia a ligação, ilegível é pior do que feio.
    Dialog {
        id: loginPinDialog
        property bool incorrect: false
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 420
        modal: true
        padding: 0

        Overlay.modal: Rectangle { color: Theme.scrim }

        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radius
        }

        header: Rectangle {
            implicitHeight: Theme.dialogHeader
            color: "transparent"
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: Theme.dialogMargin
                text: qsTr("PIN da consola")
                color: Theme.text
                font.pixelSize: 15
                font.bold: true
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        footer: Rectangle {
            implicitHeight: Theme.dialogFooter
            color: "transparent"
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 10
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancelar")
                    larguraMinima: 110
                    onClicked: loginPinDialog.close()
                }
                StyledButton {
                    text: qsTr("Enviar")
                    larguraMinima: 110
                    primary: true
                    enabled: loginPinField.text.length > 0
                    onClicked: {
                        stream.sendLoginPin(loginPinField.text)
                        loginPinDialog.close()
                    }
                }
            }
        }

        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: Theme.dialogInner
                wrapMode: Text.WordWrap
                text: loginPinDialog.incorrect
                      ? qsTr("O PIN não estava certo. Tenta outra vez.")
                      : qsTr("A consola pede o PIN de início de sessão da conta.")
                color: loginPinDialog.incorrect ? Theme.error : Theme.textSecondary
                font.pixelSize: 12
            }
            StyledField {
                id: loginPinField
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.bottomMargin: Theme.dialogInner
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhDigitsOnly
                onAccepted: {
                    if (text.length > 0) {
                        stream.sendLoginPin(text)
                        loginPinDialog.close()
                    }
                }
            }
        }
    }

    Connections {
        target: root.built ? stream : null
        function onLoginPinRequested(incorrect) {
            loginPinDialog.incorrect = incorrect
            loginPinField.text = ""
            loginPinDialog.open()
        }
    }
}
