// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The PS5 homebrew store: the apps of the homebrew.page catalog (checked by
// its signature) as cards. A click opens one — what it is, what it does on
// the console, its licence and source — and installs it on a jailbroken PS5:
// its folder goes to /data/homebrew, where ShadowMountPlus finds it and puts
// it on the home screen.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    signal back()

    // What is shown: "all", "installed", "updates", or a kind ("app", "game"…).
    property string filter: "all"
    property string search: ""

    readonly property var kinds: {
        var seen = {}
        var list = []
        for (var i = 0; i < store.apps.length; ++i) {
            var k = store.apps[i].kind
            if (k.length > 0 && seen[k] === undefined) {
                seen[k] = true
                list.push(k)
            }
        }
        list.sort()
        return list
    }
    readonly property int installedCount: {
        var n = 0
        for (var i = 0; i < store.apps.length; ++i)
            if (store.apps[i].installed)
                ++n
        return n
    }
    readonly property int updateCount: {
        var n = 0
        for (var i = 0; i < store.apps.length; ++i)
            if (store.apps[i].update)
                ++n
        return n
    }
    readonly property var shown: {
        var needle = search.trim().toLowerCase()
        var list = []
        for (var i = 0; i < store.apps.length; ++i) {
            var a = store.apps[i]
            if (filter === "installed" && !a.installed) continue
            if (filter === "updates" && !a.update) continue
            if (filter !== "all" && filter !== "installed" && filter !== "updates" && a.kind !== filter) continue
            if (needle.length > 0 && a.name.toLowerCase().indexOf(needle) < 0
                    && a.author.toLowerCase().indexOf(needle) < 0 && a.titleId.toLowerCase().indexOf(needle) < 0)
                continue
            list.push(a)
        }
        list.sort(function (x, y) {
            // What can be installed first, then by name.
            if (x.available !== y.available) return x.available ? -1 : 1
            return x.name.localeCompare(y.name)
        })
        return list
    }
    function kindLabel(k) {
        return k === "app" ? qsTr("Apps") : k === "game" ? qsTr("Games") : k === "tool" ? qsTr("Tools")
             : k === "emulator" ? qsTr("Emulators") : k.charAt(0).toUpperCase() + k.slice(1)
    }
    function stageText(stage) {
        return stage === "download" ? qsTr("Downloading…")
             : stage === "verify" ? qsTr("Checking the download…")
             : stage === "unpack" ? qsTr("Copying to the PS5…")
             : stage === "finish" ? qsTr("Putting it in place…")
             : stage
    }

    onVisibleChanged: if (visible && store.apps.length === 0 && !store.loading) store.refresh()
    Connections {
        target: store
        function onFinished(message, error) { toast.show(message, error) }
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
                        text: qsTr("Homebrew store")
                        color: Theme.text
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: store.loading ? qsTr("Reading the catalog…")
                            : store.offline ? qsTr("%n app(s) · the catalog kept from last time (the site did not answer)", "",
                                                   store.apps.length)
                            : qsTr("%n app(s) · homebrew.page, checked by its signature", "", store.apps.length)
                        color: store.offline ? Theme.warn : Theme.textSecondary
                        font.pixelSize: 12
                    }
                }
                // The PS5 apps go to.
                StyledButton {
                    id: targetChip
                    visible: store.consoles.length > 0
                    chip: true
                    iconName: "gamepad"
                    text: store.targetName + (store.consoles.length > 1 ? "  ▾" : "")
                    Layout.maximumWidth: 260
                    enabled: store.working.length === 0
                    ToolTip.visible: hovered
                    ToolTip.text: store.canInstall ? qsTr("Apps are installed on this PS5")
                                                   : qsTr("This PS5's FTP is not answering: apps can be looked at, not installed")
                    onClicked: if (store.consoles.length > 1) targetMenu.popup(targetChip, 0, targetChip.height + 4)
                }
                Item { Layout.fillWidth: true }
                StyledToolButton {
                    iconName: store.loading ? "loader" : "refresh"
                    enabled: !store.loading
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Read the catalog again")
                    onClicked: store.refresh()
                }
            }

            // ── what is needed, when it is not there
            Rectangle {
                visible: !store.canInstall
                Layout.fillWidth: true
                implicitHeight: needRow.implicitHeight + 24
                radius: 14
                color: Theme.alpha(Theme.warn, 0.10)
                border.width: 1
                border.color: Theme.alpha(Theme.warn, 0.35)
                RowLayout {
                    id: needRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 12
                    Icon {
                        name: "info"
                        size: 18
                        color: Theme.warn
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        font.pixelSize: 13
                        text: store.consoles.length === 0
                              ? qsTr("These apps are for a jailbroken PS5. Add your PS5 under Settings → Consoles to install them.")
                              : qsTr("%1's FTP is not answering. Turn on FTP in etaHEN (FTP=1 in config.ini), with "
                                     + "kstuff and ShadowMountPlus running, to install apps.").arg(store.targetName)
                    }
                }
            }

            // ── search and filters
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                StyledField {
                    Layout.preferredWidth: 240
                    placeholderText: qsTr("Search apps")
                    text: root.search
                    onTextChanged: root.search = text
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    StyledButton {
                        chip: true
                        primary: root.filter === "all"
                        text: qsTr("All") + "  " + store.apps.length
                        onClicked: root.filter = "all"
                    }
                    Repeater {
                        model: root.kinds
                        StyledButton {
                            required property string modelData
                            chip: true
                            primary: root.filter === modelData
                            text: root.kindLabel(modelData)
                            onClicked: root.filter = modelData
                        }
                    }
                    StyledButton {
                        visible: root.installedCount > 0
                        chip: true
                        iconName: "check"
                        primary: root.filter === "installed"
                        text: qsTr("Installed", "filter: the apps installed") + "  " + root.installedCount
                        onClicked: root.filter = "installed"
                    }
                    StyledButton {
                        visible: root.updateCount > 0
                        chip: true
                        iconName: "arrow-up"
                        primary: root.filter === "updates"
                        text: qsTr("Updates") + "  " + root.updateCount
                        onClicked: root.filter = "updates"
                    }
                }
            }

            // ── the apps
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                GridView {
                    id: grid
                    anchors.fill: parent
                    clip: true
                    readonly property int columns: Math.max(1, Math.floor(width / 300))
                    cellWidth: Math.floor(width / columns)
                    cellHeight: 112
                    model: root.shown
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: grid.contentHeight > grid.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }

                    delegate: Item {
                        id: cell
                        required property var modelData
                        width: grid.cellWidth
                        height: grid.cellHeight
                        readonly property bool busy: store.working === modelData.titleId

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 5
                            radius: 14
                            color: cardArea.containsMouse ? Theme.controlHover : Theme.panelAltFill
                            border.width: 1
                            border.color: cell.modelData.update ? Theme.alpha(Theme.accent, 0.6) : Theme.glassEdge
                            opacity: cell.modelData.available ? 1.0 : 0.6
                            Behavior on color { ColorAnimation { duration: Theme.fast } }

                            MouseArea {
                                id: cardArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: detailDialog.ask(cell.modelData.titleId)
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 12
                                Rectangle {
                                    implicitWidth: 72
                                    implicitHeight: 72
                                    radius: 14
                                    color: Theme.controlFill
                                    clip: true
                                    Image {
                                        id: appIcon
                                        anchors.fill: parent
                                        source: cell.modelData.icon
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                    }
                                    Icon {
                                        anchors.centerIn: parent
                                        visible: appIcon.status !== Image.Ready
                                        name: "package"
                                        size: 26
                                        color: Theme.textSecondary
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: cell.modelData.name
                                        color: Theme.text
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: [cell.modelData.author, cell.modelData.version]
                                              .filter(function (t) { return t && t.length > 0 }).join("  ·  ")
                                        color: Theme.textSecondary
                                        font.pixelSize: 12
                                    }
                                    // Where it stands, or how far it got.
                                    Row {
                                        visible: !cell.busy
                                        spacing: 6
                                        Rectangle {
                                            readonly property string state: !cell.modelData.available ? "soon"
                                                : cell.modelData.update ? "update"
                                                : cell.modelData.installed ? "installed"
                                                : !cell.modelData.installable ? "other" : ""
                                            visible: state.length > 0
                                            readonly property color tone: state === "update" ? Theme.accent
                                                                         : state === "installed" ? Theme.ok : Theme.textSecondary
                                            implicitWidth: badgeText.implicitWidth + 16
                                            implicitHeight: 20
                                            radius: 10
                                            color: Theme.alpha(tone, 0.14)
                                            border.width: 1
                                            border.color: Theme.alpha(tone, 0.4)
                                            Text {
                                                id: badgeText
                                                anchors.centerIn: parent
                                                text: parent.state === "soon" ? qsTr("Coming soon")
                                                    : parent.state === "update" ? qsTr("Update")
                                                    : parent.state === "installed" ? qsTr("Installed", "one app")
                                                    : qsTr("Not a folder app")
                                                color: parent.tone
                                                font.pixelSize: 10
                                                font.weight: Font.DemiBold
                                            }
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: cell.modelData.size
                                            color: Theme.textMuted
                                            font.pixelSize: 11
                                        }
                                    }
                                    Rectangle {
                                        visible: cell.busy
                                        Layout.fillWidth: true
                                        height: 4
                                        radius: 2
                                        color: Theme.alpha(Theme.accent, 0.18)
                                        Rectangle {
                                            width: parent.width * Math.max(0.03, Math.min(1, store.progress))
                                            height: parent.height
                                            radius: parent.radius
                                            color: Theme.accent
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Empty, or reading.
                Column {
                    anchors.centerIn: parent
                    visible: root.shown.length === 0
                    spacing: 12
                    BusyIndicator {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: store.loading
                        running: store.loading
                    }
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: !store.loading
                        name: "store"
                        size: 44
                        color: Theme.alpha(Theme.textSecondary, 0.7)
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        width: Math.min(460, root.width - 80)
                        wrapMode: Text.WordWrap
                        text: store.loading ? qsTr("Reading the catalog…")
                            : store.error.length > 0 && store.apps.length === 0
                              ? qsTr("The catalog could not be read: %1").arg(store.error)
                            : qsTr("Nothing here.")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                }
            }

            // The one thing to know, said quietly.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Icon {
                    name: "info"
                    size: 13
                    color: Theme.textMuted
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: 11
                    text: qsTr("Each app installs as its folder in /data/homebrew on the PS5, checked against the "
                               + "catalog first; ShadowMountPlus (with kstuff) puts it on the home screen. No package "
                               + "is built. The apps are their authors' work, under their own licences.")
                }
            }
        }
    }

    // One app: what it is, and installing it.
    Dialog {
        id: detailDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(640, parent.width - 32)
        height: Math.min(implicitHeight, parent.height - 48)
        modal: true
        padding: 0
        readonly property var app: store.detail
        readonly property bool busy: store.working.length > 0 && store.working === app.titleId
        // The same app as listed now (installed, update), with what was read.
        readonly property var listed: {
            for (var i = 0; i < store.apps.length; ++i)
                if (store.apps[i].titleId === app.titleId)
                    return store.apps[i]
            return app
        }
        function ask(titleId) {
            store.showDetails(titleId)
            open()
        }
        function sandboxText(s) {
            return s === "stays" ? qsTr("Stays inside the app sandbox.")
                 : s === "leaves" ? qsTr("Leaves the app sandbox (it uses the jailbreak's kernel access).")
                 : s === "unclear" ? qsTr("Whether it leaves the app sandbox is not clear.")
                 : ""
        }

        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }
        header: DialogHeader {
            title: detailDialog.listed.name !== undefined ? detailDialog.listed.name : ""
            dialog: detailDialog
        }
        contentItem: ScrollView {
            id: detailScroll
            clip: true
            implicitHeight: Math.min(detailColumn.implicitHeight, 520)
            contentWidth: availableWidth
            ColumnLayout {
                id: detailColumn
                width: detailScroll.availableWidth
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    spacing: 14
                    Rectangle {
                        implicitWidth: 88
                        implicitHeight: 88
                        radius: 18
                        color: Theme.controlFill
                        clip: true
                        Image {
                            id: bigIcon
                            anchors.fill: parent
                            source: detailDialog.listed.icon !== undefined ? detailDialog.listed.icon : ""
                            fillMode: Image.PreserveAspectCrop
                        }
                        Icon {
                            anchors.centerIn: parent
                            visible: bigIcon.status !== Image.Ready
                            name: "package"
                            size: 30
                            color: Theme.textSecondary
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: [detailDialog.listed.author, detailDialog.listed.version,
                                   detailDialog.listed.size, detailDialog.listed.titleId]
                                  .filter(function (t) { return t !== undefined && t.length > 0 }).join("  ·  ")
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: detailDialog.listed.updated !== undefined && detailDialog.listed.updated.length > 0
                            text: qsTr("Updated %1").arg(detailDialog.listed.updated)
                                  + (detailDialog.app.license !== undefined && detailDialog.app.license.length > 0
                                     ? "  ·  " + qsTr("Licence: %1").arg(detailDialog.app.license) : "")
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: detailDialog.listed.installed === true
                            text: detailDialog.listed.update
                                  ? qsTr("Installed: %1 · an update is out").arg(detailDialog.listed.installedVersion)
                                  : qsTr("Installed on %1").arg(store.targetName)
                            color: detailDialog.listed.update ? Theme.accent : Theme.ok
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: detailDialog.app.prerelease === true
                            text: qsTr("A pre-release: it may not be finished.")
                            color: Theme.warn
                            font.pixelSize: 12
                        }
                    }
                }
                BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    visible: detailDialog.app.ready !== true
                    running: visible
                }
                Text {
                    visible: detailDialog.app.error !== undefined && detailDialog.app.error.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    wrapMode: Text.WordWrap
                    text: qsTr("Its details could not be read: %1").arg(detailDialog.app.error)
                    color: Theme.error
                    font.pixelSize: 12
                }
                Text {
                    visible: detailDialog.app.description !== undefined && detailDialog.app.description.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    wrapMode: Text.WordWrap
                    text: detailDialog.app.description !== undefined ? detailDialog.app.description : ""
                    color: Theme.text
                    font.pixelSize: 13
                    lineHeight: 1.2
                }
                // What it does on the console, as the catalog's scan found.
                ColumnLayout {
                    visible: detailDialog.app.ready === true
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    spacing: 4
                    Text {
                        text: qsTr("On the console")
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: {
                            var lines = []
                            var a = detailDialog.app
                            var listedApp = detailDialog.listed
                            var sandbox = detailDialog.sandboxText(listedApp.sandbox !== undefined ? listedApp.sandbox : "")
                            if (sandbox.length > 0)
                                lines.push(sandbox)
                            if (a.safetyKnown === true) {
                                if (a.safetyNetwork === true)
                                    lines.push(qsTr("Uses the network."))
                                if (a.safetyHelpers > 0)
                                    lines.push(qsTr("Starts %n helper payload(s)", "", a.safetyHelpers)
                                               + (a.safetyHelpersUnapproved > 0
                                                  ? " " + qsTr("(%n not reviewed by the catalog)", "", a.safetyHelpersUnapproved) : "") + ".")
                                if (a.safetyRoutes !== undefined && a.safetyRoutes.length > 0)
                                    lines.push(qsTr("Reaches: %1").arg(a.safetyRoutes.join(", ")))
                            } else {
                                lines.push(qsTr("The catalog has not looked into what it does yet."))
                            }
                            return lines
                        }
                        RowLayout {
                            required property string modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Icon {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 2
                                name: "info"
                                size: 13
                                color: Theme.textSecondary
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: parent.modelData
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
                        }
                    }
                }
                // What changed in this version.
                ColumnLayout {
                    visible: detailDialog.app.releaseNotes !== undefined && detailDialog.app.releaseNotes.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    spacing: 4
                    Text {
                        text: qsTr("What's new")
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        textFormat: Text.PlainText
                        text: detailDialog.app.releaseNotes !== undefined
                              ? detailDialog.app.releaseNotes + (detailDialog.app.releaseNotesTruncated ? " …" : "") : ""
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    spacing: 6
                    StyledButton {
                        visible: detailDialog.app.sourceRepo !== undefined && detailDialog.app.sourceRepo.length > 0
                        chip: true
                        iconName: "external-link"
                        text: qsTr("Source code")
                        onClicked: Qt.openUrlExternally(detailDialog.app.sourceRepo)
                    }
                    StyledButton {
                        visible: detailDialog.app.page !== undefined && detailDialog.app.page.length > 0
                        chip: true
                        iconName: "globe"
                        text: qsTr("Catalog page")
                        onClicked: Qt.openUrlExternally(detailDialog.app.page)
                    }
                    StyledButton {
                        visible: detailDialog.app.releaseUrl !== undefined && detailDialog.app.releaseUrl.length > 0
                        chip: true
                        iconName: "external-link"
                        text: qsTr("Release")
                        onClicked: Qt.openUrlExternally(detailDialog.app.releaseUrl)
                    }
                }
                // Installing: how far it got.
                ColumnLayout {
                    visible: detailDialog.busy
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    spacing: 6
                    Text {
                        text: root.stageText(store.stage)
                        color: Theme.text
                        font.pixelSize: 12
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 6
                        radius: 3
                        color: Theme.alpha(Theme.accent, 0.18)
                        Rectangle {
                            width: parent.width * Math.max(0.02, Math.min(1, store.progress))
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accent
                            Behavior on width { NumberAnimation { duration: 200 } }
                        }
                    }
                }
                Item { implicitHeight: 4 }
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
                    visible: detailDialog.listed.installed === true && !detailDialog.busy
                    danger: true
                    iconName: "trash"
                    text: qsTr("Remove")
                    enabled: store.canInstall && store.working.length === 0
                    onClicked: store.uninstall(detailDialog.listed.titleId)
                }
                Item { Layout.fillWidth: true }
                StyledButton {
                    visible: detailDialog.busy
                    text: qsTr("Cancel")
                    onClicked: store.cancel()
                }
                StyledButton {
                    visible: !detailDialog.busy
                    text: qsTr("Close")
                    onClicked: detailDialog.close()
                }
                StyledButton {
                    visible: !detailDialog.busy && detailDialog.listed.installable === true
                    primary: true
                    iconName: detailDialog.listed.update ? "arrow-up" : "download"
                    text: detailDialog.listed.update ? qsTr("Update on %1").arg(store.targetName)
                        : detailDialog.listed.installed ? qsTr("Install again")
                        : store.targetName.length > 0 ? qsTr("Install on %1").arg(store.targetName) : qsTr("Install")
                    enabled: store.canInstall && store.working.length === 0
                    onClicked: store.install(detailDialog.listed.titleId)
                }
            }
        }
    }

    // The PS5 apps go to.
    Menu {
        id: targetMenu
        topPadding: 8
        bottomPadding: 8
        background: Rectangle {
            implicitWidth: 260
            color: Theme.menuFill
            border.color: Theme.glassEdge
            border.width: 1
            radius: 14
        }
        Instantiator {
            model: store.consoles
            delegate: StyledMenuItem {
                required property var modelData
                text: modelData.name + (modelData.ftp ? "" : "  ·  " + qsTr("no FTP"))
                iconName: modelData.target ? "check" : "gamepad"
                onTriggered: store.setTarget(modelData.address)
            }
            onObjectAdded: (index, object) => targetMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => targetMenu.removeItem(object)
        }
    }
}
