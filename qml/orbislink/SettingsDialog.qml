// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: dialog
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 730
    height: Math.min(parent ? parent.height - 80 : 620, 660)
    padding: 0

    // Um modal quase opaco: com a transparência dos painéis, o que está por
    // trás ver-se-ia através da caixa, e uma caixa que pede uma decisão não
    // pode ser uma janela.
    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radius
    }

    property var values: ({})

    // O assistente de primeira utilização vive no Main.qml; daqui só se
    // pede que volte a aparecer.
    signal abrirAssistente()

    function resolucaoParaIndice(resolucao) {
        var ordem = [1080, 720, 540, 360]
        var i = ordem.indexOf(resolucao)
        return i >= 0 ? i : 1
    }

    function idiomaParaIndice(idioma) {
        if (idioma === "pt_PT" || idioma === "pt") return 1
        if (idioma === "en") return 2
        return 0
    }

    // Verificação automática do endereço: "idle", "checking", "ok",
    // "partial" (só um dos serviços responde) ou "fail".
    property string probeState: "idle"
    property string probeDetail: ""
    property bool probeFtpOk: false
    property bool probeInstallerOk: false

    readonly property color probeColor: probeState === "ok" ? Theme.ok
        : probeState === "partial" ? Theme.warn
        : probeState === "fail" ? Theme.error
        : probeState === "checking" ? Theme.warn
        : Theme.textMuted

    readonly property string probeText: {
        if (probeState === "checking")
            return qsTr("A verificar %1…").arg(addressField.text.trim())
        if (probeState === "ok")
            return probeDetail.length > 0
                ? qsTr("Consola encontrada — instalador e FTP respondem (%1).").arg(probeDetail)
                : qsTr("Consola encontrada — instalador e FTP respondem.")
        if (probeState === "partial") {
            if (probeInstallerOk)
                return qsTr("Instalador remoto responde, FTP não. Confirma que o servidor FTP do GoldHEN está ativo.")
            return qsTr("FTP responde, instalador remoto não. Abre o Remote Package Installer na consola.")
        }
        if (probeState === "fail")
            return qsTr("Sem resposta de %1. Confirma o IP e que a consola está ligada na mesma rede.").arg(addressField.text.trim())
        return qsTr("Escreve o endereço IP da consola — é verificado sozinho.")
    }

    // Só vale a pena ligar quando o endereço está inteiro: um IPv4 completo
    // (senão verificava-se "192.168.1." a cada tecla) ou um nome de máquina.
    function addressLooksComplete(text) {
        var value = (text || "").trim()
        if (value.length === 0)
            return false
        if (/^[0-9.]+$/.test(value)) {
            var parts = value.match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/)
            if (!parts)
                return false
            for (var i = 1; i <= 4; ++i) {
                if (parseInt(parts[i], 10) > 255)
                    return false
            }
            return true
        }
        return /^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(value)
    }

    function scheduleProbe() {
        if (!addressLooksComplete(addressField.text)) {
            probeState = "idle"
            probeDetail = ""
            probeTimer.stop()
            return
        }
        probeState = "checking"
        probeTimer.restart()
    }

    Timer {
        id: probeTimer
        // Espera que se pare de escrever antes de ir à rede.
        interval: 600
        repeat: false
        onTriggered: app.probeConsole(addressField.text.trim(),
            parseInt(ftpPortField.text) || 2121,
            parseInt(installerPortField.text) || 12800)
    }

    Connections {
        target: app
        function onConsoleProbed(address, ftpOk, installerOk, detail) {
            // Uma resposta de um endereço que já não é o escrito não conta.
            if (address !== addressField.text.trim())
                return
            dialog.probeFtpOk = ftpOk
            dialog.probeInstallerOk = installerOk
            dialog.probeDetail = detail
            dialog.probeState = (ftpOk && installerOk) ? "ok"
                : (ftpOk || installerOk) ? "partial" : "fail"
        }
    }

    onOpened: scheduleProbe()

    function loadValues() {
        values = app.settingsMap()
        nameField.text = values.consoleName
        addressField.text = values.consoleAddress
        ftpPortField.text = values.ftpPort
        installerPortField.text = values.installerPort
        httpPortField.text = values.httpPort
        restrictBox.checked = values.restrictToConsoleIp
        modeBox.currentIndex = values.defaultMode
        resolucaoBox.currentIndex = resolucaoParaIndice(values.streamResolution)
        fpsBox.currentIndex = values.streamFps === 30 ? 1 : 0
        bitrateField.text = values.streamBitrateKbps > 0 ? String(values.streamBitrateKbps) : ""
        hardwareBox.checked = values.streamHardwareDecode
        fullscreenBox.checked = values.streamFullscreenOnConnect
        rumbleBox.checked = values.streamRumble
        touchpadBox.checked = values.streamTouchpadFromMouse
        accountField.text = values.streamAccountId
        idiomaBox.currentIndex = idiomaParaIndice(values.language)
        uploadDirField.text = values.ftpUploadDirectory
        existsBox.checked = values.checkAlreadyInstalled
        installAfterBox.checked = values.installAfterUpload
        updatesBox.checked = values.checkForUpdates
        updateRepoField.text = values.updateRepository
        updateChannelBox.currentIndex = values.updateChannel === "testes" ? 1 : 0
        deleteAfterBox.checked = values.deleteFromConsoleAfterInstall
        advancedBox.checked = values.ftpAdvancedMode
        debugBox.checked = values.debugLogging
    }

    function save() {
        app.applySettings({
            "consoleName": nameField.text,
            "consoleAddress": addressField.text,
            "ftpPort": parseInt(ftpPortField.text) || 2121,
            "installerPort": parseInt(installerPortField.text) || 12800,
            "httpPort": parseInt(httpPortField.text) || 8765,
            "restrictToConsoleIp": restrictBox.checked,
            "defaultMode": modeBox.currentIndex,
            "ftpUploadDirectory": uploadDirField.text,
            "checkAlreadyInstalled": existsBox.checked,
            "installAfterUpload": installAfterBox.checked,
            "checkForUpdates": updatesBox.checked,
            "updateRepository": updateRepoField.text.trim(),
            "updateChannel": updateChannelBox.currentIndex === 1 ? "testes" : "estavel",
            "deleteFromConsoleAfterInstall": deleteAfterBox.checked,
            "ftpAdvancedMode": advancedBox.checked,
            "debugLogging": debugBox.checked,
            "streamResolution": [1080, 720, 540, 360][resolucaoBox.currentIndex],
            "streamFps": fpsBox.currentIndex === 1 ? 30 : 60,
            "streamBitrateKbps": parseInt(bitrateField.text) || 0,
            "streamHardwareDecode": hardwareBox.checked,
            "streamFullscreenOnConnect": fullscreenBox.checked,
            "streamRumble": rumbleBox.checked,
            "streamTouchpadFromMouse": touchpadBox.checked,
            "streamAccountId": accountField.ok ? accountField.base64 : accountField.text.trim(),
            "language": ["auto", "pt_PT", "en"][idiomaBox.currentIndex]
        })
        close()
    }

    header: Rectangle {
        implicitHeight: Theme.dialogHeader
        color: "transparent"
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.dialogMargin
            text: qsTr("Definições")
            color: Theme.text
            font.pixelSize: 16
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
        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.border
        }
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogInner
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 10
            StyledButton {
                text: qsTr("Assistente…")
                larguraMinima: 130
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Voltar a ver os três passos da primeira utilização")
                onClicked: dialog.abrirAssistente()
            }
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Cancelar")
                larguraMinima: 110
                onClicked: dialog.close()
            }
            StyledButton {
                text: qsTr("Guardar")
                larguraMinima: 110
                primary: true
                onClicked: dialog.save()
            }
        }
    }

    contentItem: ScrollView {
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: dialog.width - Theme.dialogMargin * 2
            x: Theme.dialogMargin
            y: Theme.dialogInner
            spacing: 14

            Text { text: qsTr("Consola"); color: Theme.accent; font.bold: true; font.pixelSize: 12 }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                Text { text: qsTr("Nome"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField { id: nameField; Layout.fillWidth: true }

                Text { text: qsTr("Endereço IP"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField {
                    id: addressField
                    Layout.fillWidth: true
                    placeholderText: "192.168.1.42"
                    onTextChanged: dialog.scheduleProbe()
                }

                Text { text: qsTr("Porta FTP"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField {
                    id: ftpPortField
                    Layout.fillWidth: true
                    onTextChanged: dialog.scheduleProbe()
                }

                Text { text: qsTr("Porta do instalador"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField {
                    id: installerPortField
                    Layout.fillWidth: true
                    onTextChanged: dialog.scheduleProbe()
                }
            }

            // Resultado da verificação automática, sem botão nenhum.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item {
                    implicitWidth: 12
                    implicitHeight: 12
                    Rectangle {
                        id: probeDot
                        anchors.centerIn: parent
                        width: 8
                        height: 8
                        radius: 4
                        color: dialog.probeColor
                        SequentialAnimation on opacity {
                            running: dialog.probeState === "checking"
                            loops: Animation.Infinite
                            alwaysRunToEnd: true
                            NumberAnimation { to: 0.25; duration: 480; easing.type: Easing.InOutQuad }
                            NumberAnimation { to: 1.0; duration: 480; easing.type: Easing.InOutQuad }
                        }
                        onVisibleChanged: if (!visible) opacity = 1.0
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 12
                        height: 12
                        radius: 6
                        color: "transparent"
                        border.width: 1
                        border.color: dialog.probeColor
                        opacity: dialog.probeState === "ok" ? 0.45 : 0.0
                        Behavior on opacity { NumberAnimation { duration: 180 } }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: dialog.probeText
                    color: dialog.probeState === "idle" ? Theme.textMuted : dialog.probeColor
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Text { text: qsTr("Instalação"); color: Theme.accent; font.bold: true; font.pixelSize: 12 }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                Text { text: qsTr("Modo por omissão"); color: Theme.textMuted; font.pixelSize: 12 }
                ComboBox {
                    id: modeBox
                    Layout.fillWidth: true
                    implicitHeight: 32
                    model: [qsTr("Instalação direta"), qsTr("Envio por FTP")]

                    background: Rectangle {
                        color: Theme.panelAltFill
                        border.color: modeBox.activeFocus ? Theme.accent : Theme.border
                        radius: 6
                    }
                    contentItem: Text {
                        leftPadding: 8
                        text: modeBox.displayText
                        color: Theme.text
                        font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                    }
                    indicator: Text {
                        x: modeBox.width - width - 10
                        y: modeBox.height / 2 - height / 2
                        text: "⌄"
                        color: Theme.textMuted
                        font.pixelSize: 14
                    }
                    delegate: ItemDelegate {
                        width: modeBox.width
                        contentItem: Text {
                            text: modelData
                            color: Theme.text
                            font.pixelSize: 12
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: highlighted ? Theme.accentSoft : Theme.panelAlt
                        }
                        highlighted: modeBox.highlightedIndex === index
                    }
                    popup: Popup {
                        y: modeBox.height
                        width: modeBox.width
                        implicitHeight: contentItem.implicitHeight
                        padding: 1
                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: modeBox.popup.visible ? modeBox.delegateModel : null
                        }
                        background: Rectangle {
                            color: Theme.panelAltFill
                            border.color: Theme.border
                            radius: 6
                        }
                    }
                }

                Text { text: qsTr("Pasta no FTP"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField { id: uploadDirField; Layout.fillWidth: true }
            }

            StyledCheck {
                id: existsBox
                Layout.fillWidth: true
                text: qsTr("Verificar se o título já existe na consola antes de instalar")
            }

            StyledCheck {
                id: installAfterBox
                Layout.fillWidth: true
                text: qsTr("Instalar também depois de enviar por FTP")
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: 11
                text: qsTr("O ficheiro fica guardado na consola e é logo instalado a partir do PC "
                           + "— o instalador remoto só sabe descarregar por HTTP.")
            }
            StyledCheck {
                id: deleteAfterBox
                Layout.fillWidth: true
                Layout.leftMargin: 20
                enabled: installAfterBox.checked
                text: qsTr("E apagar a cópia da consola depois de instalar")
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Text {
                text: qsTr("Servidor HTTP local")
                color: Theme.accent
                font.bold: true
                font.pixelSize: 12
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Text { text: qsTr("Porta"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField { id: httpPortField; Layout.fillWidth: true }
            }

            StyledCheck {
                id: restrictBox
                Layout.fillWidth: true
                text: qsTr("Aceitar pedidos apenas do IP da consola")
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Text { text: qsTr("Remote Play"); color: Theme.accent; font.bold: true; font.pixelSize: 12 }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                Text { text: qsTr("Qualidade"); color: Theme.textMuted; font.pixelSize: 12 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    StyledCombo {
                        id: resolucaoBox
                        Layout.fillWidth: true
                        // O 1080p só existe em PS4 Pro e em PS5. Numa PS4
                        // normal o pedido é baixado para 720p pela própria
                        // consola, e a etiqueta diz isso em vez de deixar
                        // a pessoa a pensar que a definição não faz nada.
                        model: [qsTr("1080p — só em PS4 Pro e PS5"), qsTr("720p — equilíbrio"),
                                qsTr("540p"), qsTr("360p — rede fraca")]
                    }
                    StyledCombo {
                        id: fpsBox
                        implicitWidth: 110
                        model: ["60 fps", "30 fps"]
                    }
                }

                Item {}
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Estas três só valem a partir da próxima ligação: o perfil de "
                               + "vídeo é combinado com a consola no início da sessão. "
                               + "Durante o stream, a barra de baixo mostra o que está "
                               + "mesmo a chegar.")
                    color: Theme.textSecondary
                    font.pixelSize: 10
                }

                Text { text: qsTr("Bitrate"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField {
                    id: bitrateField
                    Layout.fillWidth: true
                    placeholderText: qsTr("automático (kbps)")
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: RegularExpressionValidator { regularExpression: /[0-9]{0,6}/ }
                }

                Text {
                    text: qsTr("Account ID (PSN)")
                    color: Theme.textMuted
                    font.pixelSize: 12
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 8
                }
                // Aceita hexadecimal, decimal ou base64, e mostra as três
                // por baixo. A consola só entende base64, mas isso é
                // problema nosso e não de quem o está a copiar de um ecrã.
                AccountIdField {
                    id: accountField
                    Layout.fillWidth: true
                }
            }

            StyledCheck {
                id: hardwareBox
                text: qsTr("Descodificar o vídeo na placa gráfica (recua para o processador se não der)")
            }
            StyledCheck {
                id: fullscreenBox
                text: qsTr("Ecrã inteiro ao ligar")
            }
            StyledCheck {
                id: rumbleBox
                text: qsTr("Vibração no comando")
            }
            StyledCheck {
                id: touchpadBox
                text: qsTr("Rato faz de touchpad durante o stream")
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Text { text: qsTr("Aspeto"); color: Theme.accent; font.bold: true; font.pixelSize: 12 }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Text { text: qsTr("Idioma"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledCombo {
                    id: idiomaBox
                    Layout.fillWidth: true
                    model: [qsTr("Como o sistema"), "Português", "English"]
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: 11
                text: qsTr("O idioma muda na próxima abertura. O tema escolhe-se nos ícones da barra de cima, ao lado das definições.")
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Text { text: qsTr("Actualizações"); color: Theme.accent; font.bold: true; font.pixelSize: 12 }

            StyledCheck {
                id: updatesBox
                Layout.fillWidth: true
                text: qsTr("Updates pela internet")
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: 11
                text: updatesBox.checked
                      ? qsTr("Ao abrir, procura uma versão nova no repositório abaixo e pergunta "
                             + "antes de instalar. Nada é instalado sem o teu clique.")
                      : qsTr("Desligado: a aplicação não vai à internet à procura de versões. "
                             + "\"Verificar agora\" continua a funcionar.")
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Text { text: qsTr("Repositório"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField {
                    id: updateRepoField
                    Layout.fillWidth: true
                    placeholderText: "dono/nome"
                }
                Text { text: qsTr("Canal"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledCombo {
                    id: updateChannelBox
                    Layout.fillWidth: true
                    model: [qsTr("Estável"), qsTr("Testes (builds da branch)")]
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                StyledButton {
                    text: qsTr("Verificar agora")
                    enabled: app.updateState !== "a-verificar"
                             && app.updateState !== "a-descarregar"
                    onClicked: {
                        // Guardar primeiro: senão verificava-se o repositório
                        // antigo, não o que está escrito no campo. Só estes
                        // três valores, e sem fechar: o resultado aparece
                        // aqui ao lado, e quem carregou quer vê-lo.
                        app.saveUpdateSettings(updatesBox.checked,
                                               updateRepoField.text.trim(),
                                               updateChannelBox.currentIndex === 1
                                                   ? "testes" : "estavel")
                        app.checkForUpdatesNow(false)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: app.updateMessage
                    color: app.updateState === "erro" ? Theme.error
                         : app.updateState === "disponivel" ? Theme.ok
                         : Theme.textMuted
                    font.pixelSize: 11
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: 11
                text: qsTr("Os lançamentos são lidos da API do GitHub e o repositório tem "
                           + "de ser público. \"Verificar agora\" guarda as definições primeiro.")
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Text { text: qsTr("Avançado"); color: Theme.accent; font.bold: true; font.pixelSize: 12 }

            StyledCheck {
                id: advancedBox
                Layout.fillWidth: true
                labelColor: Theme.warn
                text: qsTr("Modo avançado: permite escrever nas zonas de sistema do FTP")
            }

            StyledCheck {
                id: debugBox
                Layout.fillWidth: true
                text: qsTr("Registo detalhado (debug)")
            }

            Item { Layout.fillWidth: true; Layout.preferredHeight: 6 }
        }
    }
}
