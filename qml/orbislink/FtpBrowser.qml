// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    // Last folder chosen in "Download to…".
    // The uploads going into the folder on screen: they show in the list
    // right away, faded, with a thin progress bar, and become normal rows
    // once they arrive (the list refreshes itself then).
    readonly property string shownDirectory: {
        var path = app.ftpPath
        while (path.length > 1 && path.charAt(path.length - 1) === "/")
            path = path.substring(0, path.length - 1)
        return path
    }
    readonly property var uploadsHere: app.ftpUploads.filter(function (upload) {
        return upload.directory === root.shownDirectory
    })
    function beingUploaded(name) {
        for (var i = 0; i < uploadsHere.length; ++i)
            if (uploadsHere[i].name === name)
                return true
        return false
    }

    // A file or a whole folder to the PC (empty destination: the desktop).
    function download(path, name, isDirectory, destination) {
        if (isDirectory)
            app.ftpDownloadFolder(path, name, destination)
        else
            app.ftpDownload(path, name, destination)
    }

    property url lastDestination

    // Lists when the tab appears for the first time.
    onVisibleChanged: if (visible && app.files.count === 0 && !app.ftpBusy) app.ftpRefresh()
    Component.onCompleted: if (visible && app.files.count === 0) app.ftpRefresh()

    // Screenshots only: opens the menu on the first row.
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
                iconName: "arrow-up"
                iconSize: 16
                implicitWidth: 32
                implicitHeight: 32
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
                    color: Theme.controlFill
                    border.color: pathField.activeFocus ? Theme.accent : Theme.border
                    radius: 10
                }
                onAccepted: app.ftpNavigate(text)
            }
            StyledToolButton {
                iconName: "refresh"
                iconSize: 16
                implicitWidth: 32
                implicitHeight: 32
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

                header: Column {
                    width: files.width
                    Repeater {
                        model: root.uploadsHere
                        Item {
                            required property var modelData
                            width: files.width
                            height: 40
                            // Faded until it has arrived.
                            opacity: 0.55

                            Icon {
                                id: uploadGlyph
                                x: 12
                                y: 8
                                name: "upload"
                                size: 15
                                color: Theme.accent
                            }
                            Text {
                                anchors.left: uploadGlyph.right
                                anchors.leftMargin: 8
                                anchors.right: uploadPercent.left
                                anchors.rightMargin: 8
                                y: 7
                                text: modelData.name
                                color: Theme.text
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                            }
                            Text {
                                id: uploadPercent
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                y: 8
                                text: modelData.sending ? Math.floor(modelData.percent) + "%" : qsTr("queued")
                                color: Theme.textSecondary
                                font.pixelSize: 11
                            }
                            // The thin bar under the name.
                            Rectangle {
                                x: 35
                                y: 28
                                width: parent.width - 47
                                height: 3
                                radius: 1.5
                                color: Theme.controlFill
                                Rectangle {
                                    width: parent.width * Math.max(0, Math.min(1, modelData.percent / 100))
                                    height: parent.height
                                    radius: parent.radius
                                    color: Theme.accent
                                    Behavior on width { NumberAnimation { duration: 200 } }
                                }
                            }
                        }
                    }
                }

                delegate: ItemDelegate {
                    id: row
                    width: files.width
                    // A file being replaced shows only once, as the upload above.
                    readonly property bool replaced: root.beingUploaded(model.name)
                    visible: !replaced
                    height: replaced ? 0 : 34
                    // The Basic style pads 12px all round, which in a 34px row
                    // squeezes the content into a sliver and leaves it off the
                    // lit box. The row sets its own.
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 12
                    rightPadding: 6

                    // The row under the mouse lights up; folders open with a
                    // double click, so they get the hand.
                    background: Rectangle {
                        radius: 8
                        color: row.hovered ? Theme.controlHover : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                    }
                    HoverHandler {
                        cursorShape: model.isDirectory ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }

                    // Filled in as soon as the file is in the local cache:
                    // that is what allows dragging it out of the window.
                    property string localUrl: model.isDirectory
                        ? "" : app.cachedFileUrl(model.path, model.size)
                    property bool preparing: false
                    // Exposed for the screenshots' demo mode.
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
                            // Still holding the button: the drag goes on.
                            if (dragArea.pressed && dragArea.dragged)
                                app.startFtpDrag(model.path, localUrl)
                        }
                        function onDownloadChanged() {
                            if (row.preparing && !app.downloadActive)
                                row.preparing = false
                        }
                    }

                    contentItem: RowLayout {
                        spacing: 8

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            RowLayout {
                                anchors.fill: parent
                                spacing: 8
                                Icon {
                                    name: model.isDirectory ? "folder" : (row.localUrl.length > 0 ? "download" : "file")
                                    size: 15
                                    color: model.isDirectory ? Theme.accent : Theme.textSecondary
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
                                cursorShape: model.isDirectory ? Qt.PointingHandCursor
                                           : row.localUrl.length > 0 ? Qt.OpenHandCursor : Qt.ArrowCursor
                                // Otherwise the list steals the gesture and treats
                                // it as scrolling instead of dragging the file.
                                preventStealing: true
                                property point pressedAt
                                property bool dragged: false

                                onPressed: (mouse) => {
                                    pressedAt = Qt.point(mouse.x, mouse.y)
                                    dragged = false
                                }
                                // One click opens a folder; on a file it shows
                                // what can be done with it.
                                onClicked: (mouse) => {
                                    if (dragged)
                                        return
                                    if (mouse.button === Qt.LeftButton && model.isDirectory)
                                        app.ftpNavigate(model.path)
                                    else
                                        rowMenu.popupFor(model.path, model.name, model.isDirectory, model.size, model.sizeText, row)
                                }
                                onPressAndHold: {
                                    if (!dragged)
                                        rowMenu.popupFor(model.path, model.name, model.isDirectory, model.size, model.sizeText, row)
                                }
                                // Dragging: onto a folder of the list it moves
                                // there; out of the window it needs the file on
                                // the PC, so a small file not fetched yet is
                                // fetched first and the drag goes on when it
                                // arrives. A big one is dragged at once, for
                                // the folders of the list ("Get it ready to
                                // drag" brings it to the PC first).
                                onPositionChanged: (mouse) => {
                                    if (!pressed || dragged || !(mouse.buttons & Qt.LeftButton))
                                        return
                                    if (Math.abs(mouse.x - pressedAt.x) + Math.abs(mouse.y - pressedAt.y) < 10)
                                        return
                                    dragged = true
                                    if (model.isDirectory || row.localUrl.length > 0
                                            || model.size > 64 * 1024 * 1024) {
                                        app.startFtpDrag(model.path, row.localUrl)
                                        return
                                    }
                                    if (row.preparing || app.downloadActive)
                                        return
                                    row.preparing = true
                                    app.ftpPrepareForDrag(model.path, model.name, model.size)
                                }
                            }

                            // A folder takes what is dropped on it from the list.
                            DropArea {
                                anchors.fill: parent
                                enabled: model.isDirectory
                                keys: ["application/x-orbislink-ftp-path"]
                                onEntered: (drag) => {
                                    const from = drag.getDataAsString("application/x-orbislink-ftp-path")
                                    if (from === model.path)
                                        drag.accepted = false
                                }
                                onDropped: (drop) => {
                                    const from = drop.getDataAsString("application/x-orbislink-ftp-path")
                                    if (from.length > 0 && from !== model.path) {
                                        app.ftpMove(from, model.path)
                                        drop.accept(Qt.MoveAction)
                                    }
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    radius: 8
                                    visible: parent.containsDrag
                                    color: Theme.alpha(Theme.accent, 0.12)
                                    border.width: 1
                                    border.color: Theme.accent
                                }
                            }
                        }

                        Text {
                            text: row.preparing ? qsTr("getting it…") : model.sizeText
                            color: row.preparing ? Theme.accent : Theme.textMuted
                            font.pixelSize: 11
                        }
                        StyledToolButton {
                            iconName: "more"
                            iconSize: 14
                            implicitWidth: 26; implicitHeight: 26
                            onClicked: rowMenu.popupFor(model.path, model.name, model.isDirectory, model.size, model.sizeText, row)
                        }
                        StyledToolButton {
                            iconName: "trash"
                            iconSize: 14
                            danger: true
                            implicitWidth: 26; implicitHeight: 26
                            onClicked: confirmDelete.open(model.path, model.isDirectory, model.name)
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: files.count === 0 && !app.ftpBusy
                    text: qsTr("No files (or FTP is down)")
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

        // Transfer in progress (from the console to the PC).
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: app.downloadActive

            Text {
                text: qsTr("Downloading %1").arg(app.downloadName)
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
                text: qsTr("Cancel")
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
                placeholderText: qsTr("new folder")
                color: Theme.text
                font.pixelSize: 12
                background: Rectangle {
                    color: Theme.controlFill
                    border.color: newFolderName.activeFocus ? Theme.accent : Theme.border
                    radius: 10
                }
            }
            StyledButton {
                text: qsTr("Create")
                enabled: newFolderName.text.length > 0 && !app.ftpBusy
                onClicked: { app.ftpMakeDirectory(newFolderName.text); newFolderName.text = "" }
            }
        }
    }

    // What can be done with what is under the cursor.
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

        // Room above and below so the highlight of the first and last
        // rows never reaches the rounded corners.
        topPadding: 8
        bottomPadding: 8

        background: Rectangle {
            implicitWidth: 300
            color: Theme.menuFill
            border.color: Theme.glassEdge
            border.width: 1
            radius: 14

            // A soft shadow, so the menu reads as floating over the list.
            Repeater {
                model: 3
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -(index + 1) * 3
                    radius: parent.radius + (index + 1) * 3
                    color: "transparent"
                    border.width: 3
                    border.color: Qt.rgba(0, 0, 0, Theme.light ? 0.04 - index * 0.012 : 0.16 - index * 0.05)
                    z: -1
                }
            }
        }

        // Header with the name and size, so there is no doubt about which
        // file the menu is acting on.
        Rectangle {
            implicitHeight: 56
            implicitWidth: rowMenu.width
            color: "transparent"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                anchors.topMargin: 6
                anchors.bottomMargin: 6
                spacing: 12
                Icon {
                    name: rowMenu.targetIsDirectory ? "folder" : "file"
                    size: 20
                    color: rowMenu.targetIsDirectory ? Theme.accent : Theme.textSecondary
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: rowMenu.targetName
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }
                    Text {
                        text: rowMenu.targetIsDirectory ? qsTr("folder") : rowMenu.targetSizeText
                        color: Theme.textSecondary
                        font.pixelSize: 11
                    }
                }
            }
        }

        MenuSeparator {
            // The separator breathes too: without margins it would run edge
            // to edge and cut the menu in half.
            padding: 6
            leftPadding: 16
            rightPadding: 16
            contentItem: Rectangle { implicitHeight: 1; color: Theme.glassEdge }
        }

        StyledMenuItem {
            text: qsTr("Open")
            iconName: "folder"
            visible: rowMenu.targetIsDirectory
            height: visible ? implicitHeight : 0
            onTriggered: app.ftpNavigate(rowMenu.targetPath)
        }
        StyledMenuItem {
            text: qsTr("Use as the upload folder")
            iconName: "upload"
            visible: rowMenu.targetIsDirectory
            height: visible ? implicitHeight : 0
            onTriggered: app.setFtpUploadDirectory(rowMenu.targetPath)
        }
        StyledMenuItem {
            text: qsTr("Download to the desktop")
            iconName: "download"
            enabled: !app.downloadActive
            onTriggered: root.download(rowMenu.targetPath, rowMenu.targetName, rowMenu.targetIsDirectory, "")
        }
        StyledMenuItem {
            text: qsTr("Download to…")
            iconName: "save"
            enabled: !app.downloadActive
            onTriggered: destinationDialog.open()
        }
        StyledMenuItem {
            text: qsTr("Get it ready to drag")
            iconName: "hard-drive"
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
            text: qsTr("Show the local copy")
            iconName: "external-link"
            visible: rowMenu.targetRow ? rowMenu.targetRow.localUrl.length > 0 : false
            height: visible ? implicitHeight : 0
            onTriggered: app.openLocalFolder(rowMenu.targetRow.localUrl)
        }

        MenuSeparator {
            // The separator breathes too: without margins it would run edge
            // to edge and cut the menu in half.
            padding: 6
            leftPadding: 16
            rightPadding: 16
            contentItem: Rectangle { implicitHeight: 1; color: Theme.glassEdge }
        }

        StyledMenuItem {
            text: qsTr("Copy the path")
            iconName: "copy"
            onTriggered: app.copyToClipboard(rowMenu.targetPath)
        }
        StyledMenuItem {
            text: qsTr("Rename…")
            iconName: "pencil"
            onTriggered: renameDialog.open(rowMenu.targetPath, rowMenu.targetName)
        }
        StyledMenuItem {
            text: qsTr("Move to…")
            iconName: "folder-open"
            onTriggered: renameDialog.openMove(rowMenu.targetPath, app.ftpPath)
        }
        StyledMenuItem {
            text: qsTr("Delete on the console")
            iconName: "trash"
            danger: true
            onTriggered: confirmDelete.open(rowMenu.targetPath, rowMenu.targetIsDirectory,
                rowMenu.targetName)
        }
    }

    FolderDialog {
        id: destinationDialog
        title: qsTr("Where to save")
        currentFolder: root.lastDestination.length > 0
            ? root.lastDestination
            : "file://" + app.defaultDownloadDirectory()
        onAccepted: {
            root.lastDestination = selectedFolder
            root.download(rowMenu.targetPath, rowMenu.targetName, rowMenu.targetIsDirectory, selectedFolder)
        }
    }

    Dialog {
        id: renameDialog
        property string targetPath: ""
        // "rename": a new name; "move": the console folder to move it to.
        property string mode: "rename"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 440
        modal: true
        padding: 0

        function open(path, name) {
            mode = "rename"
            targetPath = path
            nameInput.text = name
            visible = true
            nameInput.forceActiveFocus()
            nameInput.selectAll()
        }

        function openMove(path, folder) {
            mode = "move"
            targetPath = path
            nameInput.text = folder
            visible = true
            nameInput.forceActiveFocus()
            nameInput.selectAll()
        }

        function confirm() {
            if (nameInput.text.trim().length === 0)
                return
            if (mode === "move")
                app.ftpMove(targetPath, nameInput.text.trim())
            else
                app.ftpRename(targetPath, nameInput.text.trim())
            close()
        }

        Overlay.modal: Rectangle { color: Theme.scrim }

        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }

        header: DialogHeader {
            title: renameDialog.mode === "move" ? qsTr("Move to the folder") : qsTr("Rename")
            dialog: renameDialog
        }

        contentItem: StyledField {
            id: nameInput
            leftInset: Theme.dialogMargin
            rightInset: Theme.dialogMargin
            leftPadding: Theme.dialogMargin + 14
            rightPadding: Theme.dialogMargin + 14
            onAccepted: renameDialog.confirm()
        }

        footer: Item {
            implicitHeight: Theme.dialogFooter
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 10
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancel")
                    minimumWidth: 100
                    onClicked: renameDialog.close()
                }
                StyledButton {
                    text: renameDialog.mode === "move" ? qsTr("Move") : qsTr("Rename")
                    primary: true
                    minimumWidth: 110
                    enabled: nameInput.text.trim().length > 0
                    onClicked: renameDialog.confirm()
                }
            }
        }
    }

    // Deleting on the console always asks first.
    Dialog {
        id: confirmDelete
        property string targetPath: ""
        property bool targetIsDirectory: false
        property string targetName: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        // Fixed width: without it the dialog sizes itself by the text and the
        // text by the dialog, and Qt warns about the loop.
        width: 460
        modal: true
        padding: 0

        function open(path, isDirectory, name) {
            targetPath = path
            targetIsDirectory = isDirectory
            targetName = name
            visible = true
        }

        // Without this the dialog would use the Basic style: white background
        // with the theme's text on top — on the dark theme, light on white, unreadable.
        Overlay.modal: Rectangle { color: Theme.scrim }

        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }

        header: DialogHeader {
            title: confirmDelete.targetIsDirectory ? qsTr("Delete the folder on the console?") : qsTr("Delete on the console?")
            dialog: confirmDelete
        }

        contentItem: RowLayout {
            spacing: 16

            // The trash can in a soft red circle: what is about to happen.
            Rectangle {
                Layout.leftMargin: Theme.dialogMargin
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 10
                implicitWidth: 44
                implicitHeight: 44
                radius: 22
                color: Theme.alpha(Theme.error, 0.14)
                Icon {
                    anchors.centerIn: parent
                    name: "trash"
                    size: 20
                    color: Theme.error
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: 8
                Layout.bottomMargin: 8
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    text: confirmDelete.targetName
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 3
                    elide: Text.ElideMiddle
                }
                Text {
                    Layout.fillWidth: true
                    text: confirmDelete.targetIsDirectory
                        ? qsTr("The folder and everything inside it are deleted from the console. "
                               + "This cannot be undone.")
                        : qsTr("The file is deleted from the console. This cannot be undone.")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }
        }

        footer: Item {
            implicitHeight: Theme.dialogFooter
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 10
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancel")
                    minimumWidth: 100
                    onClicked: confirmDelete.close()
                }
                StyledButton {
                    text: qsTr("Delete")
                    iconName: "trash"
                    danger: true
                    solid: true
                    minimumWidth: 110
                    onClicked: {
                        app.ftpDelete(confirmDelete.targetPath, confirmDelete.targetIsDirectory)
                        confirmDelete.close()
                    }
                }
            }
        }
    }
}
