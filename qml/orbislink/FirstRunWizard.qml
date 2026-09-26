// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Primeira abertura: em vez de uma janela vazia com serviços vermelhos e
// nenhuma explicação, três passos que põem a aplicação a funcionar.
//
// Aparece uma vez. Quem quiser voltar a vê-lo tem o botão nas definições.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: wizard
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 630
    modal: true
    closePolicy: Popup.NoAutoClose
    padding: 0

    property int passo: 0
    readonly property int passos: 3

    // A mesma verificação automática das definições: escreve-se o IP e o
    // assistente diz se a consola respondeu, sem carregar em nada.
    property string probeState: "idle"
    property bool probeFtpOk: false
    property bool probeInstallerOk: false

    readonly property color probeColor: probeState === "ok" ? Theme.ok
        : probeState === "partial" ? Theme.warn
        : probeState === "fail" ? Theme.error
        : probeState === "checking" ? Theme.warn
        : Theme.textMuted

    readonly property string probeText: {
        if (probeState === "checking")
            return qsTr("Checking %1…").arg(enderecoField.text.trim())
        if (probeState === "ok")
            return qsTr("Console found — the installer and FTP both answer.")
        if (probeState === "partial") {
            if (probeInstallerOk)
                return qsTr("The remote installer answers, FTP does not — see the next step.")
            return qsTr("FTP answers, the remote installer does not — see the next step.")
        }
        if (probeState === "fail")
            return qsTr("No answer from %1. Check the IP and that the console is on.")
                .arg(enderecoField.text.trim())
        return qsTr("Type the address — it is checked on its own.")
    }

    function enderecoCompleto(texto) {
        var valor = (texto || "").trim()
        if (valor.length === 0)
            return false
        if (/^[0-9.]+$/.test(valor)) {
            var partes = valor.match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/)
            if (!partes)
                return false
            for (var i = 1; i <= 4; ++i) {
                if (parseInt(partes[i], 10) > 255)
                    return false
            }
            return true
        }
        return /^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(valor)
    }

    function agendarVerificacao() {
        if (!enderecoCompleto(enderecoField.text)) {
            probeState = "idle"
            probeTimer.stop()
            return
        }
        probeState = "checking"
        probeTimer.restart()
    }

    Timer {
        id: probeTimer
        interval: 600
        repeat: false
        onTriggered: app.probeConsole(enderecoField.text.trim(), 2121, 12800)
    }

    Connections {
        target: app
        function onConsoleProbed(address, ftpOk, installerOk, detail) {
            if (address !== enderecoField.text.trim())
                return
            wizard.probeFtpOk = ftpOk
            wizard.probeInstallerOk = installerOk
            wizard.probeState = (ftpOk && installerOk) ? "ok"
                : (ftpOk || installerOk) ? "partial" : "fail"
        }
    }

    function comecar() {
        passo = 0
        var valores = app.settingsMap()
        enderecoField.text = valores.consoleAddress
        nomeField.text = valores.consoleName
        accountWizardField.text = valores.streamAccountId
        probeState = "idle"
        open()
        agendarVerificacao()
    }

    function seguinte() {
        if (passo < passos - 1) {
            passo++
            return
        }
        guardar()
        close()
    }

    // Guardar fecha o assistente para sempre: a partir daqui abre-se pelo
    // botão nas definições.
    function guardar() {
        app.applySettings({
            "consoleAddress": enderecoField.text.trim(),
            "consoleName": nomeField.text.trim().length > 0 ? nomeField.text.trim() : "PS4",
            // Guarda-se convertido: a consola só aceita base64, e assim o
            // que fica gravado é directamente utilizável.
            "streamAccountId": accountWizardField.ok ? accountWizardField.base64
                                                     : accountWizardField.text.trim(),
            "firstRunDone": true
        })
    }

    // Um modal quase opaco: com a transparência dos painéis, o que está por
    // trás ver-se-ia através da caixa, e uma caixa que pede uma decisão não
    // pode ser uma janela.
    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radius
    }

    header: Rectangle {
        implicitHeight: Theme.dialogHeader
        color: "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 10
            Image {
                source: "qrc:/icons/mark.png"
                sourceSize.width: 26
                sourceSize.height: 26
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Welcome to OrbisLink")
                color: Theme.text
                font.pixelSize: 16
                font.bold: true
            }
            // Três pontos a dizer onde se vai.
            Row {
                spacing: 6
                Repeater {
                    model: wizard.passos
                    delegate: Rectangle {
                        width: 7
                        height: 7
                        radius: 4
                        color: index === wizard.passo ? Theme.accent : Theme.border
                    }
                }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
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
            spacing: 8
            StyledButton {
                text: qsTr("Skip")
                implicitHeight: 32
                onClicked: { wizard.guardar(); wizard.close() }
            }
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Back")
                implicitHeight: 32
                enabled: wizard.passo > 0
                onClicked: wizard.passo--
            }
            StyledButton {
                text: wizard.passo === wizard.passos - 1 ? qsTr("Start") : qsTr("Next")
                implicitHeight: 32
                larguraMinima: 120
                primary: true
                onClicked: wizard.seguinte()
            }
        }
    }

    contentItem: StackLayout {
        currentIndex: wizard.passo

        // ── 1. a consola
        ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: Theme.dialogInner
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: 13
                text: qsTr("First of all, where the console is.")
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: 12
                text: qsTr("The IP is on the console under Settings → Network → View Connection "
                           + "Status. It has to be on the same network as this PC.")
            }
            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Text { text: qsTr("IP address"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField {
                    id: enderecoField
                    Layout.fillWidth: true
                    placeholderText: "192.168.1.42"
                    onTextChanged: wizard.agendarVerificacao()
                }
                Text { text: qsTr("Name"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField {
                    id: nomeField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Living room PS4")
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.bottomMargin: Theme.dialogInner
                spacing: 8
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: wizard.probeColor
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: wizard.probeText
                    color: wizard.probeColor
                    font.pixelSize: 11
                }
            }
        }

        // ── 2. o que tem de estar ligado na consola
        ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: Theme.dialogInner
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: 13
                text: qsTr("On the console, three things:")
            }
            Repeater {
                model: [
                    qsTr("GoldHEN loaded — it is what brings the FTP server (port 2121)."),
                    qsTr("Remote Package Installer open, to install .pkg files from here."),
                    qsTr("Remote Play enabled under Settings → Remote Play Connection Settings.")
                ]
                delegate: RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    spacing: 10
                    Text {
                        text: (index + 1) + "."
                        color: Theme.accent
                        font.pixelSize: 13
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: modelData
                        color: Theme.textMuted
                        font.pixelSize: 12
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.bottomMargin: Theme.dialogInner
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: 11
                text: qsTr("None of this is required right now — the indicators in the top bar "
                           + "always say what is missing.")
            }
        }

        // ── 3. Remote Play
        ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: Theme.dialogInner
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: 13
                text: qsTr("For Remote Play, the console has to authorise this PC.")
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: 12
                text: qsTr("You need the PSN Account ID — the 64-bit number of the account that "
                           + "uses the console. Paste it however you have it: hexadecimal, decimal "
                           + "or base64. The conversion is done here. You can leave it blank and "
                           + "fill it in later.")
            }
            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.bottomMargin: Theme.dialogInner
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Text {
                    text: qsTr("Account ID (PSN)")
                    color: Theme.textMuted
                    font.pixelSize: 12
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 8
                }
                AccountIdField {
                    id: accountWizardField
                    Layout.fillWidth: true
                }
            }
        }
    }
}
