// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The payload manager: what a jailbroken console keeps in its payload and
// plugin folders — GoldHEN on a PS4, etaHEN, the autoloader and PLK's
// Payload Manager on a PS5 — read over FTP. Files are added from this PC,
// renamed, deleted or downloaded; a switch decides which start by
// themselves, and the autoload lists can be put in order with waits between;
// the settings files open in an editor; and any payload, on the console or
// on this PC, can be sent to the console's loader to run now. On a PS5 the
// Library tab lists the community's payloads (the list PLK's Payload Manager
// reads): run one now, or install it, or bring an older copy up to date.
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
    // 0: what is on the console; 1: the library (PS5).
    property int tab: 0
    readonly property bool library: ps5 && tab === 1
    property string search: ""
    property string category: "all"

    // The folders a payload from the library can go to: those that take its
    // kind of file, plugins aside.
    function installFolders(fileName) {
        var dot = fileName.lastIndexOf(".")
        var ext = dot > 0 ? fileName.substring(dot).toLowerCase() : ""
        var ids = ["etahen-payloads", "autoloader", "pldmgr"]
        var exts = { "etahen-payloads": [".elf", ".bin"], "autoloader": [".elf", ".bin", ".lua", ".js", ".jar"],
                     "pldmgr": [".elf", ".bin"] }
        var out = []
        for (var i = 0; i < ids.length; ++i)
            if (exts[ids[i]].indexOf(ext) >= 0)
                out.push(ids[i])
        return out
    }
    function categories() {
        var seen = []
        var items = payloads.catalog
        for (var i = 0; i < items.length; ++i)
            if (seen.indexOf(items[i].category) < 0)
                seen.push(items[i].category)
        seen.sort()
        return seen
    }
    function countWhere(test) {
        var n = 0
        var items = payloads.catalog
        for (var i = 0; i < items.length; ++i)
            if (test(items[i]))
                ++n
        return n
    }
    function shownPayloads() {
        var needle = search.trim().toLowerCase()
        var out = []
        var items = payloads.catalog
        for (var i = 0; i < items.length; ++i) {
            var p = items[i]
            if (category === "installed" ? !p.installed : category === "updates" ? !p.update
                : category !== "all" && p.category !== category)
                continue
            if (needle.length > 0 && p.name.toLowerCase().indexOf(needle) < 0
                    && p.description.toLowerCase().indexOf(needle) < 0)
                continue
            out.push(p)
        }
        return out
    }
    function copyLine(p) {
        var places = []
        for (var i = 0; i < p.copies.length; ++i) {
            var c = p.copies[i]
            places.push(folderTitle(c.folder) + (c.version.length > 0 ? " (" + c.version + ")" : ""))
        }
        return places.join(", ")
    }

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
        case "pldmgr": return qsTr("Payload Manager (PLDMGR)")
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
        case "pldmgr":
            return qsTr("PLK's Payload Manager. Switched on: it is in its autoload list and starts when the "
                        + "Payload Manager loads, in that order.")
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
    // The list is read the first time the library is opened.
    onLibraryChanged: if (library && payloads.catalog.length === 0 && !payloads.catalogLoading) payloads.refreshCatalog()
    // FTP came back (or went): read again.
    onOnlineChanged: if (visible && online && !payloads.busy) payloads.refresh()

    Connections {
        target: payloads
        function onFinished(message, error) { toast.show(message, error) }
        function onTextLoaded(path, text, error) {
            if (path === orderDialog.pending) {
                orderDialog.pending = ""
                orderDialog.show(path, text)
            } else {
                editor.show(path, text)
            }
        }
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
                    iconName: payloads.busy || (root.library && payloads.catalogLoading) ? "loader" : "refresh"
                    enabled: root.library ? !payloads.catalogLoading : !payloads.busy
                    ToolTip.visible: hovered
                    ToolTip.text: root.library ? qsTr("Read the payload list again") : qsTr("Read the console again")
                    onClicked: {
                        root.probeConsoles()
                        if (root.library)
                            payloads.refreshCatalog()
                        payloads.refresh()
                    }
                }
            }

            // ── on the console | the library (PS5)
            RowLayout {
                visible: root.ps5
                Layout.fillWidth: true
                spacing: 10
                TabBar {
                    id: pageTabs
                    // Room for "Library (2 updates)" in any language.
                    Layout.preferredWidth: 400
                    currentIndex: root.tab
                    onCurrentIndexChanged: root.tab = currentIndex
                    padding: 4
                    spacing: 4
                    background: Rectangle {
                        radius: 12
                        color: Theme.controlFill
                        border.width: 1
                        border.color: Theme.glassEdge
                    }
                    StyledTab { text: qsTr("On the console") }
                    StyledTab {
                        readonly property int updates: root.countWhere(function (p) { return p.update })
                        text: updates > 0 ? qsTr("Library (%n update(s))", "", updates) : qsTr("Library")
                    }
                }
                StyledField {
                    visible: root.library
                    Layout.preferredWidth: 220
                    placeholderText: qsTr("Search payloads")
                    text: root.search
                    onTextChanged: root.search = text
                }
                Item { Layout.fillWidth: true }
            }

            // ── no FTP: nothing to read
            Rectangle {
                visible: !root.online && !root.library
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
                visible: !root.library
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
                                        visible: folderCard.modelData.autoStart === "list" && folderCard.modelData.exists
                                        iconName: "list-ordered"
                                        text: qsTr("Order and waits…")
                                        enabled: !payloads.busy
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("The order they start in, and how long to wait before each")
                                        onClicked: {
                                            orderDialog.files = folderCard.modelData.files
                                            orderDialog.listTitle = root.folderTitle(folderCard.modelData.id)
                                            orderDialog.pending = folderCard.modelData.configPath
                                            payloads.loadText(folderCard.modelData.configPath)
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
                                                      + (fileRow.modelData.version.length > 0
                                                         ? "  ·  " + fileRow.modelData.version : "")
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

            // ── the library (PS5)
            ColumnLayout {
                visible: root.library
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                Flow {
                    visible: payloads.catalog.length > 0
                    Layout.fillWidth: true
                    spacing: 6
                    StyledButton {
                        chip: true
                        primary: root.category === "all"
                        text: qsTr("All") + "  " + payloads.catalog.length
                        onClicked: root.category = "all"
                    }
                    Repeater {
                        model: root.library ? root.categories() : []
                        StyledButton {
                            required property string modelData
                            chip: true
                            primary: root.category === modelData
                            text: modelData
                            onClicked: root.category = modelData
                        }
                    }
                    StyledButton {
                        readonly property int count: root.countWhere(function (p) { return p.installed })
                        visible: count > 0
                        chip: true
                        iconName: "check"
                        primary: root.category === "installed"
                        text: qsTr("Installed", "filter: the payloads on the console") + "  " + count
                        onClicked: root.category = "installed"
                    }
                    StyledButton {
                        readonly property int count: root.countWhere(function (p) { return p.update })
                        visible: count > 0
                        chip: true
                        iconName: "arrow-up"
                        primary: root.category === "updates"
                        text: qsTr("Updates") + "  " + count
                        onClicked: root.category = "updates"
                    }
                }

                // Could not read the list (perhaps showing last time's).
                Rectangle {
                    visible: payloads.catalogError.length > 0 && !payloads.catalogLoading
                    Layout.fillWidth: true
                    implicitHeight: catalogErrorRow.implicitHeight + 24
                    radius: 14
                    color: Theme.alpha(Theme.warn, 0.10)
                    border.width: 1
                    border.color: Theme.alpha(Theme.warn, 0.35)
                    RowLayout {
                        id: catalogErrorRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 16
                        anchors.rightMargin: 12
                        spacing: 12
                        Icon { name: "warning"; size: 20; color: Theme.warn }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            font.pixelSize: 12
                            text: payloads.catalogError
                                  + (payloads.catalogOffline ? " " + qsTr("Shown: the list from last time.") : "")
                        }
                        StyledButton {
                            chip: true
                            iconName: "refresh"
                            text: qsTr("Try again")
                            onClicked: payloads.refreshCatalog()
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    text: payloads.catalogLoading && payloads.catalog.length === 0
                          ? qsTr("Reading the payload list…")
                          : !root.online
                            ? qsTr("%1's FTP is not answering: installing needs it. ▶ runs a payload without it.")
                              .arg(payloads.targetName)
                            : qsTr("The payloads PLK's Payload Manager lists, each one checked against its "
                                   + "SHA-256. ▶ runs one now; installing puts it in a folder on the console, "
                                   + "in place of an older copy there.")
                }

                // What the last payload run said (all of it under "On the console").
                Rectangle {
                    visible: payloads.output.length > 0
                    Layout.fillWidth: true
                    Layout.rightMargin: 12
                    implicitHeight: libraryOutput.implicitHeight + 20
                    radius: 12
                    color: Theme.panelAltFill
                    border.width: 1
                    border.color: Theme.border
                    RowLayout {
                        id: libraryOutput
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 14
                        anchors.rightMargin: 10
                        spacing: 10
                        Icon { name: "terminal"; size: 16; color: Theme.accent; Layout.alignment: Qt.AlignTop }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 4
                            elide: Text.ElideRight
                            text: payloads.output.slice(-4).join("\n")
                            color: Theme.text
                            font.pixelSize: 12
                            font.family: Theme.fontMono
                        }
                        StyledButton {
                            chip: true
                            text: qsTr("Clear")
                            Layout.alignment: Qt.AlignTop
                            onClicked: payloads.clearOutput()
                        }
                    }
                }

                ListView {
                    id: libraryList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    model: root.library ? root.shownPayloads() : []

                    Text {
                        visible: libraryList.count === 0 && payloads.catalog.length > 0
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 24
                        text: qsTr("No payload here.")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }

                    delegate: Rectangle {
                        id: payloadCard
                        required property var modelData
                        width: libraryList.width - 12
                        implicitHeight: payloadRow.implicitHeight + 24
                        radius: 14
                        color: Theme.panelAltFill
                        border.width: 1
                        border.color: modelData.update ? Theme.alpha(Theme.accent, 0.55) : Theme.border

                        RowLayout {
                            id: payloadRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 12
                            anchors.leftMargin: 16
                            spacing: 12

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                RowLayout {
                                    spacing: 8
                                    Text {
                                        text: payloadCard.modelData.name
                                        color: Theme.text
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        text: payloadCard.modelData.version
                                        color: Theme.textSecondary
                                        font.pixelSize: 12
                                    }
                                    Rectangle {
                                        visible: payloadCard.modelData.installed
                                        implicitHeight: 20
                                        implicitWidth: stateText.implicitWidth + 16
                                        radius: 10
                                        color: Theme.alpha(payloadCard.modelData.update ? Theme.accent : Theme.ok, 0.16)
                                        Text {
                                            id: stateText
                                            anchors.centerIn: parent
                                            text: payloadCard.modelData.update
                                                  ? qsTr("Update: %1 → %2").arg(payloadCard.modelData.installedVersion)
                                                    .arg(payloadCard.modelData.version)
                                                  : qsTr("Installed", "a payload on the console")
                                            color: payloadCard.modelData.update ? Theme.accent : Theme.ok
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: payloadCard.modelData.description
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: payloadCard.modelData.category
                                          + (payloadCard.modelData.lastUpdate.length > 0
                                             ? "  ·  " + payloadCard.modelData.lastUpdate : "")
                                          + (payloadCard.modelData.installed
                                             ? "  ·  " + qsTr("in %1").arg(root.copyLine(payloadCard.modelData)) : "")
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                }
                            }
                            StyledToolButton {
                                visible: payloadCard.modelData.source.length > 0
                                iconName: "external-link"
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Its page, at its author's")
                                onClicked: Qt.openUrlExternally(payloadCard.modelData.source)
                            }
                            StyledToolButton {
                                visible: payloadCard.modelData.sendable
                                iconName: "play"
                                enabled: !payloads.busy && payloads.target.length > 0
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Run it now, without installing it (port %1)")
                                              .arg(payloadCard.modelData.port)
                                onClicked: payloads.runFromCatalog(payloadCard.modelData.name)
                            }
                            StyledButton {
                                id: installButton
                                iconName: payloadCard.modelData.update ? "arrow-up" : "download"
                                text: payloadCard.modelData.update ? qsTr("Update…")
                                      : payloadCard.modelData.installed ? qsTr("Install again…") : qsTr("Install…")
                                primary: payloadCard.modelData.update
                                enabled: !payloads.busy && root.online
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Put it in a folder on the console")
                                onClicked: installMenu.ask(payloadCard.modelData, installButton)
                            }
                        }
                    }
                }
            }
        }
    }

    // ───────────────────────────── menus and dialogs

    // Where a payload from the library goes.
    Menu {
        id: installMenu
        property var payload: ({ name: "", filename: "", copies: [] })
        property var choices: []
        function ask(p, from) {
            payload = p
            choices = root.installFolders(p.filename)
            popup(from, 0, from.height + 4)
        }
        function copyIn(folderId) {
            for (var i = 0; i < payload.copies.length; ++i)
                if (payload.copies[i].folder === folderId)
                    return payload.copies[i]
            return null
        }
        topPadding: 8
        bottomPadding: 8
        background: Rectangle {
            implicitWidth: 320
            color: Theme.menuFill
            border.color: Theme.glassEdge
            border.width: 1
            radius: 14
        }
        Instantiator {
            model: installMenu.choices
            delegate: StyledMenuItem {
                required property string modelData
                readonly property var copy: installMenu.copyIn(modelData)
                text: root.folderTitle(modelData)
                      + (copy ? "  ·  " + (copy.version.length > 0 ? qsTr("has %1").arg(copy.version) : qsTr("has it"))
                              : "")
                iconName: root.folderIcon(modelData)
                onTriggered: {
                    var name = installMenu.payload.name
                    installMenu.close()
                    payloads.installFromCatalog(name, modelData)
                }
            }
            onObjectAdded: (index, object) => installMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => installMenu.removeItem(object)
        }
    }

    // The steps of the autoload list being put in order.
    ListModel { id: orderSteps }

    Menu {
        id: addStepMenu
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
            model: orderDialog.unlisted
            delegate: StyledMenuItem {
                required property string modelData
                text: modelData
                iconName: "plus"
                onTriggered: {
                    var name = modelData
                    addStepMenu.close()
                    orderSteps.append({ name: name, delayMs: 0 })
                    orderDialog.touch()
                }
            }
            onObjectAdded: (index, object) => addStepMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => addStepMenu.removeItem(object)
        }
    }

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

    // An autoload list: which start, in what order, and the wait before each.
    Dialog {
        id: orderDialog
        // The list asked for: its text opens this, not the editor.
        property string pending: ""
        property string path: ""
        property string original: ""
        property string listTitle: ""
        property var files: []
        property int revision: 0
        // The folder's files not in the list yet.
        readonly property var unlisted: {
            revision
            var listed = []
            for (var i = 0; i < orderSteps.count; ++i)
                listed.push(orderSteps.get(i).name)
            var out = []
            for (var j = 0; j < files.length; ++j)
                if (listed.indexOf(files[j].name) < 0 && files[j].sendable)
                    out.push(files[j].name)
            return out
        }
        function show(p, text) {
            path = p
            original = text
            orderSteps.clear()
            var list = payloads.autoloadSteps(text)
            for (var i = 0; i < list.length; ++i)
                orderSteps.append({ name: list[i].name, delayMs: list[i].delayMs })
            revision = 0
            open()
        }
        function touch() { revision += 1 }
        function steps() {
            var out = []
            for (var i = 0; i < orderSteps.count; ++i)
                out.push({ name: orderSteps.get(i).name, delayMs: orderSteps.get(i).delayMs })
            return out
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(620, parent.width - 48)
        modal: true
        padding: 0
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }
        header: DialogHeader {
            title: qsTr("%1: order and waits").arg(orderDialog.listTitle)
            dialog: orderDialog
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
                text: qsTr("They start from the top. The wait holds one back, in milliseconds (1000 is one second): "
                           + "for a payload that needs the one before it running first.")
            }
            ListView {
                id: stepList
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.preferredHeight: Math.min(contentHeight, 340)
                clip: true
                spacing: 4
                boundsBehavior: Flickable.StopAtBounds
                model: orderSteps
                delegate: Rectangle {
                    id: stepRow
                    required property int index
                    required property string name
                    required property int delayMs
                    width: stepList.width
                    implicitHeight: 44
                    radius: 10
                    color: Theme.panelAltFill
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 4
                        spacing: 8
                        Text {
                            text: (stepRow.index + 1) + "."
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            Layout.preferredWidth: 22
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            text: stepRow.name
                            color: Theme.text
                            font.pixelSize: 13
                        }
                        Text { text: qsTr("wait"); color: Theme.textMuted; font.pixelSize: 12 }
                        StyledField {
                            Layout.preferredWidth: 84
                            implicitHeight: 32
                            horizontalAlignment: TextInput.AlignRight
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 0; top: 600000 }
                            Component.onCompleted: text = stepRow.delayMs
                            onTextEdited: {
                                orderSteps.setProperty(stepRow.index, "delayMs", parseInt(text) || 0)
                                orderDialog.touch()
                            }
                        }
                        Text { text: qsTr("ms"); color: Theme.textMuted; font.pixelSize: 12 }
                        StyledToolButton {
                            iconName: "arrow-up"
                            iconSize: 16
                            enabled: stepRow.index > 0
                            onClicked: { orderSteps.move(stepRow.index, stepRow.index - 1, 1); orderDialog.touch() }
                        }
                        StyledToolButton {
                            iconName: "arrow-down"
                            iconSize: 16
                            enabled: stepRow.index < orderSteps.count - 1
                            onClicked: { orderSteps.move(stepRow.index, stepRow.index + 1, 1); orderDialog.touch() }
                        }
                        StyledToolButton {
                            iconName: "close"
                            iconSize: 16
                            danger: true
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Take it out: it no longer starts by itself")
                            onClicked: { orderSteps.remove(stepRow.index); orderDialog.touch() }
                        }
                    }
                }
            }
            Text {
                visible: orderSteps.count === 0
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 13
                text: qsTr("Nothing starts by itself from here yet.")
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                StyledButton {
                    id: addStepButton
                    chip: true
                    iconName: "plus"
                    text: qsTr("Add a file from the folder")
                    enabled: orderDialog.unlisted.length > 0
                    onClicked: addStepMenu.popup(addStepButton, 0, addStepButton.height + 4)
                }
                Item { Layout.fillWidth: true }
            }
            Item { implicitHeight: 2 }
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
                    text: orderDialog.path
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    font.family: Theme.fontMono
                }
                StyledButton {
                    text: qsTr("Cancel")
                    onClicked: orderDialog.close()
                }
                StyledButton {
                    text: qsTr("Save on the console")
                    iconName: "save"
                    primary: true
                    enabled: orderDialog.revision > 0 && !payloads.busy
                    onClicked: {
                        payloads.saveText(orderDialog.path, payloads.autoloadText(orderDialog.steps(), orderDialog.original))
                        orderDialog.close()
                    }
                }
            }
        }
    }
}
