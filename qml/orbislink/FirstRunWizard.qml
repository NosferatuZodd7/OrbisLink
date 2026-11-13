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
    width: 660
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
        startWizardBox.currentIndex = 0
        var items = app.consoles
        for (var i = 0; i < items.length; ++i)
            if (items[i].active && items[i].startMode === "ftp")
                startWizardBox.currentIndex = 1
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
        // The console is in the list now: its preferred start goes with it.
        app.setConsoleStartMode(addressInput.text.trim(),
                                startWizardBox.currentIndex === 1 ? "ftp" : "remoteplay")
    }

    // A nearly opaque modal: with the panels' transparency, what is behind
    // would show through the box, and a box asking for a decision must not
    // be a window.
    Overlay.modal: Rectangle { color: Theme.scrim }

    // A large sheet of glass floating over the window: nearly opaque, lit
    // along its upper edge, with a sheen from the top and a soft shadow.
    background: Plate {
        level: "secondary"
        radius: Theme.radiusDialog
        color: Theme.dialogFill
        border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Theme.glassEdge
    }

    // It fades in and settles, without bouncing.
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
        NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast; easing.type: Theme.easeOut }
    }

    // The names of the three steps, beside their marks in the progress.
    readonly property var stepNames: [qsTr("The console"), qsTr("On the console"), qsTr("Remote Play")]

    header: Item {
        implicitHeight: headColumn.implicitHeight + 46

        ColumnLayout {
            id: headColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 24
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 20

            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                Image {
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    source: "qrc:/icons/mark.png"
                    sourceSize.width: 80
                    sourceSize.height: 80
                    smooth: true
                    mipmap: true
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Eyebrow { text: qsTr("First run") }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Welcome to OrbisLink")
                        color: Theme.ink
                        font.pixelSize: 24
                        font.family: Theme.fontDisplay
                        font.weight: Font.Bold
                        font.letterSpacing: -0.3
                        elide: Text.ElideRight
                    }
                }
            }

            // The way through: a glass disc for each step, joined by a
            // hairline. The open step's disc is the bright glass pill, the
            // steps done show a check, those ahead are faint.
            RowLayout {
                Layout.fillWidth: true
                spacing: 0
                Repeater {
                    model: wizard.steps
                    delegate: RowLayout {
                        id: stepMark
                        required property int index
                        readonly property bool done: index < wizard.step
                        readonly property bool current: index === wizard.step
                        Layout.fillWidth: index < wizard.steps - 1
                        spacing: 10

                        Rectangle {
                            implicitWidth: 28
                            implicitHeight: 28
                            radius: 14
                            color: stepMark.current ? (Theme.light ? Qt.rgba(1, 1, 1, 0.92) : Qt.rgba(1, 1, 1, 0.16))
                                 : stepMark.done ? Theme.controlFill
                                 : "transparent"
                            border.width: 1
                            border.color: stepMark.current ? (Theme.light ? Qt.rgba(1, 1, 1, 1)
                                                                          : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.14))
                                        : stepMark.done ? (Theme.light ? Qt.rgba(1, 1, 1, 0.9) : Theme.glassEdge)
                                        : Theme.rule
                            Behavior on color { ColorAnimation { duration: Theme.normal } }
                            // The light on the lit disc.
                            Rectangle {
                                visible: stepMark.current
                                anchors.fill: parent
                                anchors.margins: 1
                                radius: width / 2
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.6 : 0.14) }
                                    GradientStop { position: 0.6; color: Qt.rgba(1, 1, 1, 0) }
                                }
                            }
                            // The current step glows faintly in the accent.
                            Rectangle {
                                visible: stepMark.current
                                anchors.fill: parent
                                anchors.margins: -3
                                radius: width / 2
                                color: "transparent"
                                border.width: 2
                                border.color: Theme.alpha(Theme.accent, 0.35)
                            }
                            Icon {
                                anchors.centerIn: parent
                                visible: stepMark.done
                                name: "check"
                                size: 14
                                color: Theme.ink
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: !stepMark.done
                                text: stepMark.index + 1
                                color: stepMark.current ? Theme.ink : Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                font.weight: Font.Bold
                            }
                        }
                        Text {
                            text: wizard.stepNames[stepMark.index]
                            color: stepMark.current ? Theme.ink : stepMark.done ? Theme.text : Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            font.weight: stepMark.current ? Font.Bold : Font.Medium
                        }
                        Rectangle {
                            visible: stepMark.index < wizard.steps - 1
                            Layout.fillWidth: true
                            Layout.leftMargin: 4
                            Layout.rightMargin: 14
                            Layout.preferredHeight: 1
                            color: stepMark.done ? Theme.alpha(Theme.ink, 0.35) : Theme.rule
                            Behavior on color { ColorAnimation { duration: Theme.normal } }
                        }
                    }
                }
            }
        }

        EngravedRule {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            star: false
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
                text: qsTr("Skip")
                onClicked: { wizard.save(); wizard.close() }
            }
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Back")
                iconName: "chevron-left"
                enabled: wizard.step > 0
                onClicked: wizard.step--
            }
            StyledButton {
                text: wizard.step === wizard.steps - 1 ? qsTr("Start") : qsTr("Next")
                minimumWidth: 120
                primary: true
                onClicked: wizard.next()
            }
        }
    }

    // The head of a step: which one it is and, in bold, what it is about.
    component StepHead: ColumnLayout {
        id: stepHead
        property int number: 1
        property string title: ""
        Layout.fillWidth: true
        Layout.leftMargin: Theme.dialogMargin
        Layout.rightMargin: Theme.dialogMargin
        Layout.topMargin: 20
        spacing: 4
        Eyebrow { text: qsTr("Step %1 of %2").arg(stepHead.number).arg(wizard.steps) }
        Text {
            Layout.fillWidth: true
            text: stepHead.title
            color: Theme.ink
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontHeading
            font.weight: Font.Bold
            font.letterSpacing: -0.2
            wrapMode: Text.WordWrap
        }
    }

    // A label in the left column of a step's form.
    component FormLabel: Text {
        color: Theme.text
        font.pixelSize: 13
        font.weight: Font.Medium
        Layout.preferredWidth: 130
    }

    contentItem: StackLayout {
        currentIndex: wizard.step

        // ── 1. the console
        ColumnLayout {
            spacing: 12
            StepHead {
                number: 1
                title: qsTr("First of all, where the console is.")
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 13
                text: qsTr("The IP is on the console under Settings → Network → View Connection "
                           + "Status. It has to be on the same network as this PC.")
            }
            Plate {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: 4
                implicitHeight: formGrid.implicitHeight + probeLine.implicitHeight + 52

                GridLayout {
                    id: formGrid
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 18
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 10
                    FormLabel { text: qsTr("IP address") }
                    StyledField {
                        id: addressInput
                        Layout.fillWidth: true
                        font.family: Theme.fontMono
                        placeholderText: "192.168.1.42"
                        onTextChanged: wizard.scheduleCheck()
                    }
                    FormLabel { text: qsTr("Name") }
                    StyledField {
                        id: consoleNameField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Living room PS4")
                    }
                    // What a click on the console's card starts.
                    FormLabel { text: qsTr("Start with") }
                    StyledCombo {
                        id: startWizardBox
                        Layout.fillWidth: true
                        // Only once the check above found FTP answering.
                        enabled: wizard.probeFtpOk
                        model: [qsTr("Remote Play (with FTP and the installer)"), qsTr("FTP only")]
                        onEnabledChanged: if (!enabled) currentIndex = 0
                    }
                }

                // The answer of the automatic check, at the foot of the plate.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: formGrid.bottom
                    anchors.topMargin: 14
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    height: 1
                    color: Theme.glassEdge
                }
                RowLayout {
                    id: probeLine
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 18
                    anchors.bottomMargin: 14
                    spacing: 10
                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        color: wizard.probeColor
                        Layout.alignment: Qt.AlignVCenter
                        SequentialAnimation on opacity {
                            running: wizard.probeState === "checking"
                            loops: Animation.Infinite
                            alwaysRunToEnd: true
                            NumberAnimation { to: 0.25; duration: 480; easing.type: Easing.InOutQuad }
                            NumberAnimation { to: 1.0; duration: 480; easing.type: Easing.InOutQuad }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: wizard.probeText
                        color: wizard.probeState === "idle" ? Theme.textSecondary
                             : Theme.light ? Qt.darker(wizard.probeColor, 1.3) : wizard.probeColor
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }
            Item { Layout.preferredHeight: 4 }
        }

        // ── 2. what has to be enabled on the console
        ColumnLayout {
            spacing: 12
            StepHead {
                number: 2
                title: qsTr("On the console, three things:")
            }
            Plate {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: 4
                implicitHeight: thingsColumn.implicitHeight + 8

                ColumnLayout {
                    id: thingsColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 4
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 0
                    Repeater {
                        model: [
                            qsTr("GoldHEN loaded — it is what brings the FTP server (port 2121)."),
                            qsTr("Remote Package Installer open, to install .pkg files from here."),
                            qsTr("Remote Play enabled under Settings → Remote Play Connection Settings.")
                        ]
                        delegate: Item {
                            id: thing
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            implicitHeight: thingRow.implicitHeight + 28
                            Rectangle {
                                visible: thing.index > 0
                                width: parent.width
                                height: 1
                                color: Theme.glassEdge
                            }
                            RowLayout {
                                id: thingRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 14
                                // Its number, on a small disc of glass.
                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    implicitWidth: 26
                                    implicitHeight: 26
                                    radius: 13
                                    color: Theme.controlFill
                                    border.width: 1
                                    border.color: Theme.light ? Qt.rgba(1, 1, 1, 0.9) : Theme.glassEdge
                                    Text {
                                        anchors.centerIn: parent
                                        text: thing.index + 1
                                        color: Theme.ink
                                        font.pixelSize: Theme.fontSmall
                                        font.weight: Font.Bold
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: thing.modelData
                                    color: Theme.text
                                    font.pixelSize: 13
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin + 4
                Layout.rightMargin: Theme.dialogMargin
                spacing: 8
                Icon {
                    Layout.alignment: Qt.AlignTop
                    name: "info"
                    size: 15
                    color: Theme.textSecondary
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    text: qsTr("None of this is required right now — the indicators in the navigation "
                               + "always say what is missing.")
                }
            }
            Item { Layout.preferredHeight: 4 }
        }

        // ── 3. Remote Play
        ColumnLayout {
            spacing: 12
            StepHead {
                number: 3
                title: qsTr("For Remote Play, the console has to authorise this PC.")
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 13
                text: qsTr("You need the PSN Account ID — the 64-bit number of the account that "
                           + "uses the console. Paste it however you have it: hexadecimal, decimal "
                           + "or base64. The conversion is done here. You can leave it blank and "
                           + "fill it in later.")
            }
            Plate {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: 4
                implicitHeight: accountGrid.implicitHeight + 36

                GridLayout {
                    id: accountGrid
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 18
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 8
                    FormLabel {
                        text: qsTr("Account ID (PSN)")
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 10
                    }
                    AccountIdField {
                        id: accountWizardField
                        Layout.fillWidth: true
                    }
                }
            }
            Item { Layout.preferredHeight: 4 }
        }
    }
}
