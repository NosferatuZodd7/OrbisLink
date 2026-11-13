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

    // In the main area (framed) the page sits on the sheet with its margins;
    // over Remote Play (SectionOverlay) the overlay is the frame and closes it.
    // back() stays for whoever shows it to listen to.
    property bool framed: true

    readonly property bool online: payloads.online
    readonly property bool ps5: payloads.kind === "ps5"
    // 0: what is on the console; 1: the library (PS5).
    property int tab: typeof demoView !== "undefined" && demoView === "payloads-library" ? 1 : 0
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
    // Shown from the start (a page opened by --demo-view).
    Component.onCompleted: if (visible) visibleChanged()
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

    // A filter of the library: a round-ended pill, filled in the accent when
    // it is the one in use (a chip button cannot say "chosen").
    component FilterPill: Rectangle {
        id: pill
        property string text: ""
        property string iconName: ""
        property string count: ""
        property bool chosen: false
        signal clicked()

        implicitWidth: pillRow.implicitWidth + 26
        implicitHeight: 32
        radius: height / 2
        color: chosen ? Theme.accentFill : pillHover.hovered ? Theme.alpha(Theme.ink, 0.05) : "transparent"
        border.width: activeFocus ? 2 : 1
        border.color: activeFocus ? Theme.alpha(Theme.accent, 0.8)
                    : chosen ? Theme.alpha(Theme.accent, 0.6) : Theme.glassEdge
        Behavior on color { ColorAnimation { duration: Theme.fast } }

        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: text
        Keys.onSpacePressed: clicked()
        Keys.onReturnPressed: clicked()

        HoverHandler { id: pillHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: pill.clicked() }

        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: 7
            Icon {
                visible: pill.iconName.length > 0
                anchors.verticalCenter: parent.verticalCenter
                name: pill.iconName
                size: 13
                color: pill.chosen ? Theme.accent : Theme.textSecondary
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: pill.text
                color: pill.chosen ? Theme.ink : Theme.text
                font.pixelSize: 12
                font.weight: pill.chosen ? Font.DemiBold : Font.Medium
            }
            Text {
                visible: pill.count.length > 0
                anchors.verticalCenter: parent.verticalCenter
                text: pill.count
                color: Theme.textSecondary
                font.family: Theme.fontMono
                font.pixelSize: 11
            }
        }
    }

    // The head of a plate on this page: a small round seal with its icon,
    // the name in the serif, a count, and the line under it (a path).
    component SectionHead: RowLayout {
        id: head
        property string iconName: ""
        property string title: ""
        property string count: ""
        property string path: ""
        default property alias actions: headActions.data
        Layout.fillWidth: true
        spacing: 12
        Rectangle {
            Layout.alignment: Qt.AlignTop
            implicitWidth: 36
            implicitHeight: 36
            radius: 18
            color: Theme.alpha(Theme.ink, Theme.light ? 0.05 : 0.06)
            border.width: 1
            border.color: Theme.alpha(Theme.brass, 0.5)
            Icon {
                anchors.centerIn: parent
                name: head.iconName
                size: 16
                color: Theme.ink
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    Layout.fillWidth: implicitWidth > width
                    Layout.maximumWidth: implicitWidth
                    elide: Text.ElideRight
                    text: head.title
                    color: Theme.ink
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.fontCardTitle
                    font.weight: Font.DemiBold
                }
                Tag {
                    visible: head.count.length > 0
                    text: head.count
                    mono: true
                }
                Item { Layout.fillWidth: true }
            }
            Text {
                Layout.fillWidth: true
                visible: head.path.length > 0
                elide: Text.ElideMiddle
                text: head.path
                color: Theme.textSecondary
                font.pixelSize: 11
                font.family: Theme.fontMono
            }
        }
        RowLayout {
            id: headActions
            Layout.alignment: Qt.AlignTop
            spacing: 6
        }
    }

    // The page, straight on the sheet: no panel of its own (in the main
    // area the sheet is the frame; over Remote Play the overlay is).
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.framed ? Theme.gutter : 18
        anchors.rightMargin: root.framed ? Theme.gutter : 18
        anchors.topMargin: root.framed ? 22 : 18
        anchors.bottomMargin: root.framed ? 12 : 14
        spacing: 16

        // ── the page's head: which console, which jailbreak, and the two
        // actions that do not depend on a folder
        PageHeader {
            id: pageHeader
            visible: root.framed
            eyebrow: qsTr("Console tools")
            title: qsTr("Payloads")
            subtitle: payloads.busy && payloads.status.length > 0 ? payloads.status
                    : root.ps5 ? qsTr("PS5 · etaHEN and the autoloader")
                    : qsTr("PS4 · GoldHEN")

            // The page's actions sit here; over Remote Play, where the
            // overlay's own header names the page, on the line below.
            Item {
                id: headerSlot
                implicitWidth: childrenRect.width
                implicitHeight: childrenRect.height
            }
        }
        // Over Remote Play: what stands, in a line, and the same actions.
        RowLayout {
            visible: !root.framed
            Layout.fillWidth: true
            spacing: 12
            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: pageHeader.subtitle
                color: Theme.textSecondary
                font.pixelSize: 12
            }
            Item {
                id: overlaySlot
                implicitWidth: childrenRect.width
                implicitHeight: childrenRect.height
            }
        }
        Row {
            id: pageActions
            parent: root.framed ? headerSlot : overlaySlot
            spacing: 8

            StyledButton {
                anchors.verticalCenter: parent.verticalCenter
                id: consoleChip
                chip: true
                iconName: "gamepad"
                text: payloads.targetName + (payloads.consoles.length > 1 ? "  ▾" : "")
                width: Math.min(implicitWidth, 220)
                enabled: !payloads.busy
                ToolTip.visible: hovered
                ToolTip.text: payloads.consoles.length > 1 ? qsTr("The console shown: click for another")
                                                           : qsTr("The console shown")
                onClicked: if (payloads.consoles.length > 1) consoleMenu.popup(consoleChip, 0, consoleChip.height + 4)
            }
            StyledButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "upload"
                text: qsTr("Send from this PC…")
                // The page's one solid button, except in the library, where
                // an update waiting is what stands out.
                primary: !root.library
                enabled: !payloads.busy && payloads.target.length > 0
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Run a payload from this PC on the console now, without copying it there")
                onClicked: pcPayloadDialog.open()
            }
            StyledToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: payloads.busy || (root.library && payloads.catalogLoading) ? "loader" : "refresh"
                spinning: payloads.busy || (root.library && payloads.catalogLoading)
                enabled: root.library ? !payloads.catalogLoading : !payloads.busy
                ToolTip.visible: hovered
                ToolTip.text: root.library ? qsTr("Read the payload list again") : qsTr("Read the console again")
                Accessible.name: ToolTip.text
                onClicked: {
                    root.probeConsoles()
                    if (root.library)
                        payloads.refreshCatalog()
                    payloads.refresh()
                }
            }
        }

        // ── on the console | the library (PS5): a segmented row of tabs
        RowLayout {
            visible: root.ps5
            Layout.fillWidth: true
            Layout.topMargin: -4
            spacing: 12
            TabBar {
                id: pageTabs
                Layout.fillWidth: false
                currentIndex: root.tab
                onCurrentIndexChanged: root.tab = currentIndex
                padding: 3
                spacing: 2
                background: ShelfTile.TabTrack {}
                StyledTab {
                    width: implicitWidth
                    leftPadding: 14
                    rightPadding: 14
                    text: qsTr("On the console")
                }
                StyledTab {
                    readonly property int updates: root.countWhere(function (p) { return p.update })
                    width: implicitWidth
                    leftPadding: 14
                    rightPadding: 14
                    text: updates > 0 ? qsTr("Library (%n update(s))", "", updates) : qsTr("Library")
                }
            }
            Item { Layout.fillWidth: true }
            StyledField {
                visible: root.library
                Layout.preferredWidth: 240
                placeholderText: qsTr("Search payloads")
                text: root.search
                onTextChanged: root.search = text
            }
        }

        // ── no FTP: nothing to read on the console, though a payload from
        // this PC can still be sent
        ColumnLayout {
            visible: !root.online && !root.library
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            Item { Layout.fillHeight: true }
            Image {
                Layout.alignment: Qt.AlignHCenter
                source: "qrc:/icons/planet.png"
                sourceSize.width: 112
                sourceSize.height: 112
                fillMode: Image.PreserveAspectFit
                opacity: Theme.light ? 0.35 : 0.45
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("No FTP to read the console with")
                color: Theme.ink
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontHeading
                font.weight: Font.DemiBold
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: 520
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 13
                text: qsTr("%1's FTP is not answering. Load the jailbreak (GoldHEN or etaHEN) with its "
                           + "FTP server on, then read again. Payloads from this PC can still be sent.")
                      .arg(payloads.targetName)
            }
            StyledButton {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 6
                iconName: "refresh"
                text: qsTr("Read the console again")
                enabled: !payloads.busy
                onClicked: { root.probeConsoles(); payloads.refresh() }
            }
            Item { Layout.fillHeight: true }
        }

        // ── the folders, the settings files and what the payloads said
        Flickable {
            id: body
            visible: !root.library && root.online
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: sections.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: sections
                width: body.width - 14
                spacing: 16

                // Reading: the shape of the folders about to come.
                Repeater {
                    model: payloads.busy && payloads.folders.length === 0 ? 4 : 0
                    SkeletonRow {
                        required property int index
                        Layout.fillWidth: true
                        delay: index * 80
                    }
                }

                // Each folder: a plate with its head, what switching on
                // means there, and its files in a hairline list.
                Repeater {
                    model: root.online ? payloads.folders : []

                    Plate {
                        id: folderCard
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: folderColumn.implicitHeight + 36

                        ColumnLayout {
                            id: folderColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 18
                            spacing: 10

                            SectionHead {
                                iconName: root.folderIcon(folderCard.modelData.id)
                                title: root.folderTitle(folderCard.modelData.id)
                                count: folderCard.modelData.files.length > 0 ? String(folderCard.modelData.files.length) : ""
                                path: folderCard.modelData.path

                                StyledButton {
                                    visible: folderCard.modelData.autoStart === "list" && folderCard.modelData.exists
                                    chip: true
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
                                    chip: true
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
                                Layout.leftMargin: 48
                                wrapMode: Text.WordWrap
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                text: folderCard.modelData.exists
                                      ? root.folderNote(folderCard.modelData.id)
                                      : qsTr("Not on this console yet. Adding a file makes the folder.")
                            }

                            // The files, one per line under a hairline.
                            ColumnLayout {
                                visible: folderCard.modelData.files.length > 0
                                Layout.fillWidth: true
                                Layout.topMargin: 2
                                spacing: 0
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 1
                                    color: Theme.glassEdge
                                }

                                Repeater {
                                    model: folderCard.modelData.files

                                    Rectangle {
                                        id: fileRow
                                        required property var modelData
                                        required property int index
                                        // What it does: the payload list's words, what is
                                        // known of it, or what kind of file it is.
                                        readonly property string about: (modelData.description || "").length > 0
                                                                        ? modelData.description : (modelData.typeNote || "")
                                        Layout.fillWidth: true
                                        implicitHeight: about.length > 0 ? 56 : 46
                                        radius: Theme.radiusSmall
                                        color: rowHover.hovered ? Theme.alpha(Theme.ink, 0.05) : "transparent"
                                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                                        HoverHandler { id: rowHover }

                                        // The hairline under each but the last.
                                        Rectangle {
                                            visible: fileRow.index < folderCard.modelData.files.length - 1
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.leftMargin: 60
                                            height: 1
                                            color: Theme.glassEdge
                                        }

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 8
                                            anchors.rightMargin: 4
                                            spacing: 12

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
                                                    Accessible.name: qsTr("Start %1 by itself").arg(fileRow.modelData.name)
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
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 8
                                                    // A file's name: in the mono, as on the console.
                                                    Text {
                                                        Layout.fillWidth: implicitWidth > width
                                                        Layout.maximumWidth: implicitWidth
                                                        elide: Text.ElideMiddle
                                                        text: fileRow.modelData.name
                                                        color: fileRow.modelData.autoStart || folderCard.modelData.autoStart === "none"
                                                               ? Theme.ink : Theme.text
                                                        font.family: Theme.fontMono
                                                        font.pixelSize: 12
                                                        font.weight: Font.Medium
                                                    }
                                                    Tag {
                                                        visible: fileRow.modelData.version.length > 0
                                                        text: fileRow.modelData.version
                                                        mono: true
                                                    }
                                                    Tag {
                                                        visible: fileRow.modelData.critical
                                                        text: qsTr("the jailbreak")
                                                        tone: "brass"
                                                        solid: true
                                                    }
                                                    Item { Layout.fillWidth: true }
                                                }
                                                Text {
                                                    id: aboutText
                                                    Layout.fillWidth: true
                                                    visible: fileRow.about.length > 0
                                                    elide: Text.ElideRight
                                                    text: fileRow.about
                                                    color: Theme.textSecondary
                                                    // Only the kind of file it is: quieter.
                                                    opacity: (fileRow.modelData.description || "").length > 0 ? 1 : 0.8
                                                    font.pixelSize: 11
                                                    ToolTip.visible: aboutHover.hovered && aboutText.truncated
                                                    ToolTip.text: fileRow.about
                                                    HoverHandler { id: aboutHover }
                                                }
                                            }
                                            Text {
                                                text: root.sizeText(fileRow.modelData.size)
                                                color: Theme.textSecondary
                                                font.family: Theme.fontMono
                                                font.pixelSize: 11
                                            }
                                            StyledToolButton {
                                                visible: fileRow.modelData.sendable
                                                iconName: "play"
                                                enabled: !payloads.busy
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Run it now (port %1)").arg(fileRow.modelData.port)
                                                Accessible.name: ToolTip.text
                                                onClicked: payloads.sendFromConsole(fileRow.modelData.path, 0)
                                            }
                                            StyledToolButton {
                                                iconName: "download"
                                                enabled: !payloads.busy
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Download to this PC")
                                                Accessible.name: ToolTip.text
                                                onClicked: payloads.download(fileRow.modelData.path)
                                            }
                                            StyledToolButton {
                                                iconName: "pencil"
                                                enabled: !payloads.busy
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Rename")
                                                Accessible.name: ToolTip.text
                                                onClicked: renameDialog.ask(fileRow.modelData)
                                            }
                                            StyledToolButton {
                                                iconName: "trash"
                                                danger: true
                                                enabled: !payloads.busy
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Delete from the console…")
                                                Accessible.name: ToolTip.text
                                                onClicked: deleteDialog.ask(fileRow.modelData)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ── settings files: plugins.ini, autoload.txt, config.ini…
                Plate {
                    visible: root.online && payloads.configs.length > 0
                    Layout.fillWidth: true
                    implicitHeight: configColumn.implicitHeight + 36

                    ColumnLayout {
                        id: configColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 18
                        spacing: 8
                        SectionHead {
                            iconName: "settings"
                            title: qsTr("Settings files")
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: Theme.glassEdge
                        }
                        Repeater {
                            model: payloads.configs
                            RowLayout {
                                id: configRow
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.leftMargin: 8
                                spacing: 10
                                Icon {
                                    name: "file"
                                    size: 14
                                    color: Theme.textSecondary
                                }
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                    text: configRow.modelData.path
                                    color: configRow.modelData.exists ? Theme.ink : Theme.textSecondary
                                    font.pixelSize: 12
                                    font.family: Theme.fontMono
                                }
                                Tag {
                                    visible: !configRow.modelData.exists
                                    text: qsTr("not there yet")
                                }
                                StyledButton {
                                    chip: true
                                    iconName: "pencil"
                                    text: qsTr("Edit")
                                    enabled: !payloads.busy
                                    onClicked: payloads.loadText(configRow.modelData.path)
                                }
                            }
                        }
                    }
                }

                // ── what the payloads said: a log, so a sunken well
                Plate {
                    visible: payloads.output.length > 0
                    sunken: true
                    Layout.fillWidth: true
                    implicitHeight: outputColumn.implicitHeight + 32

                    ColumnLayout {
                        id: outputColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 16
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Icon { name: "terminal"; size: 14; color: Theme.brass }
                            Eyebrow {
                                Layout.fillWidth: true
                                text: qsTr("What the console answered")
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
                            lineHeight: 1.15
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
            spacing: 12

            Flow {
                visible: payloads.catalog.length > 0
                Layout.fillWidth: true
                spacing: 6
                FilterPill {
                    chosen: root.category === "all"
                    text: qsTr("All")
                    count: String(payloads.catalog.length)
                    onClicked: root.category = "all"
                }
                Repeater {
                    model: root.library ? root.categories() : []
                    FilterPill {
                        required property string modelData
                        chosen: root.category === modelData
                        text: modelData
                        onClicked: root.category = modelData
                    }
                }
                FilterPill {
                    readonly property int n: root.countWhere(function (p) { return p.installed })
                    visible: n > 0
                    iconName: "check"
                    chosen: root.category === "installed"
                    text: qsTr("Installed", "filter: the payloads on the console")
                    count: String(n)
                    onClicked: root.category = "installed"
                }
                FilterPill {
                    readonly property int n: root.countWhere(function (p) { return p.update })
                    visible: n > 0
                    iconName: "arrow-up"
                    chosen: root.category === "updates"
                    text: qsTr("Updates")
                    count: String(n)
                    onClicked: root.category = "updates"
                }
            }

            // Could not read the list (perhaps showing last time's).
            Rectangle {
                visible: payloads.catalogError.length > 0 && !payloads.catalogLoading
                Layout.fillWidth: true
                implicitHeight: catalogErrorRow.implicitHeight + 24
                radius: Theme.radius
                color: Theme.alpha(Theme.warn, 0.08)
                border.width: 1
                border.color: Theme.alpha(Theme.warn, 0.4)
                RowLayout {
                    id: catalogErrorRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 16
                    anchors.rightMargin: 12
                    spacing: 12
                    Icon { name: "warning"; size: 18; color: Theme.light ? Qt.darker(Theme.warn, 1.3) : Theme.warn }
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
            Plate {
                visible: payloads.output.length > 0
                sunken: true
                Layout.fillWidth: true
                Layout.rightMargin: 14
                implicitHeight: libraryOutput.implicitHeight + 22
                RowLayout {
                    id: libraryOutput
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 14
                    anchors.rightMargin: 10
                    spacing: 10
                    Icon { name: "terminal"; size: 14; color: Theme.brass; Layout.alignment: Qt.AlignTop; Layout.topMargin: 2 }
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

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Reading the list: the shape of the rows about to come.
                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    visible: payloads.catalogLoading && payloads.catalog.length === 0
                    spacing: 6
                    Repeater {
                        model: parent.visible ? 5 : 0
                        SkeletonRow {
                            required property int index
                            width: parent.width
                            delay: index * 80
                        }
                    }
                }

                // The payloads: a hairline list, the name in the serif.
                ListView {
                    id: libraryList
                    anchors.fill: parent
                    clip: true
                    spacing: 0
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    model: root.library ? root.shownPayloads() : []

                    ColumnLayout {
                        visible: libraryList.count === 0 && payloads.catalog.length > 0
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 32
                        spacing: 8
                        Sparkle {
                            Layout.alignment: Qt.AlignHCenter
                            size: 18
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("No payload here.")
                            color: Theme.ink
                            font.family: Theme.fontDisplay
                            font.pixelSize: Theme.fontHeading
                            font.weight: Font.DemiBold
                        }
                        StyledButton {
                            Layout.alignment: Qt.AlignHCenter
                            visible: root.category !== "all" || root.search.length > 0
                            text: qsTr("Show all")
                            onClicked: { root.category = "all"; root.search = "" }
                        }
                    }

                    delegate: Rectangle {
                        id: payloadCard
                        required property var modelData
                        required property int index
                        width: libraryList.width - 14
                        implicitHeight: payloadRow.implicitHeight + 28
                        radius: Theme.radiusSmall
                        color: modelData.update ? Theme.alpha(Theme.accent, cardHover.hovered ? 0.12 : 0.08)
                             : cardHover.hovered ? Theme.alpha(Theme.ink, 0.05) : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                        HoverHandler { id: cardHover }

                        // The hairline between payloads.
                        Rectangle {
                            visible: payloadCard.index < libraryList.count - 1
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            height: 1
                            color: Theme.glassEdge
                        }

                        RowLayout {
                            id: payloadRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 14
                            anchors.rightMargin: 8
                            spacing: 12

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        Layout.fillWidth: implicitWidth > width
                                        Layout.maximumWidth: implicitWidth
                                        elide: Text.ElideRight
                                        text: payloadCard.modelData.name
                                        color: Theme.ink
                                        font.family: Theme.fontDisplay
                                        font.pixelSize: 17
                                        font.weight: Font.DemiBold
                                    }
                                    Tag {
                                        visible: payloadCard.modelData.version.length > 0
                                        text: payloadCard.modelData.version
                                        mono: true
                                    }
                                    Tag {
                                        visible: payloadCard.modelData.installed
                                        tone: payloadCard.modelData.update ? "accent" : "ok"
                                        iconName: payloadCard.modelData.update ? "arrow-up" : "check"
                                        text: payloadCard.modelData.update
                                              ? qsTr("Update: %1 → %2").arg(payloadCard.modelData.installedVersion)
                                                .arg(payloadCard.modelData.version)
                                              : qsTr("Installed", "a payload on the console")
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: payloadCard.modelData.description
                                    color: Theme.text
                                    font.pixelSize: 12
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        text: payloadCard.modelData.category
                                        color: Theme.brass
                                        font.pixelSize: Theme.fontEyebrow
                                        font.weight: Font.Bold
                                        font.letterSpacing: Theme.eyebrowSpacing
                                        font.capitalization: Font.AllUppercase
                                    }
                                    Text {
                                        visible: payloadCard.modelData.lastUpdate.length > 0
                                        text: payloadCard.modelData.lastUpdate
                                        color: Theme.textSecondary
                                        font.family: Theme.fontMono
                                        font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: payloadCard.modelData.installed
                                        elide: Text.ElideRight
                                        text: qsTr("in %1").arg(root.copyLine(payloadCard.modelData))
                                        color: Theme.textSecondary
                                        font.pixelSize: 11
                                    }
                                    Item { Layout.fillWidth: true; visible: !payloadCard.modelData.installed }
                                }
                            }
                            StyledToolButton {
                                visible: payloadCard.modelData.source.length > 0
                                iconName: "external-link"
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Its page, at its author's")
                                Accessible.name: ToolTip.text
                                onClicked: Qt.openUrlExternally(payloadCard.modelData.source)
                            }
                            StyledToolButton {
                                visible: payloadCard.modelData.sendable
                                iconName: "play"
                                enabled: !payloads.busy && payloads.target.length > 0
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Run it now, without installing it (port %1)")
                                              .arg(payloadCard.modelData.port)
                                Accessible.name: ToolTip.text
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
        background: ShelfTile.MenuGlass { implicitWidth: 320 }
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
        background: ShelfTile.MenuGlass { implicitWidth: 280 }
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
        background: ShelfTile.MenuGlass { implicitWidth: 280 }
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
        background: ShelfTile.DialogGlass {}
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
        background: ShelfTile.DialogGlass {}
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
        background: ShelfTile.DialogGlass {}
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
        background: ShelfTile.DialogGlass {}
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
        background: ShelfTile.DialogGlass {}
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
