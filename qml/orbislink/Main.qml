// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1280
    height: 800
    // The top bar fits from here, at its most compact.
    minimumWidth: 880
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
    // Whether the side panel is on screen; without it the page takes the
    // window's right gutter, so it lines up with the top bar.
    readonly property bool panelShown: panelVisible && panelAllowed && !streamFullscreen
                                         || games.conversions.length > 0
    // What the main area shows: the consoles ("home"), "games", "store",
    // "saves" or "payloads".
    property string view: typeof demoView !== "undefined" && demoView.length > 0
                          ? (demoView === "store-local" ? "store"
                             : demoView === "payloads-library" ? "payloads" : demoView)
                          : typeof demoGames !== "undefined" && demoGames ? "games" : "home"
    readonly property string panelLockReason: qsTr("This console only offers Remote Play: it has "
        + "neither FTP nor the remote installer running (a jailbreak adds them).")
    // Full screen for the stream: hides the top bar and the side panel,
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
    // A page's button: over the picture while it plays, in the main area
    // otherwise; the same button again closes it.
    function openSection(name) {
        if (streamOnScreen) {
            overSection = overSection === name ? "" : name
            return
        }
        overSection = ""
        view = view === name ? "home" : name
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

    function setStreamFullscreen(isActive) {
        if (streamFullscreen === isActive)
            return
        streamFullscreen = isActive
        window.visibility = isActive ? Window.FullScreen : Window.Windowed
    }

    // F9 shows and hides the side panel.
    Shortcut {
        sequence: "F9"
        onActivated: {
            if (window.panelAllowed)
                window.panelVisible = !window.panelVisible
            else
                toast.show(window.panelLockReason, false)
        }
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ───────────────────────────── top bar
        Item {
            Layout.fillWidth: true
            // In full screen only the picture matters.
            visible: !window.streamFullscreen
            implicitHeight: visible ? 84 : 0

            // A floating bar: it does not touch the window edges.
            Rectangle {
                id: topBar
                anchors.fill: parent
                anchors.leftMargin: Theme.gutter
                anchors.rightMargin: Theme.gutter
                anchors.topMargin: 14
                anchors.bottomMargin: 8
                radius: 18
                color: Theme.panelFill
                border.width: 1
                border.color: Theme.glassEdge

                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    visible: Theme.glassHighlight > 0
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.glassSheen }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                }
            }

            RowLayout {
                id: barRow
                anchors.fill: topBar
                anchors.leftMargin: 14
                anchors.rightMargin: 12
                spacing: 10

                // A narrow window drops the console's name and address on the
                // left (the console chip still shows it); they come back only
                // with room to spare, so it never flickers.
                property bool tight: false
                function fit() {
                    if (!tight && implicitWidth > width + 0.5)
                        tight = true
                    else if (tight && implicitWidth + consoleInfo.implicitWidth + spacing + 18 <= width)
                        tight = false
                }
                onWidthChanged: Qt.callLater(fit)
                onImplicitWidthChanged: Qt.callLater(fit)
                Component.onCompleted: Qt.callLater(fit)

                // The app's symbol as the console's avatar.
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 38
                    implicitHeight: 38
                    radius: 19
                    color: Theme.accentFill
                    border.width: 1
                    border.color: Theme.alpha(Theme.accent, 0.35)
                    Image {
                        anchors.centerIn: parent
                        source: "qrc:/icons/mark.png"
                        sourceSize.width: 48
                        sourceSize.height: 48
                        width: 24
                        height: 24
                    }
                }

                ColumnLayout {
                    id: consoleInfo
                    visible: !barRow.tight
                    spacing: 1
                    Layout.leftMargin: 2
                    Text {
                        text: app.consoleName
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: app.consoleAddress.length > 0 ? app.consoleAddress
                                                            : qsTr("no address set")
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                }

                Item { Layout.fillWidth: true }

                // ── What is: the console everything goes to, and its services.
                ConsoleChip {
                    onShowConsoles: window.view = "home"
                }
                BarGroup {
                    ServiceIndicator {
                        label: qsTr("Remote Play")
                        iconName: "gamepad"
                        state_: app.remotePlayState
                        hint: app.remotePlayHint
                    }
                    ServiceIndicator {
                        label: qsTr("FTP")
                        state_: app.ftpState
                        hint: app.ftpHint
                    }
                    ServiceIndicator {
                        label: qsTr("Installer")
                        iconName: "package"
                        state_: app.installerState
                        hint: app.installerHint
                    }
                }

                BarDivider {}

                // ── Where to go: the pages, as icons.
                BarGroup {
                    // Away from a Remote Play session that is still running: one
                    // click goes back to the picture.
                    StyledToolButton {
                        readonly property bool live: typeof stream !== "undefined" && stream !== null
                                                     && stream.streaming
                        visible: live && window.view !== "home"
                        iconName: "play"
                        active: true
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Back to Remote Play") + " — " + qsTr("The session is still running")
                        onClicked: window.view = "home"
                    }
                    StyledToolButton {
                        iconName: "disc"
                        active: window.view === "games" || window.overSection === "games"
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Games") + " — "
                                      + (window.overSection === "games" ? qsTr("Close") : window.view === "games" ? qsTr("Back to the consoles")
                                         : games.available
                                         ? qsTr("Every game and app in the console's library, and the PS1/PS2 converter")
                                         : qsTr("Every game and app in the console's library"))
                        onClicked: window.openSection("games")
                    }
                    StyledToolButton {
                        visible: typeof store !== "undefined" && store !== null
                        iconName: "store"
                        active: window.view === "store" || window.overSection === "store"
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Homebrew store") + " — "
                                      + (window.overSection === "store" ? qsTr("Close") : window.view === "store" ? qsTr("Back to the consoles")
                                         : qsTr("Find PS5 homebrew apps and install them on the console"))
                        onClicked: window.openSection("store")
                    }
                    StyledToolButton {
                        iconName: "archive"
                        active: window.view === "saves" || window.overSection === "saves"
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Save vault") + " — "
                                      + (window.overSection === "saves" ? qsTr("Close") : window.view === "saves" ? qsTr("Back to the consoles")
                                         : qsTr("Back up the console's saves on this PC"))
                        onClicked: window.openSection("saves")
                    }
                    StyledToolButton {
                        iconName: "zap"
                        active: window.view === "payloads" || window.overSection === "payloads"
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Payloads") + " — "
                                      + (window.overSection === "payloads" ? qsTr("Close") : window.view === "payloads" ? qsTr("Back to the consoles")
                                         : qsTr("See and manage the console's payloads and plugins, and run one now"))
                        onClicked: window.openSection("payloads")
                    }
                }

                BarDivider {}

                // ── Tools.
                StyledToolButton {
                    iconName: "refresh"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Check the services now")
                    onClicked: app.checkServicesNow()
                }
                StyledToolButton {
                    iconName: "panel"
                    active: window.panelVisible && window.panelAllowed
                    opacity: window.panelAllowed ? 1.0 : 0.4
                    ToolTip.visible: hovered
                    ToolTip.text: window.panelAllowed ? qsTr("Side panel (F9)") : window.panelLockReason
                    onClicked: {
                        if (window.panelAllowed)
                            window.panelVisible = !window.panelVisible
                        else
                            toast.show(window.panelLockReason, false)
                    }
                }
                StyledToolButton {
                    iconName: "log"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Log and diagnostics (Ctrl+L)")
                    onClicked: diagnosticsDialog.open()
                }
                ThemeSwitcher {
                    onCustomRequested: {
                        settingsDialog.loadValues()
                        settingsDialog.section = 4
                        settingsDialog.open()
                    }
                }
                StyledToolButton {
                    iconName: "settings"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Settings (Ctrl+,)")
                    onClicked: { settingsDialog.loadValues(); settingsDialog.open() }
                }
            }
        }

        // ───────────────────────────── body
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Each page's place in the main area (the pages themselves are
            // further down: over Remote Play they move into the overlay).
            Item {
                id: gamesSlot
                visible: window.view === "games"
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            Item {
                id: savesSlot
                visible: window.view === "saves"
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            Item {
                id: payloadsSlot
                visible: window.view === "payloads"
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            Item {
                id: storeSlot
                visible: window.view === "store"
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            Item {
                visible: window.view !== "games" && window.view !== "saves" && window.view !== "store"
                         && window.view !== "payloads"
                Layout.fillWidth: true
                Layout.fillHeight: true

                StreamArea {
                    id: streamArea
                    anchors.fill: parent
                    // Back to the home page: whatever was open on top of the
                    // cards gets out of the way of the console just registered.
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

            Item {
                Layout.fillHeight: true
                Layout.preferredWidth: 380
                visible: window.panelShown

                Rectangle {
                    anchors.fill: parent
                    anchors.rightMargin: Theme.gutter
                    anchors.topMargin: 4
                    anchors.bottomMargin: 4
                    anchors.leftMargin: 8
                    radius: Theme.radius
                    color: Theme.panelFill
                    border.width: 1
                    border.color: Theme.glassEdge
                    clip: true

                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        visible: Theme.glassHighlight > 0
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Theme.glassSheen }
                            GradientStop { position: 0.4; color: "transparent" }
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 12

                        // Queue and files as a segmented control.
                        // Without FTP on the console there is only the queue: no tabs.
                        TabBar {
                            id: tabs
                            visible: !window.ftpAbsent
                            Layout.fillWidth: true
                            currentIndex: demoTab
                            padding: 4
                            spacing: 4
                            background: Rectangle {
                                radius: 12
                                color: Theme.controlFill
                                border.width: 1
                                border.color: Theme.glassEdge
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
                            currentIndex: window.ftpAbsent ? 0 : tabs.currentIndex

                            TransferPanel { id: transferPanel }
                            FtpBrowser { }
                        }
                    }
                }
            }
        }

        // ───────────────────────────── status line
        Item {
            Layout.fillWidth: true
            visible: !window.streamFullscreen
            implicitHeight: visible ? 40 : 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.gutter + 6
                anchors.rightMargin: Theme.gutter + 6
                spacing: 16

                Icon {
                    visible: app.statusMessage.length > 0
                    name: "check-circle"
                    size: 14
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
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
                Text {
                    text: "v" + app.version
                    color: Theme.alpha(Theme.textSecondary, 0.7)
                    font.pixelSize: 12
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

    // A few things of the top bar together, on one soft surface.
    component BarGroup: Rectangle {
        default property alias content: groupRow.data
        implicitWidth: groupRow.implicitWidth + 6
        implicitHeight: 40
        radius: 13
        color: Theme.controlFill
        border.width: 1
        border.color: Theme.glassEdge
        Row {
            id: groupRow
            anchors.centerIn: parent
            spacing: 1
        }
    }
    // Between what is (the console and its services) and what can be done.
    component BarDivider: Rectangle {
        implicitWidth: 1
        implicitHeight: 22
        color: Theme.glassEdge
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
    // shared by all) in the app's look: a dark rounded card with wrapped
    // text, not the Basic style's plain box across the window.
    Item {
        id: tooltipLook
        Component {
            id: tooltipBackground
            Rectangle {
                color: Theme.dialogFill
                radius: 10
                border.width: 1
                border.color: Theme.border
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
