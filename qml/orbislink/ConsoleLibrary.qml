// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The console library: what is in the OrbisLinkFPKG folders of the console
// in use (its memory, USB drives, extended storage), read over FTP. Each
// thing is told apart — a package, a PS1/PS2 disc, a PS5 image or app
// folder, a payload — and has the one button that makes it playable. Games
// and apps, their updates and add-ons, payloads and the rest are listed
// apart, each under its own heading.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    property string filter: "all"
    property string search: ""
    // Every row's button is this wide, so they line up.
    readonly property int actionWidth: 196

    function kindLabel(item) {
        switch (item.kind) {
        case "package": return item.category === "gp" ? qsTr("PS4 patch")
                             : item.category === "ac" ? qsTr("PS4 add-on") : qsTr("PS4 package")
        case "disc": return item.platform === "ps2" ? qsTr("PS2 disc") : item.platform === "ps1" ? qsTr("PS1 disc")
                                                                                              : qsTr("Disc image")
        case "image": return qsTr("PS5 image")
        case "folder": return item.platform === "ps4" ? qsTr("PS4 app folder") : qsTr("PS5 app folder")
        case "payload": return qsTr("Payload")
        case "archive": return qsTr("Archive")
        }
        return item.kind
    }
    function driveLabel(drive) {
        return drive === "usb" ? qsTr("USB drive") : drive === "ext" ? qsTr("Extended storage") : qsTr("Console memory")
    }
    function actionLabel(action) {
        switch (action) {
        case "install": return qsTr("Install")
        case "convert": return qsTr("Convert…")
        case "mount": return qsTr("Put on the home screen")
        case "run": return qsTr("Run")
        }
        return ""
    }
    function actionHint(item) {
        switch (item.action) {
        case "install": return qsTr("The console installs it from where it is")
        case "convert": return qsTr("Made into a PS4 package on this PC and put back in this folder: installed too, or not, as you choose")
        case "mount": return qsTr("Moved next door into %1, where ShadowMountPlus mounts it from (instant: same drive)")
                             .arg(item.drive === "internal" ? "/data/homebrew" : item.path.split("/").slice(0, 3).join("/") + "/homebrew")
        case "run": return qsTr("Sent to the console's loader now (port %1)").arg(item.port)
        }
        return ""
    }
    function icon(item) {
        switch (item.kind) {
        case "disc": return "disc"
        case "payload": return "zap"
        case "archive": return "archive"
        case "folder": return "folder"
        }
        return "package"
    }
    readonly property var groups: ["games", "extras", "payloads", "other"]
    function groupLabel(group) {
        switch (group) {
        case "games": return qsTr("Games and apps")
        case "extras": return qsTr("Updates and add-ons")
        case "payloads": return qsTr("Payloads")
        }
        return qsTr("Other files")
    }
    function groupHint(group) {
        switch (group) {
        case "games": return qsTr("Packages, PS1/PS2 discs, PS5 images and app folders")
        case "extras": return qsTr("Patches, add-ons and themes for games")
        case "payloads": return qsTr("Run on the console when you want them")
        }
        return qsTr("What cannot be used as it is")
    }
    // The items shown, each group under a heading ({ header: group, count }).
    function shown() {
        var needle = search.trim().toLowerCase()
        var byGroup = {}
        var items = consoleLibrary.items
        for (var i = 0; i < items.length; ++i) {
            var it = items[i]
            if (filter === "todo" ? it.installed || it.action.length === 0
                : filter !== "all" && it.group !== filter)
                continue
            if (needle.length > 0 && it.title.toLowerCase().indexOf(needle) < 0
                    && it.name.toLowerCase().indexOf(needle) < 0 && it.titleId.toLowerCase().indexOf(needle) < 0)
                continue
            if (!byGroup[it.group])
                byGroup[it.group] = []
            byGroup[it.group].push(it)
        }
        var out = []
        for (var g = 0; g < groups.length; ++g) {
            var list = byGroup[groups[g]]
            if (!list)
                continue
            out.push({ header: groups[g], count: list.length })
            out = out.concat(list)
        }
        return out
    }
    function count(group) {
        var n = 0
        for (var i = 0; i < consoleLibrary.items.length; ++i)
            if (consoleLibrary.items[i].group === group)
                ++n
        return n
    }

    onVisibleChanged: if (visible && !consoleLibrary.scanning) consoleLibrary.refresh()
    Connections {
        target: consoleLibrary
        function onFinished(message, error) { toast.show(message, error) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // How it works, and the folders the console has.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: howColumn.implicitHeight + 24
            radius: 14
            color: Theme.alpha(Theme.accent, 0.08)
            border.width: 1
            border.color: Theme.alpha(Theme.accent, 0.30)
            ColumnLayout {
                id: howColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 16
                anchors.rightMargin: 14
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Icon { name: "info"; size: 18; color: Theme.accent; Layout.alignment: Qt.AlignTop }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        font.pixelSize: 12
                        text: qsTr("Put games and apps in /data/OrbisLinkFPKG on the console, or in an OrbisLinkFPKG "
                                   + "folder on a USB drive or the extended storage, as they come: packages, "
                                   + "PS1/PS2 discs (.iso, .bin/.cue), PS5 images (.ffpkg, .exfat), app folders, "
                                   + "payloads. The app says what each one is and does the rest.")
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: consoleLibrary.folders.length > 0
                    Repeater {
                        model: consoleLibrary.folders
                        Rectangle {
                            required property var modelData
                            implicitHeight: 24
                            implicitWidth: folderText.implicitWidth + 20
                            radius: 12
                            color: Theme.controlFill
                            Text {
                                id: folderText
                                anchors.centerIn: parent
                                text: root.driveLabel(modelData.drive) + "  ·  " + modelData.path
                                color: Theme.textSecondary
                                font.pixelSize: 11
                                font.family: Theme.fontMono
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: consoleLibrary.items.length > 0
            StyledField {
                Layout.preferredWidth: 200
                Layout.alignment: Qt.AlignTop
                placeholderText: qsTr("Search")
                text: root.search
                onTextChanged: root.search = text
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                StyledButton {
                    chip: true
                    primary: root.filter === "all"
                    text: qsTr("All") + "  " + consoleLibrary.items.length
                    onClicked: root.filter = "all"
                }
                StyledButton {
                    chip: true
                    iconName: "download"
                    primary: root.filter === "todo"
                    text: qsTr("Not installed")
                    onClicked: root.filter = "todo"
                }
                Repeater {
                    model: root.groups
                    StyledButton {
                        required property string modelData
                        visible: root.count(modelData) > 0
                        chip: true
                        primary: root.filter === modelData
                        text: root.groupLabel(modelData) + "  " + root.count(modelData)
                        onClicked: root.filter = modelData
                    }
                }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            model: root.visible ? root.shown() : []

            delegate: Item {
                id: entry
                required property var modelData
                required property int index
                readonly property bool header: modelData.header !== undefined
                width: list.width - 12
                // A heading has room above it, but not at the top.
                implicitHeight: header ? headerRow.implicitHeight + (index > 0 ? 14 : 2) : row.implicitHeight

                // ── a group's heading
                RowLayout {
                    id: headerRow
                    visible: entry.header
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 4
                    spacing: 8
                    Text {
                        text: entry.header ? root.groupLabel(entry.modelData.header) : ""
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        implicitHeight: 18
                        implicitWidth: headerCount.implicitWidth + 12
                        radius: 9
                        color: Theme.controlFill
                        Text {
                            id: headerCount
                            anchors.centerIn: parent
                            text: entry.header ? entry.modelData.count : ""
                            color: Theme.textSecondary
                            font.pixelSize: 11
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: entry.header ? root.groupHint(entry.modelData.header) : ""
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                }

                // ── an item: its picture, what it is, then its button and,
                // at the edge, the bin; every row keeps the same columns
                Rectangle {
                    id: row
                    visible: !entry.header
                    readonly property var item: entry.modelData
                    width: parent.width
                    implicitHeight: entry.header ? 0 : Math.max(rowLayout.implicitHeight, 52) + 24
                    radius: 14
                    color: Theme.panelAltFill
                    border.width: 1
                    border.color: Theme.border

                    RowLayout {
                        id: rowLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12

                        Rectangle {
                            width: 52; height: 52; radius: 10
                            color: Theme.controlFill
                            border.color: Theme.border
                            clip: true
                            Layout.alignment: Qt.AlignVCenter
                            Image {
                                id: picture
                                anchors.fill: parent
                                anchors.margins: 1
                                source: !entry.header && row.item.icon ? row.item.icon : ""
                                fillMode: Image.PreserveAspectCrop
                                visible: status === Image.Ready
                            }
                            Icon {
                                anchors.centerIn: parent
                                visible: !picture.visible
                                name: entry.header ? "package" : root.icon(row.item)
                                size: 24
                                color: Theme.textSecondary
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 3
                            RowLayout {
                                spacing: 8
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: entry.header ? "" : row.item.title
                                    color: Theme.text
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Rectangle {
                                    visible: !entry.header && row.item.installed
                                    implicitHeight: 20
                                    implicitWidth: installedText.implicitWidth + 16
                                    radius: 10
                                    color: Theme.alpha(Theme.ok, 0.16)
                                    Text {
                                        id: installedText
                                        anchors.centerIn: parent
                                        text: qsTr("Installed")
                                        color: Theme.ok
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: entry.header ? "" : [root.kindLabel(row.item), row.item.titleId, row.item.version,
                                       row.item.sizeText, root.driveLabel(row.item.drive)].filter(function (p) {
                                    return p && p.length > 0
                                }).join("  ·  ")
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                                text: entry.header ? "" : row.item.name
                                color: Theme.textMuted
                                font.pixelSize: 11
                                font.family: Theme.fontMono
                            }
                            Text {
                                visible: !entry.header && row.item.note.length > 0
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: entry.header ? "" : row.item.note
                                color: Theme.textSecondary
                                font.pixelSize: 11
                            }
                        }
                        // The button, the same width on every row (a row
                        // without one keeps its place).
                        Item {
                            Layout.preferredWidth: root.actionWidth
                            Layout.preferredHeight: actionButton.implicitHeight
                            Layout.alignment: Qt.AlignVCenter
                            StyledButton {
                                id: actionButton
                                anchors.fill: parent
                                visible: !entry.header && row.item.action.length > 0
                                text: entry.header ? "" : row.item.action === "convert" ? root.actionLabel("convert")
                                      : row.item.installed && row.item.action !== "run"
                                      ? qsTr("Again") : root.actionLabel(row.item.action)
                                iconName: entry.header ? "" : row.item.action === "run" ? "play"
                                          : row.item.action === "mount" ? "home"
                                          : row.item.action === "convert" ? "disc" : "download"
                                primary: !entry.header && !row.item.installed
                                enabled: !consoleLibrary.busy
                                ToolTip.visible: hovered
                                ToolTip.text: entry.header ? "" : root.actionHint(row.item)
                                onClicked: row.item.action === "convert" ? convertMenu.ask(row.item, actionButton)
                                                                       : consoleLibrary.act(row.item.path)
                            }
                        }
                        // The bin, at the edge: kept in place where there is
                        // nothing to delete, so the buttons line up.
                        StyledToolButton {
                            Layout.alignment: Qt.AlignVCenter
                            opacity: !entry.header && !row.item.folder ? 1 : 0
                            enabled: opacity > 0 && !consoleLibrary.busy
                            iconName: "trash"
                            danger: true
                            ToolTip.visible: hovered && opacity > 0
                            ToolTip.text: qsTr("Delete from the console…")
                            onClicked: deleteDialog.ask(row.item)
                        }
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                width: Math.min(460, parent.width - 32)
                visible: list.count === 0
                spacing: 12
                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: consoleLibrary.scanning
                    running: consoleLibrary.scanning
                }
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !consoleLibrary.scanning
                    name: "hard-drive"
                    size: 44
                    color: Theme.alpha(Theme.textSecondary, 0.7)
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: consoleLibrary.status.length > 0 ? consoleLibrary.status
                          : consoleLibrary.items.length > 0 ? qsTr("Nothing here.")
                          : qsTr("The library is empty: put something in /data/OrbisLinkFPKG over FTP (the Files tab), "
                                 + "or in an OrbisLinkFPKG folder on a USB drive, and read it again.")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
            }
        }
    }

    // A disc: converted and installed, or only converted (its package is put
    // in the same folder, to install later).
    Menu {
        id: convertMenu
        property var item: ({ path: "" })
        function ask(it, from) {
            item = it
            popup(from, 0, from.height + 4)
        }
        topPadding: 8
        bottomPadding: 8
        background: Rectangle {
            implicitWidth: 380
            color: Theme.menuFill
            border.color: Theme.glassEdge
            border.width: 1
            radius: 14
        }
        StyledMenuItem {
            text: qsTr("Convert and install")
            iconName: "download"
            onTriggered: consoleLibrary.convert(convertMenu.item.path, true)
        }
        StyledMenuItem {
            text: qsTr("Only convert (the package stays in this folder)")
            iconName: "package"
            onTriggered: consoleLibrary.convert(convertMenu.item.path, false)
        }
    }

    Dialog {
        id: deleteDialog
        property var item: ({ name: "", path: "" })
        function ask(it) {
            item = it
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
            title: qsTr("Delete %1 from the console?").arg(deleteDialog.item.name)
            dialog: deleteDialog
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
                    onClicked: deleteDialog.close()
                }
                StyledButton {
                    text: qsTr("Delete")
                    iconName: "trash"
                    danger: true
                    solid: true
                    onClicked: {
                        consoleLibrary.remove(deleteDialog.item.path)
                        deleteDialog.close()
                    }
                }
            }
        }
    }
}
