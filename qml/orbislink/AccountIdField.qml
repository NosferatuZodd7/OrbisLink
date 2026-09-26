// SPDX-License-Identifier: AGPL-3.0-or-later
//
// O campo do Account ID da PSN, com a conversão à vista.
//
// Aceita-se o valor na forma em que aparecer (hexadecimal, decimal ou
// base64), e mostram-se as outras duas por baixo: quem tem o ID à frente
// confirma de relance que é o mesmo número, em vez de ter de acreditar.
//
// A ordem dos bytes tem um interruptor, e ele NÃO toca no que está
// escrito: lê o mesmo texto de outra maneira, e desligá-lo põe tudo como
// estava. Um controlo que reescrevesse o campo deixaria quem lhe tocasse
// para ver o que fazia com outro Account ID e sem o seu.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root

    // O que a consola precisa. Vazio enquanto o que está escrito não der um
    // Account ID válido.
    readonly property string base64: formas.valid ? formas.base64 : ""
    readonly property bool ok: formas.valid === true
    property alias text: campo.text
    property var formas: ({ valid: false })

    spacing: 8

    function reler() {
        if (typeof stream === "undefined" || !stream) {
            formas = ({ valid: false })
            return
        }
        formas = inverso.checked ? stream.accountIdReversed(campo.text)
                                 : stream.accountIdForms(campo.text)
    }

    StyledField {
        id: campo
        Layout.fillWidth: true
        placeholderText: qsTr("hexadecimal, decimal ou base64")
        onTextChanged: {
            // Um ID novo começa sempre pela leitura normal: manter o
            // interruptor ligado de uma tentativa anterior seria uma
            // armadilha silenciosa.
            inverso.checked = false
            root.reler()
        }
    }

    // A caixa de conversão. Só aparece depois de haver alguma coisa escrita:
    // um painel de ajuda permanentemente vazio é ruído.
    Rectangle {
        Layout.fillWidth: true
        visible: campo.text.trim().length > 0
        color: Theme.panelAltFill
        border.color: root.ok ? Theme.glassEdge
                              : Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.5)
        border.width: 1
        radius: Theme.radiusSmall
        implicitHeight: conteudo.implicitHeight + 24

        Behavior on border.color { ColorAnimation { duration: Theme.fast } }

        ColumnLayout {
            id: conteudo
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 7

            // Quando não dá, diz-se porquê em vez de deixar a caixa vazia.
            Text {
                Layout.fillWidth: true
                visible: !root.ok
                wrapMode: Text.WordWrap
                text: root.formas.error ? root.formas.error : ""
                color: Theme.error
                font.pixelSize: 11
            }

            Text {
                Layout.fillWidth: true
                visible: root.ok
                text: {
                    if (root.formas.format === "base64") return qsTr("Lido como base64")
                    if (root.formas.format === "decimal") return qsTr("Lido como decimal")
                    return qsTr("Lido como hexadecimal")
                }
                color: Theme.textSecondary
                font.pixelSize: 11
            }

            Repeater {
                model: root.ok ? [
                    { etiqueta: qsTr("Base64 — o que a consola recebe"), valor: root.formas.base64, destaque: true },
                    { etiqueta: qsTr("Hexadecimal"), valor: root.formas.hex, destaque: false },
                    { etiqueta: qsTr("Decimal"), valor: root.formas.decimal, destaque: false }
                ] : []

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.preferredWidth: 180
                        text: modelData.etiqueta
                        color: Theme.textSecondary
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    // Seleccionável: o valor convertido só serve se puder
                    // ser copiado para outro sítio.
                    TextInput {
                        Layout.fillWidth: true
                        text: modelData.valor
                        readOnly: true
                        selectByMouse: true
                        color: modelData.destaque ? Theme.accent : Theme.text
                        font.pixelSize: 12
                        font.family: "monospace"
                        font.bold: modelData.destaque
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 3
                visible: root.ok
                height: 1
                color: Theme.glassEdge
            }

            // A escotilha de emergência, e só isso: há ferramentas que
            // mostram os oito bytes em bruto em vez do número, e aí o
            // hexadecimal aparece pela ordem contrária.
            StyledCheck {
                id: inverso
                visible: root.ok
                text: qsTr("Os bytes estão pela ordem contrária")
                labelColor: Theme.textSecondary
                font.pixelSize: 11
                onCheckedChanged: root.reler()
            }

            Text {
                Layout.fillWidth: true
                visible: root.ok
                wrapMode: Text.WordWrap
                text: inverso.checked
                    ? qsTr("A ler o ID ao contrário. Desliga isto se as linhas acima "
                           + "deixaram de bater certo com o que a consola mostra.")
                    : qsTr("Só liga isto se as linhas acima não baterem certo com o que "
                           + "viste na consola. O que escreveste não é alterado.")
                color: inverso.checked ? Theme.warn : Theme.textSecondary
                font.pixelSize: 10
            }
        }
    }
}
