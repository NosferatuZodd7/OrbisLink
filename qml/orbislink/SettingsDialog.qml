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
    width: 730
    height: Math.min(parent ? parent.height - 80 : 620, 660)
    padding: 0

    // A nearly opaque modal: with the panels' transparency, what is behind
    // would show through the box, and a box asking for a decision must not
    // be a window.
    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radius
    }

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

    header: Rectangle {
        implicitHeight: Theme.dialogHeader
        color: "transparent"
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.dialogMargin
            text: qsTr("Settings")
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    footer: Rectangle {
        implicitHeight: Theme.dialogFooter
        color: "transparent"
        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.border
        }
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogInner
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 10
            StyledButton {
                text: qsTr("Setup wizard…")
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

    contentItem: ScrollView {
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: dialog.width - Theme.dialogMargin * 2
            x: Theme.dialogMargin
            y: Theme.dialogInner
            spacing: 14

            ConsoleManager { Layout.fillWidth: true }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            AccountManager { Layout.fillWidth: true }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Text { text: qsTr("Console in use"); color: Theme.accent; font.bold: true; font.pixelSize: 12 }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                Text { text: qsTr("Name"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField { id: nameField; Layout.fillWidth: true }

                Text { text: qsTr("IP address"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField {
                    id: addressField
                    Layout.fillWidth: true
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

                Text { text: qsTr("Installer port"); color: Theme.textMuted; font.pixelSize: 12 }
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

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Text { text: qsTr("Installation"); color: Theme.accent; font.bold: true; font.pixelSize: 12 }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                Text { text: qsTr("Default mode"); color: Theme.textMuted; font.pixelSize: 12 }
                ComboBox {
                    id: modeBox
                    Layout.fillWidth: true
                    implicitHeight: 32
                    model: [qsTr("Direct install"), qsTr("FTP upload")]

                    background: Rectangle {
                        color: Theme.panelAltFill
                        border.color: modeBox.activeFocus ? Theme.accent : Theme.border
                        radius: 6
                    }
                    contentItem: Text {
                        leftPadding: 8
                        text: modeBox.displayText
                        color: Theme.text
                        font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                    }
                    indicator: Text {
                        x: modeBox.width - width - 10
                        y: modeBox.height / 2 - height / 2
                        text: "⌄"
                        color: Theme.textMuted
                        font.pixelSize: 14
                    }
                    delegate: ItemDelegate {
                        width: modeBox.width
                        contentItem: Text {
                            text: modelData
                            color: Theme.text
                            font.pixelSize: 12
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: highlighted ? Theme.accentSoft : Theme.panelAlt
                        }
                        highlighted: modeBox.highlightedIndex === index
                    }
                    popup: Popup {
                        y: modeBox.height
                        width: modeBox.width
                        implicitHeight: contentItem.implicitHeight
                        padding: 1
                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: modeBox.popup.visible ? modeBox.delegateModel : null
                        }
                        background: Rectangle {
                            color: Theme.panelAltFill
                            border.color: Theme.border
                            radius: 6
                        }
                    }
                }

                Text { text: qsTr("FTP folder"); color: Theme.textMuted; font.pixelSize: 12 }
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

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Text {
                text: qsTr("Local HTTP server")
                color: Theme.accent
                font.bold: true
                font.pixelSize: 12
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Text { text: qsTr("Port"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField { id: httpPortField; Layout.fillWidth: true }
            }

            StyledCheck {
                id: restrictBox
                Layout.fillWidth: true
                text: qsTr("Only accept requests from the console's IP")
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Text { text: qsTr("Remote Play"); color: Theme.accent; font.bold: true; font.pixelSize: 12 }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                Text { text: qsTr("Quality"); color: Theme.textMuted; font.pixelSize: 12 }
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

                Text { text: qsTr("Bitrate"); color: Theme.textMuted; font.pixelSize: 12 }
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

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Text { text: qsTr("Appearance"); color: Theme.accent; font.bold: true; font.pixelSize: 12 }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Text { text: qsTr("Language"); color: Theme.textMuted; font.pixelSize: 12 }
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

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Text { text: qsTr("Updates"); color: Theme.accent; font.bold: true; font.pixelSize: 12 }

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
                Text { text: qsTr("Repository"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField {
                    id: updateRepoField
                    Layout.fillWidth: true
                    placeholderText: "dono/nome"
                }
                Text { text: qsTr("Channel"); color: Theme.textMuted; font.pixelSize: 12 }
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
                    onClicked: {
                        // Save first: otherwise the old repository would be
                        // checked, not the one typed in the field. Only these
                        // three values, and without closing: the result shows
                        // up right here, and whoever pressed wants to see it.
                        app.saveUpdateSettings(updatesBox.checked,
                                               updateRepoField.text.trim(),
                                               updateChannelBox.currentIndex === 1
                                                   ? "testing" : "stable")
                        app.checkForUpdatesNow(false)
                    }
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

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Text { text: qsTr("Advanced"); color: Theme.accent; font.bold: true; font.pixelSize: 12 }

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

            Item { Layout.fillWidth: true; Layout.preferredHeight: 6 }
        }
    }
}
