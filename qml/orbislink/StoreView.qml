// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The homebrew store: the apps of every store turned on, together, as
// cards — homebrew.page's catalog (checked by its signature), whose apps are
// folders for a jailbroken PS5 (they go to /data/homebrew, where
// ShadowMountPlus puts them on the home screen), and the Homebrew Store
// databases (PKG-Zone, and those added), whose apps are packages installed
// through the queue on the console in use. An app in more than one store
// shows each store's tag, and installing it asks which store to take it
// from. A click opens one — what it is, what it does on the console, its
// licence and source.
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

    // What is shown: "all", "installed", "updates", a kind ("app", "game"…),
    // or "local" — the app archives of a folder of this PC.
    property string filter: typeof demoView !== "undefined" && demoView === "store-local" ? "local" : "all"
    property string search: ""
    // Only the apps for one console ("ps4", "ps5"), or for any ("").
    property string platform: ""

    function runsOn(app, platform) {
        return platform.length === 0 || (app.platforms !== undefined && app.platforms.indexOf(platform) >= 0)
    }
    function platformCount(platform) {
        var n = 0
        for (var i = 0; i < store.apps.length; ++i)
            if (runsOn(store.apps[i], platform))
                ++n
        return n
    }
    readonly property int ps4Count: platformCount("ps4")
    readonly property int ps5Count: platformCount("ps5")
    // 2706838 → "2.7M", 15234 → "15K": how many times, in short.
    function shortCount(n) {
        if (n >= 1000000)
            return qsTr("%1M", "millions of downloads").arg(Number(n / 1000000).toLocaleString(Qt.locale(), "f", n >= 10000000 ? 0 : 1))
        if (n >= 1000)
            return qsTr("%1K", "thousands of downloads").arg(Number(n / 1000).toLocaleString(Qt.locale(), "f", n >= 10000 ? 0 : 1))
        return String(n)
    }

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
            if (filter === "local") continue
            if (filter === "installed" && !a.installed) continue
            if (filter === "updates" && !a.update) continue
            if (filter !== "all" && filter !== "installed" && filter !== "updates" && a.kind !== filter) continue
            if (!runsOn(a, platform)) continue
            if (needle.length > 0 && a.name.toLowerCase().indexOf(needle) < 0
                    && a.author.toLowerCase().indexOf(needle) < 0 && a.titleId.toLowerCase().indexOf(needle) < 0)
                continue
            list.push(a)
        }
        list.sort(function (x, y) {
            // The most downloaded first, whatever is shown; those no store
            // counts (homebrew.page does not) after them, by name.
            var dx = x.downloads !== undefined ? x.downloads : -1
            var dy = y.downloads !== undefined ? y.downloads : -1
            if (dx !== dy) return dy - dx
            return x.name.localeCompare(y.name)
        })
        return list
    }
    function kindLabel(k) {
        return k === "app" ? qsTr("Apps") : k === "game" ? qsTr("Games") : k === "tool" ? qsTr("Tools")
             : k === "emulator" ? qsTr("Emulators") : k === "media" ? qsTr("Media") : k === "theme" ? qsTr("Themes")
             : k === "mod" ? qsTr("Mods") : k.charAt(0).toUpperCase() + k.slice(1)
    }
    // The stores turned on, by name, and those that could not be read.
    readonly property var storesOn: {
        var names = []
        for (var i = 0; i < store.stores.length; ++i)
            if (store.stores[i].enabled)
                names.push(store.stores[i].name)
        return names
    }
    readonly property var storeTroubles: {
        var lines = []
        for (var i = 0; i < store.stores.length; ++i) {
            var s = store.stores[i]
            if (!s.enabled || s.error.length === 0)
                continue
            lines.push(s.offline ? qsTr("%1 did not answer (%2): its apps are the ones kept from last time.").arg(s.name).arg(s.error)
                                 : qsTr("%1 could not be read: %2").arg(s.name).arg(s.error))
        }
        return lines
    }
    // The stores an app can come from now, onto the console in use.
    function readySources(app) {
        var list = []
        var sources = app.sources !== undefined ? app.sources : []
        for (var i = 0; i < sources.length; ++i) {
            var s = sources[i]
            if (s.installable && (s.format === "pkg" ? store.canInstallPackages : store.canInstall))
                list.push(s)
        }
        return list
    }
    // Installing: straight away from the one store that can, else asking.
    function installApp(app) {
        var ready = readySources(app)
        if (ready.length === 1)
            store.install(app.titleId, ready[0].store)
        else if (ready.length > 1)
            sourceDialog.ask(app, ready)
    }
    function formatLabel(f) {
        return f === "folder" ? qsTr("app folder") : qsTr("package")
    }
    // Why none of its stores can put it on this console, in a word or two.
    function briefNote(app) {
        var sources = app.sources !== undefined ? app.sources : []
        for (var i = 0; i < sources.length; ++i)
            if (sources[i].brief.length > 0)
                return sources[i].brief
        return qsTr("Not for this console")
    }
    // "45% · 3.2 MB/s" while bytes move (the download, the copy to the
    // PS5); "" in the stages that have none to count.
    function progressLine() {
        if (store.amountText.length === 0)
            return ""
        var percent = qsTr("%1%").arg(Math.floor(Math.min(1, store.progress) * 100))
        return store.speedText.length > 0 ? percent + "  ·  " + store.speedText : percent
    }
    function stageText(stage) {
        return stage === "download" ? qsTr("Downloading…")
             : stage === "verify" ? qsTr("Checking the download…")
             : stage === "unpack" ? qsTr("Copying to the PS5…")
             : stage === "finish" ? qsTr("Putting it in place…")
             : stage === "shadowmount" ? qsTr("Starting ShadowMountPlus again…")
             : stage
    }

    // The console an app is for, as a tag: the PS4 in the accent, the PS5
    // in the ink.
    component PlatformTag: Tag {
        required property string platform
        text: platform === "ps4" ? "PS4" : "PS5"
        tone: platform === "ps4" ? "accent" : "neutral"
    }

    // A filter: a round-ended pill of glass, bright when it is the one in
    // use (a chip button cannot say "chosen").
    component FilterPill: Rectangle {
        id: pill
        property string text: ""
        property string iconName: ""
        property string count: ""
        property bool chosen: false
        property string tip: ""
        signal clicked()

        implicitWidth: pillRow.implicitWidth + 24
        implicitHeight: 32
        radius: height / 2
        color: chosen ? (Theme.light ? Qt.rgba(1, 1, 1, 0.92) : Qt.rgba(1, 1, 1, 0.14))
             : pillHover.hovered ? Theme.controlHover : Theme.controlFill
        border.width: activeFocus ? 2 : 1
        border.color: activeFocus ? Theme.alpha(Theme.accent, 0.8)
                    : chosen ? (Theme.light ? Qt.rgba(1, 1, 1, 1) : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.14))
                    : Theme.light ? Qt.rgba(1, 1, 1, 0.75) : Theme.glassEdge
        Behavior on color { ColorAnimation { duration: Theme.fast } }

        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: text
        Keys.onSpacePressed: clicked()
        Keys.onReturnPressed: clicked()
        ToolTip.visible: pillHover.hovered && tip.length > 0
        ToolTip.text: tip
        ToolTip.delay: 400

        HoverHandler { id: pillHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: pill.clicked() }

        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: 6
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

    onVisibleChanged: if (visible && store.apps.length === 0 && !store.loading) store.refresh()
    // Shown from the start (a page opened by --demo-view).
    Component.onCompleted: {
        if (!visible)
            return
        visibleChanged()
        if (filter === "local")
            store.scanLocal()
    }
    // Whether the catalog was last read with the PS5's FTP answering (it
    // then also says what the PS5 has installed).
    property bool readWithFtp: false
    Connections {
        target: store
        function onFinished(message, error) { toast.show(message, error) }
        function onStateChanged() {
            if (store.loading)
                root.readWithFtp = store.canInstall
        }
        // The PS5's FTP answered after the page was read: what it has.
        function onConsolesChanged() {
            if (root.visible && store.canInstall && !root.readWithFtp && !store.loading)
                store.refresh()
        }
    }

    // ── the page, straight on the sheet: no panel of its own (in the main
    // area the sheet is the frame; over Remote Play the overlay is)
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.framed ? Theme.gutter : 18
        anchors.rightMargin: root.framed ? Theme.gutter : 18
        anchors.topMargin: root.framed ? 22 : 18
        anchors.bottomMargin: root.framed ? 12 : 14
        spacing: 14

        // ── the page's head: how many apps, the console they go to, the stores
        PageHeader {
            id: pageHeader
            visible: root.framed
            eyebrow: qsTr("Homebrew")
            title: qsTr("Homebrew store")
            subtitle: store.loading && store.apps.length === 0 ? qsTr("Reading the stores…")
                    : root.storesOn.length === 0 ? qsTr("Every store is turned off")
                    : qsTr("%n app(s), the most downloaded first", "", store.apps.length)
                      + (store.loading ? "  ·  " + qsTr("still reading…") : "")

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

            // The console apps go to: the one in use.
            StyledButton {
                anchors.verticalCenter: parent.verticalCenter
                id: targetChip
                visible: store.consoles.length > 0
                chip: true
                iconName: "gamepad"
                text: store.targetName + (store.consoles.length > 1 ? "  ▾" : "")
                width: Math.min(implicitWidth, 220)
                enabled: store.working.length === 0
                ToolTip.visible: hovered
                ToolTip.text: store.consoles.length > 1
                              ? qsTr("Apps are installed on this console, the one in use. Click for another (it becomes the one in use).")
                              : qsTr("Apps are installed on this console")
                onClicked: if (store.consoles.length > 1) targetMenu.popup(targetChip, 0, targetChip.height + 4)
            }
            StyledToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: store.loading ? "loader" : "refresh"
                spinning: store.loading
                enabled: !store.loading
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Read the stores again")
                Accessible.name: ToolTip.text
                onClicked: root.filter === "local" ? store.scanLocal() : store.refresh()
            }
            StyledToolButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "sliders"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("The stores: add one, turn them on and off")
                Accessible.name: ToolTip.text
                onClicked: storesDialog.open()
            }
        }

        // ── where the apps come from: one tag per store turned on (a store
        // that could not be read says so in its tag), and the order forming
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: -4
            visible: store.stores.length > 0
            spacing: 10
            Eyebrow {
                text: qsTr("Sources")
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: store.stores
                    Tag {
                        id: sourceTag
                        required property var modelData
                        visible: modelData.enabled
                        text: modelData.name + (modelData.count > 0 ? "  " + modelData.count : "")
                        tone: modelData.error.length > 0 ? "warn" : "neutral"
                        iconName: modelData.error.length > 0 ? "warning"
                                : modelData.kind === "catalog" ? "shield-check" : "store"
                        ToolTip.visible: sourceHover.hovered
                        ToolTip.text: modelData.error.length === 0 ? modelData.address
                                    : modelData.offline
                                      ? qsTr("%1 did not answer (%2): its apps are the ones kept from last time.")
                                            .arg(modelData.name).arg(modelData.error)
                                      : qsTr("%1 could not be read: %2").arg(modelData.name).arg(modelData.error)
                        HoverHandler { id: sourceHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: storesDialog.open() }
                    }
                }
            }
            // The first time a store's downloads are counted: the order
            // forms as they come.
            Text {
                visible: store.countsTotal > 0 && !store.loading
                Layout.maximumWidth: 320
                elide: Text.ElideRight
                text: store.countsWaiting
                      ? qsTr("Ordering by downloads: the store is busy, waiting… %1 of %2").arg(store.countsDone).arg(store.countsTotal)
                      : qsTr("Ordering by downloads… %1 of %2").arg(store.countsDone).arg(store.countsTotal)
                color: Theme.textSecondary
                font.pixelSize: 11
            }
        }

        // ── what is needed, when it is not there
        Rectangle {
            visible: store.consoles.length === 0
                     || (store.targetType === "ps5" && !store.canInstall)
            Layout.fillWidth: true
            implicitHeight: needRow.implicitHeight + 24
            radius: Theme.radius
            color: Theme.alpha(Theme.warn, 0.08)
            border.width: 1
            border.color: Theme.alpha(Theme.warn, 0.4)
            RowLayout {
                id: needRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12
                Icon {
                    Layout.alignment: Qt.AlignTop
                    name: "info"
                    size: 18
                    color: Theme.light ? Qt.darker(Theme.warn, 1.3) : Theme.warn
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: 13
                    text: store.consoles.length === 0
                          ? qsTr("These apps are for a jailbroken console. Add yours under Settings → Consoles to install them.")
                          : qsTr("%1's FTP is not answering. Turn on FTP in etaHEN (FTP=1 in config.ini), with "
                                 + "kstuff and ShadowMountPlus running, to install app folders; packages wait "
                                 + "in the queue for its installer.").arg(store.targetName)
                }
            }
        }

        // ── the stores' catalog | the archives on this PC: a segmented row
        // of tabs (tabs that do not toggle: the filter says which)
        ShelfTile.TabTrack {
            implicitWidth: storeTabs.implicitWidth + 6
            implicitHeight: storeTabs.implicitHeight + 6
            Row {
                id: storeTabs
                anchors.centerIn: parent
                spacing: 2
                StyledTab {
                    checkable: false
                    checked: root.filter !== "local"
                    leftPadding: 14
                    rightPadding: 14
                    text: qsTr("Catalog") + "  " + store.apps.length
                    onClicked: if (root.filter === "local") root.filter = "all"
                }
                StyledTab {
                    checkable: false
                    checked: root.filter === "local"
                    leftPadding: 14
                    rightPadding: 14
                    text: qsTr("On this PC") + (store.localApps.length > 0 ? "  " + store.localApps.length : "")
                    onClicked: {
                        root.filter = "local"
                        store.scanLocal()
                    }
                }
            }
        }

        // ── search and the console on one line, the kinds under them
        RowLayout {
            visible: root.filter !== "local"
            Layout.fillWidth: true
            spacing: 10
            StyledField {
                Layout.fillWidth: true
                Layout.maximumWidth: 360
                placeholderText: qsTr("Search apps")
                text: root.search
                onTextChanged: root.search = text
            }
            // Only one console's apps: a click again shows both.
            Row {
                spacing: 4
                Repeater {
                    model: ["ps4", "ps5"]
                    FilterPill {
                        required property string modelData
                        readonly property string label: modelData === "ps4" ? "PS4" : "PS5"
                        iconName: "gamepad"
                        chosen: root.platform === modelData
                        text: label
                        count: String(modelData === "ps4" ? root.ps4Count : root.ps5Count)
                        tip: root.platform === modelData
                             ? qsTr("Showing only the apps for the %1. Click again to show those for both consoles.").arg(label)
                             : qsTr("Show only the apps for the %1").arg(label)
                        onClicked: root.platform = root.platform === modelData ? "" : modelData
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }
        Flow {
            visible: root.filter !== "local"
            Layout.fillWidth: true
            Layout.topMargin: -4
            spacing: 4
            FilterPill {
                chosen: root.filter === "all"
                text: qsTr("All")
                count: String(root.platformCount(root.platform))
                onClicked: root.filter = "all"
            }
            Repeater {
                model: root.kinds
                FilterPill {
                    required property string modelData
                    chosen: root.filter === modelData
                    text: root.kindLabel(modelData)
                    onClicked: root.filter = modelData
                }
            }
            FilterPill {
                visible: root.installedCount > 0
                iconName: "check"
                chosen: root.filter === "installed"
                text: qsTr("Installed", "filter: the apps installed")
                count: String(root.installedCount)
                onClicked: root.filter = "installed"
            }
            FilterPill {
                visible: root.updateCount > 0
                iconName: "arrow-up"
                chosen: root.filter === "updates"
                text: qsTr("Updates")
                count: String(root.updateCount)
                onClicked: root.filter = "updates"
            }
        }

        // ── the apps
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                id: grid
                anchors.fill: parent
                anchors.leftMargin: -6
                anchors.rightMargin: -6
                visible: root.filter !== "local"
                clip: true
                readonly property int columns: Math.max(1, Math.floor((width - 10) / 214))
                cellWidth: Math.floor((width - 10) / columns)
                cellHeight: 254
                model: root.shown
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: grid.contentHeight > grid.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }

                // An app, as a catalogue card: its icon large in a window of
                // glass lit by its own colours, its name in bold, who made it,
                // the facts in mono, then where it stands and the stores it
                // comes from, each by its own tag.
                delegate: Item {
                    id: cell
                    required property var modelData
                    width: grid.cellWidth
                    height: grid.cellHeight
                    readonly property bool busy: store.working === modelData.titleId
                    readonly property string iconUrl: modelData.icon ? String(modelData.icon) : ""
                    readonly property string state_: !modelData.available ? "soon"
                        : modelData.update ? "update"
                        : modelData.installed ? "installed"
                        : !modelData.installable ? "other" : ""

                    Plate {
                        id: card
                        anchors.fill: parent
                        anchors.margins: 6
                        hoverable: true
                        border.color: card.activeFocus ? Theme.alpha(Theme.accent, 0.85)
                                    : cell.modelData.update ? Theme.alpha(Theme.accent, card.hovered ? 0.7 : 0.45)
                                    : card.hovered ? (Theme.light ? Qt.rgba(1, 1, 1, 1) : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.16))
                                    : Theme.light ? Qt.rgba(1, 1, 1, 0.80) : Theme.glassEdge
                        border.width: card.activeFocus ? 2 : 1
                        opacity: cell.modelData.available ? 1.0 : 0.6
                        scale: cardArea.pressed ? Theme.pressScale : 1
                        Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }

                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: cell.modelData.name
                        Accessible.description: cell.modelData.author
                        Keys.onReturnPressed: detailDialog.ask(cell.modelData.titleId)
                        Keys.onSpacePressed: detailDialog.ask(cell.modelData.titleId)

                        MouseArea {
                            id: cardArea
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: detailDialog.ask(cell.modelData.titleId)
                        }

                        // The window: the icon's own colours, softened (the
                        // icon drawn tiny and stretched), under a pane of glass.
                        Rectangle {
                            id: window_
                            x: 7
                            y: 7
                            width: parent.width - 14
                            height: 100
                            radius: Theme.radius - 6
                            color: Theme.light ? Qt.rgba(1, 1, 1, 0.50) : Qt.rgba(0, 0, 0, 0.22)
                            Image {
                                anchors.fill: parent
                                visible: cell.iconUrl.length > 0 && status === Image.Ready
                                opacity: Theme.light ? 0.55 : 0.50
                                asynchronous: true
                                smooth: true
                                source: cell.iconUrl.length > 0
                                        ? "image://cover/12x5/1/crop/" + encodeURIComponent("#00000000") + "/" + encodeURIComponent(cell.iconUrl)
                                        : ""
                            }
                            // The glass over it: lighter at the top.
                            Rectangle {
                                anchors.fill: parent
                                radius: parent.radius
                                border.width: 1
                                border.color: Theme.light ? Theme.lightHairline : Theme.glassEdge
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.30 : 0.08) }
                                    GradientStop { position: 1.0; color: Theme.light ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.18) }
                                }
                            }
                            // The icon, lifted off the glass by a soft shadow.
                            Rectangle {
                                anchors.centerIn: appIcon
                                anchors.verticalCenterOffset: 4
                                width: appIcon.width
                                height: appIcon.height
                                radius: 16
                                color: Qt.rgba(0, 0, 0, Theme.light ? 0.10 : 0.30)
                            }
                            CoverArt {
                                id: appIcon
                                anchors.centerIn: parent
                                width: 68
                                height: 68
                                radius: 16
                                crop: true
                                picture: cell.iconUrl
                                placeholder: "archive"
                            }
                            // The consoles it is for, in the corner.
                            Row {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 8
                                spacing: 4
                                Repeater {
                                    model: cell.modelData.platforms !== undefined ? cell.modelData.platforms : []
                                    ShelfTile.CoverTag {
                                        required property string modelData
                                        text: modelData === "ps4" ? "PS4" : "PS5"
                                        tone: modelData === "ps4" ? "accent" : "neutral"
                                    }
                                }
                            }
                            // Where it stands, in the other corner.
                            ShelfTile.CoverTag {
                                visible: cell.state_.length > 0 && cell.state_ !== "other" && !cell.busy
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 8
                                tone: cell.state_ === "update" ? "accent" : cell.state_ === "installed" ? "ok" : "neutral"
                                iconName: cell.state_ === "update" ? "arrow-up" : cell.state_ === "installed" ? "check" : ""
                                text: cell.state_ === "soon" ? qsTr("Coming soon")
                                    : cell.state_ === "update" ? qsTr("Update")
                                    : qsTr("Installed", "one app")
                            }
                        }

                        ColumnLayout {
                            anchors.top: window_.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            anchors.topMargin: 12
                            anchors.bottomMargin: 12
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                maximumLineCount: 2
                                wrapMode: Text.Wrap
                                lineHeight: 1.05
                                text: cell.modelData.name
                                color: Theme.ink
                                font.family: Theme.fontDisplay
                                font.pixelSize: 15
                                font.weight: Font.Bold
                                font.letterSpacing: -0.2
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                elide: Text.ElideRight
                                text: cell.modelData.author || ""
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
                            // The facts: version, size, downloads — in the mono.
                            Row {
                                visible: !cell.busy
                                Layout.fillWidth: true
                                Layout.topMargin: 2
                                spacing: 10
                                clip: true
                                Text {
                                    visible: text.length > 0
                                    width: Math.min(implicitWidth, 90)
                                    elide: Text.ElideRight
                                    text: cell.modelData.version || ""
                                    color: Theme.text
                                    font.family: Theme.fontMono
                                    font.pixelSize: 11
                                }
                                Text {
                                    visible: text.length > 0
                                    text: cell.modelData.size || ""
                                    color: Theme.textSecondary
                                    font.family: Theme.fontMono
                                    font.pixelSize: 11
                                }
                                // How many times it was downloaded, as its store counts.
                                Row {
                                    visible: cell.modelData.downloads !== undefined && cell.modelData.downloads >= 0
                                    spacing: 3
                                    Icon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "download"
                                        size: 11
                                        color: Theme.textSecondary
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: cell.modelData.downloads !== undefined ? root.shortCount(cell.modelData.downloads) : ""
                                        color: Theme.textSecondary
                                        font.family: Theme.fontMono
                                        font.pixelSize: 11
                                    }
                                    HoverHandler { id: countHover }
                                    ToolTip.visible: countHover.hovered
                                    ToolTip.text: qsTr("Downloaded %1 times, as %2 counts")
                                                  .arg(Number(cell.modelData.downloads).toLocaleString(Qt.locale(), "f", 0))
                                                  .arg(cell.modelData.stores.join(", "))
                                }
                            }
                            Item { Layout.fillHeight: true }
                            // Why it cannot be installed from here, when it cannot.
                            Text {
                                visible: cell.state_ === "other" && !cell.busy
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: root.briefNote(cell.modelData)
                                color: Theme.textSecondary
                                font.pixelSize: 11
                            }
                            // The stores it comes from: a tag each.
                            Flow {
                                visible: !cell.busy
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: cell.modelData.stores
                                    Tag {
                                        required property string modelData
                                        implicitHeight: 20
                                        iconName: "store"
                                        text: modelData
                                    }
                                }
                            }
                            Rectangle {
                                visible: cell.busy
                                Layout.fillWidth: true
                                height: 3
                                radius: 1.5
                                color: Theme.alpha(Theme.accent, 0.18)
                                Rectangle {
                                    width: parent.width * Math.max(0.03, Math.min(1, store.progress))
                                    height: parent.height
                                    radius: parent.radius
                                    color: Theme.accent
                                    Behavior on width { NumberAnimation { duration: 200 } }
                                }
                            }
                            // How far, and how fast; the stage when there is nothing to count.
                            Text {
                                visible: cell.busy
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: root.progressLine().length > 0 ? root.progressLine() : root.stageText(store.stage)
                                color: Theme.textSecondary
                                font.family: Theme.fontMono
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            // ── "On this PC": the app archives of a folder of this PC
            ColumnLayout {
                anchors.fill: parent
                visible: root.filter === "local"
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Icon {
                        name: "folder"
                        size: 16
                        color: Theme.textSecondary
                    }
                    Text {
                        Layout.fillWidth: true
                        text: store.localFolder
                        color: Theme.text
                        font.family: Theme.fontMono
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                    StyledButton {
                        chip: true
                        text: qsTr("Change…")
                        onClicked: localFolderDialog.open()
                    }
                    StyledButton {
                        chip: true
                        iconName: "plus"
                        text: qsTr("Install a file…")
                        enabled: store.canInstall && store.working.length === 0
                        onClicked: localFileDialog.open()
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: Theme.glassEdge
                }
                // A hairline list: each archive, what it holds, and installing it.
                ListView {
                    id: localList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 0
                    model: store.localApps
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        id: localRow
                        required property var modelData
                        required property int index
                        readonly property bool usable: (modelData.problem || "").length === 0
                        readonly property bool busy: store.working.length > 0 && store.working === modelData.titleId
                        width: localList.width - 14
                        implicitHeight: localRowLayout.implicitHeight + 24
                        radius: Theme.radiusSmall
                        color: modelData.update ? Theme.alpha(Theme.accent, 0.08)
                             : localHover.hovered ? Theme.alpha(Theme.ink, 0.05) : "transparent"
                        HoverHandler { id: localHover }
                        Rectangle {
                            visible: localRow.index < localList.count - 1
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: 70
                            height: 1
                            color: Theme.glassEdge
                        }
                        RowLayout {
                            id: localRowLayout
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 8
                            spacing: 14
                            Rectangle {
                                implicitWidth: 44
                                implicitHeight: 44
                                radius: 12
                                color: Theme.controlFill
                                border.width: 1
                                border.color: Theme.glassEdge
                                Icon {
                                    anchors.centerIn: parent
                                    name: localRow.usable ? "package" : "warning"
                                    size: 20
                                    color: localRow.usable ? Theme.textSecondary : Theme.warn
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    Layout.fillWidth: true
                                    text: localRow.modelData.name || localRow.modelData.file
                                    color: Theme.ink
                                    font.family: Theme.fontDisplay
                                    font.pixelSize: 16
                                    font.weight: Font.Bold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: localRow.usable
                                          ? [localRow.modelData.titleId, localRow.modelData.version,
                                             localRow.modelData.sizeText, localRow.modelData.file].filter(function (t) {
                                                return t && t.length > 0
                                            }).join("  ·  ")
                                          : qsTr("Not an app archive: %1").arg(localRow.modelData.problem)
                                    color: localRow.usable ? Theme.textSecondary
                                         : Theme.light ? Qt.darker(Theme.warn, 1.3) : Theme.warn
                                    font.family: localRow.usable ? Theme.fontMono : Theme.fontSans
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                            Tag {
                                visible: localRow.modelData.installed === true
                                tone: localRow.modelData.update ? "accent" : "ok"
                                iconName: localRow.modelData.update ? "arrow-up" : "check"
                                text: localRow.modelData.update
                                      ? qsTr("Newer than on the PS5 (%1)").arg(localRow.modelData.installedVersion)
                                      : qsTr("On the PS5")
                            }
                            StyledButton {
                                visible: localRow.usable
                                primary: !localRow.modelData.installed || localRow.modelData.update
                                iconName: localRow.busy ? "loader" : "download"
                                text: localRow.modelData.update ? qsTr("Update")
                                      : localRow.modelData.installed ? qsTr("Install again") : qsTr("Install")
                                enabled: store.canInstall && store.working.length === 0
                                onClicked: store.installLocal(localRow.modelData.path)
                            }
                        }
                    }
                }
            }

            // Reading: the shape of the apps about to come.
            Grid {
                id: storeSkeletons
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: root.filter === "local" ? 56 : 0
                visible: store.loading && (root.filter === "local" ? store.localApps.length === 0 : root.shown.length === 0)
                columns: root.filter === "local" ? 1 : grid.columns
                Repeater {
                    model: storeSkeletons.visible ? storeSkeletons.columns * (root.filter === "local" ? 5 : 3) : 0
                    Item {
                        required property int index
                        width: storeSkeletons.width / storeSkeletons.columns
                        height: root.filter === "local" ? 68 : 120
                        SkeletonRow {
                            anchors.fill: parent
                            anchors.margins: root.filter === "local" ? 4 : 6
                            delay: parent.index * 80
                        }
                    }
                }
            }

            // Empty: the faint orb, what is (not) here, and the way on.
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, 460)
                visible: !store.loading && (root.filter === "local" ? store.localApps.length === 0 : root.shown.length === 0)
                spacing: 10
                Image {
                    Layout.alignment: Qt.AlignHCenter
                    source: "qrc:/icons/planet.png"
                    sourceSize.width: 104
                    sourceSize.height: 104
                    fillMode: Image.PreserveAspectFit
                    opacity: Theme.light ? 0.35 : 0.45
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: root.filter === "local"
                          ? (store.localScanning ? qsTr("Reading the folder…")
                             : qsTr("No app archives (.zip) in this folder. Choose another, or install a file."))
                        : store.loading ? qsTr("Reading the catalog…")
                        : store.error.length > 0 && store.apps.length === 0
                          ? qsTr("The catalog could not be read: %1").arg(store.error)
                        : qsTr("Nothing here.")
                    color: Theme.ink
                    font.family: Theme.fontDisplay
                    font.pixelSize: root.filter === "local" || store.error.length > 0 ? 17 : Theme.fontHeading
                    font.weight: Font.Bold
                }
                StyledButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 6
                    visible: root.filter === "local" ? !store.localScanning : store.error.length > 0 && store.apps.length === 0
                    iconName: root.filter === "local" ? "folder-open" : "refresh"
                    text: root.filter === "local" ? qsTr("Change…") : qsTr("Read the stores again")
                    onClicked: root.filter === "local" ? localFolderDialog.open() : store.refresh()
                }
                StyledButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 6
                    visible: root.filter !== "local" && store.apps.length > 0
                             && (root.filter !== "all" || root.platform.length > 0 || root.search.length > 0)
                    text: qsTr("Show all")
                    onClicked: { root.filter = "all"; root.platform = ""; root.search = "" }
                }
            }
        }

        // The one thing to know, said quietly, under a hairline.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.glassEdge
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: -4
            spacing: 8
            Icon {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 1
                name: "info"
                size: 13
                color: Theme.textSecondary
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                // Two lines at most, so the catalogue keeps the room; the
                // whole of it under the mouse.
                maximumLineCount: 2
                elide: Text.ElideRight
                HoverHandler { id: storeNoteHover }
                ToolTip.visible: storeNoteHover.hovered && truncated
                ToolTip.text: text
                color: Theme.textSecondary
                font.pixelSize: 11
                text: root.filter === "local"
                      ? qsTr("An archive of this PC installs the same way, as its folder in /data/homebrew; being "
                             + "your own file, nothing checks it against the catalog. Only install what you trust.")
                      : qsTr("homebrew.page's apps install as their folder in /data/homebrew on a PS5, checked "
                           + "against the catalog first; ShadowMountPlus (with kstuff) puts them on the home "
                           + "screen. A store's packages are downloaded to this PC (checked against their MD5 when "
                           + "the store has one) and installed through the queue on the console in use. The apps "
                           + "are their authors' work, under their own licences.")
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
        // What ShadowMountPlus said about this app, when it was asked.
        readonly property var check: store.homeCheck.titleId === app.titleId ? store.homeCheck : ({})
        readonly property bool checking: check.busy === true
        function verdictText(c) {
            switch (c.verdict) {
            case "registered":
                return qsTr("ShadowMountPlus registered it: it is on the home screen (at the end, or in the "
                            + "game library). If not, restart the console once.")
            case "settling":
                return qsTr("ShadowMountPlus saw it while its files were still changing; it looks again every "
                            + "15 seconds.")
            case "bad-metadata":
                return qsTr("ShadowMountPlus cannot read its sce_sys/param.json. Install it again.")
            case "failed":
                return qsTr("The console refused to register it%1. The app may need a newer ShadowMountPlus "
                            + "(see its notes above); update it and install again.")
                       .arg(c.code !== undefined && c.code.length > 0 ? " (" + c.code + ")" : "")
            case "gave-up":
                return c.rescanned
                    ? qsTr("ShadowMountPlus had given up on it after failed tries; it was asked to try again now.")
                    : qsTr("ShadowMountPlus gave up on it after failed tries. Restart the console, or let "
                           + "OrbisLink ask it to try again: api_bind_address=0.0.0.0 in "
                           + "/data/shadowmount/config.ini.")
            case "duplicate":
                return qsTr("Another copy with the same title ID is in a folder ShadowMountPlus checks (another "
                            + "drive, or etaHEN/games): ShadowMountPlus keeps that one. Remove one of them.")
            case "not-seen":
                return qsTr("ShadowMountPlus has not mentioned it. Is it running? (\"ShadowMount+\" shows on the "
                            + "console when it starts.) It checks /data/homebrew every 15 seconds.")
            case "no-log":
                return qsTr("This PS5 has no ShadowMountPlus log: it is not installed, or its log is off. It is "
                            + "what puts these apps on the home screen.")
            case "no-ftp":
                return qsTr("This PS5's FTP is not answering.")
            }
            return ""
        }
        // The same app as listed now (installed, update), with what was read.
        readonly property var listed: {
            for (var i = 0; i < store.apps.length; ++i)
                if (store.apps[i].titleId === app.titleId)
                    return store.apps[i]
            return app
        }
        readonly property var sources: listed.sources !== undefined ? listed.sources : []
        readonly property bool fromCatalog: {
            for (var i = 0; i < sources.length; ++i)
                if (sources[i].format === "folder")
                    return true
            return false
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
        background: ShelfTile.DialogGlass {}
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
                        // The consoles it is for, and how many times it was downloaded.
                        RowLayout {
                            readonly property var platforms: detailDialog.listed.platforms !== undefined ? detailDialog.listed.platforms : []
                            readonly property real downloads: detailDialog.listed.downloads !== undefined ? detailDialog.listed.downloads : -1
                            visible: platforms.length > 0 || downloads >= 0
                            spacing: 6
                            Repeater {
                                model: parent.platforms
                                PlatformTag {
                                    required property string modelData
                                    platform: modelData
                                }
                            }
                            Text {
                                visible: parent.downloads >= 0
                                text: qsTr("Downloaded %1 times").arg(Number(parent.downloads).toLocaleString(Qt.locale(), "f", 0))
                                color: Theme.textSecondary
                                font.pixelSize: 12
                            }
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
                        // Installed but not on the home screen: ShadowMountPlus,
                        // which puts it there, says why in its log.
                        StyledButton {
                            visible: detailDialog.listed.installedAs === "folder" && !detailDialog.busy
                            chip: true
                            iconName: detailDialog.checking ? "loader" : "info"
                            text: qsTr("Not on the home screen?")
                            enabled: !detailDialog.checking
                            onClicked: store.checkHomeScreen(detailDialog.listed.titleId)
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
                // ShadowMountPlus's word on it.
                Rectangle {
                    visible: detailDialog.check.verdict !== undefined && !detailDialog.checking
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    implicitHeight: checkColumn.implicitHeight + 24
                    radius: 10
                    color: Theme.panelAltFill
                    border.width: 1
                    border.color: detailDialog.check.verdict === "registered" ? Theme.alpha(Theme.ok, 0.5)
                                : Theme.alpha(Theme.warn, 0.5)
                    ColumnLayout {
                        id: checkColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 6
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: detailDialog.verdictText(detailDialog.check)
                            color: Theme.text
                            font.pixelSize: 12
                        }
                        Text {
                            visible: detailDialog.check.lines !== undefined && detailDialog.check.lines.length > 0
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            text: detailDialog.check.lines !== undefined ? detailDialog.check.lines.slice(-10).join("\n") : ""
                            color: Theme.textSecondary
                            font.pixelSize: 11
                            font.family: Theme.fontMono
                        }
                    }
                }
                Spinner {
                    Layout.alignment: Qt.AlignHCenter
                    visible: detailDialog.app.ready !== true
                    size: 28
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
                    // The catalog's words, else the store's few lines.
                    readonly property string words: detailDialog.app.description !== undefined
                                                    && detailDialog.app.description.length > 0
                                                    ? detailDialog.app.description
                                                    : detailDialog.listed.description !== undefined
                                                      ? detailDialog.listed.description : ""
                    visible: words.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    text: words
                    color: Theme.text
                    font.pixelSize: 13
                    lineHeight: 1.2
                }
                // Where it can come from: each store, what it has of it and
                // whether that goes on this console.
                ColumnLayout {
                    visible: detailDialog.sources.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    spacing: 4
                    Text {
                        text: qsTr("In the stores")
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: detailDialog.sources
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                implicitWidth: sourceTag.implicitWidth + 12
                                implicitHeight: 18
                                radius: 5
                                color: Theme.alpha(Theme.textSecondary, 0.10)
                                border.width: 1
                                border.color: Theme.alpha(Theme.textSecondary, 0.25)
                                Text {
                                    id: sourceTag
                                    anchors.centerIn: parent
                                    text: parent.parent.modelData.storeName
                                    color: Theme.textSecondary
                                    font.pixelSize: 10
                                    font.weight: Font.Medium
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: [root.formatLabel(parent.modelData.format), parent.modelData.version,
                                       parent.modelData.size,
                                       parent.modelData.firmware !== undefined && parent.modelData.firmware.length > 0
                                       ? qsTr("PS4 firmware %1").arg(parent.modelData.firmware) : ""]
                                      .filter(function (t) { return t && t.length > 0 }).join("  ·  ")
                                      + (parent.modelData.note.length > 0 ? "\n" + parent.modelData.note : "")
                                color: parent.modelData.installable ? Theme.textSecondary : Theme.warn
                                font.pixelSize: 12
                            }
                        }
                    }
                }
                // What it does on the console, as the catalog's scan found.
                ColumnLayout {
                    visible: detailDialog.app.ready === true && detailDialog.fromCatalog
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
                    visible: detailDialog.listed.installedAs === "folder" && !detailDialog.busy
                    danger: true
                    iconName: "trash"
                    text: qsTr("Remove")
                    enabled: store.canInstall && store.working.length === 0
                    onClicked: store.uninstall(detailDialog.listed.titleId)
                }
                Item {
                    visible: !detailDialog.busy
                    Layout.fillWidth: true
                }
                // Installing: how far it got, always in sight however long
                // the description above is.
                ColumnLayout {
                    visible: detailDialog.busy
                    Layout.fillWidth: true
                    Layout.rightMargin: 8
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: root.stageText(store.stage)
                                  + (store.amountText.length > 0 ? "  ·  " + store.amountText : "")
                            color: Theme.text
                            font.pixelSize: 12
                        }
                        Text {
                            text: root.progressLine()
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
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
                    readonly property int ready: root.readySources(detailDialog.listed).length
                    visible: !detailDialog.busy && detailDialog.listed.installable === true
                    primary: true
                    iconName: detailDialog.listed.update ? "arrow-up" : "download"
                    text: (detailDialog.listed.update ? qsTr("Update on %1").arg(store.targetName)
                        : detailDialog.listed.installed ? qsTr("Install again")
                        : store.targetName.length > 0 ? qsTr("Install on %1").arg(store.targetName) : qsTr("Install"))
                          + (ready > 1 ? "…" : "")
                    enabled: ready > 0 && store.working.length === 0
                    onClicked: root.installApp(detailDialog.listed)
                }
            }
        }
    }

    // The PS5 apps go to.
    Menu {
        id: targetMenu
        topPadding: 8
        bottomPadding: 8
        background: ShelfTile.MenuGlass { implicitWidth: 260 }
        Instantiator {
            model: store.consoles
            delegate: StyledMenuItem {
                required property var modelData
                text: modelData.name + (modelData.type.length > 0 ? "  ·  " + modelData.type.toUpperCase() : "")
                      + (modelData.ftp ? "" : "  ·  " + qsTr("no FTP"))
                iconName: modelData.target ? "check" : "gamepad"
                onTriggered: store.setTarget(modelData.address)
            }
            onObjectAdded: (index, object) => targetMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => targetMenu.removeItem(object)
        }
    }

    // ── which store an app in more than one comes from
    Dialog {
        id: sourceDialog
        property var app: ({})
        property var choices: []
        function ask(app, choices) {
            sourceDialog.app = app
            sourceDialog.choices = choices
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
            title: qsTr("Install %1 from which store?").arg(sourceDialog.app.name !== undefined ? sourceDialog.app.name : "")
            dialog: sourceDialog
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                text: qsTr("It is in more than one store. Each has its own copy, and it goes on %1 from the one you pick.")
                      .arg(store.targetName)
                color: Theme.textSecondary
                font.pixelSize: 13
            }
            Repeater {
                model: sourceDialog.choices
                Rectangle {
                    id: choice
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    implicitHeight: 52
                    radius: 12
                    color: choiceArea.containsMouse ? Theme.controlHover : Theme.panelAltFill
                    border.width: 1
                    border.color: Theme.glassEdge
                    MouseArea {
                        id: choiceArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            store.install(sourceDialog.app.titleId, choice.modelData.store)
                            sourceDialog.close()
                        }
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 10
                        Icon {
                            name: choice.modelData.format === "folder" ? "folder" : "package"
                            size: 18
                            color: Theme.textSecondary
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: choice.modelData.storeName
                                color: Theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: [root.formatLabel(choice.modelData.format), choice.modelData.version,
                                       choice.modelData.size].filter(function (t) { return t && t.length > 0 }).join("  ·  ")
                                color: Theme.textSecondary
                                font.pixelSize: 11
                            }
                        }
                        Icon {
                            name: "download"
                            size: 16
                            color: Theme.accent
                        }
                    }
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
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancel")
                    onClicked: sourceDialog.close()
                }
            }
        }
    }

    // ── the stores: those built in and the user's, on or off, and adding one
    Dialog {
        id: storesDialog
        property string message: ""
        property bool failed: false
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(620, parent.width - 32)
        height: Math.min(implicitHeight, parent.height - 48)
        modal: true
        padding: 0
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: ShelfTile.DialogGlass {}
        onOpened: {
            message = ""
            newStoreField.text = ""
            newStoreName.text = ""
        }
        Connections {
            target: store
            function onStoreAdded(ok, message) {
                storesDialog.failed = !ok
                storesDialog.message = message
                if (ok) {
                    newStoreField.text = ""
                    newStoreName.text = ""
                }
            }
        }
        header: DialogHeader {
            title: qsTr("Stores")
            dialog: storesDialog
        }
        contentItem: ScrollView {
            id: storesScroll
            clip: true
            implicitHeight: Math.min(storesColumn.implicitHeight, 520)
            contentWidth: availableWidth
            ColumnLayout {
                id: storesColumn
                width: storesScroll.availableWidth
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    wrapMode: Text.WordWrap
                    text: qsTr("The apps of every store turned on are shown together; an app in more than one shows "
                               + "each store's tag, and installing it asks which to take it from.")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
                Repeater {
                    model: store.stores
                    Rectangle {
                        id: storeRow
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.dialogMargin
                        Layout.rightMargin: Theme.dialogMargin
                        implicitHeight: storeRowLayout.implicitHeight + 20
                        radius: 12
                        color: Theme.panelAltFill
                        border.width: 1
                        border.color: Theme.glassEdge
                        opacity: modelData.enabled ? 1.0 : 0.7
                        RowLayout {
                            id: storeRowLayout
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 14
                            anchors.rightMargin: 10
                            spacing: 10
                            Icon {
                                name: storeRow.modelData.kind === "catalog" ? "shield-check" : "store"
                                size: 18
                                color: storeRow.modelData.enabled ? Theme.accent : Theme.textSecondary
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: storeRow.modelData.name
                                    color: Theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: [storeRow.modelData.address,
                                           storeRow.modelData.kind === "catalog"
                                           ? qsTr("signed catalog · PS5 app folders") : qsTr("packages · PS4 and PS5"),
                                           storeRow.modelData.enabled && storeRow.modelData.count > 0
                                           ? qsTr("%n app(s)", "", storeRow.modelData.count) : ""]
                                          .filter(function (t) { return t.length > 0 }).join("  ·  ")
                                    color: Theme.textSecondary
                                    font.pixelSize: 11
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: storeRow.modelData.enabled && storeRow.modelData.error.length > 0
                                    wrapMode: Text.WordWrap
                                    text: storeRow.modelData.offline
                                          ? qsTr("Did not answer (%1): the apps kept from last time are shown.").arg(storeRow.modelData.error)
                                          : storeRow.modelData.error
                                    color: Theme.warn
                                    font.pixelSize: 11
                                }
                            }
                            // homebrew.page's catalog can come from a mirror.
                            StyledButton {
                                visible: storeRow.modelData.kind === "catalog"
                                chip: true
                                iconName: "link"
                                text: store.catalogAddress.length > 0 ? qsTr("Mirror ✓") : qsTr("Mirror…")
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("A mirror of the catalog, tried first")
                                onClicked: catalogDialog.ask()
                            }
                            StyledCheck {
                                Layout.fillWidth: false
                                implicitWidth: 40
                                checked: storeRow.modelData.enabled
                                ToolTip.visible: hovered
                                ToolTip.text: checked ? qsTr("Shown: click to leave its apps out")
                                                      : qsTr("Left out: click to show its apps")
                                onToggled: store.setStoreEnabled(storeRow.modelData.url, checked)
                            }
                            StyledToolButton {
                                visible: !storeRow.modelData.builtIn
                                iconName: "trash"
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Take this store away")
                                onClicked: store.removeStore(storeRow.modelData.url)
                            }
                        }
                    }
                }
                // Adding one.
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    Layout.topMargin: 4
                    text: qsTr("Add a store")
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    wrapMode: Text.WordWrap
                    text: qsTr("The address of a Homebrew Store (the one whose store.db the PS4's Homebrew Store "
                               + "reads, as PKG-Zone's). Its apps are its own: nothing signs them, so only add stores "
                               + "you trust.")
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    spacing: 8
                    StyledField {
                        id: newStoreField
                        Layout.fillWidth: true
                        placeholderText: "https://…"
                        enabled: !store.addingStore
                        onTextEdited: storesDialog.message = ""
                        onAccepted: addStoreButton.clicked()
                    }
                    StyledField {
                        id: newStoreName
                        Layout.preferredWidth: 150
                        placeholderText: qsTr("Name (optional)")
                        enabled: !store.addingStore
                        onAccepted: addStoreButton.clicked()
                    }
                    StyledButton {
                        id: addStoreButton
                        primary: true
                        iconName: store.addingStore ? "loader" : "plus"
                        text: store.addingStore ? qsTr("Looking…") : qsTr("Add")
                        enabled: !store.addingStore && newStoreField.text.trim().length > 0
                        onClicked: {
                            if (!store.addStore(newStoreField.text, newStoreName.text)) {
                                storesDialog.failed = true
                                storesDialog.message = qsTr("That is not an http(s) address.")
                            }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    visible: storesDialog.message.length > 0
                    wrapMode: Text.WordWrap
                    text: storesDialog.message
                    color: storesDialog.failed ? Theme.error : Theme.ok
                    font.pixelSize: 12
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
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Close")
                    onClicked: storesDialog.close()
                }
            }
        }
    }

    // ── the catalog's address: a mirror of the user's own, tried first
    Dialog {
        id: catalogDialog
        property bool invalid: false
        function ask() {
            addressField.text = store.catalogAddress
            invalid = false
            open()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 32)
        modal: true
        padding: 0
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: ShelfTile.DialogGlass {}
        onOpened: addressField.forceActiveFocus()
        header: DialogHeader {
            title: qsTr("The catalog's address")
            dialog: catalogDialog
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
                text: qsTr("A mirror of the catalog, tried before %1 and its own mirror. Whatever it serves is "
                           + "checked against the catalog's signature, as always: a mirror cannot change the apps. "
                           + "Empty, the built-in addresses alone.").arg(store.defaultCatalogAddress())
            }
            StyledField {
                id: addressField
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                placeholderText: store.defaultCatalogAddress()
                onTextEdited: catalogDialog.invalid = false
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                visible: catalogDialog.invalid
                text: qsTr("That is not an http(s) address.")
                color: Theme.error
                font.pixelSize: 12
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
                    text: qsTr("Built-in addresses")
                    enabled: store.catalogAddress.length > 0
                    onClicked: {
                        store.setCatalogAddress("")
                        catalogDialog.close()
                    }
                }
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancel")
                    onClicked: catalogDialog.close()
                }
                StyledButton {
                    text: qsTr("Save")
                    primary: true
                    onClicked: {
                        if (store.setCatalogAddress(addressField.text))
                            catalogDialog.close()
                        else
                            catalogDialog.invalid = true
                    }
                }
            }
        }
    }

    FolderDialog {
        id: localFolderDialog
        title: qsTr("The folder with your app archives")
        currentFolder: store.localFolderUrl()
        onAccepted: store.setLocalFolder(selectedFolder)
    }
    FileDialog {
        id: localFileDialog
        title: qsTr("An app archive to install")
        currentFolder: store.localFolderUrl()
        nameFilters: [qsTr("App archives (*.zip)")]
        onAccepted: store.installLocal(selectedFile.toString())
    }
}
