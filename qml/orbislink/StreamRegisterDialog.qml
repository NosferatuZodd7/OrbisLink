// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Registar este PC na consola. Sem isto não há Remote Play: a consola só
// aceita sessões de dispositivos que ela própria autorizou.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: dialog

    // Ver a nota em StreamArea.qml: ao fechar a janela o controlador some
    // antes das bindings.
    readonly property bool ready: typeof stream !== "undefined" && stream !== null
    readonly property bool busy: ready && stream.registering
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 610
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
        pinField.text = ""
        pinField.forceActiveFocus()
    }

    header: Rectangle {
        implicitHeight: Theme.dialogHeader
        color: "transparent"
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.dialogMargin
            text: qsTr("Registar este PC na consola")
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
            spacing: 10
            BusyIndicator {
                running: dialog.busy
                visible: dialog.busy
                implicitWidth: 22
                implicitHeight: 22
            }
            Text {
                visible: dialog.busy
                text: qsTr("A falar com a consola…")
                color: Theme.textMuted
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Fechar")
                larguraMinima: 100
                onClicked: dialog.close()
            }
            StyledButton {
                text: qsTr("Registar")
                larguraMinima: 110
                // Só activo quando o Account ID já dá um valor válido: um
                // botão que se pode carregar e falha na consola é pior do
                // que um botão apagado.
                enabled: pinField.text.length >= 8 && accountField.ok
                         && dialog.ready && !dialog.busy
                primary: true
                onClicked: if (dialog.ready) stream.registerConsole(pinField.text, accountField.base64)
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        // Passo a passo, porque isto falha sempre pelo mesmo: PIN expirado
        // ou Account ID trocado.
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: Theme.dialogInner
            color: Theme.panelAltFill
            border.color: Theme.border
            radius: 8
            implicitHeight: passos.implicitHeight + 28

            ColumnLayout {
                id: passos
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.dialogInner
                anchors.rightMargin: Theme.dialogInner
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: 12
                    text: qsTr("Na consola: Definições → Definições de Ligação do Remote Play "
                               + "→ Adicionar Dispositivo.")
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: 11
                    text: qsTr("O PIN de 8 dígitos que aparece dura poucos minutos. Se falhar, "
                               + "pede outro na consola.")
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            columns: 2
            columnSpacing: 12
            rowSpacing: 8

            Text { text: qsTr("PIN"); color: Theme.textMuted; font.pixelSize: 12 }
            StyledField {
                id: pinField
                Layout.fillWidth: true
                placeholderText: "12345678"
                maximumLength: 8
                inputMethodHints: Qt.ImhDigitsOnly
                validator: RegularExpressionValidator { regularExpression: /[0-9]{0,8}/ }
            }

            Text {
                text: qsTr("Account ID (PSN)")
                color: Theme.textMuted
                font.pixelSize: 12
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 8
            }
            AccountIdField {
                id: accountField
                Layout.fillWidth: true
                // Vem preenchido com o último que a consola aceitou. O PIN
                // muda de cada vez; o Account ID não.
                //
                // O guarda não é por preciosismo: num build sem Remote Play
                // o "stream" não existe e a ligação rebentaria com
                // "Cannot read property of null".
                text: (typeof stream !== "undefined" && stream) ? stream.savedAccountId : ""
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
            text: qsTr("O Account ID é um número de 64 bits da conta PSN que usa a consola "
                       + "— não é o nome de utilizador. Cola-o como o tiveres: em "
                       + "hexadecimal, em decimal ou já em base64. A consola só aceita a "
                       + "forma em base64, e essa conversão passa a ser feita aqui.")
        }
    }
}
