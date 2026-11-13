// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1280
    height: 800
    // The navigation folded, the transfers open and a page between them fit
    // from here.
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    color: Theme.background
    title: qsTr("OrbisLink — %1").arg(app.consoleName)

    property bool panelVisible: true
    // What the console in use offers besides Remote Play. When it has neither
    // FTP nor the installer (a PS5 with no jailbreak), the side panel has
    // nothing to show and stays closed — unless there are still items in the
    // queue to follow.
    readonly property bool ftpAbsent: app.ftpState === "unavailable" || app.ftpState === "not-applicable"
    readonly property bool installerAbsent: app.installerState === "unavailable"
                                            || app.installerState === "not-applicable"
    readonly property bool panelAllowed: !(ftpAbsent && installerAbsent) || app.queue.count > 0
    // Whether the transfers' spine is on screen.
    readonly property bool panelShown: panelVisible && panelAllowed && !streamFullscreen
    // The panel closed while jobs go on: a narrow column with them instead,
    // until they are done.
    readonly property bool railShown: !panelShown && !streamFullscreen && transferPanel.busy
    readonly property bool sideShown: panelShown || railShown
    // What the main area shows: the consoles ("home"), "games", "store",
    // "saves" or "payloads".
    property string view: typeof demoView !== "undefined" && demoView.length > 0
                          ? (demoView === "store-local" ? "store"
                             : demoView === "payloads-library" ? "payloads" : demoView)
                          : typeof demoGames !== "undefined" && demoGames ? "games" : "home"
    readonly property string panelLockReason: qsTr("This console only offers Remote Play: it has "
        + "neither FTP nor the remote installer running (a jailbreak adds them).")
    // Full screen for the stream: hides the navigation and the transfers,
    // and the window takes up the whole screen.
    property bool streamFullscreen: false

    // Remote Play's picture on screen: a page opened then shows over it
    // (SectionOverlay), so the game stays in sight; otherwise it takes the
    // main area, as always.
    readonly property bool demoSectionOn: typeof demoSection !== "undefined" && demoSection.length > 0
    readonly property bool streamOnScreen: (demoSectionOn || typeof stream !== "undefined" && stream !== null
                                            && stream.streaming) && view === "home" && !streamFullscreen
    // The page over the picture ("games", "store", "saves", "payloads"), or "".
    property string overSection: ""
    onStreamOnScreenChanged: if (!streamOnScreen) overSection = ""
    // The keyboard goes with what is in front: the page while it is open,
    // the console again once it closes.
    onOverSectionChanged: {
        if (overSection.length > 0)
            sectionOverlay.forceActiveFocus()
        else
            streamArea.focusPicture()
    }
    // A page, open (over the picture while it plays), whatever was before.
    function showSection(name) {
        if (streamOnScreen)
            overSection = name
        else
            view = name
    }
    // A page's own way back: out of the overlay, or back to the consoles.
    function closeSection() {
        if (overSection.length > 0)
            overSection = ""
        else
            view = "home"
    }
    function sectionTitle(name) {
        switch (name) {
        case "games": return qsTr("Games")
        case "store": return qsTr("Homebrew store")
        case "saves": return qsTr("Save vault")
        case "payloads": return qsTr("Payloads")
        }
        return ""
    }
    function sectionIcon(name) {
        switch (name) {
        case "games": return "disc"
        case "store": return "store"
        case "saves": return "archive"
        case "payloads": return "zap"
        }
        return ""
    }

    // The navigation: open (wide, with names) or folded to its icons. Left
    // to itself it folds when the window is narrow; a click on its foot
    // decides instead, until the app closes.
    property var navChoice: undefined
    readonly property bool navCompact: navChoice !== undefined ? navChoice : width < 1140

    // A page from the navigation: over the picture while it plays (the same
    // page again closes it), in the main area otherwise.
    function goTo(name) {
        if (streamOnScreen) {
            overSection = overSection === name ? "" : name
            return
        }
        overSection = ""
        view = name
    }
    // The consoles — or, while Remote Play plays, the picture alone.
    function showHome() {
        overSection = ""
        view = "home"
    }
    function togglePanel() {
        if (window.panelAllowed)
            window.panelVisible = !window.panelVisible
        else
            toast.show(window.panelLockReason, false)
    }

    function setStreamFullscreen(isActive) {
        if (streamFullscreen === isActive)
            return
        streamFullscreen = isActive
        window.visibility = isActive ? Window.FullScreen : Window.Windowed
    }

    // F9 shows and hides the side panel.
    Shortcut {
        sequence: "F9"
        onActivated: window.togglePanel()
    }
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: settingsDialog.loadValues(), settingsDialog.open()
    }
    // The log one shortcut away: when something goes wrong, it is the
    // first place to look.
    Shortcut {
        sequence: "Ctrl+L"
        onActivated: diagnosticsDialog.open()
    }
    // F11 enters and leaves full screen; Esc only leaves (inside the stream,
    // Esc ends the session — see StreamVideo.qml).
    Shortcut {
        sequence: "F11"
        onActivated: window.setStreamFullscreen(!window.streamFullscreen)
    }

    // The background the glass takes its light from.
    GlassBackground { anchors.fill: parent }

    // The window of glass the whole application sits in, floating over the
    // room; inside it, its panes. In full screen only the picture.
    Plate {
        id: glassWindow
        level: "window"
        anchors.fill: parent
        anchors.margins: window.streamFullscreen ? 0 : 16
        visible: !window.streamFullscreen
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: window.streamFullscreen ? 0 : 16 + 14
        spacing: window.streamFullscreen ? 0 : Theme.paneGap

        // ───────────────────────────── the navigation
        SideNav {
            id: sideNav
            visible: !window.streamFullscreen
            Layout.fillHeight: true
            Layout.preferredWidth: visible ? (compact ? Theme.sideNavCompact : Theme.sideNavWidth) : 0
            compact: window.navCompact
            transfers: transferPanel.cardCount
            transfersOpen: window.panelShown
            transfersAllowed: window.panelAllowed
            onFoldToggled: window.navChoice = !window.navCompact
            onTransfersToggled: window.togglePanel()
            onLogRequested: diagnosticsDialog.open()
            onSettingsRequested: { settingsDialog.loadValues(); settingsDialog.open() }
            onCustomThemeRequested: {
                settingsDialog.loadValues()
                settingsDialog.section = 4
                settingsDialog.open()
            }
            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.paneGap

            // ───────────────────────────── the toolbar
            TopBar {
                visible: !window.streamFullscreen
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.toolbarHeight
                transfers: transferPanel.cardCount
                transfersOpen: window.panelShown
                transfersAllowed: window.panelAllowed
                onTransfersToggled: window.togglePanel()
                onLogRequested: diagnosticsDialog.open()
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.paneGap

                // ───────────────────────────── the page
                Plate {
                    id: sheet
                    level: "primary"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // In full screen the picture has the whole window.
                    color: window.streamFullscreen ? "transparent" : Qt.rgba(1, 1, 1, fillAlpha)
                    border.width: window.streamFullscreen ? 0 : 1
                    radius: window.streamFullscreen ? 0 : Theme.radiusPane

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            // Each page's place (the pages themselves are further
                            // down: over Remote Play they move into the overlay).
                            Item {
                                id: gamesSlot
                                anchors.fill: parent
                                visible: window.view === "games"
                            }
                            Item {
                                id: savesSlot
                                anchors.fill: parent
                                visible: window.view === "saves"
                            }
                            Item {
                                id: payloadsSlot
                                anchors.fill: parent
                                visible: window.view === "payloads"
                            }
                            Item {
                                id: storeSlot
                                anchors.fill: parent
                                visible: window.view === "store"
                            }

                            Item {
                                anchors.fill: parent
                                visible: window.view !== "games" && window.view !== "saves" && window.view !== "store"
                                         && window.view !== "payloads"

                                StreamArea {
                                    id: streamArea
                                    anchors.fill: parent
                                    // Back to the home page: whatever was open on top of
                                    // the cards gets out of the way of the console just
                                    // registered.
                                    onRegisteredHere: {
                                        settingsDialog.close()
                                        firstRunWizard.close()
                                    }
                                }

                                // A page over the picture, while Remote Play plays.
                                SectionOverlay {
                                    id: sectionOverlay
                                    anchors.fill: parent
                                    section: window.overSection
                                    title: window.sectionTitle(window.overSection)
                                    iconName: window.sectionIcon(window.overSection)
                                    onClosed: window.overSection = ""
                                }
                            }
                        }

                        // ── The status line, at the foot of the page.
                        Item {
                            Layout.fillWidth: true
                            visible: !window.streamFullscreen
                            implicitHeight: visible ? 34 : 0

                            EngravedRule {
                                anchors.top: parent.top
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: Theme.gutter
                                anchors.rightMargin: Theme.gutter
                                height: 1
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.gutter
                                anchors.rightMargin: Theme.gutter
                                spacing: 14

                                Icon {
                                    visible: app.statusMessage.length > 0
                                    name: "check-circle"
                                    size: 13
                                    color: Theme.ok
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: app.statusMessage
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: qsTr("Local HTTP: %1").arg(app.httpServerAddress)
                                    color: Theme.alpha(Theme.textSecondary, 0.85)
                                    font.pixelSize: 11
                                    font.family: Theme.fontMono
                                }
                                Text {
                                    text: "v" + app.version
                                    color: Theme.alpha(Theme.textSecondary, 0.7)
                                    font.pixelSize: 11
                                    font.family: Theme.fontMono
                                }
                            }
                        }
                    }
                }

                // ───────────────────────────── the transfers
                // The queue and the console's files, in their own pane.
                Plate {
                    level: "primary"
                    Layout.fillHeight: true
                    Layout.preferredWidth: Theme.inspectorWidth
                    visible: window.panelShown

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 14
                        anchors.topMargin: 16
                        anchors.bottomMargin: 14
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Eyebrow { text: qsTr("Transfers") }
                                Text {
                                    Layout.fillWidth: true
                                    text: app.consoleName
                                    color: Theme.ink
                                    font.pixelSize: 17
                                    font.weight: Font.Bold
                                    elide: Text.ElideRight
                                }
                            }
                            StyledToolButton {
                                iconName: "close"
                                iconSize: 15
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Hide the transfers (F9)")
                                onClicked: window.panelVisible = false
                            }
                        }

                        // Queue and files as a segmented control. Without FTP on
                        // the console there is only the queue: no tabs. With
                        // nothing in the queue there is only the file list: the
                        // queue's tab comes back with its first job.
                        TabBar {
                            id: tabs
                            visible: !window.ftpAbsent && transferPanel.cardCount > 0
                            Layout.fillWidth: true
                            currentIndex: demoTab
                            padding: 3
                            spacing: 2
                            background: Rectangle {
                                radius: height / 2
                                color: Theme.light ? Qt.rgba(1, 1, 1, 0.45) : Qt.rgba(0, 0, 0, 0.20)
                                border.width: 1
                                border.color: Theme.light ? Qt.rgba(1, 1, 1, 0.9) : Theme.glassEdge
                            }

                            StyledTab {
                                text: transferPanel.cardCount > 0 ? qsTr("Queue (%1)").arg(transferPanel.cardCount)
                                                                  : qsTr("Queue")
                            }
                            StyledTab { text: qsTr("Files (FTP)") }
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: window.ftpAbsent ? 0 : transferPanel.cardCount === 0 ? 1 : tabs.currentIndex

                            TransferPanel { id: transferPanel }
                            FtpBrowser { }
                        }
                    }
                }

                // The transfers closed while jobs go on: a narrow pane with
                // them instead, until they are done.
                Plate {
                    level: "primary"
                    Layout.fillHeight: true
                    Layout.preferredWidth: 72
                    visible: window.railShown

                    QueueRail {
                        anchors.fill: parent
                        anchors.margins: 4
                        panel: transferPanel
                        onOpenRequested: window.panelVisible = true
                    }
                }
            }
        }
    }

    // ───────────────────────────── the pages
    // In their place in the main area, or — over Remote Play — in the overlay.
    GamesView {
        parent: window.overSection === "games" ? sectionOverlay.slot : gamesSlot
        anchors.fill: parent
        visible: window.view === "games" || window.overSection === "games"
        framed: window.overSection !== "games"
        onBack: window.closeSection()
    }
    SavesView {
        parent: window.overSection === "saves" ? sectionOverlay.slot : savesSlot
        anchors.fill: parent
        visible: window.view === "saves" || window.overSection === "saves"
        framed: window.overSection !== "saves"
        onBack: window.closeSection()
    }
    PayloadsView {
        parent: window.overSection === "payloads" ? sectionOverlay.slot : payloadsSlot
        anchors.fill: parent
        visible: window.view === "payloads" || window.overSection === "payloads"
        framed: window.overSection !== "payloads"
        onBack: window.closeSection()
    }
    Loader {
        // Only in builds with the store.
        active: typeof store !== "undefined" && store !== null
        parent: window.overSection === "store" ? sectionOverlay.slot : storeSlot
        anchors.fill: parent
        visible: window.view === "store" || window.overSection === "store"
        sourceComponent: StoreView {
            framed: window.overSection !== "store"
            onBack: window.closeSection()
        }
    }

    // Dragging over the window shows the overlay. It is the application's
    // only DropArea: having one more per zone seemed natural but does not
    // work — this one keeps hold of the drag and the zones never see it.
    DropArea {
        anchors.fill: parent
        onEntered: function(drag) {
            // Logged before any decision: if this never shows up in the
            // diagnostics, Windows did not deliver the event, and then the
            // problem is not here.
            app.noteDrag("entered", drag.hasUrls)
            // A row of the FTP list being dragged: it is for the list's
            // folders, not an upload.
            if (!drag.hasUrls || drag.formats.indexOf("application/x-orbislink-ftp-path") >= 0) {
                drag.accepted = false
                return
            }
            overlay.show(drag.x, drag.y)
            drag.accept()
        }
        onPositionChanged: function(drag) { overlay.movePointer(drag.x, drag.y) }
        onExited: overlay.hide()
        onDropped: function(drop) {
            app.noteDrag("dropped", drop.hasUrls)
            if (drop.hasUrls && overlay.dropAt(drop.x, drop.y, drop.urls))
                drop.accept()
            else
                overlay.hide()
        }
    }

    DropOverlay {
        id: overlay
        // Used by the automatic drag test (--selftest-drag).
        objectName: "dropOverlay"
        active: demoOverlay
    }

    SettingsDialog {
        id: settingsDialog
        onOpenWizard: {
            settingsDialog.close()
            firstRunWizard.begin()
        }
    }
    DiagnosticsDialog { id: diagnosticsDialog }
    UploadConflictDialog { id: uploadConflictDialog }
    ServiceHelpDialog {
        id: serviceHelpDialog
        onOpenPayloads: window.showSection("payloads")
    }
    FirstRunWizard { id: firstRunWizard }
    UpdateDialog { id: updateDialog }

    Connections {
        target: app
        function onUpdateAvailable(version) { updateDialog.open() }
        function onUploadConflicts(conflicts) { uploadConflictDialog.begin(conflicts) }
        function onServiceHelp(kind, title, text) { serviceHelpDialog.begin(kind, title, text) }
        // Files were queued: show where they can be followed.
        function onShowPanel(which) {
            window.panelVisible = true
            tabs.currentIndex = which === "files" ? 1 : 0
        }
    }

    // Only for screenshots and for the full-screen test.
    Timer {
        running: typeof demoFullscreen !== "undefined" && demoFullscreen
        interval: 1200
        onTriggered: {
            window.setStreamFullscreen(true)
            console.log("DIAG full screen: visibility=" + window.visibility
                        + " expected=" + Window.FullScreen
                        + " bar=" + window.streamFullscreen)
        }
    }

    Timer {
        running: typeof demoLog !== "undefined" && demoLog
        interval: 1800
        onTriggered: diagnosticsDialog.open()
    }

    // Screenshot only: shows the update dialog without needing a new
    // published release.
    Timer {
        running: typeof demoUpdate !== "undefined" && demoUpdate
        interval: 1500
        onTriggered: { app.loadDemoUpdate(); updateDialog.open() }
    }

    // The theme comes from the settings and changes in real time.
    function applyTheme() {
        if (typeof demoTheme !== "undefined" && demoTheme.length > 0) {
            Theme.apply(demoTheme)
            applySystemFrame()
            return
        }
        var values = app.settingsMap()
        Theme.apply(values.theme, app.themeColors)
        applySystemFrame()
    }

    // The title bar and frame are drawn by the system, not by us, and on
    // Windows they are white by default. This asks it to follow the theme —
    // and, where the system knows how, to use its own translucent
    // material.
    function applySystemFrame() {
        if (typeof chrome === "undefined" || !chrome)
            return
        // The bar sits against the bottom of the window, not the floating
        // bar: the point is for it to look like the same surface.
        var frame = Theme.light ? Qt.darker(Theme.background, 1.10)
                                  : Qt.lighter(Theme.background, 2.2)
        chrome.applyTheme(Theme.background, Theme.text, frame,
                          !Theme.light, Theme.name === "glass")
    }

    Connections {
        target: app
        function onSettingsChanged() { window.applyTheme() }
        function onThemeColorsChanged() { window.applyTheme() }
    }

    Component.onCompleted: {
        applyTheme()
        if (demoSectionOn) {
            overSection = demoSection
            return
        }
        if (typeof demoSettings !== "undefined" && demoSettings) {
            settingsDialog.loadValues()
            if (typeof demoSettingsSection !== "undefined")
                settingsDialog.section = demoSettingsSection
            settingsDialog.open()
            return
        }
        if (typeof demoWizard !== "undefined" && demoWizard) {
            firstRunWizard.begin()
            return
        }
        // First launch: instead of an empty window with everything red,
        // three steps. The flag is stored in the wizard's `save()`.
        var values = app.settingsMap()
        if (!values.firstRunDone)
            firstRunWizard.begin()
    }

    Connections {
        target: app
        function onNotify(title, message, error) {
            toast.show(title + (message.length > 0 ? " — " + message : ""), error)
        }
    }

    // Every other tooltip of the app (the attached ToolTip, one instance
    // shared by all) in the app's look: a small pane of glass with wrapped
    // text, not the Basic style's plain box across the window.
    Item {
        id: tooltipLook
        Component {
            id: tooltipBackground
            Rectangle {
                color: Theme.menuFill
                radius: 12
                border.width: 1
                border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Theme.glassEdge
            }
        }
        Component {
            id: tooltipContent
            Item {
                required property ToolTip control
                implicitWidth: Math.min(360, label.implicitWidth)
                implicitHeight: label.implicitHeight
                Text {
                    id: label
                    width: parent.width
                    text: parent.control.text
                    color: Theme.text
                    font.pixelSize: 12
                    lineHeight: 1.15
                    wrapMode: Text.WordWrap
                }
            }
        }
        Component.onCompleted: {
            var shared = ToolTip.toolTip
            // Made inside the tooltip's own item, where the style's parts live.
            var host = shared.background ? shared.background.parent : shared.contentItem.parent
            shared.background = tooltipBackground.createObject(host)
            shared.contentItem = tooltipContent.createObject(host, { control: shared })
            shared.padding = 10
            shared.leftPadding = 12
            shared.rightPadding = 12
            shared.margins = 12
        }
    }

    // Floating glass notice inside the window. It comes in from below with
    // a scale, like a panel rising, instead of appearing out of nowhere.
    Item {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: opacity > 0 ? 56 : 36
        implicitWidth: Math.min(window.width - 120, toastText.implicitWidth + 56)
        implicitHeight: toastText.implicitHeight + 32
        opacity: 0
        visible: opacity > 0.01
        scale: opacity > 0 ? 1.0 : 0.96

        property bool isError: false

        function show(message, error) {
            toastText.text = message
            isError = error
            opacity = 1
            hideTimer.restart()
        }

        Behavior on opacity { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
        Behavior on scale {
            NumberAnimation { duration: Theme.normal; easing.type: Theme.easeSpring; easing.overshoot: 1.08 }
        }
        Behavior on anchors.bottomMargin {
            NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut }
        }

        // Halo in the state colour, under the glass.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            radius: (parent.height + 12) / 2
            color: "transparent"
            border.width: 6
            border.color: toast.isError
                ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.18)
                : Qt.rgba(Theme.ok.r, Theme.ok.g, Theme.ok.b, 0.14)
        }

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b,
                           Theme.light ? 0.88 : 0.82)
            border.width: 1
            border.color: toast.isError
                ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.5)
                : Theme.glassEdge

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.glassHighlight) }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
        }

        Row {
            anchors.centerIn: parent
            spacing: 10

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: toast.isError ? Theme.error : Theme.ok
            }

            Text {
                id: toastText
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, window.width - 180)
                color: Theme.text
                font.pixelSize: 12
                font.weight: Font.Medium
                font.letterSpacing: -0.2
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Timer { id: hideTimer; interval: 5000; onTriggered: toast.opacity = 0 }
    }
}
