// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: dialog
    // The console in use is a PS5: the FTP port being edited is its own.
    property bool consoleIsPs5: false
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 60 : 920, 920)
    height: Math.min(parent ? parent.height - 60 : 660, 680)
    padding: 0

    // A nearly opaque modal: with the panels' transparency, what is behind
    // would show through the box, and a box asking for a decision must not
    // be a window.
    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radiusDialog
    }

    // The section shown on the right: 0 Consoles, 1 Account IDs, 2 Console in
    // use, 3 Remote Play, 4 General.
    property int section: 0

    // While a Remote Play session runs, the IP of the console in use stays as
    // it is: changing it would point FTP and the installer somewhere else
    // while the picture still comes from this console.
    readonly property bool sessionActive: typeof stream !== "undefined" && stream !== null
        && (stream.streaming || stream.sessionState === "connecting")

    property var values: ({})

    // The first-run wizard lives in Main.qml; from here we only ask for it
    // to appear again.
    signal openWizard()

    function resolutionToIndex(resolution) {
        var order = [1080, 720, 540, 360]
        var i = order.indexOf(resolution)
        return i >= 0 ? i : 1
    }

    function languageToIndex(language) {
        return language === "pt_PT" || language === "pt" ? 1 : 0
    }

    // Automatic address check: "idle", "checking", "ok",
    // "partial" (only one of the services answers) or "fail".
    property string probeState: "idle"
    property string probeDetail: ""
    property bool probeFtpOk: false
    property bool probeInstallerOk: false

    readonly property color probeColor: probeState === "ok" ? Theme.ok
        : probeState === "partial" ? Theme.warn
        : probeState === "fail" ? Theme.error
        : probeState === "checking" ? Theme.warn
        : Theme.textMuted

    readonly property string probeText: {
        if (probeState === "checking")
            return qsTr("Checking %1…").arg(addressField.text.trim())
        if (probeState === "ok")
            return probeDetail.length > 0
                ? qsTr("Console found — the installer and FTP both answer (%1).").arg(probeDetail)
                : qsTr("Console found — the installer and FTP both answer.")
        if (probeState === "partial") {
            if (probeInstallerOk)
                return qsTr("The remote installer answers, FTP does not. Check that GoldHEN's "
                            + "FTP server is running.")
            return qsTr("FTP answers, the remote installer does not. Open Remote Package "
                        + "Installer on the console.")
        }
        if (probeState === "fail")
            return qsTr("No answer from %1. Check the IP, and that the console is on and on the "
                        + "same network.").arg(addressField.text.trim())
        return qsTr("Type the console's IP address — it is checked on its own.")
    }

    // Only worth connecting when the address is complete: a full IPv4
    // (otherwise "192.168.1." would be checked on every key) or a host name.
    function addressLooksComplete(text) {
        var value = (text || "").trim()
        if (value.length === 0)
            return false
        if (/^[0-9.]+$/.test(value)) {
            var parts = value.match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/)
            if (!parts)
                return false
            for (var i = 1; i <= 4; ++i) {
                if (parseInt(parts[i], 10) > 255)
                    return false
            }
            return true
        }
        return /^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(value)
    }

    function scheduleProbe() {
        if (!addressLooksComplete(addressField.text)) {
            probeState = "idle"
            probeDetail = ""
            probeTimer.stop()
            return
        }
        probeState = "checking"
        probeTimer.restart()
    }

    Timer {
        id: probeTimer
        // Waits for typing to stop before going to the network.
        interval: 600
        repeat: false
        onTriggered: app.probeConsole(addressField.text.trim(),
            parseInt(ftpPortField.text) || (dialog.consoleIsPs5 ? 1337 : 2121),
            parseInt(installerPortField.text) || 12800)
    }

    Connections {
        target: app
        function onConsoleProbed(address, ftpOk, installerOk, detail) {
            // A reply for an address that is no longer the typed one does not count.
            if (address !== addressField.text.trim())
                return
            dialog.probeFtpOk = ftpOk
            dialog.probeInstallerOk = installerOk
            dialog.probeDetail = detail
            dialog.probeState = (ftpOk && installerOk) ? "ok"
                : (ftpOk || installerOk) ? "partial" : "fail"
        }
    }

    onOpened: scheduleProbe()

    function loadValues() {
        values = app.settingsMap()
        nameField.text = values.consoleName
        addressField.text = values.consoleAddress
        ftpPortField.text = values.ftpPort
        consoleIsPs5 = values.consoleIsPs5 === true
        installerPortField.text = values.installerPort
        httpPortField.text = values.httpPort
        restrictBox.checked = values.restrictToConsoleIp
        modeBox.currentIndex = values.defaultMode
        resolutionBox.currentIndex = resolutionToIndex(values.streamResolution)
        fpsBox.currentIndex = values.streamFps === 30 ? 1 : 0
        bitrateField.text = values.streamBitrateKbps > 0 ? String(values.streamBitrateKbps) : ""
        hardwareBox.checked = values.streamHardwareDecode
        fullscreenBox.checked = values.streamFullscreenOnConnect
        rumbleBox.checked = values.streamRumble
        touchpadBox.checked = values.streamTouchpadFromMouse
        languageBox.currentIndex = languageToIndex(values.language)
        uploadDirField.text = values.ftpUploadDirectory
        existsBox.checked = values.checkAlreadyInstalled
        installAfterBox.checked = values.installAfterUpload
        updatesBox.checked = values.checkForUpdates
        updateRepoField.text = values.updateRepository
        updateChannelBox.currentIndex = values.updateChannel === "testing" ? 1 : 0
        deleteAfterBox.checked = values.deleteFromConsoleAfterInstall
        advancedBox.checked = values.ftpAdvancedMode
        debugBox.checked = values.debugLogging
    }

    // Switching, editing or removing consoles in the list changes which one
    // is in use: its fields follow, so saving never writes one console's
    // values over another's.
    Connections {
        target: app
        function onSettingsChanged() {
            if (!dialog.visible)
                return
            var fresh = app.settingsMap()
            if (fresh.consoleAddress === dialog.values.consoleAddress
                    && fresh.consoleName === dialog.values.consoleName)
                return
            dialog.values.consoleName = fresh.consoleName
            dialog.values.consoleAddress = fresh.consoleAddress
            nameField.text = fresh.consoleName
            addressField.text = fresh.consoleAddress
            ftpPortField.text = fresh.ftpPort
            dialog.consoleIsPs5 = fresh.consoleIsPs5 === true
        }
    }

    function save() {
        app.applySettings({
            "consoleName": nameField.text,
            "consoleAddress": addressField.text,
            "ftpPort": parseInt(ftpPortField.text) || (dialog.consoleIsPs5 ? 1337 : 2121),
            "installerPort": parseInt(installerPortField.text) || 12800,
            "httpPort": parseInt(httpPortField.text) || 8765,
            "restrictToConsoleIp": restrictBox.checked,
            "defaultMode": modeBox.currentIndex,
            "ftpUploadDirectory": uploadDirField.text,
            "checkAlreadyInstalled": existsBox.checked,
            "installAfterUpload": installAfterBox.checked,
            "checkForUpdates": updatesBox.checked,
            "updateRepository": updateRepoField.text.trim(),
            "updateChannel": updateChannelBox.currentIndex === 1 ? "testing" : "stable",
            "deleteFromConsoleAfterInstall": deleteAfterBox.checked,
            "ftpAdvancedMode": advancedBox.checked,
            "debugLogging": debugBox.checked,
            "streamResolution": [1080, 720, 540, 360][resolutionBox.currentIndex],
            "streamFps": fpsBox.currentIndex === 1 ? 30 : 60,
            "streamBitrateKbps": parseInt(bitrateField.text) || 0,
            "streamHardwareDecode": hardwareBox.checked,
            "streamFullscreenOnConnect": fullscreenBox.checked,
            "streamRumble": rumbleBox.checked,
            "streamTouchpadFromMouse": touchpadBox.checked,
            "language": ["en", "pt_PT"][languageBox.currentIndex]
        })
        close()
    }

    // Save first: otherwise the old repository would be checked, not the one
    // typed in the field. Only these three values, and without closing: the
    // result shows up in the dialog, and whoever pressed wants to see it.
    function checkUpdatesNow() {
        if (app.updateState === "checking" || app.updateState === "downloading")
            return
        app.saveUpdateSettings(updatesBox.checked, updateRepoField.text.trim(),
                               updateChannelBox.currentIndex === 1 ? "testing" : "stable")
        app.checkForUpdatesNow(false)
    }

    header: Item {
        implicitHeight: Theme.dialogHeader
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 24
            spacing: 12
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "settings"
                size: 20
                color: Theme.accent
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Settings")
                color: Theme.text
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }
        }
        StyledToolButton {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 16
            iconName: "close"
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Close")
            onClicked: dialog.close()
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    footer: Item {
        implicitHeight: Theme.dialogFooter
        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.border
        }
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogInner
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 10
            StyledButton {
                text: qsTr("Setup wizard…")
                iconName: "sparkles"
                minimumWidth: 130
                ToolTip.visible: hovered
                ToolTip.text: qsTr("See the three first-run steps again")
                onClicked: dialog.openWizard()
            }
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Cancel")
                minimumWidth: 110
                onClicked: dialog.close()
            }
            StyledButton {
                text: qsTr("Save")
                minimumWidth: 110
                primary: true
                onClicked: dialog.save()
            }
        }
    }

    // One page of the settings: its title and its content, scrolling on its own.
    component SettingsPage: ScrollView {
        id: page
        property string title: ""
        default property alias content: pageColumn.data
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: pageColumn
            x: 28
            y: 22
            width: page.availableWidth - 56
            spacing: 14

            Text {
                visible: page.title.length > 0
                text: page.title
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                Layout.bottomMargin: 4
            }
        }
    }

    // A few related settings together, on a surface of their own.
    component SettingsGroup: Rectangle {
        default property alias content: groupColumn.data
        Layout.fillWidth: true
        implicitHeight: groupColumn.implicitHeight + 32
        radius: 16
        color: Theme.panelAltFill
        border.width: 1
        border.color: Theme.glassEdge
        ColumnLayout {
            id: groupColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            spacing: 12
        }
    }

    // A small title over a group.
    component GroupTitle: Text {
        color: Theme.textSecondary
        font.pixelSize: 12
        font.weight: Font.DemiBold
        Layout.topMargin: 8
    }

    contentItem: RowLayout {
        spacing: 0

        // ── The sections, on the left.
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 230
            color: Theme.light ? "#F6F8FB" : Qt.rgba(0, 0, 0, 0.14)

            Column {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 4

                Repeater {
                    model: [
                        { icon: "monitor", label: qsTr("Consoles") },
                        { icon: "id-card", label: qsTr("Account IDs (PSID)") },
                        { icon: "server", label: qsTr("Console in use") },
                        { icon: "cast", label: qsTr("Remote Play") },
                        { icon: "sparkles", label: qsTr("Personalisation") },
                        { icon: "sliders", label: qsTr("General") }
                    ]

                    delegate: Rectangle {
                        id: navItem
                        required property var modelData
                        required property int index
                        readonly property bool selected: dialog.section === index
                        width: parent.width
                        height: 42
                        radius: 12
                        color: selected ? Theme.accentFill
                             : navArea.containsMouse ? Theme.controlFill : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.fast } }

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 12
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: navItem.modelData.icon
                                size: 18
                                color: navItem.selected ? Theme.accent : Theme.textSecondary
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: navItem.modelData.label
                                color: navItem.selected ? Theme.accent : Theme.text
                                font.pixelSize: 13
                                font.weight: navItem.selected ? Font.DemiBold : Font.Medium
                            }
                        }
                        MouseArea {
                            id: navArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: dialog.section = navItem.index
                        }
                    }
                }
            }

            // Not a section: looks for a new version straight away, on the
            // repository and channel set under General, and says how it went.
            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 14
                spacing: 6

                Rectangle { width: parent.width; height: 1; color: Theme.border }

                Rectangle {
                    id: updateItem
                    readonly property bool busy: app.updateState === "checking"
                                                 || app.updateState === "downloading"
                    width: parent.width
                    height: 42
                    radius: 12
                    color: updateArea.containsMouse && !busy ? Theme.controlFill : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.fast } }

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "refresh"
                            size: 18
                            spinning: updateItem.busy
                            color: updateItem.busy ? Theme.accent : Theme.textSecondary
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Update check")
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }
                    }
                    MouseArea {
                        id: updateArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: updateItem.busy ? Qt.ArrowCursor : Qt.PointingHandCursor
                        onClicked: dialog.checkUpdatesNow()
                    }
                }

                Text {
                    width: parent.width
                    leftPadding: 12
                    rightPadding: 4
                    visible: app.updateMessage.length > 0
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    text: app.updateMessage
                    color: app.updateState === "error" ? Theme.error
                         : app.updateState === "available" ? Theme.ok
                         : Theme.textSecondary
                    font.pixelSize: 11
                }
            }
        }

        Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: Theme.border }

        // ── The section's content, on the right.
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: dialog.section

            SettingsPage {
                ConsoleManager { Layout.fillWidth: true }
                Item { Layout.preferredHeight: 8 }
            }

            SettingsPage {
                AccountManager { Layout.fillWidth: true }
                Item { Layout.preferredHeight: 8 }
            }

            SettingsPage {
                title: qsTr("Console in use")

                SettingsGroup {
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 8

                        Text { text: qsTr("Name"); color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 120 }
                        StyledField { id: nameField; Layout.fillWidth: true }

                        Text { text: qsTr("IP address"); color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 120 }
                        StyledField {
                            id: addressField
                            Layout.fillWidth: true
                            enabled: !dialog.sessionActive
                            ToolTip.visible: dialog.sessionActive && addressHover.hovered
                            ToolTip.text: qsTr("End the Remote Play session first: it is running on "
                                               + "the console in use.")
                            HoverHandler { id: addressHover }
                            placeholderText: "192.168.1.42"
                            onTextChanged: dialog.scheduleProbe()
                        }

                        Text {
                            // The port is the console in use's: a PS5 with etaHEN uses another.
                            text: dialog.consoleIsPs5 ? qsTr("FTP port (PS5)") : qsTr("FTP port")
                            color: Theme.textMuted
                            font.pixelSize: 12
                        }
                        StyledField {
                            id: ftpPortField
                            Layout.fillWidth: true
                            onTextChanged: dialog.scheduleProbe()
                        }

                        Text { text: qsTr("Installer port"); color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 120 }
                        StyledField {
                            id: installerPortField
                            Layout.fillWidth: true
                            onTextChanged: dialog.scheduleProbe()
                        }
                    }

                    // Result of the automatic check, with no button at all.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Item {
                            implicitWidth: 12
                            implicitHeight: 12
                            Rectangle {
                                id: probeDot
                                anchors.centerIn: parent
                                width: 8
                                height: 8
                                radius: 4
                                color: dialog.probeColor
                                SequentialAnimation on opacity {
                                    running: dialog.probeState === "checking"
                                    loops: Animation.Infinite
                                    alwaysRunToEnd: true
                                    NumberAnimation { to: 0.25; duration: 480; easing.type: Easing.InOutQuad }
                                    NumberAnimation { to: 1.0; duration: 480; easing.type: Easing.InOutQuad }
                                }
                                onVisibleChanged: if (!visible) opacity = 1.0
                            }
                            Rectangle {
                                anchors.centerIn: parent
                                width: 12
                                height: 12
                                radius: 6
                                color: "transparent"
                                border.width: 1
                                border.color: dialog.probeColor
                                opacity: dialog.probeState === "ok" ? 0.45 : 0.0
                                Behavior on opacity { NumberAnimation { duration: 180 } }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: dialog.probeText
                            color: dialog.probeState === "idle" ? Theme.textMuted : dialog.probeColor
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                GroupTitle { text: qsTr("Installation") }
                SettingsGroup {
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 8

                        Text { text: qsTr("Default mode"); color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 120 }
                        StyledCombo {
                            id: modeBox
                            Layout.fillWidth: true
                            model: [qsTr("Direct install"), qsTr("FTP upload")]
                        }

                        Text { text: qsTr("FTP folder"); color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 120 }
                        StyledField { id: uploadDirField; Layout.fillWidth: true }
                    }

                    StyledCheck {
                        id: existsBox
                        Layout.fillWidth: true
                        text: qsTr("Check whether the title is already on the console before installing")
                    }

                    StyledCheck {
                        id: installAfterBox
                        Layout.fillWidth: true
                        text: qsTr("Also install after sending over FTP")
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: 11
                        text: qsTr("The file stays on the console and is installed right away from the "
                                   + "PC — the remote installer only knows how to download over HTTP.")
                    }
                    StyledCheck {
                        id: deleteAfterBox
                        Layout.fillWidth: true
                        Layout.leftMargin: 20
                        enabled: installAfterBox.checked
                        text: qsTr("And delete the console copy once installed")
                    }
                }

                GroupTitle { text: qsTr("Local HTTP server") }
                SettingsGroup {
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 8
                        Text { text: qsTr("Port"); color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 120 }
                        StyledField { id: httpPortField; Layout.fillWidth: true }
                    }

                    StyledCheck {
                        id: restrictBox
                        Layout.fillWidth: true
                        text: qsTr("Only accept requests from the console's IP")
                    }
                }
                Item { Layout.preferredHeight: 8 }
            }

            SettingsPage {
                title: qsTr("Remote Play")

                SettingsGroup {
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 8

                        Text { text: qsTr("Quality"); color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 120 }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            StyledCombo {
                                id: resolutionBox
                                Layout.fillWidth: true
                                // 1080p only exists on PS4 Pro and PS5. On a regular
                                // PS4 the request is lowered to 720p by the console
                                // itself, and the label says so instead of leaving
                                // the person thinking the setting does nothing.
                                model: [qsTr("1080p — PS4 Pro and PS5 only"), qsTr("720p — balanced"),
                                        qsTr("540p"), qsTr("360p — weak network")]
                            }
                            StyledCombo {
                                id: fpsBox
                                implicitWidth: 110
                                model: ["60 fps", "30 fps"]
                            }
                        }

                        Item {}
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("These three only take effect from the next connection: the video "
                                       + "profile is agreed with the console when the session starts. "
                                       + "During the stream, the bar at the bottom shows what is actually "
                                       + "arriving.")
                            color: Theme.textSecondary
                            font.pixelSize: 10
                        }

                        Text { text: qsTr("Bitrate"); color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 120 }
                        StyledField {
                            id: bitrateField
                            Layout.fillWidth: true
                            placeholderText: qsTr("automatic (kbps)")
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: RegularExpressionValidator { regularExpression: /[0-9]{0,6}/ }
                        }

                    }

                    // The Account ID is chosen per console, from the saved ones.
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("The Account ID each console registers with is chosen in Consoles (Edit), "
                                   + "from the ones saved in Account IDs.")
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    StyledCheck {
                        id: hardwareBox
                        text: qsTr("Decode video on the graphics card (falls back to the processor if it "
                                   + "cannot)")
                    }
                    StyledCheck {
                        id: fullscreenBox
                        text: qsTr("Full screen on connect")
                    }
                    StyledCheck {
                        id: rumbleBox
                        text: qsTr("Controller rumble")
                    }
                    StyledCheck {
                        id: touchpadBox
                        text: qsTr("Mouse acts as the touchpad while streaming")
                    }
                }
                Item { Layout.preferredHeight: 8 }
            }

            SettingsPage {
                id: lookPage
                title: qsTr("Personalisation")

                // The colours one can change; everything else is made from them.
                readonly property var colourKeys: [
                    { key: "accent", label: qsTr("Accent"),
                      hint: qsTr("Buttons, selection, links and the console in use") },
                    { key: "hen", label: qsTr("Jailbreak"),
                      hint: qsTr("GoldHEN, etaHEN, HEN: the console chip, badges and cards") },
                    { key: "ok", label: qsTr("Available"),
                      hint: qsTr("Services that answer, finished tasks") },
                    { key: "warn", label: qsTr("Checking"),
                      hint: qsTr("Services being checked, warnings") },
                    { key: "error", label: qsTr("Error"),
                      hint: qsTr("Errors, services that do not answer") },
                    { key: "background", label: qsTr("Window background"), hint: "" },
                    { key: "panel", label: qsTr("Panels"),
                      hint: qsTr("Top bar, side panel, dialogs and cards") },
                    { key: "text", label: qsTr("Text"), hint: "" }
                ]

                // While dragging in the picker the colour changes on screen at
                // once; it is written to the settings a moment after it stops.
                property string pendingKey: ""
                property string pendingColour: ""
                Timer {
                    id: commitColour
                    interval: 80
                    onTriggered: app.setThemeColor(lookPage.pendingKey, lookPage.pendingColour)
                }

                ColorPicker {
                    id: colourPicker
                    property string key: ""
                    onPicked: (value) => {
                        lookPage.pendingKey = key
                        lookPage.pendingColour = value.toString()
                        commitColour.restart()
                    }
                }

                GroupTitle { text: qsTr("Colours") }
                SettingsGroup {
                    Repeater {
                        model: lookPage.colourKeys
                        RowLayout {
                            id: colourRow
                            required property var modelData
                            readonly property bool changed: app.themeColors[modelData.key] !== undefined
                            Layout.fillWidth: true
                            spacing: 12
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: colourRow.modelData.label
                                    color: Theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                }
                                Text {
                                    visible: text.length > 0
                                    Layout.fillWidth: true
                                    text: colourRow.modelData.hint
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                            StyledToolButton {
                                visible: colourRow.changed
                                iconName: "rotate-ccw"
                                iconSize: 15
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Back to the theme's colour")
                                onClicked: app.setThemeColor(colourRow.modelData.key, "")
                            }
                            // The swatch: the colour in use and its code; a click opens the picker.
                            Rectangle {
                                id: swatch
                                Layout.preferredWidth: 118
                                Layout.preferredHeight: 34
                                radius: 10
                                color: Theme.controlFill
                                border.width: 1
                                border.color: swatchArea.containsMouse ? Theme.alpha(Theme.accent, 0.6)
                                                                       : Theme.glassEdge
                                readonly property color shown: Theme[colourRow.modelData.key]
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 6
                                    spacing: 8
                                    Rectangle {
                                        width: 22
                                        height: 22
                                        radius: 7
                                        color: swatch.shown
                                        border.width: 1
                                        border.color: Qt.rgba(0.5, 0.5, 0.5, 0.4)
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: Qt.rgba(swatch.shown.r, swatch.shown.g, swatch.shown.b, 1)
                                              .toString().toUpperCase()
                                        color: Theme.text
                                        font.family: Theme.fontMono
                                        font.pixelSize: 12
                                    }
                                }
                                MouseArea {
                                    id: swatchArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        colourPicker.key = colourRow.modelData.key
                                        colourPicker.load(Qt.rgba(swatch.shown.r, swatch.shown.g,
                                                                  swatch.shown.b, 1))
                                        colourPicker.parent = swatch
                                        colourPicker.x = swatch.width - colourPicker.width
                                        colourPicker.y = swatch.height + 6
                                        colourPicker.open()
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.textMuted
                            font.pixelSize: 11
                            text: qsTr("The colours go on top of the theme picked in the top bar "
                                       + "(dark, glass or light) and change at once.")
                        }
                        StyledButton {
                            text: qsTr("Theme's colours")
                            chip: true
                            iconName: "rotate-ccw"
                            enabled: Object.keys(app.themeColors).length > 0
                            onClicked: app.resetThemeColors()
                        }
                    }
                }

                GroupTitle { text: qsTr("Saved looks") }
                SettingsGroup {
                    Text {
                        visible: app.themePresets.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: 12
                        text: qsTr("None yet. Give the look in use a name below to keep it.")
                    }

                    Repeater {
                        model: app.themePresets
                        RowLayout {
                            id: presetRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 12
                            // Its colours at a glance (the theme's own where it kept none).
                            Row {
                                spacing: 3
                                Repeater {
                                    model: ["accent", "hen", "background", "panel"]
                                    Rectangle {
                                        required property string modelData
                                        width: 14
                                        height: 26
                                        radius: 4
                                        color: presetRow.modelData.colors[modelData] || Theme.controlFill
                                        border.width: 1
                                        border.color: Theme.glassEdge
                                    }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text {
                                    Layout.fillWidth: true
                                    text: presetRow.modelData.name
                                    color: Theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: presetRow.modelData.theme === "light" ? qsTr("Light theme")
                                        : presetRow.modelData.theme === "glass" ? qsTr("Glass theme")
                                        : qsTr("Dark theme")
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                }
                            }
                            StyledButton {
                                text: qsTr("Apply")
                                chip: true
                                onClicked: app.applyThemePreset(presetRow.modelData.name)
                            }
                            StyledToolButton {
                                iconName: "trash"
                                iconSize: 15
                                danger: true
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Delete this look")
                                onClicked: app.deleteThemePreset(presetRow.modelData.name)
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        StyledField {
                            id: presetName
                            Layout.fillWidth: true
                            placeholderText: qsTr("Name of the look in use")
                            onAccepted: saveLook.clicked()
                        }
                        StyledButton {
                            id: saveLook
                            text: qsTr("Save look")
                            iconName: "save"
                            enabled: presetName.text.trim().length > 0
                            onClicked: {
                                app.saveThemePreset(presetName.text)
                                presetName.text = ""
                            }
                        }
                    }
                }
                Item { Layout.preferredHeight: 8 }
            }

            SettingsPage {
                title: qsTr("General")

                GroupTitle { text: qsTr("Appearance") }
                SettingsGroup {
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 8
                        Text { text: qsTr("Language"); color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 120 }
                        StyledCombo {
                            id: languageBox
                            Layout.fillWidth: true
                            model: ["English", "Português"]
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: 11
                        text: qsTr("The language changes the next time the app opens. The theme is "
                                   + "picked with the icons in the top bar, next to settings.")
                    }
                }

                GroupTitle { text: qsTr("Updates") }
                SettingsGroup {
                    StyledCheck {
                        id: updatesBox
                        Layout.fillWidth: true
                        text: qsTr("Updates over the internet")
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: 11
                        text: updatesBox.checked
                              ? qsTr("On start, looks for a new version in the repository below and asks "
                                     + "before installing. Nothing is installed without your click.")
                              : qsTr("Off: the app does not go online looking for versions. \"Check now\" "
                                     + "still works.")
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 8
                        Text { text: qsTr("Repository"); color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 120 }
                        StyledField {
                            id: updateRepoField
                            Layout.fillWidth: true
                            placeholderText: "owner/name"
                        }
                        Text { text: qsTr("Channel"); color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 120 }
                        StyledCombo {
                            id: updateChannelBox
                            Layout.fillWidth: true
                            model: [qsTr("Stable"), qsTr("Testing (branch builds)")]
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        StyledButton {
                            text: qsTr("Check now")
                            enabled: app.updateState !== "checking"
                                     && app.updateState !== "downloading"
                            onClicked: dialog.checkUpdatesNow()
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: app.updateMessage
                            color: app.updateState === "error" ? Theme.error
                                 : app.updateState === "available" ? Theme.ok
                                 : Theme.textMuted
                            font.pixelSize: 11
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: 11
                        text: qsTr("Releases are read from the GitHub API and the repository has to be "
                                   + "public. \"Check now\" saves the settings first.")
                    }
                }

                GroupTitle { text: qsTr("Advanced") }
                SettingsGroup {
                    StyledCheck {
                        id: advancedBox
                        Layout.fillWidth: true
                        labelColor: Theme.warn
                        text: qsTr("Advanced mode: allows writing to the FTP system areas")
                    }

                    StyledCheck {
                        id: debugBox
                        Layout.fillWidth: true
                        text: qsTr("Verbose log (debug)")
                    }
                }
                Item { Layout.preferredHeight: 8 }
            }
        }
    }
}
