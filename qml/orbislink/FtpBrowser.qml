// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    // The uploads going into the folder on screen: they show in the list
    // right away, with how far they are (a percentage and a thin bar), and
    // become normal rows once they arrive (the list refreshes itself then).
    readonly property string shownDirectory: {
        var path = app.ftpPath
        while (path.length > 1 && path.charAt(path.length - 1) === "/")
            path = path.substring(0, path.length - 1)
        return path
    }
    readonly property var uploadsHere: app.ftpUploads.filter(function (upload) {
        return upload.directory === root.shownDirectory
    })
    // The same folder, with or without the trailing slash.
    function samePath(a, b) {
        var strip = function (p) { return p.length > 1 && p.endsWith("/") ? p.slice(0, -1) : p }
        return strip(a) === strip(b)
    }
    // Re-read whenever the pinned list changes (isFtpPinned is a call).
    readonly property int pinnedVersion: app.ftpShortcuts.length
    readonly property bool pinnedHere: {
        for (var i = 0; i < app.ftpShortcuts.length; ++i)
            if (samePath(app.ftpShortcuts[i], app.ftpPath))
                return true
        return false
    }

    // The upload of a file of this folder still on its way, or null.
    function uploadOf(name) {
        for (var i = 0; i < uploadsHere.length; ++i)
            if (uploadsHere[i].name === name)
                return uploadsHere[i]
        return null
    }

    // Several rows picked — with their round mark, Ctrl+click or Shift+click
    // — to download, move or delete together: path → true. Another folder
    // starts with none.
    property var picked: ({})
    readonly property int pickedCount: Object.keys(picked).length
    readonly property bool picking: pickedCount > 0
    property int pickAnchor: -1
    function togglePick(index, path) {
        var next = Object.assign({}, picked)
        if (next[path])
            delete next[path]
        else
            next[path] = true
        picked = next
        pickAnchor = index
    }
    function pickRange(index) {
        var from = pickAnchor < 0 ? index : Math.min(pickAnchor, index)
        var to = pickAnchor < 0 ? index : Math.max(pickAnchor, index)
        var next = Object.assign({}, picked)
        for (var i = from; i <= to; ++i) {
            var row = app.files.get(i)
            if (row.path)
                next[row.path] = true
        }
        picked = next
        pickAnchor = index
    }
    function pickAll() {
        var next = {}
        for (var i = 0; i < app.files.count; ++i) {
            var row = app.files.get(i)
            if (row.path)
                next[row.path] = true
        }
        picked = next
    }
    function clearPicks() {
        picked = ({})
        pickAnchor = -1
    }
    // The rows picked, as the list has them now.
    function pickedRows() {
        var rows = []
        for (var i = 0; i < app.files.count; ++i) {
            var row = app.files.get(i)
            if (picked[row.path] === true)
                rows.push(row)
        }
        return rows
    }
    Connections {
        target: app
        function onFtpPathChanged() { root.clearPicks() }
    }
    // What left the list (deleted, moved) is not picked any more.
    Connections {
        target: app.files
        function onModelReset() {
            if (!root.picking)
                return
            var still = {}
            for (var i = 0; i < app.files.count; ++i) {
                var path = app.files.get(i).path
                if (root.picked[path] === true)
                    still[path] = true
            }
            root.picked = still
        }
    }

    // Files and folders to the PC: each one a card in the queue, where it
    // can be followed, stopped and taken up again (empty destination: the
    // desktop).
    function download(rows, destination) {
        if (rows.length === 0)
            return
        consoleLibrary.download(rows, destination)
        notice.show(qsTr("%n item(s) added to the queue.", "", rows.length))
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
                selectionColor: Theme.alpha(Theme.accent, 0.35)
                selectedTextColor: Theme.text
                background: Rectangle {
                    color: Theme.controlFill
                    border.color: pathField.activeFocus ? Theme.accent : Theme.border
                    radius: 10
                }
                onAccepted: app.ftpNavigate(text)
            }
            // While the folder is being read, its icon turns: the rows stay
            // where they are, with nothing over them.
            StyledToolButton {
                iconName: "refresh"
                iconSize: 16
                implicitWidth: 32
                implicitHeight: 32
                spinning: app.ftpBusy
                onClicked: app.ftpRefresh()
                enabled: !app.ftpBusy
            }
        }

        // Pinned folders, as tags: a click opens one, its ✕ unpins it; the
        // last tag pins the folder that is open.
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: app.ftpShortcuts
                delegate: Rectangle {
                    id: tag
                    required property string modelData
                    readonly property bool here: root.samePath(modelData, app.ftpPath)
                    implicitWidth: tagRow.implicitWidth + 22
                    implicitHeight: 30
                    radius: height / 2
                    color: here ? Theme.alpha(Theme.accent, 0.16)
                         : tagHover.hovered ? Theme.controlHover : Theme.controlFill
                    border.width: 1
                    border.color: here || tagHover.hovered ? Theme.alpha(Theme.accent, 0.6) : Theme.glassEdge
                    Behavior on color { ColorAnimation { duration: Theme.fast } }

                    HoverHandler { id: tagHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: app.ftpNavigate(tag.modelData) }

                    Row {
                        id: tagRow
                        anchors.verticalCenter: parent.verticalCenter
                        x: 11
                        spacing: 6
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "folder"
                            size: 13
                            color: tag.here ? Theme.accent : Theme.textSecondary
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: tag.modelData
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                        // Unpin: a small round ✕, clearer once the tag is under the mouse.
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            radius: 8
                            color: unpinHover.hovered ? Theme.alpha(Theme.error, 0.22) : "transparent"
                            opacity: tagHover.hovered ? 1.0 : 0.45
                            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                            Icon {
                                anchors.centerIn: parent
                                name: "close"
                                size: 11
                                color: unpinHover.hovered ? Theme.error : Theme.textSecondary
                            }
                            HoverHandler { id: unpinHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: app.unpinFtpFolder(tag.modelData) }
                            ToolTip.visible: unpinHover.hovered
                            ToolTip.delay: 400
                            ToolTip.text: qsTr("Remove from the top")
                        }
                    }
                }
            }
            StyledButton {
                chip: true
                iconName: "pin"
                text: qsTr("Pin this folder")
                visible: app.ftpPath.length > 0 && !root.pinnedHere
                onClicked: app.pinFtpFolder(app.ftpPath)
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
                    // A file still on its way here: how far it is.
                    readonly property var upload: root.uploadOf(model.name)
                    readonly property bool arriving: upload !== null
                    // Being deleted: the row says so where it is.
                    readonly property bool deleting: app.ftpDeleting.indexOf(model.path) >= 0
                    readonly property bool picked: root.picked[model.path] === true
                    height: 34
                    opacity: deleting ? 0.55 : 1.0
                    Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                    // The Basic style pads 12px all round, which in a 34px row
                    // squeezes the content into a sliver and leaves it off the
                    // lit box. The row sets its own.
                    topPadding: 0
                    bottomPadding: 0
                    leftPadding: 8
                    rightPadding: 6

                    // The row under the mouse lights up, a picked one stays lit;
                    // an upload draws its progress along the bottom.
                    background: Rectangle {
                        radius: 8
                        color: row.picked ? Theme.alpha(Theme.accent, 0.14)
                             : row.hovered ? Theme.controlHover : "transparent"
                        border.width: row.picked ? 1 : 0
                        border.color: Theme.alpha(Theme.accent, 0.45)
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                        Rectangle {
                            visible: row.arriving
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: 8
                            anchors.bottomMargin: 2
                            height: 2
                            radius: 1
                            width: (parent.width - 16) * Math.max(0, Math.min(1, (row.upload ? row.upload.percent : 0) / 100))
                            color: Theme.accent
                            Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
                        }
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

                        // The round mark that picks the row: under the mouse,
                        // and on every row while some are picked.
                        Rectangle {
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            radius: 9
                            opacity: root.picking || row.hovered ? 1.0 : 0.0
                            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                            color: row.picked ? Theme.accent : "transparent"
                            border.width: 1.5
                            border.color: row.picked ? Theme.accent : Theme.alpha(Theme.textSecondary, 0.7)
                            Icon {
                                anchors.centerIn: parent
                                visible: row.picked
                                name: "check"
                                size: 11
                                strokeWidth: 3
                                color: "#FFFFFF"
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: root.togglePick(index, model.path) }
                        }

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
                                // what can be done with it. With Ctrl it picks
                                // the row, with Shift every row up to it; while
                                // some are picked, a click picks too.
                                onClicked: (mouse) => {
                                    if (dragged)
                                        return
                                    if (mouse.button === Qt.LeftButton) {
                                        if (mouse.modifiers & Qt.ShiftModifier) {
                                            root.pickRange(index)
                                            return
                                        }
                                        if ((mouse.modifiers & Qt.ControlModifier) || root.picking) {
                                            root.togglePick(index, model.path)
                                            return
                                        }
                                    }
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
                            text: row.deleting ? qsTr("deleting…")
                                : row.preparing ? qsTr("getting it…")
                                : row.arriving ? (row.upload.sending ? Math.floor(row.upload.percent) + "%" : qsTr("waiting"))
                                : model.sizeText
                            color: row.deleting ? Theme.error
                                 : row.preparing || row.arriving ? Theme.accent : Theme.textMuted
                            font.pixelSize: 11
                            font.weight: row.arriving ? Font.DemiBold : Font.Normal
                        }
                        StyledToolButton {
                            iconName: "more"
                            iconSize: 14
                            implicitWidth: 26; implicitHeight: 26
                            enabled: !row.deleting
                            onClicked: rowMenu.popupFor(model.path, model.name, model.isDirectory, model.size, model.sizeText, row)
                        }
                        // Deleting: the row's own wheel, where its bin was.
                        Item {
                            implicitWidth: 26
                            implicitHeight: 26
                            Spinner {
                                anchors.centerIn: parent
                                visible: row.deleting
                                size: 14
                                color: Theme.error
                            }
                            StyledToolButton {
                                anchors.fill: parent
                                visible: !row.deleting
                                iconName: "trash"
                                iconSize: 14
                                danger: true
                                onClicked: confirmDelete.openRows([{ path: model.path, name: model.name,
                                                                     isDirectory: model.isDirectory }])
                            }
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

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    visible: app.ftpBusy && files.count === 0
                    Repeater {
                        model: parent.visible ? 8 : 0
                        SkeletonRow {
                            required property int index
                            width: parent.width
                            compact: true
                            delay: index * 60
                        }
                    }
                }
            }

            // A word that something went to the queue, for a moment.
            Rectangle {
                id: notice
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                width: Math.min(parent.width - 24, noticeText.implicitWidth + 28)
                height: 30
                radius: 15
                color: Theme.menuFill
                border.width: 1
                border.color: Theme.alpha(Theme.accent, 0.5)
                opacity: 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: Theme.normal } }
                function show(text) {
                    noticeText.text = text
                    opacity = 1
                    noticeTimer.restart()
                }
                Text {
                    id: noticeText
                    anchors.centerIn: parent
                    width: parent.width - 20
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    color: Theme.text
                    font.pixelSize: 12
                }
                Timer {
                    id: noticeTimer
                    interval: 2600
                    onTriggered: notice.opacity = 0
                }
            }
        }

        // What the folder holds, and the room left on its disk when the
        // console's server says.
        Text {
            Layout.fillWidth: true
            readonly property var space: app.ftpSpace
            visible: files.count > 0
            text: {
                var parts = []
                if ((space.folders || 0) > 0)
                    parts.push(qsTr("%n folder(s)", "", space.folders))
                if ((space.files || 0) > 0)
                    parts.push(qsTr("%n file(s)", "", space.files) + " · " + app.queue.bytesText(space.bytes || 0))
                if (space.free !== undefined && space.free >= 0)
                    parts.push(qsTr("%1 free on this disk").arg(app.queue.bytesText(space.free)))
                return parts.join("  ·  ")
            }
            color: Theme.textMuted
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        // A move between two disks of the console (through this PC), or a
        // file brought here to be dragged out: one at a time, here.
        // Downloads go to the queue.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: app.downloadActive

            Text {
                text: app.downloadMoving ? qsTr("Moving %1").arg(app.downloadName)
                                         : qsTr("Downloading %1").arg(app.downloadName)
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

        // What can be done with the rows picked, all at once: in place of the
        // new folder's field, so the rows never move under the mouse.
        Rectangle {
            Layout.fillWidth: true
            visible: root.picking
            implicitHeight: 38
            radius: 12
            color: Theme.alpha(Theme.accent, 0.10)
            border.width: 1
            border.color: Theme.alpha(Theme.accent, 0.4)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 4
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: qsTr("%n selected", "", root.pickedCount)
                    color: Theme.text
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                StyledToolButton {
                    readonly property bool all: root.pickedCount === app.files.count
                    iconName: all ? "square" : "square-check"
                    iconSize: 15
                    implicitWidth: 30; implicitHeight: 30
                    ToolTip.visible: hovered
                    ToolTip.text: all ? qsTr("Clear the selection") : qsTr("Select all")
                    onClicked: all ? root.clearPicks() : root.pickAll()
                }
                StyledToolButton {
                    iconName: "download"
                    iconSize: 15
                    implicitWidth: 30; implicitHeight: 30
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Download to the desktop")
                    onClicked: {
                        root.download(root.pickedRows(), "")
                        root.clearPicks()
                    }
                }
                StyledToolButton {
                    iconName: "save"
                    iconSize: 15
                    implicitWidth: 30; implicitHeight: 30
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Download to…")
                    onClicked: {
                        destinationDialog.rows = root.pickedRows()
                        destinationDialog.open()
                    }
                }
                StyledToolButton {
                    iconName: "folder-open"
                    iconSize: 15
                    implicitWidth: 30; implicitHeight: 30
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Move to…")
                    onClicked: {
                        var paths = root.pickedRows().map(function (row) { return row.path })
                        folderPicker.openForMany(paths, qsTr("%n item(s)", "", paths.length), app.ftpPath)
                    }
                }
                StyledToolButton {
                    iconName: "trash"
                    iconSize: 15
                    danger: true
                    implicitWidth: 30; implicitHeight: 30
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Delete on the console")
                    onClicked: confirmDelete.openRows(root.pickedRows())
                }
                StyledToolButton {
                    iconName: "close"
                    iconSize: 15
                    implicitWidth: 30; implicitHeight: 30
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Clear the selection")
                    onClicked: root.clearPicks()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: !root.picking
            spacing: 6
            StyledField {
                id: newFolderName
                Layout.fillWidth: true
                implicitHeight: 34
                font.pixelSize: 12
                placeholderText: qsTr("New folder name")
                onAccepted: if (text.trim().length > 0 && !app.ftpBusy) create()
                function create() {
                    app.ftpMakeDirectory(text.trim())
                    text = ""
                }
            }
            StyledButton {
                text: qsTr("Create")
                iconName: "folder-plus"
                enabled: newFolderName.text.trim().length > 0 && !app.ftpBusy
                onClicked: newFolderName.create()
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
        function targetRows() {
            return [{ path: targetPath, name: targetName, isDirectory: targetIsDirectory, size: targetSize }]
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
            readonly property bool pinned: rowMenu.targetIsDirectory && root.pinnedVersion >= 0
                                            && app.isFtpPinned(rowMenu.targetPath)
            text: pinned ? qsTr("Remove from the top") : qsTr("Pin to the top")
            iconName: pinned ? "pin-off" : "pin"
            visible: rowMenu.targetIsDirectory
            height: visible ? implicitHeight : 0
            onTriggered: pinned ? app.unpinFtpFolder(rowMenu.targetPath) : app.pinFtpFolder(rowMenu.targetPath)
        }
        StyledMenuItem {
            text: qsTr("Use as the upload folder")
            iconName: "upload"
            visible: rowMenu.targetIsDirectory
            height: visible ? implicitHeight : 0
            onTriggered: app.setFtpUploadDirectory(rowMenu.targetPath)
        }
        StyledMenuItem {
            // A package already on a PS5 installs from there, as it is.
            text: qsTr("Install on the console")
            iconName: "package"
            visible: app.installsFromConsole && !rowMenu.targetIsDirectory
                     && rowMenu.targetName.toLowerCase().endsWith(".pkg")
            height: visible ? implicitHeight : 0
            onTriggered: app.installFromConsole(rowMenu.targetPath)
        }
        StyledMenuItem {
            text: qsTr("Download to the desktop")
            iconName: "download"
            onTriggered: root.download(rowMenu.targetRows(), "")
        }
        StyledMenuItem {
            text: qsTr("Download to…")
            iconName: "save"
            onTriggered: {
                destinationDialog.rows = rowMenu.targetRows()
                destinationDialog.open()
            }
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
            text: qsTr("Select")
            iconName: "square-check"
            onTriggered: {
                for (var i = 0; i < app.files.count; ++i)
                    if (app.files.get(i).path === rowMenu.targetPath) {
                        root.togglePick(i, rowMenu.targetPath)
                        break
                    }
            }
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
            onTriggered: folderPicker.openFor(rowMenu.targetPath, rowMenu.targetName, app.ftpPath)
        }
        StyledMenuItem {
            text: qsTr("Delete on the console")
            iconName: "trash"
            danger: true
            onTriggered: confirmDelete.openRows(rowMenu.targetRows())
        }
    }

    FolderDialog {
        id: destinationDialog
        // What goes there: the rows of the menu, or those picked.
        property var rows: []
        title: qsTr("Where to save")
        currentFolder: root.lastDestination.length > 0
            ? root.lastDestination
            : "file://" + app.defaultDownloadDirectory()
        onAccepted: {
            root.lastDestination = selectedFolder
            root.download(rows, selectedFolder)
            if (rows.length > 1)
                root.clearPicks()
        }
    }

    Dialog {
        id: renameDialog
        property string targetPath: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 440
        modal: true
        padding: 0

        function open(path, name) {
            targetPath = path
            nameInput.text = name
            visible = true
            nameInput.forceActiveFocus()
            nameInput.selectAll()
        }

        function confirm() {
            if (nameInput.text.trim().length === 0)
                return
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
            title: qsTr("Rename")
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
                    text: qsTr("Rename")
                    primary: true
                    minimumWidth: 110
                    enabled: nameInput.text.trim().length > 0
                    onClicked: renameDialog.confirm()
                }
            }
        }
    }

    // "Move to…": the console's folders as a tree.
    FtpFolderPicker {
        id: folderPicker
        onChosen: (path) => {
            if (movingPaths.length > 0) {
                app.ftpMoveMany(movingPaths, path)
                root.clearPicks()
            } else
                app.ftpMove(movingPath, path)
        }
    }

    // Deleting on the console always asks first.
    Dialog {
        id: confirmDelete
        // [{ path, name, isDirectory }]
        property var targets: []
        readonly property bool single: targets.length === 1
        readonly property bool anyFolder: {
            for (var i = 0; i < targets.length; ++i)
                if (targets[i].isDirectory)
                    return true
            return false
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        // Fixed width: without it the dialog sizes itself by the text and the
        // text by the dialog, and Qt warns about the loop.
        width: 460
        modal: true
        padding: 0

        function openRows(rows) {
            if (rows.length === 0)
                return
            targets = rows
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
            title: !confirmDelete.single ? qsTr("Delete %n item(s) on the console?", "", confirmDelete.targets.length)
                 : confirmDelete.anyFolder ? qsTr("Delete the folder on the console?") : qsTr("Delete on the console?")
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
                    // One name, or the first few.
                    text: {
                        var names = confirmDelete.targets.slice(0, 4).map(function (t) { return t.name })
                        var more = confirmDelete.targets.length - names.length
                        return names.join(", ") + (more > 0 ? " " + qsTr("and %n more", "", more) : "")
                    }
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 3
                    elide: Text.ElideMiddle
                }
                Text {
                    Layout.fillWidth: true
                    text: confirmDelete.anyFolder
                        ? (confirmDelete.single
                           ? qsTr("The folder and everything inside it are deleted from the console. "
                                  + "This cannot be undone.")
                           : qsTr("They are deleted from the console, folders with everything inside them. "
                                  + "This cannot be undone."))
                        : confirmDelete.single ? qsTr("The file is deleted from the console. This cannot be undone.")
                        : qsTr("The files are deleted from the console. This cannot be undone.")
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
                        app.ftpDeleteMany(confirmDelete.targets)
                        if (!confirmDelete.single)
                            root.clearPicks()
                        confirmDelete.close()
                    }
                }
            }
        }
    }
}
