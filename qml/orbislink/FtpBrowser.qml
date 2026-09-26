// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    // Última pasta escolhida em "Transferir para…".
    property url lastDestination

    // Lista quando o separador aparece pela primeira vez.
    onVisibleChanged: if (visible && app.files.count === 0 && !app.ftpBusy) app.ftpRefresh()
    Component.onCompleted: if (visible && app.files.count === 0) app.ftpRefresh()

    // Só para as capturas de ecrã: abre o menu na primeira linha.
    Timer {
        running: typeof demoMenu !== "undefined" && demoMenu
        interval: 2200
        onTriggered: {
            var item = files.itemAtIndex(files.count - 1)
            if (!item)
                return
            rowMenu.popupFor(item.rowPath, item.rowName, item.rowIsDirectory, item.rowSize,
                item.rowSizeText, item)
            rowMenu.x = 120
            rowMenu.y = 240
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            StyledToolButton {
                text: "↑"
                implicitWidth: 30
                onClicked: app.ftpUp()
                enabled: !app.ftpBusy
            }
            TextField {
                id: pathField
                Layout.fillWidth: true
                text: app.ftpPath
                color: Theme.text
                font.pixelSize: 12
                selectByMouse: true
                background: Rectangle {
                    color: Theme.panelAltFill
                    border.color: Theme.border
                    radius: 6
                }
                onAccepted: app.ftpNavigate(text)
            }
            StyledToolButton {
                text: "⟳"
                implicitWidth: 30
                onClicked: app.ftpRefresh()
                enabled: !app.ftpBusy
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: app.ftpShortcuts
                delegate: StyledButton {
                    text: modelData
                    chip: true
                    onClicked: app.ftpNavigate(modelData)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.panelAltFill
            border.color: Theme.border

            ListView {
                id: files
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: app.files
                ScrollBar.vertical: ScrollBar { }

                delegate: ItemDelegate {
                    id: row
                    width: files.width
                    height: 32

                    // Preenchido assim que o ficheiro estiver na cache local:
                    // é o que permite arrastá-lo para fora da janela.
                    property string localUrl: model.isDirectory
                        ? "" : app.cachedFileUrl(model.path, model.size)
                    property bool preparing: false
                    // Expostos para o modo de demonstração das capturas.
                    readonly property string rowPath: model.path
                    readonly property string rowName: model.name
                    readonly property bool rowIsDirectory: model.isDirectory
                    readonly property real rowSize: model.size
                    readonly property string rowSizeText: model.sizeText

                    Connections {
                        target: app
                        function onDragFileReady(remotePath, localUrl) {
                            if (remotePath !== model.path)
                                return
                            row.localUrl = localUrl
                            row.preparing = false
                        }
                        function onDownloadChanged() {
                            if (row.preparing && !app.downloadActive)
                                row.preparing = false
                        }
                    }

                    // O arrastar para fora entrega um ficheiro local ao
                    // sistema; por isso o proxy só existe depois da cópia.
                    Item {
                        id: dragProxy
                        Drag.active: dragArea.drag.active
                        Drag.dragType: Drag.Automatic
                        Drag.supportedActions: Qt.CopyAction
                        Drag.mimeData: ({ "text/uri-list": row.localUrl })
                        // Sem imagem, o cursor arrasta um nada invisível.
                        Drag.imageSource: "qrc:/icons/logo.png"
                    }

                    contentItem: RowLayout {
                        spacing: 8

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            RowLayout {
                                anchors.fill: parent
                                spacing: 8
                                Text {
                                    text: model.isDirectory ? "📁" : (row.localUrl.length > 0 ? "📥" : "📄")
                                    font.pixelSize: 13
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: model.name
                                    color: Theme.text
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                id: dragArea
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                cursorShape: row.localUrl.length > 0 ? Qt.OpenHandCursor : Qt.ArrowCursor
                                drag.target: row.localUrl.length > 0 ? dragProxy : null
                                drag.threshold: 10
                                // Senão a lista rouba o gesto e trata-o como
                                // rolar em vez de arrastar o ficheiro.
                                preventStealing: row.localUrl.length > 0

                                onClicked: (mouse) => {
                                    if (mouse.button === Qt.RightButton)
                                        rowMenu.popupFor(model.path, model.name, model.isDirectory, model.size, model.sizeText, row)
                                }
                                onDoubleClicked: {
                                    if (model.isDirectory)
                                        app.ftpNavigate(model.path)
                                    else
                                        app.ftpDownload(model.path, model.name, "")
                                }
                                onPressAndHold: rowMenu.popupFor(model.path, model.name, model.isDirectory, model.size, model.sizeText, row)
                                // Arrastar um ficheiro que ainda não está no PC
                                // começa por o trazer; quando chegar, arrasta-se.
                                onPositionChanged: {
                                    if (!pressed || model.isDirectory || row.localUrl.length > 0)
                                        return
                                    if (row.preparing || app.downloadActive)
                                        return
                                    row.preparing = true
                                    app.ftpPrepareForDrag(model.path, model.name, model.size)
                                }
                            }
                        }

                        Text {
                            text: row.preparing ? qsTr("a preparar…") : model.sizeText
                            color: row.preparing ? Theme.accent : Theme.textMuted
                            font.pixelSize: 11
                        }
                        StyledToolButton {
                            text: "⋮"
                            implicitWidth: 22; implicitHeight: 22
                            onClicked: rowMenu.popupFor(model.path, model.name, model.isDirectory, model.size, model.sizeText, row)
                        }
                        StyledToolButton {
                            text: "✕"
                            danger: true
                            implicitWidth: 24; implicitHeight: 22
                            onClicked: confirmDelete.open(model.path, model.isDirectory, model.name)
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: files.count === 0 && !app.ftpBusy
                    text: qsTr("Sem ficheiros (ou FTP indisponível)")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    running: app.ftpBusy
                    visible: app.ftpBusy
                }
            }
        }

        // Transferência em curso (da consola para o PC).
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: app.downloadActive

            Text {
                text: qsTr("A transferir %1").arg(app.downloadName)
                color: Theme.textMuted
                font.pixelSize: 11
                elide: Text.ElideMiddle
                Layout.maximumWidth: 200
            }
            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 1
                value: app.downloadProgress
            }
            Text {
                text: Math.round(app.downloadProgress * 100) + "%"
                color: Theme.text
                font.pixelSize: 11
            }
            StyledButton {
                text: qsTr("Cancelar")
                implicitHeight: 24
                font.pixelSize: 10
                onClicked: app.cancelDownload()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: newFolderName
                Layout.fillWidth: true
                placeholderText: qsTr("nova pasta")
                color: Theme.text
                font.pixelSize: 12
                background: Rectangle {
                    color: Theme.panelAltFill
                    border.color: Theme.border
                    radius: 6
                }
            }
            StyledButton {
                text: qsTr("Criar")
                enabled: newFolderName.text.length > 0 && !app.ftpBusy
                onClicked: { app.ftpMakeDirectory(newFolderName.text); newFolderName.text = "" }
            }
        }
    }

    // O que se pode fazer com o que está por baixo do cursor.
    Menu {
        id: rowMenu
        property string targetPath: ""
        property string targetName: ""
        property bool targetIsDirectory: false
        property real targetSize: 0
        property string targetSizeText: ""
        property var targetRow: null

        function popupFor(path, name, isDirectory, size, sizeText, rowItem) {
            targetPath = path
            targetName = name
            targetIsDirectory = isDirectory
            targetSize = size
            targetSizeText = sizeText
            targetRow = rowItem
            popup()
        }

        // Folga em cima e em baixo para que o realce da primeira e da
        // última linha nunca chegue aos cantos arredondados.
        topPadding: 8
        bottomPadding: 8

        background: Rectangle {
            implicitWidth: 320
            color: Theme.dialogFill
            border.color: Theme.glassEdge
            border.width: 1
            radius: Theme.radiusSmall
        }

        // Cabeçalho com o nome e o tamanho, para não haver dúvidas sobre
        // em que ficheiro o menu está a agir.
        Rectangle {
            implicitHeight: 50
            implicitWidth: rowMenu.width
            color: "transparent"
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 6
                anchors.bottomMargin: 6
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    text: rowMenu.targetName
                    color: Theme.text
                    font.pixelSize: 11
                    font.bold: true
                    elide: Text.ElideMiddle
                }
                Text {
                    text: rowMenu.targetIsDirectory ? qsTr("pasta") : rowMenu.targetSizeText
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
            }
        }

        MenuSeparator {
            // A linha também respira: sem margens iria de aresta a aresta
            // e cortaria o menu ao meio.
            padding: 6
            leftPadding: 16
            rightPadding: 16
            contentItem: Rectangle { implicitHeight: 1; color: Theme.glassEdge }
        }

        StyledMenuItem {
            text: qsTr("Abrir")
            visible: rowMenu.targetIsDirectory
            height: visible ? implicitHeight : 0
            onTriggered: app.ftpNavigate(rowMenu.targetPath)
        }
        StyledMenuItem {
            text: qsTr("Usar como pasta de envio")
            visible: rowMenu.targetIsDirectory
            height: visible ? implicitHeight : 0
            onTriggered: app.setFtpUploadDirectory(rowMenu.targetPath)
        }
        StyledMenuItem {
            text: qsTr("Transferir para o ambiente de trabalho")
            visible: !rowMenu.targetIsDirectory
            height: visible ? implicitHeight : 0
            enabled: !app.downloadActive
            onTriggered: app.ftpDownload(rowMenu.targetPath, rowMenu.targetName, "")
        }
        StyledMenuItem {
            text: qsTr("Transferir para…")
            visible: !rowMenu.targetIsDirectory
            height: visible ? implicitHeight : 0
            enabled: !app.downloadActive
            onTriggered: destinationDialog.open()
        }
        StyledMenuItem {
            text: qsTr("Preparar para arrastar")
            visible: !rowMenu.targetIsDirectory && (rowMenu.targetRow ? rowMenu.targetRow.localUrl.length === 0 : false)
            height: visible ? implicitHeight : 0
            enabled: !app.downloadActive
            onTriggered: {
                if (rowMenu.targetRow)
                    rowMenu.targetRow.preparing = true
                app.ftpPrepareForDrag(rowMenu.targetPath, rowMenu.targetName, rowMenu.targetSize)
            }
        }
        StyledMenuItem {
            text: qsTr("Mostrar a cópia local")
            visible: rowMenu.targetRow ? rowMenu.targetRow.localUrl.length > 0 : false
            height: visible ? implicitHeight : 0
            onTriggered: app.openLocalFolder(rowMenu.targetRow.localUrl)
        }

        MenuSeparator {
            // A linha também respira: sem margens iria de aresta a aresta
            // e cortaria o menu ao meio.
            padding: 6
            leftPadding: 16
            rightPadding: 16
            contentItem: Rectangle { implicitHeight: 1; color: Theme.glassEdge }
        }

        StyledMenuItem {
            text: qsTr("Copiar o caminho")
            onTriggered: app.copyToClipboard(rowMenu.targetPath)
        }
        StyledMenuItem {
            text: qsTr("Mudar o nome…")
            onTriggered: renameDialog.open(rowMenu.targetPath, rowMenu.targetName)
        }
        StyledMenuItem {
            text: qsTr("Apagar na consola")
            onTriggered: confirmDelete.open(rowMenu.targetPath, rowMenu.targetIsDirectory,
                rowMenu.targetName)
        }
    }

    FolderDialog {
        id: destinationDialog
        title: qsTr("Onde guardar")
        currentFolder: root.lastDestination.length > 0
            ? root.lastDestination
            : "file://" + app.defaultDownloadDirectory()
        onAccepted: {
            root.lastDestination = selectedFolder
            app.ftpDownload(rowMenu.targetPath, rowMenu.targetName, selectedFolder)
        }
    }

    Dialog {
        id: renameDialog
        property string targetPath: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 380
        modal: true
        title: qsTr("Mudar o nome")
        standardButtons: Dialog.Ok | Dialog.Cancel

        function open(path, name) {
            targetPath = path
            nameInput.text = name
            visible = true
            nameInput.forceActiveFocus()
            nameInput.selectAll()
        }

        contentItem: TextField {
            id: nameInput
            color: Theme.text
            font.pixelSize: 12
            selectByMouse: true
            background: Rectangle {
                color: Theme.panelAltFill
                border.color: nameInput.activeFocus ? Theme.accent : Theme.border
                radius: 6
            }
        }

        onAccepted: app.ftpRename(targetPath, nameInput.text)
    }

    // Operações destrutivas exigem confirmação (§5.5).
    Dialog {
        id: confirmDelete
        property string targetPath: ""
        property bool targetIsDirectory: false
        property string targetName: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        // Largura fixa: sem isto o diálogo mede-se pelo texto e o texto
        // mede-se pelo diálogo, e o Qt avisa do ciclo.
        width: 400
        modal: true
        padding: 0

        function open(path, isDirectory, name) {
            targetPath = path
            targetIsDirectory = isDirectory
            targetName = name
            visible = true
        }

        // Sem isto o diálogo usaria o estilo Basic: fundo branco com o texto
        // do tema por cima — no tema escuro, claro sobre branco, ilegível.
        Overlay.modal: Rectangle { color: Theme.scrim }

        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radius
        }

        header: Rectangle {
            implicitHeight: 48
            color: "transparent"
            Text {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                verticalAlignment: Text.AlignVCenter
                text: confirmDelete.targetIsDirectory ? qsTr("Apagar a pasta na consola?")
                                                      : qsTr("Apagar na consola?")
                color: Theme.text
                font.pixelSize: 15
                font.bold: true
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
        }

        contentItem: Text {
            leftPadding: 18
            rightPadding: 18
            topPadding: 14
            bottomPadding: 14
            text: confirmDelete.targetIsDirectory
                ? qsTr("Apagar \"%1\" e tudo o que está lá dentro? Não há como desfazer.")
                    .arg(confirmDelete.targetName)
                : qsTr("Apagar \"%1\"? Não há como desfazer.").arg(confirmDelete.targetName)
            color: Theme.text
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        footer: Rectangle {
            implicitHeight: 56
            color: "transparent"
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancelar")
                    onClicked: confirmDelete.close()
                }
                StyledButton {
                    text: qsTr("Apagar")
                    danger: true
                    larguraMinima: 110
                    onClicked: {
                        app.ftpDelete(confirmDelete.targetPath, confirmDelete.targetIsDirectory)
                        confirmDelete.close()
                    }
                }
            }
        }
    }
}
