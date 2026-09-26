// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A janela do registo. Serve para duas coisas: ver ao vivo o que a
// aplicação está a fazer (sobretudo o Remote Play, que tem muitos passos e
// falha em qualquer um deles), e produzir um ficheiro que se possa anexar
// a uma mensagem sem ter de explicar nada.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: dialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 60 : 900, 900)
    height: Math.min(parent ? parent.height - 60 : 620, 620)
    modal: true
    padding: 0

    property bool followTail: true
    property string filtro: ""

    // Um modal quase opaco: com a transparência dos painéis, o que está por
    // trás ver-se-ia através da caixa, e uma caixa que pede uma decisão não
    // pode ser uma janela.
    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radius
    }

    function corDoNivel(nivel) {
        if (nivel === "ERRO" || nivel === "ERROR") return Theme.error
        if (nivel === "AVISO" || nivel === "WARNING") return Theme.warn
        if (nivel === "DEBUG") return Theme.textMuted
        return Theme.text
    }

    function carregar() {
        linhas.clear()
        var recentes = app.recentLog(1000)
        for (var i = 0; i < recentes.length; ++i)
            acrescentar(recentes[i])
        if (followTail)
            lista.positionViewAtEnd()
    }

    // As linhas vindas do registo já trazem "data [nível] texto".
    function acrescentar(linha) {
        var nivel = ""
        var abre = linha.indexOf("[")
        var fecha = linha.indexOf("]")
        if (abre > 0 && fecha > abre)
            nivel = linha.substring(abre + 1, fecha)
        linhas.append({ "texto": linha, "nivel": nivel })
        while (linhas.count > 2000)
            linhas.remove(0)
    }

    onOpened: carregar()

    ListModel { id: linhas }

    Connections {
        target: app
        function onLogLine(level, text) {
            if (!dialog.visible)
                return
            // Recompõe a linha como ela aparece no ficheiro.
            dialog.acrescentar(Qt.formatDateTime(new Date(), "yyyy-MM-dd hh:mm:ss.zzz")
                               + " [" + level + "] " + text)
            if (dialog.followTail)
                lista.positionViewAtEnd()
        }
    }

    header: Rectangle {
        implicitHeight: Theme.dialogHeader
        color: "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogInner
            spacing: 12
            Text {
                text: qsTr("Registo e diagnóstico")
                color: Theme.text
                font.pixelSize: 15
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            StyledCheck {
                text: qsTr("Detalhe do Remote Play")
                checked: app.streamVerbose()
                onCheckedChanged: app.setStreamVerbose(checked)
            }
            StyledCheck {
                text: qsTr("Acompanhar o fim")
                checked: dialog.followTail
                onCheckedChanged: dialog.followTail = checked
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
            Text {
                Layout.fillWidth: true
                text: qsTr("Ficheiro: %1").arg(app.logFilePath())
                color: Theme.textMuted
                font.pixelSize: 10
                elide: Text.ElideMiddle
            }
            StyledButton {
                text: qsTr("Copiar tudo")
                implicitHeight: 30
                onClicked: app.copyDiagnosticsToClipboard()
            }
            StyledButton {
                text: qsTr("Guardar em…")
                implicitHeight: 30
                onClicked: destinoDiagnostico.open()
            }
            StyledButton {
                text: qsTr("Guardar no ambiente de trabalho")
                implicitHeight: 30
                primary: true
                onClicked: {
                    var caminho = app.exportDiagnostics("")
                    if (caminho.length > 0)
                        app.openLocalFolder(caminho)
                }
            }
            StyledButton {
                text: qsTr("Fechar")
                implicitHeight: 30
                onClicked: dialog.close()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: Theme.dialogInner
            spacing: 8
            StyledField {
                Layout.fillWidth: true
                placeholderText: qsTr("filtrar (ex.: Remote Play, FALHOU, FTP)")
                onTextChanged: dialog.filtro = text
            }
            StyledButton {
                text: qsTr("Só erros")
                implicitHeight: 30
                onClicked: dialog.filtro = "ERRO"
            }
            StyledButton {
                text: qsTr("Limpar filtro")
                implicitHeight: 30
                onClicked: dialog.filtro = ""
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.bottomMargin: Theme.dialogInner
            color: "#0b0d12"
            border.color: Theme.border
            radius: 6

            ListView {
                id: lista
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: linhas
                spacing: 1
                ScrollBar.vertical: ScrollBar { }

                delegate: Text {
                    width: lista.width
                    visible: dialog.filtro.length === 0
                             || model.texto.toLowerCase().indexOf(dialog.filtro.toLowerCase()) >= 0
                    height: visible ? implicitHeight : 0
                    text: model.texto
                    color: dialog.corDoNivel(model.nivel)
                    font.family: "monospace"
                    font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
                }
            }

            Text {
                anchors.centerIn: parent
                visible: linhas.count === 0
                text: qsTr("Sem nada registado ainda.")
                color: Theme.textMuted
                font.pixelSize: 12
            }
        }
    }

    FolderDialog {
        id: destinoDiagnostico
        title: qsTr("Onde guardar o diagnóstico")
        onAccepted: {
            var caminho = app.exportDiagnostics(selectedFolder)
            if (caminho.length > 0)
                app.openLocalFolder(caminho)
        }
    }
}
