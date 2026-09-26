// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1280
    height: 800
    visible: true
    color: Theme.background
    title: qsTr("OrbisLink — %1").arg(app.consoleName)

    property bool panelVisible: true
    // Full screen for the stream: hides the top bar and the side panel,
    // and the window takes up the whole screen.
    property bool streamFullscreen: false

    function setStreamFullscreen(isActive) {
        if (streamFullscreen === isActive)
            return
        streamFullscreen = isActive
        window.visibility = isActive ? Window.FullScreen : Window.Windowed
    }

    // F9 abre/fecha o painel lateral (§5.7).
    Shortcut {
        sequence: "F9"
        onActivated: window.panelVisible = !window.panelVisible
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

        // ───────────────────────────── barra superior
        Item {
            Layout.fillWidth: true
            // In full screen only the picture matters.
            visible: !window.streamFullscreen
            implicitHeight: visible ? 76 : 0

            // Floating bar: it does not touch the window edges and has no
            // line underneath — separation comes from depth.
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: Theme.gutter
                anchors.rightMargin: Theme.gutter
                anchors.topMargin: 14
                anchors.bottomMargin: 6
                radius: Theme.radiusControl + 4
                color: Theme.panelFill
                border.width: 1
                border.color: Theme.glassEdge

                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.glassHighlight) }
                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.05) }
                    }
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.gutter + 20
                anchors.rightMargin: Theme.gutter + 12
                anchors.topMargin: 14
                anchors.bottomMargin: 6
                spacing: 14

                Image {
                    source: "qrc:/icons/mark.png"
                    sourceSize.width: 28
                    sourceSize.height: 28
                    Layout.alignment: Qt.AlignVCenter
                }

                ColumnLayout {
                    spacing: 0
                    Text {
                        text: app.consoleName
                        color: Theme.text
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        font.letterSpacing: -0.4
                    }
                    Text {
                        text: app.consoleAddress.length > 0 ? app.consoleAddress
                                                            : qsTr("no address set")
                        color: Theme.textSecondary
                        font.pixelSize: 11
                        font.letterSpacing: -0.1
                    }
                }

                Item { Layout.fillWidth: true }

                ServiceIndicator {
                    label: qsTr("Remote Play")
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
                    state_: app.installerState
                    hint: app.installerHint
                }

                StyledToolButton {
                    text: "⟳"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Check the services now")
                    onClicked: app.checkServicesNow()
                }
                StyledToolButton {
                    text: "☰"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Side panel (F9)")
                    onClicked: window.panelVisible = !window.panelVisible
                }
                StyledToolButton {
                    text: "📋"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Log and diagnostics (Ctrl+L)")
                    onClicked: diagnosticsDialog.open()
                }
                ThemeSwitcher {}
                StyledToolButton {
                    text: "⚙"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Settings (Ctrl+,)")
                    onClicked: { settingsDialog.loadValues(); settingsDialog.open() }
                }
            }
        }

        // ───────────────────────────── corpo
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            StreamArea {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            Item {
                Layout.fillHeight: true
                Layout.preferredWidth: 440
                visible: window.panelVisible && !window.streamFullscreen

                Rectangle {
                    anchors.fill: parent
                    anchors.rightMargin: Theme.gutter
                    anchors.topMargin: 6
                    anchors.bottomMargin: Theme.gutter
                    anchors.leftMargin: 6
                    radius: Theme.radius
                    color: Theme.panelFill
                    border.width: 1
                    border.color: Theme.glassEdge
                    clip: true

                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        gradient: Gradient {
                            GradientStop {
                                position: 0.0
                                color: Qt.rgba(1, 1, 1, Theme.glassHighlight)
                            }
                            GradientStop { position: 0.4; color: "transparent" }
                        }
                    }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    TabBar {
                        id: tabs
                        Layout.fillWidth: true
                        currentIndex: demoTab
                        background: Rectangle { color: "transparent" }

                        StyledTab {
                            text: app.queue.count > 0 ? qsTr("Queue (%1)").arg(app.queue.count)
                                                      : qsTr("Queue")
                        }
                        StyledTab { text: qsTr("Files (FTP)") }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: tabs.currentIndex

                        TransferPanel { }
                        FtpBrowser { }
                    }
                }
                }
            }
        }

        // ───────────────────────────── barra de estado
        Item {
            Layout.fillWidth: true
            visible: !window.streamFullscreen
            implicitHeight: visible ? 34 : 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.gutter + 20
                anchors.rightMargin: Theme.gutter + 20
                spacing: 14

                Text {
                    Layout.fillWidth: true
                    text: app.statusMessage
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Text {
                    text: qsTr("Local HTTP: %1").arg(app.httpServerAddress)
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
                Text {
                    text: "v" + app.version
                    color: Qt.rgba(Theme.textMuted.r, Theme.textMuted.g, Theme.textMuted.b, 0.3)
                    font.pixelSize: 11
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
            if (!drag.hasUrls) {
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
    FirstRunWizard { id: firstRunWizard }
    UpdateDialog { id: updateDialog }

    Connections {
        target: app
        function onUpdateAvailable(version) { updateDialog.open() }
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
        Theme.apply(values.theme)
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
