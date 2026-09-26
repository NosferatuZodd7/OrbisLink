// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Sobreposição de arrastar e largar (§5.7): duas zonas, uma por modo.
// O stream continua a correr por baixo — nada é pausado.
//
// Não há aqui nenhum DropArea. Um DropArea por zona não serve: o da janela
// fica com o arrasto agarrado e nunca larga, e as zonas nem chegam a saber
// que o cursor está em cima delas. Há um único DropArea, na janela, que diz
// onde está o cursor; a zona apontada calcula-se daqui.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    property bool active: false

    // Posição do cursor dentro da janela, em coordenadas da janela.
    property real pointerX: -1
    property real pointerY: -1

    // 0 = instalação direta, 1 = envio por FTP, -1 = fora das duas.
    readonly property int zoneUnderPointer: {
        if (!active || pointerX < 0)
            return -1
        if (containsPoint(installZone))
            return 0
        if (containsPoint(ftpZone))
            return 1
        return -1
    }

    function containsPoint(item) {
        var p = item.mapFromItem(null, pointerX, pointerY)
        return p.x >= 0 && p.y >= 0 && p.x <= item.width && p.y <= item.height
    }

    function show(x, y) {
        pointerX = x === undefined ? pointerX : x
        pointerY = y === undefined ? pointerY : y
        active = true
    }

    function movePointer(x, y) {
        pointerX = x
        pointerY = y
    }

    function hide() {
        active = false
        pointerX = -1
        pointerY = -1
    }

    // Chamado pelo DropArea da janela: devolve true se o ficheiro foi aceite.
    function dropAt(x, y, urls) {
        movePointer(x, y)
        var zona = zoneUnderPointer
        hide()
        if (zona === 0 && app.canInstallDirectly) {
            app.dropUrls(urls, 0)
            return true
        }
        if (zona === 1 && app.canUseFtp) {
            app.dropUrls(urls, 1)
            return true
        }
        return false
    }

    anchors.fill: parent
    anchors.topMargin: 56
    anchors.bottomMargin: 26
    visible: opacity > 0
    opacity: active ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 120 } }

    Rectangle {
        anchors.fill: parent
        color: "#0b0d12"
        opacity: 0.88
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Larga para…")
            color: Theme.onStage
            font.pixelSize: 22
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 18

            DropZone {
                id: installZone
                objectName: "installZone"
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: qsTr("Instalar diretamente")
                subtitle: qsTr("A consola descarrega do PC e instala")
                glyph: "⤓"
                enabledZone: app.canInstallDirectly
                disabledReason: app.installerHint
                highlighted: root.zoneUnderPointer === 0
            }

            DropZone {
                id: ftpZone
                objectName: "ftpZone"
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: qsTr("Enviar por FTP")
                subtitle: qsTr("Copia para %1").arg(app.ftpPath)
                glyph: "⇪"
                enabledZone: app.canUseFtp
                disabledReason: app.ftpHint
                highlighted: root.zoneUnderPointer === 1
            }
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Aceita vários ficheiros e pastas (procura .pkg lá dentro)")
            color: Theme.onStageMuted
            font.pixelSize: 12
        }
    }
}
