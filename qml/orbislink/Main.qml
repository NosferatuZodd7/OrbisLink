// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1280
    height: 800
    visible: true
    color: Theme.background
    title: qsTr("OrbisLink — %1").arg(app.consoleName)

    property bool panelVisible: true
    // Ecrã inteiro para o stream: esconde a barra de cima e o painel
    // lateral, e a janela passa a ocupar o ecrã todo.
    property bool streamFullscreen: false

    function setStreamFullscreen(activo) {
        if (streamFullscreen === activo)
            return
        streamFullscreen = activo
        window.visibility = activo ? Window.FullScreen : Window.Windowed
    }

    // F9 abre/fecha o painel lateral (§5.7).
    Shortcut {
        sequence: "F9"
        onActivated: window.panelVisible = !window.panelVisible
    }
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: settingsDialog.loadValues(), settingsDialog.open()
    }
    // O registo à distância de um atalho: quando alguma coisa corre mal, é
    // o primeiro sítio onde olhar.
    Shortcut {
        sequence: "Ctrl+L"
        onActivated: diagnosticsDialog.open()
    }
    // F11 entra e sai de ecrã inteiro; Esc só sai (dentro do stream, o Esc
    // termina a sessão — ver StreamVideo.qml).
    Shortcut {
        sequence: "F11"
        onActivated: window.setStreamFullscreen(!window.streamFullscreen)
    }

    // O fundo de onde o vidro tira a luz.
    GlassBackground { anchors.fill: parent }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ───────────────────────────── barra superior
        Item {
            Layout.fillWidth: true
            // Em ecrã inteiro só interessa a imagem.
            visible: !window.streamFullscreen
            implicitHeight: visible ? 76 : 0

            // Barra flutuante: não toca nas arestas da janela e não tem
            // linha por baixo — a separação faz-se por profundidade.
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: Theme.gutter
                anchors.rightMargin: Theme.gutter
                anchors.topMargin: 14
                anchors.bottomMargin: 6
                radius: Theme.radiusControl + 4
                color: Theme.panelFill
                border.width: 1
                border.color: Theme.glassEdge

                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.glassHighlight) }
                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.05) }
                    }
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.gutter + 20
                anchors.rightMargin: Theme.gutter + 12
                anchors.topMargin: 14
                anchors.bottomMargin: 6
                spacing: 14

                Image {
                    source: "qrc:/icons/mark.png"
                    sourceSize.width: 28
                    sourceSize.height: 28
                    Layout.alignment: Qt.AlignVCenter
                }

                ColumnLayout {
                    spacing: 0
                    Text {
                        text: app.consoleName
                        color: Theme.text
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        font.letterSpacing: -0.4
                    }
                    Text {
                        text: app.consoleAddress.length > 0 ? app.consoleAddress
                                                            : qsTr("sem endereço definido")
                        color: Theme.textSecondary
                        font.pixelSize: 11
                        font.letterSpacing: -0.1
                    }
                }

                Item { Layout.fillWidth: true }

                ServiceIndicator {
                    label: qsTr("Remote Play")
                    state_: app.remotePlayState
                    hint: app.remotePlayHint
                }
                ServiceIndicator {
                    label: qsTr("FTP")
                    state_: app.ftpState
                    hint: app.ftpHint
                }
                ServiceIndicator {
                    label: qsTr("Instalador")
                    state_: app.installerState
                    hint: app.installerHint
                }

                StyledToolButton {
                    text: "⟳"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Verificar serviços agora")
                    onClicked: app.checkServicesNow()
                }
                StyledToolButton {
                    text: "☰"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Painel lateral (F9)")
                    onClicked: window.panelVisible = !window.panelVisible
                }
                StyledToolButton {
                    text: "📋"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Registo e diagnóstico (Ctrl+L)")
                    onClicked: diagnosticsDialog.open()
                }
                ThemeSwitcher {}
                StyledToolButton {
                    text: "⚙"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Definições (Ctrl+,)")
                    onClicked: { settingsDialog.loadValues(); settingsDialog.open() }
                }
            }
        }

        // ───────────────────────────── corpo
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            StreamArea {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            Item {
                Layout.fillHeight: true
                Layout.preferredWidth: 440
                visible: window.panelVisible && !window.streamFullscreen

                Rectangle {
                    anchors.fill: parent
                    anchors.rightMargin: Theme.gutter
                    anchors.topMargin: 6
                    anchors.bottomMargin: Theme.gutter
                    anchors.leftMargin: 6
                    radius: Theme.radius
                    color: Theme.panelFill
                    border.width: 1
                    border.color: Theme.glassEdge
                    clip: true

                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        gradient: Gradient {
                            GradientStop {
                                position: 0.0
                                color: Qt.rgba(1, 1, 1, Theme.glassHighlight)
                            }
                            GradientStop { position: 0.4; color: "transparent" }
                        }
                    }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    TabBar {
                        id: tabs
                        Layout.fillWidth: true
                        currentIndex: demoTab
                        background: Rectangle { color: "transparent" }

                        StyledTab {
                            text: app.queue.count > 0 ? qsTr("Fila (%1)").arg(app.queue.count)
                                                      : qsTr("Fila")
                        }
                        StyledTab { text: qsTr("Ficheiros (FTP)") }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: tabs.currentIndex

                        TransferPanel { }
                        FtpBrowser { }
                    }
                }
                }
            }
        }

        // ───────────────────────────── barra de estado
        Item {
            Layout.fillWidth: true
            visible: !window.streamFullscreen
            implicitHeight: visible ? 34 : 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.gutter + 20
                anchors.rightMargin: Theme.gutter + 20
                spacing: 14

                Text {
                    Layout.fillWidth: true
                    text: app.statusMessage
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Text {
                    text: qsTr("HTTP local: %1").arg(app.httpServerAddress)
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
                Text {
                    text: "v" + app.version
                    color: Qt.rgba(Theme.textMuted.r, Theme.textMuted.g, Theme.textMuted.b, 0.3)
                    font.pixelSize: 11
                }
            }
        }
    }

    // Arrastar sobre a janela mostra a sobreposição. É o único DropArea da
    // aplicação: ter mais um por zona parecia natural mas não funciona —
    // este fica com o arrasto agarrado e as zonas nunca o veem.
    DropArea {
        anchors.fill: parent
        onEntered: function(drag) {
            // Registado antes de qualquer decisão: se isto nunca aparecer
            // no diagnóstico, é porque o Windows não entregou o evento, e
            // aí o problema não está aqui.
            app.noteDrag("entrou", drag.hasUrls)
            if (!drag.hasUrls) {
                drag.accepted = false
                return
            }
            overlay.show(drag.x, drag.y)
            drag.accept()
        }
        onPositionChanged: function(drag) { overlay.movePointer(drag.x, drag.y) }
        onExited: overlay.hide()
        onDropped: function(drop) {
            app.noteDrag("largado", drop.hasUrls)
            if (drop.hasUrls && overlay.dropAt(drop.x, drop.y, drop.urls))
                drop.accept()
            else
                overlay.hide()
        }
    }

    DropOverlay {
        id: overlay
        // Usado pelo teste automático de arrastar (--selftest-drag).
        objectName: "dropOverlay"
        active: demoOverlay
    }

    SettingsDialog {
        id: settingsDialog
        onAbrirAssistente: {
            settingsDialog.close()
            firstRunWizard.comecar()
        }
    }
    DiagnosticsDialog { id: diagnosticsDialog }
    FirstRunWizard { id: firstRunWizard }
    UpdateDialog { id: updateDialog }

    Connections {
        target: app
        function onUpdateAvailable(version) { updateDialog.open() }
    }

    // Só para as capturas de ecrã e para o teste do ecrã inteiro.
    Timer {
        running: typeof demoFullscreen !== "undefined" && demoFullscreen
        interval: 1200
        onTriggered: {
            window.setStreamFullscreen(true)
            console.log("DIAG ecrã inteiro: visibility=" + window.visibility
                        + " esperado=" + Window.FullScreen
                        + " barra=" + window.streamFullscreen)
        }
    }

    Timer {
        running: typeof demoLog !== "undefined" && demoLog
        interval: 1800
        onTriggered: diagnosticsDialog.open()
    }

    // Só para a captura de ecrã: mostra o diálogo de actualização sem
    // precisar de um lançamento novo publicado.
    Timer {
        running: typeof demoUpdate !== "undefined" && demoUpdate
        interval: 1500
        onTriggered: { app.loadDemoUpdate(); updateDialog.open() }
    }

    // O tema vem das definições e muda em tempo real.
    function aplicarTema() {
        if (typeof demoTheme !== "undefined" && demoTheme.length > 0) {
            Theme.aplicar(demoTheme)
            aplicarMolduraDoSistema()
            return
        }
        var valores = app.settingsMap()
        Theme.aplicar(valores.theme)
        aplicarMolduraDoSistema()
    }

    // A barra de título e a moldura são desenhadas pelo sistema, não por
    // nós, e no Windows vêm brancas por omissão. Isto pede-lhe que acompanhe
    // o tema — e, onde o sistema saiba fazê-lo, que use o seu próprio
    // material translúcido.
    function aplicarMolduraDoSistema() {
        if (typeof chrome === "undefined" || !chrome)
            return
        // A barra encosta ao fundo da janela, não à barra flutuante: o que
        // se quer é que pareça a mesma superfície.
        var moldura = Theme.claro ? Qt.darker(Theme.background, 1.10)
                                  : Qt.lighter(Theme.background, 2.2)
        chrome.applyTheme(Theme.background, Theme.text, moldura,
                          !Theme.claro, Theme.nome === "vidro")
    }

    Connections {
        target: app
        function onSettingsChanged() { window.aplicarTema() }
    }

    Component.onCompleted: {
        aplicarTema()
        if (typeof demoSettings !== "undefined" && demoSettings) {
            settingsDialog.loadValues()
            settingsDialog.open()
            return
        }
        if (typeof demoWizard !== "undefined" && demoWizard) {
            firstRunWizard.comecar()
            return
        }
        // Primeira abertura: em vez de uma janela vazia com tudo vermelho,
        // três passos. A flag fica guardada no `guardar()` do assistente.
        var valores = app.settingsMap()
        if (!valores.firstRunDone)
            firstRunWizard.comecar()
    }

    Connections {
        target: app
        function onNotify(title, message, error) {
            toast.show(title + (message.length > 0 ? " — " + message : ""), error)
        }
    }

    // Notificação simples dentro da janela (a do sistema chega na Fase 5).
    // Aviso flutuante em vidro. Entra de baixo com escala, como um painel
    // que sobe, em vez de aparecer do nada.
    Item {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: opacity > 0 ? 56 : 36
        implicitWidth: Math.min(window.width - 120, toastText.implicitWidth + 56)
        implicitHeight: toastText.implicitHeight + 32
        opacity: 0
        visible: opacity > 0.01
        scale: opacity > 0 ? 1.0 : 0.96

        property bool isError: false

        function show(message, error) {
            toastText.text = message
            isError = error
            opacity = 1
            hideTimer.restart()
        }

        Behavior on opacity { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
        Behavior on scale {
            NumberAnimation { duration: Theme.normal; easing.type: Theme.easeSpring; easing.overshoot: 1.08 }
        }
        Behavior on anchors.bottomMargin {
            NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut }
        }

        // Halo da cor do estado, por baixo do vidro.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            radius: (parent.height + 12) / 2
            color: "transparent"
            border.width: 6
            border.color: toast.isError
                ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.18)
                : Qt.rgba(Theme.ok.r, Theme.ok.g, Theme.ok.b, 0.14)
        }

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b,
                           Theme.claro ? 0.88 : 0.82)
            border.width: 1
            border.color: toast.isError
                ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.5)
                : Theme.glassEdge

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.glassHighlight) }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
        }

        Row {
            anchors.centerIn: parent
            spacing: 10

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: toast.isError ? Theme.error : Theme.ok
            }

            Text {
                id: toastText
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, window.width - 180)
                color: Theme.text
                font.pixelSize: 12
                font.weight: Font.Medium
                font.letterSpacing: -0.2
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Timer { id: hideTimer; interval: 5000; onTriggered: toast.opacity = 0 }
    }
}
