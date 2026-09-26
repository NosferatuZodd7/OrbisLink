// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Adding a console to the list: those answering on the network show up by
// themselves, and one that does not (another subnet, blocked discovery) is typed by hand.
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

    // A nearly opaque modal: with the panels' transparency, what is behind
    // would show through the box, and a box asking for a decision must not
    // be a window.
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
            text: qsTr("Add console")
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
                text: qsTr("Close")
                larguraMinima: 100
                onClicked: dialog.close()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 14

        Item { Layout.preferredHeight: Theme.dialogInner - 14 }

        // ── On the network
        RowLayout {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: qsTr("On the network")
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
                text: dialog.aProcurar ? qsTr("Searching…") : qsTr("Search again")
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
                  ? qsTr("This build has no Remote Play, so it can't search the network. Type "
                         + "the address below.")
                  : qsTr("No console answered. Check that it's on, on the same network, with "
                         + "Remote Play enabled — or type the address below.")
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
                                  + (modelData.state === "standby" ? qsTr("in rest mode")
                                                                   : qsTr("ready"))
                            color: Theme.textSecondary
                            font.pixelSize: 11
                        }
                    }
                    StyledButton {
                        text: linha.naLista ? qsTr("Already added") : qsTr("Add")
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

        // ── By hand
        Text {
            Layout.leftMargin: Theme.dialogMargin
            text: qsTr("By hand")
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
                placeholderText: qsTr("Name (optional)")
            }
            StyledField {
                id: enderecoField
                Layout.fillWidth: true
                placeholderText: qsTr("IP address, e.g. 192.168.1.50")
                onAccepted: if (text.trim().length > 0) dialog.adicionar(nomeField.text, text)
            }
            StyledButton {
                text: qsTr("Add")
                enabled: enderecoField.text.trim().length > 0
                larguraMinima: 100
                onClicked: dialog.adicionar(nomeField.text, enderecoField.text)
            }
        }
    }
}
