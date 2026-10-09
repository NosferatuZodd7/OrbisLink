// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The payload manager: what a jailbroken console keeps in its payload and
// plugin folders — GoldHEN on a PS4, etaHEN and the autoloader on a PS5 —
// read over FTP. Files are added from this PC, renamed, deleted or
// downloaded; a switch decides which start by themselves; the settings files
// open in an editor; and any payload, on the console or on this PC, can be
// sent to the console's loader to run now.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    signal back()

    readonly property bool online: payloads.online
    readonly property bool ps5: payloads.kind === "ps5"

    function sizeText(bytes) {
        if (bytes < 1024)
            return qsTr("%1 B").arg(bytes)
        if (bytes < 1024 * 1024)
            return qsTr("%1 KB").arg((bytes / 1024).toFixed(0))
        return qsTr("%1 MB").arg((bytes / (1024 * 1024)).toFixed(1))
    }
    function folderTitle(id) {
        switch (id) {
        case "goldhen-payloads": return qsTr("GoldHEN payloads")
        case "payload-library": return qsTr("Payload library")
        case "goldhen-plugins": return qsTr("GoldHEN plugins")
        case "etahen-payloads": return qsTr("etaHEN payloads")
        case "etahen-plugins": return qsTr("etaHEN plugins")
        case "autoloader": return qsTr("Autoloader")
        }
        return id
    }
    function folderNote(id) {
        switch (id) {
        case "goldhen-payloads":
            return qsTr("goldhen.bin is GoldHEN itself: the loader starts it at every boot.")
        case "payload-library":
            return qsTr("The payloads Payload Guest lists. ▶ sends one to GoldHEN's BinLoader (port 9090).")
        case "goldhen-plugins":
            return qsTr("Switched on: the plugin loads with every game ([default] in plugins.ini).")
        case "etahen-payloads":
            return qsTr("Switched on: it starts every time etaHEN loads. ▶ runs one now (port 9021).")
        case "etahen-plugins":
            return qsTr("Switched on: it starts every time etaHEN loads.")
        case "autoloader":
            return qsTr("Switched on: it is in autoload.txt and starts after the exploit, in that order.")
        }
        return ""
    }
    function folderIcon(id) {
        return id.indexOf("plugins") >= 0 ? "plug" : id === "autoloader" ? "list-ordered" : "zap"
    }
    function typeLabel(c) {
        return c.type === "ps5" ? "PS5" : c.type === "ps4" ? "PS4" : ""
    }
    function probeConsoles() {
        var all = []
        var items = payloads.consoles
        for (var i = 0; i < items.length; ++i)
            all.push(items[i].address)
        app.probeFtp(all)
    }

    onVisibleChanged: {
        if (!visible)
            return
        probeConsoles()
        if (!payloads.busy)
            payloads.refresh()
    }
    // FTP came back (or went): read again.
    onOnlineChanged: if (visible && online && !payloads.busy) payloads.refresh()

    Connections {
        target: payloads
        function onFinished(message, error) { toast.show(message, error) }
        function onTextLoaded(path, text, error) { editor.show(path, text) }
    }

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: Theme.gutter
        anchors.rightMargin: window.panelShown ? 8 : Theme.gutter
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        radius: Theme.radius
        color: Theme.panelFill
        border.width: 1
        border.color: Theme.glassEdge
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 14

            // ── header
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                StyledToolButton {
                    iconName: "chevron-left"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Back to the consoles")
                    onClicked: root.back()
                }
                ColumnLayout {
                    spacing: 2
                    Text {
                        text: qsTr("Payloads")
                        color: Theme.text
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: payloads.busy && payloads.status.length > 0 ? payloads.status
                            : root.ps5 ? qsTr("PS5 · etaHEN and the autoloader")
                            : qsTr("PS4 · GoldHEN")
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                }
                StyledButton {
                    id: consoleChip
                    chip: true
                    iconName: "gamepad"
                    text: payloads.targetName + (payloads.consoles.length > 1 ? "  ▾" : "")
                    Layout.maximumWidth: 260
                    enabled: !payloads.busy
                    ToolTip.visible: hovered
                    ToolTip.text: payloads.consoles.length > 1 ? qsTr("The console shown: click for another")
                                                               : qsTr("The console shown")
                    onClicked: if (payloads.consoles.length > 1) consoleMenu.popup(consoleChip, 0, consoleChip.height + 4)
                }
                Item { Layout.fillWidth: true }
                StyledButton {
                    iconName: "upload"
                    text: qsTr("Send from this PC…")
                    enabled: !payloads.busy && payloads.target.length > 0
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Run a payload from this PC on the console now, without copying it there")
                    onClicked: pcPayloadDialog.open()
                }
                StyledToolButton {
                    iconName: payloads.busy ? "loader" : "refresh"
                    enabled: !payloads.busy
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Read the console again")
                    onClicked: { root.probeConsoles(); payloads.refresh() }
                }
            }

            // ── no FTP: nothing to read
            Rectangle {
                visible: !root.online
                Layout.fillWidth: true
                implicitHeight: offlineRow.implicitHeight + 28
                radius: 16
                color: Theme.alpha(Theme.warn, 0.10)
                border.width: 1
                border.color: Theme.alpha(Theme.warn, 0.35)
                RowLayout {
                    id: offlineRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 14
                    Icon { name: "warning"; size: 22; color: Theme.warn }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        font.pixelSize: 13
                        text: qsTr("%1's FTP is not answering. Load the jailbreak (GoldHEN or etaHEN) with its "
                                   + "FTP server on, then read again. Payloads from this PC can still be sent.")
                              .arg(payloads.targetName)
                    }
                }
            }

            // ── the folders, the settings files and what the payloads said
            Flickable {
                id: body
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: sections.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                ColumnLayout {
                    id: sections
                    width: body.width - 12
                    spacing: 14

                    Repeater {
                        model: root.online ? payloads.folders : []

                        Rectangle {
                            id: folderCard
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: folderColumn.implicitHeight + 28
                            radius: 14
                            color: Theme.panelAltFill
                            border.width: 1
                            border.color: Theme.border

                            ColumnLayout {
                                id: folderColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 14
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Icon {
                                        name: root.folderIcon(folderCard.modelData.id)
                                        size: 18
                                        color: Theme.accent
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1
                                        Text {
                                            text: root.folderTitle(folderCard.modelData.id)
                                                  + (folderCard.modelData.files.length > 0
                                                     ? "  ·  " + folderCard.modelData.files.length : "")
                                            color: Theme.text
                                            font.pixelSize: 14
                                            font.weight: Font.DemiBold
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            text: folderCard.modelData.path
                                            color: Theme.textSecondary
                                            font.pixelSize: 11
                                            font.family: Theme.fontMono
                                        }
                                    }
                                    StyledButton {
                                        iconName: "plus"
                                        text: qsTr("Add…")
                                        enabled: !payloads.busy
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Copy files from this PC into %1").arg(folderCard.modelData.path)
                                        onClicked: {
                                            addDialog.folderId = folderCard.modelData.id
                                            addDialog.open()
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    text: folderCard.modelData.exists
                                          ? root.folderNote(folderCard.modelData.id)
                                          : qsTr("Not on this console yet. Adding a file makes the folder.")
                                }

                                Repeater {
                                    model: folderCard.modelData.files

                                    Rectangle {
                                        id: fileRow
                                        required property var modelData
                                        Layout.fillWidth: true
                                        implicitHeight: 44
                                        radius: 10
                                        color: rowHover.hovered ? Theme.controlFill : "transparent"
                                        HoverHandler { id: rowHover }

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 6
                                            spacing: 10

                                            // Starts by itself, where the folder allows it;
                                            // the same width either way, so names line up.
                                            Item {
                                                Layout.preferredWidth: 40
                                                Layout.preferredHeight: 24
                                                StyledCheck {
                                                    anchors.fill: parent
                                                    visible: folderCard.modelData.autoStart !== "none"
                                                    checked: fileRow.modelData.autoStart
                                                    enabled: !payloads.busy
                                                    ToolTip.visible: hovered
                                                    ToolTip.text: checked ? qsTr("Starts by itself: click to stop that")
                                                                          : qsTr("Click to start it by itself")
                                                    onToggled: payloads.setAutoStart(fileRow.modelData.path, checked)
                                                }
                                                Icon {
                                                    anchors.centerIn: parent
                                                    visible: folderCard.modelData.autoStart === "none"
                                                    name: "file"
                                                    size: 16
                                                    color: Theme.textSecondary
                                                }
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                elide: Text.ElideMiddle
                                                text: fileRow.modelData.name
                                                color: Theme.text
                                                font.pixelSize: 13
                                            }
                                            Rectangle {
                                                visible: fileRow.modelData.critical
                                                implicitHeight: 22
                                                implicitWidth: criticalText.implicitWidth + 16
                                                radius: 11
                                                color: Theme.henFill
                                                Text {
                                                    id: criticalText
                                                    anchors.centerIn: parent
                                                    text: qsTr("the jailbreak")
                                                    color: Theme.textOnHen
                                                    font.pixelSize: 11
                                                    font.weight: Font.Bold
                                                }
                                            }
                                            Text {
                                                text: root.sizeText(fileRow.modelData.size)
                                                color: Theme.textSecondary
                                                font.pixelSize: 12
                                            }
                                            StyledToolButton {
                                                visible: fileRow.modelData.sendable
                                                iconName: "play"
                                                enabled: !payloads.busy
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Run it now (port %1)").arg(fileRow.modelData.port)
                                                onClicked: payloads.sendFromConsole(fileRow.modelData.path, 0)
                                            }
                                            StyledToolButton {
                                                iconName: "download"
                                                enabled: !payloads.busy
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Download to this PC")
                                                onClicked: payloads.download(fileRow.modelData.path)
                                            }
                                            StyledToolButton {
                                                iconName: "pencil"
                                                enabled: !payloads.busy
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Rename")
                                                onClicked: renameDialog.ask(fileRow.modelData)
                                            }
                                            StyledToolButton {
                                                iconName: "trash"
                                                danger: true
                                                enabled: !payloads.busy
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Delete from the console…")
                                                onClicked: deleteDialog.ask(fileRow.modelData)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── settings files
                    Rectangle {
                        visible: root.online && payloads.configs.length > 0
                        Layout.fillWidth: true
                        implicitHeight: configColumn.implicitHeight + 28
                        radius: 14
                        color: Theme.panelAltFill
                        border.width: 1
                        border.color: Theme.border

                        ColumnLayout {
                            id: configColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 14
                            spacing: 6
                            RowLayout {
                                spacing: 10
                                Icon { name: "settings"; size: 18; color: Theme.accent }
                                Text {
                                    text: qsTr("Settings files")
                                    color: Theme.text
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }
                            }
                            Repeater {
                                model: payloads.configs
                                RowLayout {
                                    id: configRow
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideMiddle
                                        text: configRow.modelData.path
                                        color: configRow.modelData.exists ? Theme.text : Theme.textSecondary
                                        font.pixelSize: 12
                                        font.family: Theme.fontMono
                                    }
                                    Text {
                                        visible: !configRow.modelData.exists
                                        text: qsTr("not there yet")
                                        color: Theme.textSecondary
                                        font.pixelSize: 11
                                    }
                                    StyledButton {
                                        iconName: "pencil"
                                        text: qsTr("Edit")
                                        enabled: !payloads.busy
                                        onClicked: payloads.loadText(configRow.modelData.path)
                                    }
                                }
                            }
                        }
                    }

                    // ── what the payloads said
                    Rectangle {
                        visible: payloads.output.length > 0
                        Layout.fillWidth: true
                        implicitHeight: outputColumn.implicitHeight + 28
                        radius: 14
                        color: Theme.panelAltFill
                        border.width: 1
                        border.color: Theme.border

                        ColumnLayout {
                            id: outputColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 14
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Icon { name: "terminal"; size: 18; color: Theme.accent }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("What the console answered")
                                    color: Theme.text
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }
                                StyledButton {
                                    chip: true
                                    text: qsTr("Clear")
                                    onClicked: payloads.clearOutput()
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WrapAnywhere
                                text: payloads.output.slice(-40).join("\n")
                                color: Theme.text
                                font.pixelSize: 12
                                font.family: Theme.fontMono
                            }
                        }
                    }

                    Item { implicitHeight: 4 }
                }
            }
        }
    }

    // ───────────────────────────── menus and dialogs

    Menu {
        id: consoleMenu
        topPadding: 8
        bottomPadding: 8
        background: Rectangle {
            implicitWidth: 280
            color: Theme.menuFill
            border.color: Theme.glassEdge
            border.width: 1
            radius: 14
        }
        Instantiator {
            model: payloads.consoles
            delegate: StyledMenuItem {
                required property var modelData
                text: modelData.name + (root.typeLabel(modelData).length > 0 ? "  ·  " + root.typeLabel(modelData) : "")
                      + (modelData.ftp ? "" : "  ·  " + qsTr("no FTP"))
                iconName: modelData.target ? "check" : "gamepad"
                // Closed first: picking rebuilds this list.
                onTriggered: {
                    consoleMenu.close()
                    payloads.setTarget(modelData.address)
                }
            }
            onObjectAdded: (index, object) => consoleMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => consoleMenu.removeItem(object)
        }
    }

    FileDialog {
        id: addDialog
        property string folderId: ""
        title: qsTr("Files to copy to the console")
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            var files = []
            for (var i = 0; i < selectedFiles.length; ++i)
                files.push(selectedFiles[i].toString())
            payloads.upload(folderId, files)
        }
    }

    FileDialog {
        id: pcFileDialog
        title: qsTr("Payload to run on the console")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Payloads (*.elf *.bin *.self *.lua *.js *.jar)"), qsTr("All files (*)")]
        onAccepted: {
            pcPayloadDialog.file = selectedFile.toString()
            var name = pcPayloadDialog.file.split("/").pop()
            portField.text = payloads.portFor(name)
        }
    }

    // A payload from this PC: which file, to which port.
    Dialog {
        id: pcPayloadDialog
        property string file: ""
        readonly property string fileName: file.length > 0 ? decodeURIComponent(file.split("/").pop()) : ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 32)
        modal: true
        padding: 0
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }
        header: DialogHeader {
            title: qsTr("Run a payload from this PC")
            dialog: pcPayloadDialog
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: 6
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 12
                text: root.ps5
                      ? qsTr("ELF payloads go to the ELF loader (port 9021, etaHEN's or elfldr); .bin to the "
                             + "exploit's loader (9020); .lua to 9026, .jar to 9025, .js to 50000.")
                      : qsTr("Payloads go to GoldHEN's BinLoader, port 9090: turn it on in GoldHEN's settings.")
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                spacing: 10
                StyledButton {
                    iconName: "folder-open"
                    text: pcPayloadDialog.fileName.length > 0 ? pcPayloadDialog.fileName : qsTr("Choose the file…")
                    Layout.fillWidth: true
                    onClicked: pcFileDialog.open()
                }
                Text { text: qsTr("Port"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField {
                    id: portField
                    Layout.preferredWidth: 90
                    text: root.ps5 ? "9021" : "9090"
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 1; top: 65535 }
                }
            }
            Item { implicitHeight: 4 }
        }
        footer: Item {
            implicitHeight: Theme.dialogFooter
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 8
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancel")
                    onClicked: pcPayloadDialog.close()
                }
                StyledButton {
                    text: qsTr("Send")
                    iconName: "play"
                    primary: true
                    enabled: pcPayloadDialog.file.length > 0 && portField.acceptableInput
                    onClicked: {
                        payloads.sendFromPc(pcPayloadDialog.file, parseInt(portField.text))
                        pcPayloadDialog.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: renameDialog
        property string path: ""
        property string name: ""
        function ask(file) {
            path = file.path
            name = file.name
            renameField.text = file.name
            open()
            renameField.forceActiveFocus()
            renameField.selectAll()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 32)
        modal: true
        padding: 0
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }
        header: DialogHeader {
            title: qsTr("Rename %1").arg(renameDialog.name)
            dialog: renameDialog
        }
        contentItem: ColumnLayout {
            StyledField {
                id: renameField
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: 8
                Layout.bottomMargin: 8
                onAccepted: renameButton.clicked()
            }
        }
        footer: Item {
            implicitHeight: Theme.dialogFooter
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 8
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancel")
                    onClicked: renameDialog.close()
                }
                StyledButton {
                    id: renameButton
                    text: qsTr("Rename")
                    primary: true
                    enabled: renameField.text.trim().length > 0 && renameField.text.indexOf("/") < 0
                             && renameField.text.trim() !== renameDialog.name
                    onClicked: {
                        payloads.rename(renameDialog.path, renameField.text.trim())
                        renameDialog.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: deleteDialog
        property var file: ({ name: "", path: "", critical: false })
        function ask(f) {
            file = f
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent.width - 32)
        modal: true
        padding: 0
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }
        header: DialogHeader {
            title: qsTr("Delete %1 from the console?").arg(deleteDialog.file.name)
            dialog: deleteDialog
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: 6
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 13
                text: qsTr("It goes from the console, and from the lists that start it by itself. Download it "
                           + "first if you have no other copy.")
            }
            Text {
                visible: deleteDialog.file.critical === true
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                color: Theme.error
                font.pixelSize: 13
                font.weight: Font.DemiBold
                text: qsTr("This is the jailbreak itself: without it, GoldHEN does not come back after a "
                           + "restart until it is copied there again.")
            }
            Item { implicitHeight: 4 }
        }
        footer: Item {
            implicitHeight: Theme.dialogFooter
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 8
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancel")
                    onClicked: deleteDialog.close()
                }
                StyledButton {
                    text: qsTr("Delete")
                    iconName: "trash"
                    danger: true
                    solid: true
                    onClicked: {
                        payloads.remove(deleteDialog.file.path)
                        deleteDialog.close()
                    }
                }
            }
        }
    }

    // A settings file, as text.
    Dialog {
        id: editor
        property string path: ""
        property string original: ""
        function show(p, text) {
            path = p
            original = text
            editorArea.text = text
            open()
            editorArea.forceActiveFocus()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(760, parent.width - 48)
        height: Math.min(620, parent.height - 48)
        modal: true
        padding: 0
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }
        header: DialogHeader {
            title: editor.path
            dialog: editor
        }
        contentItem: Item {
            ScrollView {
                anchors.fill: parent
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                anchors.topMargin: 4
                TextArea {
                    id: editorArea
                    wrapMode: TextEdit.NoWrap
                    selectByMouse: true
                    color: Theme.text
                    selectionColor: Theme.alpha(Theme.accent, 0.4)
                    font.family: Theme.fontMono
                    font.pixelSize: 13
                    background: Rectangle {
                        color: Theme.panelAltFill
                        border.color: Theme.border
                        radius: 8
                    }
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
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: editorArea.text !== editor.original ? qsTr("Changed — saving puts it on the console.") : ""
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
                StyledButton {
                    text: qsTr("Cancel")
                    onClicked: editor.close()
                }
                StyledButton {
                    text: qsTr("Save on the console")
                    iconName: "save"
                    primary: true
                    enabled: editorArea.text !== editor.original && !payloads.busy
                    onClicked: {
                        payloads.saveText(editor.path, editorArea.text)
                        editor.close()
                    }
                }
            }
        }
    }
}
