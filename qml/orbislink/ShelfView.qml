// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The games page's library (`shelf`): every place games can be, as the
// console shows its library and folders. Home has the places — the console's
// library, data/pkg and USB drives, this PC's USB drives, Desktop, Downloads,
// drives and folders added by hand — each with the covers of the first
// games in it. Opening one shows its folders (with their own covers) and its
// games as cards, the same look all the way down; the path above leads back.
// Arrow keys move, Enter opens, Escape or Backspace go back.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

FocusScope {
    id: view

    // The card picked (by the keys, or the last one clicked).
    property int current: 0
    // Coming back out of a folder, it is picked again.
    property var trail: []
    property string restoreKey: ""

    readonly property bool home: shelf.level === "home"
    readonly property int tile: home ? 176 : 148
    readonly property int cellWidth: tile + 34
    readonly property int cellHeight: tile + 74

    function sectionLabel(section) {
        switch (section) {
        case "favorites": return qsTr("Favourites")
        case "console": return qsTr("Console")
        case "usb": return qsTr("USB drives")
        case "pc": return qsTr("This PC")
        case "custom": return qsTr("Your folders")
        case "places": return qsTr("Places")
        case "folders": return qsTr("Folders")
        case "games": return qsTr("Games and apps")
        case "extras": return qsTr("Updates and add-ons")
        case "payloads": return qsTr("Payloads")
        }
        return qsTr("Other files")
    }
    function actionLabel(action, card) {
        switch (action) {
        case "open": return qsTr("Open")
        case "install": return card.where === "console" ? qsTr("Install") : qsTr("Install on the console")
        case "send": return qsTr("Send to the console")
        case "sendFile": return qsTr("Send the disc file")
        case "convert": return qsTr("Convert and install")
        case "convertOnly": return card.where === "console" ? qsTr("Only convert (the package stays in this folder)")
                                                            : qsTr("Only convert")
        case "mount": return qsTr("Put on the home screen")
        case "run": return qsTr("Run")
        case "runPayload": return qsTr("Run on the console")
        case "reveal": return qsTr("Show in its folder")
        case "delete": return qsTr("Delete from the console…")
        }
        return action
    }
    function actionHint(action, card) {
        switch (action) {
        case "install": return card.where === "console" ? qsTr("The console installs it from where it is")
                                                        : qsTr("The console downloads it from this PC and installs it")
        case "send": return qsTr("Copied over FTP to the console's upload folder")
        case "sendFile": return qsTr("The disc image itself, copied over FTP")
        case "convert": return qsTr("Made into a PS4 package on this PC, sent and installed")
        case "convertOnly": return card.where === "console"
                                   ? qsTr("Made into a PS4 package on this PC and put back beside the disc")
                                   : qsTr("Made into a PS4 package, kept in the output folder")
        case "mount": return qsTr("Moved into the folder ShadowMountPlus mounts from, on the same drive")
        case "run": return qsTr("Sent to the console's loader now")
        case "runPayload": return qsTr("Sent from this PC to the console's loader now")
        }
        return ""
    }
    function actionIcon(action) {
        switch (action) {
        case "open": return "folder-open"
        case "install": return "download"
        case "send": case "sendFile": return "upload"
        case "convert": return "disc"
        case "convertOnly": return "package"
        case "mount": return "home"
        case "run": case "runPayload": return "play"
        case "reveal": return "external-link"
        case "delete": return "trash"
        }
        return ""
    }
    function needsConsole(action, card) {
        if (card.where === "console")
            return action !== "reveal"
        return action === "install" || action === "send" || action === "sendFile"
               || action === "convert" || action === "runPayload"
    }
    function kindText(card) {
        if (card.app) return card.platform === "ps4" ? qsTr("PS4 app or game folder") : qsTr("PS5 app or game folder")
        switch (card.kind) {
        case "package": return card.category === "gp" || card.category === "gpd" ? qsTr("PS4 update")
                             : card.category === "ac" ? qsTr("PS4 add-on")
                             : card.category === "gdt" ? qsTr("PS4 theme") : qsTr("PS4 package")
        case "disc": return card.platform === "ps2" ? qsTr("PS2 disc") : card.platform === "ps1" ? qsTr("PS1 disc")
                                                                                                : qsTr("Disc image")
        case "image": return qsTr("PS5 game image")
        case "payload": return qsTr("Payload")
        case "archive": return qsTr("Archive")
        case "folder": return qsTr("Folder")
        }
        return qsTr("File")
    }

    function select(index) {
        if (shelf.count === 0) {
            current = 0
            return
        }
        current = Math.max(0, Math.min(shelf.count - 1, index))
        list.positionViewAtIndex(shelf.rowOf(current), ListView.Contain)
    }
    function selectKey(key) {
        for (var i = 0; i < shelf.count; ++i)
            if (shelf.card(i).key === key) {
                select(i)
                return true
            }
        return false
    }
    function activate(index) {
        var card = shelf.card(index)
        if (!card.card)
            return
        select(index)
        list.forceActiveFocus()
        if (card.card === "source" || card.card === "folder") {
            var keys = trail.slice()
            keys.push(card.key)
            trail = keys
            shelf.openCard(index)
        } else {
            details.show(index)
        }
    }
    function goBack() {
        if (home)
            return false
        var keys = trail.slice()
        restoreKey = keys.length > 0 ? keys.pop() : ""
        trail = keys
        shelf.back()
        return true
    }

    Binding { target: shelf; property: "columns"; value: Math.max(1, Math.floor(list.width / view.cellWidth)) }

    Connections {
        target: shelf
        function onPlaceChanged() {
            view.current = 0
            list.positionViewAtBeginning()
            enter.restart()
            if (shelf.level === "home" && view.restoreKey.length === 0)
                view.trail = []
        }
        function onViewChanged() {
            if (view.restoreKey.length > 0 && view.selectKey(view.restoreKey))
                view.restoreKey = ""
            else if (view.current >= shelf.count)
                view.current = Math.max(0, shelf.count - 1)
        }
        function onNotice(message, error) { toast.show(message, error) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        // ── where it is, and the tools
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            StyledToolButton {
                visible: !view.home
                iconName: "chevron-left"
                Layout.alignment: Qt.AlignVCenter
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Back (Esc)")
                onClicked: view.goBack()
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: shelf.title
                    color: Theme.text
                    font.pixelSize: 17
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Flow {
                    Layout.fillWidth: true
                    visible: !view.home
                    spacing: 4
                    Repeater {
                        model: shelf.crumbs
                        Row {
                            id: crumbRow
                            required property var modelData
                            required property int index
                            spacing: 4
                            Text {
                                text: "›"
                                visible: crumbRow.index > 0
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
                            Text {
                                id: crumb
                                readonly property bool last: crumbRow.index === shelf.crumbs.length - 1
                                text: crumbRow.modelData.title
                                color: last ? Theme.textSecondary : crumbArea.containsMouse ? Theme.accentHover : Theme.accent
                                font.pixelSize: 12
                                MouseArea {
                                    id: crumbArea
                                    anchors.fill: parent
                                    enabled: !crumb.last
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        view.trail = view.trail.slice(0, Math.max(0, crumbRow.index - 1))
                                        shelf.goTo(crumbRow.index)
                                    }
                                }
                            }
                        }
                    }
                }
            }
            StyledField {
                id: search
                Layout.preferredWidth: 220
                placeholderText: view.home || shelf.level === "search" ? qsTr("Search every place") : qsTr("Search here")
                text: shelf.query
                onTextEdited: shelf.query = text
                Keys.onEscapePressed: {
                    text = ""
                    shelf.query = ""
                    list.forceActiveFocus()
                }
                Keys.onDownPressed: list.forceActiveFocus()
            }
            StyledButton {
                visible: !view.home
                chip: true
                iconName: "filter"
                text: shelf.filter === "games" ? qsTr("Games") : shelf.filter === "pkg" ? qsTr("PKG files")
                    : shelf.filter === "folders" ? qsTr("Folders") : shelf.filter === "other" ? qsTr("Other")
                    : qsTr("All")
                onClicked: filterMenu.popup(this, 0, height + 4)
            }
            StyledButton {
                visible: !view.home
                chip: true
                iconName: "arrow-up-down"
                text: shelf.sort === "name" ? qsTr("Name") : shelf.sort === "date" ? qsTr("Date") : qsTr("Type")
                onClicked: sortMenu.popup(this, 0, height + 4)
            }
            StyledToolButton {
                iconName: shelf.loading ? "loader" : "refresh"
                enabled: !shelf.loading
                ToolTip.visible: hovered
                ToolTip.text: view.home ? qsTr("Read every place again") : qsTr("Read this folder again")
                onClicked: shelf.refresh()
            }
            StyledToolButton {
                iconName: "folder-plus"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Add a place to the library")
                onClicked: addMenu.popup(this, 0, height + 4)
            }
            StyledToolButton {
                iconName: "sliders"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Manage the places")
                onClicked: places.open()
            }
        }

        // ── the cards, in rows
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                // Room for the picked card's zoom and frame.
                anchors.leftMargin: -6
                anchors.rightMargin: -6
                topMargin: 6
                bottomMargin: 16
                clip: true
                focus: true
                model: shelf.rows
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                activeFocusOnTab: true

                transform: Translate { id: slide }
                SequentialAnimation {
                    id: enter
                    ParallelAnimation {
                        NumberAnimation { target: list; property: "opacity"; from: 0.0; to: 1.0; duration: Theme.normal; easing.type: Theme.easeOut }
                        NumberAnimation { target: slide; property: "x"; from: 18; to: 0; duration: Theme.normal; easing.type: Theme.easeOut }
                    }
                }

                Keys.onPressed: (event) => {
                    var handled = true
                    switch (event.key) {
                    case Qt.Key_Left: view.select(view.current - 1); break
                    case Qt.Key_Right: view.select(view.current + 1); break
                    case Qt.Key_Up: view.select(shelf.neighbour(view.current, -1)); break
                    case Qt.Key_Down: view.select(shelf.neighbour(view.current, 1)); break
                    case Qt.Key_Home: view.select(0); break
                    case Qt.Key_End: view.select(shelf.count - 1); break
                    case Qt.Key_Return:
                    case Qt.Key_Enter:
                    case Qt.Key_Space: view.activate(view.current); break
                    case Qt.Key_Escape:
                    case Qt.Key_Backspace: handled = view.goBack(); break
                    default:
                        if (event.matches(StandardKey.Find)) {
                            search.forceActiveFocus()
                            search.selectAll()
                        } else
                            handled = false
                    }
                    event.accepted = handled
                }

                delegate: Item {
                    id: row
                    required property string header
                    required property int count
                    required property var cards
                    width: list.width
                    height: header.length > 0 ? 38 : view.cellHeight

                    // A section's heading: its name, how many, a line.
                    RowLayout {
                        visible: row.header.length > 0
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        anchors.bottomMargin: 6
                        spacing: 8
                        Text {
                            text: view.sectionLabel(row.header)
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        Rectangle {
                            implicitHeight: 18
                            implicitWidth: sectionCount.implicitWidth + 12
                            radius: 9
                            color: Theme.controlFill
                            Text {
                                id: sectionCount
                                anchors.centerIn: parent
                                text: row.count
                                color: Theme.textSecondary
                                font.pixelSize: 11
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: Theme.glassEdge
                        }
                    }

                    Row {
                        visible: row.header.length === 0
                        x: 6
                        y: 8
                        Repeater {
                            model: row.header.length === 0 ? row.cards : []
                            delegate: Item {
                                id: cell
                                required property var modelData
                                width: view.cellWidth
                                height: view.cellHeight
                                readonly property bool picked: view.current === modelData.index

                                SourceCard {
                                    visible: cell.modelData.card === "source"
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    artSize: view.tile
                                    info: visible ? cell.modelData : ({})
                                    selected: cell.picked
                                    onActivated: view.activate(cell.modelData.index)
                                }
                                FolderCard {
                                    visible: cell.modelData.card === "folder"
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    artSize: view.tile
                                    info: visible ? cell.modelData : ({})
                                    selected: cell.picked
                                    onActivated: view.activate(cell.modelData.index)
                                }
                                TitleCard {
                                    visible: cell.modelData.card === "title"
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    artSize: view.tile
                                    info: visible ? cell.modelData : ({})
                                    selected: cell.picked
                                    onActivated: view.activate(cell.modelData.index)
                                }
                            }
                        }
                    }
                }
            }

            // ── nothing to show, and why
            Column {
                anchors.centerIn: parent
                width: Math.min(460, parent.width - 32)
                visible: shelf.count === 0
                spacing: 12
                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: shelf.loading
                    running: shelf.loading
                }
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !shelf.loading
                    name: shelf.gone || shelf.message.length > 0 ? "warning"
                        : shelf.level === "search" ? "search" : view.home ? "folder-plus" : "folder-open"
                    size: 44
                    color: Theme.alpha(Theme.textSecondary, 0.7)
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    text: shelf.loading ? qsTr("Reading…")
                        : shelf.message.length > 0 ? shelf.message
                        : shelf.level === "search" ? qsTr("Nothing found for “%1” in the places searched.").arg(shelf.query)
                        : view.home ? qsTr("No places yet: add a folder of this PC, a drive or a folder of the console.")
                        : shelf.total > 0 ? qsTr("Nothing here with this filter or search.")
                        : qsTr("Nothing here the library shows: no games, apps, folders or payloads.")
                }
                StyledButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: shelf.gone
                    text: qsTr("Back to the library")
                    iconName: "home"
                    onClicked: {
                        view.trail = []
                        shelf.home()
                    }
                }
            }
        }
    }

    // ── menus
    Menu {
        id: filterMenu
        topPadding: 8
        bottomPadding: 8
        background: Rectangle {
            implicitWidth: 220
            color: Theme.menuFill
            border.color: Theme.glassEdge
            border.width: 1
            radius: 14
        }
        Instantiator {
            model: [["all", qsTr("All")], ["games", qsTr("Games found")], ["pkg", qsTr("PKG files")],
                    ["folders", qsTr("Folders")], ["other", qsTr("Other supported files")]]
            delegate: StyledMenuItem {
                required property var modelData
                text: modelData[1]
                iconName: shelf.filter === modelData[0] ? "check" : ""
                onTriggered: shelf.filter = modelData[0]
            }
            onObjectAdded: (index, object) => filterMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => filterMenu.removeItem(object)
        }
    }
    Menu {
        id: sortMenu
        topPadding: 8
        bottomPadding: 8
        background: Rectangle {
            implicitWidth: 220
            color: Theme.menuFill
            border.color: Theme.glassEdge
            border.width: 1
            radius: 14
        }
        Instantiator {
            model: [["type", qsTr("By type (in sections)")], ["name", qsTr("By name")], ["date", qsTr("Newest first")]]
            delegate: StyledMenuItem {
                required property var modelData
                text: modelData[1]
                iconName: shelf.sort === modelData[0] ? "check" : ""
                onTriggered: shelf.sort = modelData[0]
            }
            onObjectAdded: (index, object) => sortMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => sortMenu.removeItem(object)
        }
    }
    Menu {
        id: addMenu
        topPadding: 8
        bottomPadding: 8
        function has(id) {
            for (var i = 0; i < shelf.locations.length; ++i)
                if (shelf.locations[i].id === id)
                    return true
            return false
        }
        background: Rectangle {
            implicitWidth: 300
            color: Theme.menuFill
            border.color: Theme.glassEdge
            border.width: 1
            radius: 14
        }
        StyledMenuItem {
            text: qsTr("A folder of this PC…")
            iconName: "folder-open"
            onTriggered: folderDialog.open()
        }
        StyledMenuItem {
            text: qsTr("A folder of the console…")
            iconName: "gamepad"
            enabled: app.consoleName.length > 0
            onTriggered: consoleFolder.open()
        }
        MenuSeparator {
            padding: 6
            leftPadding: 16
            rightPadding: 16
            contentItem: Rectangle { implicitHeight: 1; color: Theme.glassEdge }
        }
        StyledMenuItem {
            text: qsTr("Desktop")
            iconName: "laptop"
            enabled: !addMenu.has("preset-desktop")
            onTriggered: shelf.addPreset("desktop")
        }
        StyledMenuItem {
            text: qsTr("Downloads")
            iconName: "download"
            enabled: !addMenu.has("preset-downloads")
            onTriggered: shelf.addPreset("downloads")
        }
        StyledMenuItem {
            text: qsTr("Documents")
            iconName: "file"
            enabled: !addMenu.has("preset-documents")
            onTriggered: shelf.addPreset("documents")
        }
        StyledMenuItem {
            text: qsTr("The console's data/pkg")
            iconName: "package"
            enabled: !addMenu.has("preset-console-pkg") && app.consoleName.length > 0
            onTriggered: shelf.addPreset("console-pkg")
        }
        MenuSeparator {
            padding: 6
            leftPadding: 16
            rightPadding: 16
            contentItem: Rectangle { implicitHeight: 1; color: Theme.glassEdge }
        }
        Instantiator {
            model: shelf.volumes
            delegate: StyledMenuItem {
                required property var modelData
                text: qsTr("Drive %1").arg(modelData.name)
                iconName: modelData.removable ? "usb" : "hard-drive"
                onTriggered: shelf.addPlace(modelData.path, "pc")
            }
            onObjectAdded: (index, object) => addMenu.addItem(object)
            onObjectRemoved: (index, object) => addMenu.removeItem(object)
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("A folder for the library")
        onAccepted: shelf.addFolder(selectedFolder)
    }

    // ── a console folder, by its path
    Dialog {
        id: consoleFolder
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
        onOpened: {
            consolePath.text = "/data/"
            consolePath.forceActiveFocus()
        }
        header: DialogHeader {
            title: qsTr("A folder of the console")
            dialog: consoleFolder
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 13
                text: qsTr("Its path on the console, as the Files tab shows it: /data/games, /mnt/usb0/games…")
            }
            StyledField {
                id: consolePath
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                font.family: Theme.fontMono
                onAccepted: {
                    shelf.addPlace(text, "console")
                    consoleFolder.close()
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
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancel")
                    onClicked: consoleFolder.close()
                }
                StyledButton {
                    text: qsTr("Add")
                    primary: true
                    enabled: consolePath.text.trim().length > 1
                    onClicked: {
                        shelf.addPlace(consolePath.text, "console")
                        consoleFolder.close()
                    }
                }
            }
        }
    }

    // ── a game's details, and what can be done with it
    Dialog {
        id: details
        property int index: -1
        property var card: ({})
        function show(i) {
            index = i
            card = shelf.card(i)
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(640, parent.width - 32)
        modal: true
        padding: 0
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }
        header: DialogHeader {
            title: details.card.title || ""
            dialog: details
        }
        contentItem: RowLayout {
            spacing: 22
            CoverArt {
                Layout.leftMargin: Theme.dialogMargin
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: 200
                Layout.preferredHeight: 200
                picture: details.card.picture || ""
                placeholder: details.card.placeholder || "bd"
                label: details.card.platform ? details.card.platform.toUpperCase()
                                             : details.card.ext ? details.card.ext.substring(1).toUpperCase() : ""
                radius: 8
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.rightMargin: Theme.dialogMargin
                Layout.alignment: Qt.AlignTop
                spacing: 8
                RowLayout {
                    spacing: 8
                    Text {
                        text: view.kindText(details.card)
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        visible: details.card.installed === true
                        implicitHeight: 20
                        implicitWidth: installedLabel.implicitWidth + 16
                        radius: 10
                        color: Theme.alpha(Theme.ok, 0.16)
                        Text {
                            id: installedLabel
                            anchors.centerIn: parent
                            text: qsTr("Installed")
                            color: Theme.ok
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 4
                    Repeater {
                        model: [
                            [qsTr("Title ID"), details.card.titleId || ""],
                            [qsTr("Serial"), details.card.serial || ""],
                            [qsTr("Version"), details.card.version || ""],
                            [qsTr("Size"), details.card.sizeText || ""],
                            [qsTr("Changed"), details.card.dateText || ""],
                            [qsTr("Where"), details.card.sourceName || ""],
                            [qsTr("File"), details.card.name || ""]
                        ].filter(function (pair) { return pair[1].length > 0 })
                        delegate: Item {
                            id: pair
                            required property var modelData
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            implicitHeight: pairRow.implicitHeight
                            RowLayout {
                                id: pairRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                spacing: 12
                                Text {
                                    Layout.preferredWidth: 72
                                    text: pair.modelData[0]
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: pair.modelData[1]
                                    color: Theme.text
                                    font.pixelSize: 12
                                    elide: Text.ElideMiddle
                                }
                            }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: details.card.path || ""
                    color: Theme.textMuted
                    font.pixelSize: 11
                    font.family: Theme.fontMono
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
                Text {
                    visible: (details.card.note || "").length > 0
                    Layout.fillWidth: true
                    text: details.card.note || ""
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: (details.card.actions || []).some(function (a) { return view.needsConsole(a, details.card) })
                             && !app.canUseFtp
                    Layout.fillWidth: true
                    text: qsTr("Connect a console to send, install or convert: its FTP is not answering.")
                    color: Theme.warn
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }
        footer: Item {
            implicitHeight: actions.implicitHeight + Theme.dialogInner * 2
            Flow {
                id: actions
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 8
                layoutDirection: Qt.RightToLeft
                Repeater {
                    model: details.card.actions || []
                    StyledButton {
                        required property string modelData
                        readonly property var all: details.card.actions || []
                        text: view.actionLabel(modelData, details.card)
                        iconName: view.actionIcon(modelData)
                        danger: modelData === "delete"
                        primary: modelData === all[0] && modelData !== "delete" && modelData !== "reveal"
                        enabled: !view.needsConsole(modelData, details.card) || app.canUseFtp
                                 || (modelData === "install" && details.card.where !== "console" && app.canInstallDirectly)
                        ToolTip.visible: hovered && view.actionHint(modelData, details.card).length > 0
                        ToolTip.text: view.actionHint(modelData, details.card)
                        onClicked: {
                            if (modelData === "delete") {
                                removal.ask(details.index, details.card)
                                return
                            }
                            shelf.act(details.index, modelData)
                            details.close()
                        }
                    }
                }
            }
        }
        onClosed: list.forceActiveFocus()
    }

    Dialog {
        id: removal
        property int index: -1
        property var card: ({ name: "" })
        function ask(i, c) {
            index = i
            card = c
            open()
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
            title: qsTr("Delete %1 from the console?").arg(removal.card.name)
            dialog: removal
        }
        contentItem: Text {
            leftPadding: Theme.dialogMargin
            rightPadding: Theme.dialogMargin
            topPadding: 6
            bottomPadding: 6
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: 13
            text: qsTr("The file goes from the console. What was installed from it stays installed.")
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
                    onClicked: removal.close()
                }
                StyledButton {
                    text: qsTr("Delete")
                    iconName: "trash"
                    danger: true
                    solid: true
                    onClicked: {
                        shelf.act(removal.index, "delete")
                        removal.close()
                        details.close()
                    }
                }
            }
        }
    }

    // ── the places, managed
    Dialog {
        id: places
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(720, parent.width - 32)
        height: Math.min(560, parent.height - 32)
        modal: true
        padding: 0
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }
        header: DialogHeader {
            title: qsTr("Places in the library")
            dialog: places
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 12
                text: qsTr("Renaming or taking a place off the library never touches the folder itself. "
                           + "USB drives and the console's own library show up by themselves.")
            }
            ListView {
                id: placeList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin - 8
                clip: true
                spacing: 8
                model: shelf.locations
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    id: placeCard
                    required property var modelData
                    width: placeList.width - 8
                    implicitHeight: placeRow.implicitHeight + 20
                    radius: 14
                    color: Theme.panelAltFill
                    border.width: 1
                    border.color: Theme.border
                    RowLayout {
                        id: placeRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10
                        StyledToolButton {
                            iconName: "star"
                            active: placeCard.modelData.favorite
                            ToolTip.visible: hovered
                            ToolTip.text: placeCard.modelData.favorite ? qsTr("Favourite: shown first")
                                                                          : qsTr("Make it a favourite")
                            onClicked: shelf.setFavorite(placeCard.modelData.id, !placeCard.modelData.favorite)
                        }
                        Icon {
                            name: placeCard.modelData.where === "console" ? "gamepad"
                                : placeCard.modelData.kind === "drive" ? "hard-drive"
                                : placeCard.modelData.kind === "usb" ? "usb"
                                : placeCard.modelData.kind === "pc" ? "laptop" : "folder"
                            size: 18
                            color: Theme.textSecondary
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            StyledField {
                                Layout.fillWidth: true
                                text: placeCard.modelData.name || ""
                                placeholderText: placeCard.modelData.shownName
                                onEditingFinished: shelf.renameLocation(placeCard.modelData.id, text)
                            }
                            StyledField {
                                visible: placeCard.modelData.where === "console"
                                Layout.fillWidth: true
                                text: placeCard.modelData.path
                                font.family: Theme.fontMono
                                onEditingFinished: if (text !== placeCard.modelData.path)
                                                       shelf.setLocationPath(placeCard.modelData.id, text)
                            }
                            Text {
                                visible: placeCard.modelData.where !== "console"
                                Layout.fillWidth: true
                                text: placeCard.modelData.path
                                      + (placeCard.modelData.available ? "" : "  ·  " + qsTr("not found"))
                                color: Theme.textMuted
                                font.pixelSize: 11
                                font.family: Theme.fontMono
                                elide: Text.ElideMiddle
                            }
                        }
                        StyledCheck {
                            text: qsTr("Search")
                            checked: placeCard.modelData.inSearch
                            onToggled: shelf.setInSearch(placeCard.modelData.id, checked)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Looked in by the library's search")
                        }
                        StyledToolButton {
                            iconName: "trash"
                            danger: true
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Take it off the library (the folder stays)")
                            onClicked: shelf.removeLocation(placeCard.modelData.id)
                        }
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
                StyledButton {
                    id: addPlace
                    text: qsTr("Add a place")
                    iconName: "folder-plus"
                    onClicked: addMenu.popup(addPlace, 0, -addMenu.implicitHeight - 4)
                }
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Done")
                    primary: true
                    onClicked: places.close()
                }
            }
        }
    }
}
