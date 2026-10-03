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
    // What the main area shows: the consoles ("home") or the PS1/PS2 games.
    property string view: typeof demoGames !== "undefined" && demoGames ? "games" : "home"
    readonly property string panelLockReason: qsTr("This console only offers Remote Play: it has "
        + "neither FTP nor the remote installer running (a jailbreak adds them).")
    // Full screen for the stream: hides the top bar and the side panel,
    // and the window takes up the whole screen.
    property bool streamFullscreen: false

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

                // A narrow window squeezes the bar instead of pushing its
                // right end out: 1 — the services become dots and the page
                // buttons icons; 2 — the console's name and address go too
                // (the console chip still shows it). It widens again only
                // with room to spare, so it never flickers between the two.
                property int squeeze: 0
                readonly property real saving1: remotePlayDot.labelWidth + ftpDot.labelWidth
                    + installerDot.labelWidth + gamesText.width + 8
                    + (backButton.visible ? backText.width + 8 : 0)
                readonly property real saving2: consoleInfo.implicitWidth + spacing + 2
                function fit() {
                    var need = implicitWidth
                    if (need > width + 0.5 && squeeze < 2)
                        squeeze += 1
                    else if (squeeze === 2 && need + saving2 + 16 <= width)
                        squeeze = 1
                    else if (squeeze === 1 && need + saving1 + 16 <= width)
                        squeeze = 0
                }
                onWidthChanged: Qt.callLater(fit)
                onImplicitWidthChanged: Qt.callLater(fit)
                Component.onCompleted: Qt.callLater(fit)
                TextMetrics { id: gamesText; font.pixelSize: 12; font.weight: Font.Medium; text: qsTr("PS1/PS2 Games") }
                TextMetrics { id: backText; font.pixelSize: 12; font.weight: Font.DemiBold; text: qsTr("Back to Remote Play") }

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
                    visible: barRow.squeeze < 2
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

                // Away from a Remote Play session that is still running: one
                // click goes back to the picture.
                StyledButton {
                    id: backButton
                    readonly property bool live: typeof stream !== "undefined" && stream !== null
                                                 && stream.streaming
                    visible: live && window.view !== "home"
                    text: barRow.squeeze > 0 ? "" : qsTr("Back to Remote Play")
                    iconName: "gamepad"
                    chip: true
                    primary: true
                    implicitHeight: 36
                    ToolTip.visible: hovered
                    ToolTip.text: barRow.squeeze > 0 ? qsTr("Back to Remote Play") + " — " + qsTr("The session is still running")
                                                     : qsTr("The session is still running")
                    onClicked: window.view = "home"
                }

                // PS1/PS2 games: a page of its own, in place of the consoles.
                StyledButton {
                    visible: games.available
                    text: barRow.squeeze > 0 ? "" : qsTr("PS1/PS2 Games")
                    iconName: "disc"
                    chip: true
                    primary: window.view === "games"
                    implicitHeight: 36
                    ToolTip.visible: hovered
                    ToolTip.text: (barRow.squeeze > 0 ? qsTr("PS1/PS2 Games") + " — " : "")
                                  + (window.view === "games" ? qsTr("Back to the consoles")
                                     : qsTr("Find PS1 and PS2 discs on this PC, convert them into packages and install them on the console"))
                    onClicked: window.view = window.view === "games" ? "home" : "games"
                }
                Item { implicitWidth: 6 }

                // The console everything goes to.
                ConsoleChip {
                    onShowConsoles: window.view = "home"
                }

                ServiceIndicator {
                    id: remotePlayDot
                    compact: barRow.squeeze > 0
                    label: qsTr("Remote Play")
                    state_: app.remotePlayState
                    hint: app.remotePlayHint
                }
                ServiceIndicator {
                    id: ftpDot
                    compact: barRow.squeeze > 0
                    label: qsTr("FTP")
                    state_: app.ftpState
                    hint: app.ftpHint
                }
                ServiceIndicator {
                    id: installerDot
                    compact: barRow.squeeze > 0
                    label: qsTr("Installer")
                    state_: app.installerState
                    hint: app.installerHint
                }

                Item { implicitWidth: 6 }

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
                ThemeSwitcher {}
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

            GamesView {
                visible: window.view === "games"
                Layout.fillWidth: true
                Layout.fillHeight: true
                onBack: window.view = "home"
            }

            StreamArea {
                visible: window.view !== "games"
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Back to the home page: whatever was open on top of the
                // cards gets out of the way of the console just registered.
                onRegisteredHere: {
                    settingsDialog.close()
                    firstRunWizard.close()
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
    FirstRunWizard { id: firstRunWizard }
    UpdateDialog { id: updateDialog }

    Connections {
        target: app
        function onUpdateAvailable(version) { updateDialog.open() }
        function onUploadConflicts(conflicts) { uploadConflictDialog.begin(conflicts) }
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
