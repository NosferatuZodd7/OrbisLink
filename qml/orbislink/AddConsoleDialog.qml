// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Juntar uma consola à lista: as que respondem na rede aparecem sozinhas, e
// uma que não apareça (outra sub-rede, descoberta bloqueada) escreve-se à mão.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: dialog

    readonly property bool temStream: typeof stream !== "undefined" && stream !== null
    readonly property bool aProcurar: temStream && stream.scanning
    readonly property var encontradas: temStream ? stream.scanResults : []

    function jaNaLista(endereco) {
        var lista = app.consoles
        for (var i = 0; i < lista.length; ++i)
            if (lista[i].address === endereco)
                return true
        return false
    }

    function adicionar(nome, endereco, tipo) {
        app.addConsole(nome, endereco, tipo || "")
        dialog.close()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 560
    modal: true
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

    onOpened: {
        nomeField.text = ""
        enderecoField.text = ""
        if (temStream)
            stream.scanNetwork()
    }

    header: Rectangle {
        implicitHeight: Theme.dialogHeader
        color: "transparent"
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.dialogMargin
            text: qsTr("Adicionar consola")
            color: Theme.text
            font.pixelSize: 15
            font.bold: true
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
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Fechar")
                larguraMinima: 100
                onClicked: dialog.close()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 14

        Item { Layout.preferredHeight: Theme.dialogInner - 14 }

        // ── Na rede
        RowLayout {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: qsTr("Na rede")
                color: Theme.accent
                font.bold: true
                font.pixelSize: 12
            }
            BusyIndicator {
                running: dialog.aProcurar
                visible: dialog.aProcurar
                implicitWidth: 20
                implicitHeight: 20
            }
            StyledButton {
                visible: dialog.temStream
                enabled: !dialog.aProcurar
                text: dialog.aProcurar ? qsTr("A procurar…") : qsTr("Procurar outra vez")
                implicitHeight: 30
                font.pixelSize: 11
                onClicked: stream.scanNetwork()
            }
        }

        Text {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.fillWidth: true
            visible: !dialog.aProcurar && dialog.encontradas.length === 0
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: 12
            text: !dialog.temStream
                  ? qsTr("Esta versão não tem Remote Play, por isso não procura na rede. "
                         + "Escreve o endereço em baixo.")
                  : qsTr("Nenhuma consola respondeu. Confirma que está ligada, na mesma rede, "
                         + "com o Remote Play activado — ou escreve o endereço em baixo.")
        }

        Repeater {
            model: dialog.encontradas

            Rectangle {
                id: linha
                readonly property bool naLista: dialog.jaNaLista(modelData.address)
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.fillWidth: true
                implicitHeight: 52
                radius: Theme.radiusSmall
                color: Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b,
                               Theme.claro ? 1.0 : 0.6)
                border.color: Theme.claro ? Qt.rgba(0, 0, 0, 0.12) : Theme.glassEdge

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 10
                    spacing: 12
                    Text {
                        text: modelData.ps5 ? "PS5" : "PS4"
                        color: Theme.text
                        font.pixelSize: 15
                        font.bold: true
                        font.letterSpacing: 1
                    }
                    Column {
                        Layout.fillWidth: true
                        Text {
                            text: modelData.name.length > 0 ? modelData.name : modelData.address
                            color: Theme.text
                            font.pixelSize: 13
                        }
                        Text {
                            text: modelData.address + "  ·  "
                                  + (modelData.state === "standby" ? qsTr("em repouso")
                                                                   : qsTr("pronta"))
                            color: Theme.textSecondary
                            font.pixelSize: 11
                        }
                    }
                    StyledButton {
                        text: linha.naLista ? qsTr("Já está") : qsTr("Adicionar")
                        enabled: !linha.naLista
                        primary: !linha.naLista
                        implicitHeight: 32
                        larguraMinima: 100
                        onClicked: dialog.adicionar(modelData.name, modelData.address,
                                                    modelData.ps5 ? "ps5" : "ps4")
                    }
                }
            }
        }

        Rectangle {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.fillWidth: true
            Layout.topMargin: 6
            height: 1
            color: Theme.border
        }

        // ── À mão
        Text {
            Layout.leftMargin: Theme.dialogMargin
            text: qsTr("À mão")
            color: Theme.accent
            font.bold: true
            font.pixelSize: 12
        }

        RowLayout {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.bottomMargin: Theme.dialogInner
            spacing: 10
            StyledField {
                id: nomeField
                Layout.preferredWidth: 150
                placeholderText: qsTr("Nome (opcional)")
            }
            StyledField {
                id: enderecoField
                Layout.fillWidth: true
                placeholderText: qsTr("Endereço IP, ex.: 192.168.1.50")
                onAccepted: if (text.trim().length > 0) dialog.adicionar(nomeField.text, text)
            }
            StyledButton {
                text: qsTr("Adicionar")
                enabled: enderecoField.text.trim().length > 0
                larguraMinima: 100
                onClicked: dialog.adicionar(nomeField.text, enderecoField.text)
            }
        }
    }
}
