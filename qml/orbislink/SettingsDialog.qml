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
    width: Math.min(parent ? parent.width - 60 : 980, 980)
    height: Math.min(parent ? parent.height - 60 : 700, 720)
    padding: 0

    // A nearly opaque modal: with the panels' transparency, what is behind
    // would show through the box, and a box asking for a decision must not
    // be a window.
    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.glassEdge
        radius: Theme.radiusDialog
    }

    // The section shown on the right: 0 Consoles, 1 Account IDs, 2 Console in
    // use, 3 Remote Play, 4 Personalisation, 5 General, 6 About.
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
    // Where FTP answered when it was not on the typed port (0 otherwise).
    property int probeOtherFtpPort: 0

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
        probeOtherFtpPort = 0
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
        onTriggered: app.probeConsole(addressField.text.trim(), dialog.typedFtpPort(),
            parseInt(installerPortField.text) || 12800, dialog.consoleIsPs5)
    }

    function typedFtpPort() {
        return parseInt(ftpPortField.text) || (dialog.consoleIsPs5 ? 1337 : 2121)
    }

    Connections {
        target: app
        function onConsoleProbed(address, ftpOk, installerOk, detail, ftpPort) {
            // A reply for an address that is no longer the typed one does not count.
            if (address !== addressField.text.trim())
                return
            dialog.probeFtpOk = ftpOk
            dialog.probeInstallerOk = installerOk
            dialog.probeDetail = detail
            dialog.probeOtherFtpPort = ftpOk && ftpPort > 0 && ftpPort !== dialog.typedFtpPort() ? ftpPort : 0
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
        keepConvertedBox.checked = values.keepConvertedPackages === true
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
            "keepConvertedPackages": keepConvertedBox.checked,
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

    // ── The pieces every page is made of.

    // One page of the settings: its head (eyebrow, title in the serif, what
    // the page is for) and its plates, scrolling on its own.
    component SettingsPage: ScrollView {
        id: page
        property string eyebrow: ""
        property string title: ""
        property string subtitle: ""
        default property alias content: pageColumn.data
        clip: true
        contentWidth: availableWidth
        // Told outright, top and bottom margins included: worked out on its
        // own, the height could come out short and the end of a page would
        // be out of reach.
        contentHeight: pageColumn.implicitHeight + 56
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        // In sight whenever there is more below, not only while scrolling.
        ScrollBar.vertical.policy: contentHeight > height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded

        ColumnLayout {
            id: pageColumn
            x: 36
            y: 28
            width: page.availableWidth - 72
            spacing: 12

            PageHeader {
                visible: page.title.length > 0
                // Room on the right for the dialog's round ✕.
                Layout.rightMargin: 40
                Layout.bottomMargin: 8
                eyebrow: page.eyebrow
                title: page.title
                subtitle: page.subtitle
            }
        }
    }

    // A few related settings on one plate, a hairline between each.
    component SettingsGroup: Plate {
        default property alias content: groupColumn.data
        Layout.fillWidth: true
        implicitHeight: groupColumn.implicitHeight + 8
        ColumnLayout {
            id: groupColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 4
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 0
        }
    }

    // The label over a plate, in brass small capitals.
    component GroupTitle: Eyebrow {
        Layout.fillWidth: true
        Layout.topMargin: 12
        Layout.leftMargin: 4
    }

    // One setting: what it is and what it does on the left, its control on
    // the right. The hairline above it is left out on the first row, which
    // sits at the top of its plate.
    component SettingRow: Item {
        id: setting
        property string label: ""
        property string description: ""
        property color labelColor: Theme.text
        // The control's column; 0 lets it take what it needs.
        property int controlWidth: 280
        // Indented under the setting it depends on.
        property int indent: 0
        // A click on the words does what a click on the control does.
        signal labelClicked()
        property bool labelClickable: false
        default property alias control: slot.data

        Layout.fillWidth: true
        implicitHeight: Math.max(words.implicitHeight, slot.implicitHeight) + 28

        Rectangle {
            visible: setting.y > 1
            width: parent.width
            height: 1
            color: Theme.glassEdge
        }

        RowLayout {
            id: settingLayout
            anchors.fill: parent
            anchors.topMargin: 14
            anchors.bottomMargin: 14
            anchors.leftMargin: setting.indent
            spacing: 24

            ColumnLayout {
                id: words
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: setting.label
                    color: setting.enabled ? setting.labelColor : Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Medium
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: setting.description
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    lineHeight: 1.15
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                id: slot
                Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                // A layout fills by default when a child does: told not to,
                // so every control's column keeps its width and they line up.
                Layout.fillWidth: false
                Layout.preferredWidth: setting.controlWidth > 0 ? setting.controlWidth : -1
                spacing: 8
            }
        }

        // Over the words only: the control takes its own clicks.
        MouseArea {
            x: settingLayout.x + words.x
            y: settingLayout.y + words.y
            width: words.width
            height: words.height
            visible: setting.labelClickable
            enabled: setting.enabled
            cursorShape: Qt.PointingHandCursor
            onClicked: setting.labelClicked()
        }
    }

    // An on/off setting: the words on the left, the switch on the right.
    component SwitchRow: SettingRow {
        id: switchRow
        property alias checked: toggle.checked
        controlWidth: 0
        labelClickable: true
        onLabelClicked: toggle.toggle()
        StyledCheck {
            id: toggle
            Layout.fillWidth: false
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            Accessible.name: switchRow.label
        }
    }

    // A label in the rail: an ink wash and the brass star when it is open.
    component RailItem: Item {
        id: railItem
        property string iconName: ""
        property string text: ""
        property bool selected: false
        property bool spinning: false
        signal clicked()

        width: parent ? parent.width : 0
        implicitHeight: 38
        Accessible.role: Accessible.Button
        Accessible.name: text

        Rectangle {
            anchors.fill: parent
            radius: 10
            color: railItem.selected ? Theme.alpha(Theme.ink, Theme.light ? 0.09 : 0.08)
                 : railMouse.containsMouse ? Theme.alpha(Theme.ink, Theme.light ? 0.05 : 0.045)
                 : "transparent"
            border.width: railItem.selected ? 1 : 0
            border.color: Theme.alpha(Theme.ink, Theme.light ? 0.12 : 0.10)
            Behavior on color { ColorAnimation { duration: Theme.fast } }
        }
        Icon {
            id: railGlyph
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            name: railItem.iconName
            size: 18
            spinning: railItem.spinning
            color: railItem.selected || railItem.spinning ? Theme.ink
                 : railMouse.containsMouse ? Theme.text : Theme.textSecondary
        }
        Text {
            anchors.left: railGlyph.right
            anchors.leftMargin: 12
            anchors.right: railStar.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: railItem.text
            color: railItem.selected ? Theme.ink : Theme.text
            font.pixelSize: Theme.fontBody
            font.weight: railItem.selected ? Font.Bold : Font.Medium
            elide: Text.ElideRight
        }
        Sparkle {
            id: railStar
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            size: 9
            opacity: railItem.selected ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        }
        MouseArea {
            id: railMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: railItem.clicked()
        }
    }

    // A field for a number or an address: in the mono, as every technical
    // value in the app.
    component MonoField: StyledField {
        font.family: Theme.fontMono
        font.pixelSize: 13
    }

    contentItem: RowLayout {
        spacing: 0

        // ── The rail: the index of the settings, on the left.
        Item {
            Layout.fillHeight: true
            Layout.preferredWidth: 248
            // The rail's wash keeps the dialog's rounded corners on the left
            // and a straight edge against the pages.
            clip: true

            Rectangle {
                width: parent.width + Theme.radiusDialog
                height: parent.height
                radius: Theme.radiusDialog
                color: Theme.light ? Theme.alpha(Theme.ink, 0.04) : Qt.rgba(0, 0, 0, 0.16)
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 26
                anchors.bottomMargin: 18
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 0

                // The dialog's own head, at the top of its index.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.bottomMargin: 18
                    spacing: 2
                    Eyebrow { text: qsTr("Preferences") }
                    Text {
                        text: qsTr("Settings")
                        color: Theme.ink
                        font.pixelSize: 26
                        font.family: Theme.fontDisplay
                        font.weight: Font.DemiBold
                    }
                }

                Column {
                    Layout.fillWidth: true
                    spacing: 2

                    Repeater {
                        model: [
                            { icon: "monitor", label: qsTr("Consoles") },
                            { icon: "id-card", label: qsTr("Account IDs (PSID)") },
                            { icon: "server", label: qsTr("Console in use") },
                            { icon: "cast", label: qsTr("Remote Play") },
                            { icon: "sparkles", label: qsTr("Personalisation") },
                            { icon: "sliders", label: qsTr("General") }
                        ]

                        delegate: RailItem {
                            required property var modelData
                            required property int index
                            iconName: modelData.icon
                            text: modelData.label
                            selected: dialog.section === index
                            onClicked: dialog.section = index
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                EngravedRule {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 8
                    star: false
                }

                Column {
                    Layout.fillWidth: true
                    spacing: 2

                    // Not a section: looks for a new version straight away, on
                    // the repository and channel set under General, and says
                    // how it went.
                    RailItem {
                        readonly property bool busy: app.updateState === "checking"
                                                     || app.updateState === "downloading"
                        iconName: "refresh"
                        text: qsTr("Update check")
                        spinning: busy
                        onClicked: dialog.checkUpdatesNow()
                    }

                    // About OrbisLink: a section of its own, opened here like
                    // the others.
                    RailItem {
                        iconName: "info"
                        text: qsTr("About OrbisLink")
                        selected: dialog.section === 6
                        onClicked: dialog.section = 6
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    leftPadding: 12
                    rightPadding: 4
                    visible: app.updateMessage.length > 0
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    text: app.updateMessage
                    color: app.updateState === "error" ? (Theme.light ? Qt.darker(Theme.error, 1.3) : Theme.error)
                         : app.updateState === "available" ? (Theme.light ? Qt.darker(Theme.ok, 1.3) : Theme.ok)
                         : Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                }
            }
        }

        Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: Theme.glassEdge }

        // ── The open section, on the right, and the dialog's actions under it.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                StackLayout {
                    anchors.fill: parent
                    currentIndex: dialog.section

                    SettingsPage {
                        ConsoleManager { Layout.fillWidth: true }
                    }

                    SettingsPage {
                        AccountManager { Layout.fillWidth: true }
                    }

                    SettingsPage {
                        eyebrow: qsTr("Connection")
                        title: qsTr("Console in use")
                        subtitle: qsTr("Where it is on the network, how packages reach it and the PC's "
                                       + "own HTTP server.")

                        GroupTitle { text: qsTr("Address") }
                        SettingsGroup {
                            SettingRow {
                                label: qsTr("Name")
                                StyledField { id: nameField; Layout.fillWidth: true }
                            }
                            SettingRow {
                                label: qsTr("IP address")
                                MonoField {
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
                            }
                            SettingRow {
                                // The port is the console in use's: a PS5 with etaHEN uses another.
                                label: dialog.consoleIsPs5 ? qsTr("FTP port (PS5)") : qsTr("FTP port")
                                controlWidth: 140
                                MonoField {
                                    id: ftpPortField
                                    Layout.fillWidth: true
                                    onTextChanged: dialog.scheduleProbe()
                                }
                            }
                            SettingRow {
                                label: qsTr("Installer port")
                                controlWidth: 140
                                MonoField {
                                    id: installerPortField
                                    Layout.fillWidth: true
                                    onTextChanged: dialog.scheduleProbe()
                                }
                            }

                            // Result of the automatic check, with no button at all.
                            Item {
                                id: probeRow
                                Layout.fillWidth: true
                                implicitHeight: probeColumn.implicitHeight + 28
                                Rectangle { width: parent.width; height: 1; color: Theme.glassEdge }

                                ColumnLayout {
                                    id: probeColumn
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 10

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 10

                                        Item {
                                            implicitWidth: 14
                                            implicitHeight: 14
                                            Rectangle {
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
                                                width: 14
                                                height: 14
                                                radius: 7
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
                                            color: dialog.probeState === "idle" ? Theme.textSecondary
                                                 : Theme.light ? Qt.darker(dialog.probeColor, 1.3)
                                                 : dialog.probeColor
                                            font.pixelSize: Theme.fontSmall
                                            wrapMode: Text.WordWrap
                                        }
                                    }

                                    // FTP was found, but on another port than the one typed.
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 24
                                        visible: dialog.probeOtherFtpPort > 0
                                        spacing: 10

                                        Text {
                                            Layout.fillWidth: true
                                            text: qsTr("FTP answers on port %1, not on %2.")
                                                .arg(dialog.probeOtherFtpPort).arg(dialog.typedFtpPort())
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.fontSmall
                                            wrapMode: Text.WordWrap
                                        }
                                        StyledButton {
                                            chip: true
                                            text: qsTr("Use port %1").arg(dialog.probeOtherFtpPort)
                                            onClicked: ftpPortField.text = String(dialog.probeOtherFtpPort)
                                        }
                                    }
                                }
                            }
                        }

                        GroupTitle { text: qsTr("Installation") }
                        SettingsGroup {
                            SettingRow {
                                label: qsTr("Default mode")
                                StyledCombo {
                                    id: modeBox
                                    Layout.fillWidth: true
                                    model: [qsTr("Direct install"), qsTr("FTP upload")]
                                }
                            }
                            SettingRow {
                                label: qsTr("FTP folder")
                                MonoField { id: uploadDirField; Layout.fillWidth: true }
                            }
                            SwitchRow {
                                id: existsBox
                                label: qsTr("Check whether the title is already on the console before installing")
                            }
                            SwitchRow {
                                id: installAfterBox
                                label: qsTr("Also install after sending over FTP")
                                description: qsTr("The file stays on the console and is installed right away from the "
                                                  + "PC — the remote installer only knows how to download over HTTP.")
                            }
                            SwitchRow {
                                id: deleteAfterBox
                                indent: 20
                                enabled: installAfterBox.checked
                                label: qsTr("And delete the console copy once installed")
                            }
                            SwitchRow {
                                id: keepConvertedBox
                                label: qsTr("Keep the packages the converter makes once they are on the console")
                                description: qsTr("Off, a package made from a PS1/PS2 disc or an app folder is deleted from "
                                                  + "the output folder once the console has it — installed, or sent when it "
                                                  + "is not installed. Packages of your own are never touched.")
                            }
                        }

                        GroupTitle { text: qsTr("Local HTTP server") }
                        SettingsGroup {
                            SettingRow {
                                label: qsTr("Port")
                                controlWidth: 140
                                MonoField { id: httpPortField; Layout.fillWidth: true }
                            }
                            SwitchRow {
                                id: restrictBox
                                label: qsTr("Only accept requests from the console's IP")
                            }
                        }
                    }

                    SettingsPage {
                        eyebrow: qsTr("Streaming")
                        title: qsTr("Remote Play")
                        subtitle: qsTr("The picture, the controller and the window while a console streams.")

                        GroupTitle { text: qsTr("Picture") }
                        SettingsGroup {
                            SettingRow {
                                label: qsTr("Quality")
                                description: qsTr("These three only take effect from the next connection: the video "
                                                  + "profile is agreed with the console when the session starts. "
                                                  + "During the stream, the bar at the bottom shows what is actually "
                                                  + "arriving.")
                                controlWidth: 360
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
                                    Layout.preferredWidth: 116
                                    model: ["60 fps", "30 fps"]
                                }
                            }
                            SettingRow {
                                label: qsTr("Bitrate")
                                controlWidth: 200
                                MonoField {
                                    id: bitrateField
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("automatic (kbps)")
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    validator: RegularExpressionValidator { regularExpression: /[0-9]{0,6}/ }
                                }
                            }
                            SwitchRow {
                                id: hardwareBox
                                label: qsTr("Decode video on the graphics card (falls back to the processor if it "
                                            + "cannot)")
                            }
                        }

                        GroupTitle { text: qsTr("Session") }
                        SettingsGroup {
                            SwitchRow {
                                id: fullscreenBox
                                label: qsTr("Full screen on connect")
                            }
                            SwitchRow {
                                id: rumbleBox
                                label: qsTr("Controller rumble")
                            }
                            SwitchRow {
                                id: touchpadBox
                                label: qsTr("Mouse acts as the touchpad while streaming")
                            }
                        }

                        // The Account ID is chosen per console, from the saved ones.
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            Layout.leftMargin: 4
                            spacing: 10
                            Icon {
                                Layout.alignment: Qt.AlignTop
                                name: "id-card"
                                size: 16
                                color: Theme.brass
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("The Account ID each console registers with is chosen in Consoles (Edit), "
                                           + "from the ones saved in Account IDs.")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    SettingsPage {
                        id: lookPage
                        eyebrow: qsTr("Appearance")
                        title: qsTr("Personalisation")
                        subtitle: qsTr("The colours go on top of the theme in use and change at once; a look "
                                       + "can be saved to come back to.")

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
                              hint: qsTr("The navigation, the side panels, dialogs and plates") },
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

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 12
                            spacing: 10
                            Eyebrow {
                                Layout.fillWidth: true
                                Layout.leftMargin: 4
                                text: qsTr("Colours")
                            }
                            StyledButton {
                                text: qsTr("Theme's colours")
                                chip: true
                                iconName: "rotate-ccw"
                                enabled: Object.keys(app.themeColors).length > 0
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Every colour back to the theme's own")
                                onClicked: app.resetThemeColors()
                            }
                        }
                        SettingsGroup {
                            Repeater {
                                model: lookPage.colourKeys
                                SettingRow {
                                    id: colourRow
                                    required property var modelData
                                    readonly property bool changed: app.themeColors[modelData.key] !== undefined
                                    label: modelData.label
                                    description: modelData.hint
                                    controlWidth: 0

                                    StyledToolButton {
                                        opacity: colourRow.changed ? 1 : 0
                                        enabled: colourRow.changed
                                        iconName: "rotate-ccw"
                                        iconSize: 15
                                        Accessible.name: qsTr("Back to the theme's colour")
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Back to the theme's colour")
                                        onClicked: app.setThemeColor(colourRow.modelData.key, "")
                                    }
                                    // The swatch: the colour in use and its code; a click opens the picker.
                                    Rectangle {
                                        id: swatch
                                        Layout.preferredWidth: 124
                                        Layout.preferredHeight: 36
                                        radius: height / 2
                                        color: Theme.controlFill
                                        border.width: 1
                                        border.color: swatchArea.containsMouse ? Theme.alpha(Theme.accent, 0.6)
                                                                               : Theme.glassEdge
                                        readonly property color shown: Theme[colourRow.modelData.key]
                                        Accessible.role: Accessible.Button
                                        Accessible.name: colourRow.modelData.label
                                        Row {
                                            anchors.verticalCenter: parent.verticalCenter
                                            x: 6
                                            spacing: 9
                                            Rectangle {
                                                width: 24
                                                height: 24
                                                radius: 12
                                                color: swatch.shown
                                                border.width: 1
                                                border.color: Theme.alpha(Theme.ink, 0.25)
                                            }
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: Qt.rgba(swatch.shown.r, swatch.shown.g, swatch.shown.b, 1)
                                                      .toString().toUpperCase()
                                                color: Theme.text
                                                font.family: Theme.fontMono
                                                font.pixelSize: Theme.fontSmall
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
                                                // Below the colour, or above it when the window
                                                // has no room left below.
                                                var below = swatch.mapToItem(null, 0, swatch.height).y
                                                var room = (swatch.Window.height || 0) - below
                                                colourPicker.y = room > colourPicker.implicitHeight + 16
                                                               ? swatch.height + 6
                                                               : -colourPicker.implicitHeight - 6
                                                colourPicker.open()
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        GroupTitle { text: qsTr("Saved looks") }
                        SettingsGroup {
                            Text {
                                visible: app.themePresets.length === 0
                                Layout.fillWidth: true
                                Layout.topMargin: 14
                                Layout.bottomMargin: 6
                                wrapMode: Text.WordWrap
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                text: qsTr("None yet. Give the look in use a name below to keep it.")
                            }

                            Repeater {
                                model: app.themePresets
                                Item {
                                    id: presetRow
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 62
                                    Rectangle {
                                        visible: presetRow.y > 1
                                        width: parent.width
                                        height: 1
                                        color: Theme.glassEdge
                                    }
                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 14
                                        // Its colours at a glance (the theme's own where it kept none).
                                        Row {
                                            spacing: -6
                                            Repeater {
                                                model: ["accent", "hen", "background", "panel"]
                                                Rectangle {
                                                    required property string modelData
                                                    width: 24
                                                    height: 24
                                                    radius: 12
                                                    color: presetRow.modelData.colors[modelData] || Theme.controlFill
                                                    border.width: 2
                                                    border.color: Theme.panelFill
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
                                                font.pixelSize: Theme.fontBody
                                                font.weight: Font.Medium
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                text: presetRow.modelData.theme === "light" ? qsTr("Light theme")
                                                    : presetRow.modelData.theme === "glass" ? qsTr("Glass theme")
                                                    : qsTr("Dark theme")
                                                color: Theme.textSecondary
                                                font.pixelSize: Theme.fontSmall
                                            }
                                        }
                                        StyledButton {
                                            readonly property bool inUse: app.customLook
                                                                          && app.activePreset === presetRow.modelData.name
                                            text: inUse ? qsTr("In use") : qsTr("Apply")
                                            chip: true
                                            primary: inUse
                                            enabled: !inUse
                                            onClicked: app.applyThemePreset(presetRow.modelData.name)
                                        }
                                        StyledToolButton {
                                            iconName: "trash"
                                            iconSize: 15
                                            danger: true
                                            Accessible.name: qsTr("Delete this look")
                                            ToolTip.visible: hovered
                                            ToolTip.text: qsTr("Delete this look")
                                            onClicked: app.deleteThemePreset(presetRow.modelData.name)
                                        }
                                    }
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                                implicitHeight: saveLookRow.implicitHeight + 28
                                Rectangle { width: parent.width; height: 1; color: Theme.glassEdge }
                                RowLayout {
                                    id: saveLookRow
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
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
                        }
                    }

                    SettingsPage {
                        eyebrow: qsTr("Application")
                        title: qsTr("General")
                        subtitle: qsTr("The language, how new versions are found, and the options for "
                                       + "those who know what they do.")

                        GroupTitle { text: qsTr("Language") }
                        SettingsGroup {
                            SettingRow {
                                label: qsTr("Language")
                                description: qsTr("Changes the next time the app opens. The theme is picked at "
                                                  + "the foot of the navigation.")
                                controlWidth: 220
                                StyledCombo {
                                    id: languageBox
                                    Layout.fillWidth: true
                                    model: ["English", "Português"]
                                }
                            }
                        }

                        GroupTitle { text: qsTr("Updates") }
                        SettingsGroup {
                            SwitchRow {
                                id: updatesBox
                                label: qsTr("Updates over the internet")
                                description: checked
                                      ? qsTr("On start, looks for a new version in the repository below and asks "
                                             + "before installing. Nothing is installed without your click.")
                                      : qsTr("Off: the app does not go online looking for versions. \"Check now\" "
                                             + "still works.")
                            }
                            SettingRow {
                                label: qsTr("Repository")
                                description: qsTr("Releases are read from the GitHub API and the repository has to be "
                                                  + "public. \"Check now\" saves the settings first.")
                                MonoField {
                                    id: updateRepoField
                                    Layout.fillWidth: true
                                    placeholderText: "owner/name"
                                }
                            }
                            SettingRow {
                                label: qsTr("Channel")
                                StyledCombo {
                                    id: updateChannelBox
                                    Layout.fillWidth: true
                                    model: [qsTr("Stable"), qsTr("Testing (branch builds)")]
                                }
                            }
                            SettingRow {
                                label: app.updateMessage.length > 0 ? app.updateMessage : qsTr("Look for a new version now")
                                labelColor: app.updateState === "error"
                                            ? (Theme.light ? Qt.darker(Theme.error, 1.3) : Theme.error)
                                          : app.updateState === "available"
                                            ? (Theme.light ? Qt.darker(Theme.ok, 1.3) : Theme.ok)
                                          : Theme.text
                                controlWidth: 0
                                StyledButton {
                                    text: qsTr("Check now")
                                    iconName: "refresh"
                                    enabled: app.updateState !== "checking"
                                             && app.updateState !== "downloading"
                                    onClicked: dialog.checkUpdatesNow()
                                }
                            }
                        }

                        GroupTitle { text: qsTr("Advanced") }
                        SettingsGroup {
                            SwitchRow {
                                id: advancedBox
                                labelColor: Theme.light ? Qt.darker(Theme.warn, 1.3) : Theme.warn
                                label: qsTr("Advanced mode: allows writing to the FTP system areas")
                            }
                            SwitchRow {
                                id: debugBox
                                label: qsTr("Verbose log (debug)")
                            }
                        }
                    }

                    SettingsPage {
                        AboutPage {
                            onCheckUpdates: dialog.checkUpdatesNow()
                        }
                    }
                }

                // The dialog's ✕, over the top right of every page.
                StyledToolButton {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: 22
                    anchors.rightMargin: 22
                    z: 2
                    iconName: "close"
                    Accessible.name: qsTr("Close")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Close")
                    onClicked: dialog.close()
                }
            }

            // ── The actions: the wizard on the left, cancel and save on the right.
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.glassEdge }
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.dialogFooter
                Layout.leftMargin: 28
                Layout.rightMargin: 28
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
    }
}
