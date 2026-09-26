// SPDX-License-Identifier: AGPL-3.0-or-later
//
// First launch: instead of an empty window with red services and no
// explanation, three steps that get the application working.
//
// It appears once. Anyone who wants to see it again has the button in the settings.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: wizard
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 630
    modal: true
    closePolicy: Popup.NoAutoClose
    padding: 0

    property int step: 0
    readonly property int steps: 3

    // The same automatic check as in the settings: type the IP and the
    // wizard says whether the console answered, without pressing anything.
    property string probeState: "idle"
    property bool probeFtpOk: false
    property bool probeInstallerOk: false

    readonly property color probeColor: probeState === "ok" ? Theme.ok
        : probeState === "partial" ? Theme.warn
        : probeState === "fail" ? Theme.error
        : probeState === "checking" ? Theme.warn
        : Theme.textMuted

    readonly property string probeText: {
        if (probeState === "checking")
            return qsTr("Checking %1…").arg(addressInput.text.trim())
        if (probeState === "ok")
            return qsTr("Console found — the installer and FTP both answer.")
        if (probeState === "partial") {
            if (probeInstallerOk)
                return qsTr("The remote installer answers, FTP does not — see the next step.")
            return qsTr("FTP answers, the remote installer does not — see the next step.")
        }
        if (probeState === "fail")
            return qsTr("No answer from %1. Check the IP and that the console is on.")
                .arg(addressInput.text.trim())
        return qsTr("Type the address — it is checked on its own.")
    }

    function addressComplete(message) {
        var value = (message || "").trim()
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

    function scheduleCheck() {
        if (!addressComplete(addressInput.text)) {
            probeState = "idle"
            probeTimer.stop()
            return
        }
        probeState = "checking"
        probeTimer.restart()
    }

    Timer {
        id: probeTimer
        interval: 600
        repeat: false
        onTriggered: app.probeConsole(addressInput.text.trim(), 2121, 12800)
    }

    Connections {
        target: app
        function onConsoleProbed(address, ftpOk, installerOk, detail) {
            if (address !== addressInput.text.trim())
                return
            wizard.probeFtpOk = ftpOk
            wizard.probeInstallerOk = installerOk
            wizard.probeState = (ftpOk && installerOk) ? "ok"
                : (ftpOk || installerOk) ? "partial" : "fail"
        }
    }

    function begin() {
        step = 0
        var values = app.settingsMap()
        addressInput.text = values.consoleAddress
        consoleNameField.text = values.consoleName
        accountWizardField.text = values.streamAccountId
        probeState = "idle"
        open()
        scheduleCheck()
    }

    function next() {
        if (step < steps - 1) {
            step++
            return
        }
        save()
        close()
    }

    // Saving closes the wizard for good: from then on it opens from the
    // button in the settings.
    function save() {
        app.applySettings({
            "consoleAddress": addressInput.text.trim(),
            "consoleName": consoleNameField.text.trim().length > 0 ? consoleNameField.text.trim() : "PS4",
            // Stored converted: the console only accepts base64, and this way
            // what is saved is directly usable.
            "streamAccountId": accountWizardField.ok ? accountWizardField.base64
                                                     : accountWizardField.text.trim(),
            "firstRunDone": true
        })
    }

    // A nearly opaque modal: with the panels' transparency, what is behind
    // would show through the box, and a box asking for a decision must not
    // be a window.
    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radius
    }

    header: Rectangle {
        implicitHeight: Theme.dialogHeader
        color: "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 10
            Image {
                source: "qrc:/icons/mark.png"
                sourceSize.width: 26
                sourceSize.height: 26
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Welcome to OrbisLink")
                color: Theme.text
                font.pixelSize: 16
                font.bold: true
            }
            // Three dots saying where you are going.
            Row {
                spacing: 6
                Repeater {
                    model: wizard.steps
                    delegate: Rectangle {
                        width: 7
                        height: 7
                        radius: 4
                        color: index === wizard.step ? Theme.accent : Theme.border
                    }
                }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
    }

    footer: Rectangle {
        implicitHeight: Theme.dialogFooter
        color: "transparent"
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogInner
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 8
            StyledButton {
                text: qsTr("Skip")
                implicitHeight: 32
                onClicked: { wizard.save(); wizard.close() }
            }
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Back")
                implicitHeight: 32
                enabled: wizard.step > 0
                onClicked: wizard.step--
            }
            StyledButton {
                text: wizard.step === wizard.steps - 1 ? qsTr("Start") : qsTr("Next")
                implicitHeight: 32
                minimumWidth: 120
                primary: true
                onClicked: wizard.next()
            }
        }
    }

    contentItem: StackLayout {
        currentIndex: wizard.step

        // ── 1. the console
        ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: Theme.dialogInner
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: 13
                text: qsTr("First of all, where the console is.")
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: 12
                text: qsTr("The IP is on the console under Settings → Network → View Connection "
                           + "Status. It has to be on the same network as this PC.")
            }
            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Text { text: qsTr("IP address"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField {
                    id: addressInput
                    Layout.fillWidth: true
                    placeholderText: "192.168.1.42"
                    onTextChanged: wizard.scheduleCheck()
                }
                Text { text: qsTr("Name"); color: Theme.textMuted; font.pixelSize: 12 }
                StyledField {
                    id: consoleNameField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Living room PS4")
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.bottomMargin: Theme.dialogInner
                spacing: 8
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: wizard.probeColor
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: wizard.probeText
                    color: wizard.probeColor
                    font.pixelSize: 11
                }
            }
        }

        // ── 2. what has to be enabled on the console
        ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: Theme.dialogInner
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: 13
                text: qsTr("On the console, three things:")
            }
            Repeater {
                model: [
                    qsTr("GoldHEN loaded — it is what brings the FTP server (port 2121)."),
                    qsTr("Remote Package Installer open, to install .pkg files from here."),
                    qsTr("Remote Play enabled under Settings → Remote Play Connection Settings.")
                ]
                delegate: RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    spacing: 10
                    Text {
                        text: (index + 1) + "."
                        color: Theme.accent
                        font.pixelSize: 13
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: modelData
                        color: Theme.textMuted
                        font.pixelSize: 12
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.bottomMargin: Theme.dialogInner
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: 11
                text: qsTr("None of this is required right now — the indicators in the top bar "
                           + "always say what is missing.")
            }
        }

        // ── 3. Remote Play
        ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: Theme.dialogInner
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: 13
                text: qsTr("For Remote Play, the console has to authorise this PC.")
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: 12
                text: qsTr("You need the PSN Account ID — the 64-bit number of the account that "
                           + "uses the console. Paste it however you have it: hexadecimal, decimal "
                           + "or base64. The conversion is done here. You can leave it blank and "
                           + "fill it in later.")
            }
            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.bottomMargin: Theme.dialogInner
                columns: 2
                columnSpacing: 12
                rowSpacing: 8
                Text {
                    text: qsTr("Account ID (PSN)")
                    color: Theme.textMuted
                    font.pixelSize: 12
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 8
                }
                AccountIdField {
                    id: accountWizardField
                    Layout.fillWidth: true
                }
            }
        }
    }
}
