// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The window's central area: the Remote Play video, and what can be done
// before there is any (register the PC on the console, wake it, connect).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    // The console accepted a registration made from here.
    signal registeredHere()

    // Here and not in the card: choosing a console rebuilds the list of
    // cards, and a handler whose card has just been destroyed stops halfway,
    // so the connection would only start on a second click.
    function chooseAndConnect(address) {
        app.selectConsole(address)
        root.connectConsole()
    }
    // Every connection starts here. With two or more saved accounts, which
    // one is asked first; with one, that one; the session then uses that
    // account's own registration and PIN (never another account's).
    function connectConsole() {
        if (stream.streaming || stream.sessionState === "connecting" || stream.connectStage.length > 0)
            return
        var accounts = app.accounts
        if (accounts.length >= 2) {
            accountPicker.ask()
            return
        }
        if (accounts.length === 1)
            stream.useAccount(accounts[0].accountId)
        stream.connectOneClick()
    }
    // Whether a saved console answers on FTP: for the one in use, what the
    // service check says; for the others, the last probeFtp.
    function ftpOk(entry) {
        return entry.active ? app.ftpState === "available"
                            : app.ftpReachable[entry.address] === true
    }

    // FTP only: the console becomes the one in use (if it was not) and the
    // file browser opens on it.
    function openFtp(address) {
        app.selectConsole(address)
        app.openFiles()
    }

    // The video stage floats like everything else: margins, wide corners and
    // a glass frame. In full screen all of that goes away — there the
    // picture rules.
    readonly property bool free: typeof window !== "undefined" && window
                                  && window.streamFullscreen

    Rectangle {
        id: videoStage
        anchors.fill: parent
        anchors.leftMargin: root.free ? 0 : Theme.gutter
        anchors.topMargin: root.free ? 0 : 4
        anchors.bottomMargin: root.free ? 0 : 4
        anchors.rightMargin: root.free ? 0 : window.panelShown ? 8 : Theme.gutter
        radius: root.free ? 0 : Theme.radius
        // Black whenever there is (or will be) a picture; idle, it follows the theme.
        color: root.streaming ? "#000000" : Theme.stageIdle
        Behavior on color { ColorAnimation { duration: Theme.normal } }
        border.width: root.free ? 0 : 1
        border.color: Theme.glassEdge
        clip: true

        Behavior on anchors.leftMargin { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
        Behavior on radius { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
    }

    // Tests the object itself and not just the flag: when closing the window
    // the controller dies before the bindings, and without this the log fills
    // up with "Cannot read property of null".
    readonly property bool built: typeof stream !== "undefined" && stream !== null
    readonly property bool streaming: built && stream.streaming
    readonly property string consoleState: built ? stream.consoleState : "unknown"
    readonly property bool registered: built && stream.registered
    readonly property string sessionState: built ? stream.sessionState : "idle"

    // ───────────────────────────── video
    //
    // Loaded separately because it is the only place with "import QtMultimedia":
    // where that module does not exist, this part fails on its own and the rest
    // of the window opens anyway.
    Loader {
        id: videoLoader
        // The picture's box: the whole stage, or a centred 4:3 box in it
        // (PS1/PS2 games come stretched to 16:9).
        readonly property real stageW: videoStage.width - 2
        readonly property real stageH: videoStage.height - 2
        readonly property bool boxed: app.streamAspect === "4:3"
        width: boxed ? Math.min(stageW, stageH * 4 / 3) : stageW
        height: boxed ? Math.min(stageH, stageW * 3 / 4) : stageH
        x: videoStage.x + 1 + (stageW - width) / 2
        y: videoStage.y + 1 + (stageH - height) / 2
        visible: root.streaming
        active: root.built
        source: root.built ? "qrc:/qml/StreamVideo.qml" : ""

        onStatusChanged: {
            if (status === Loader.Error)
                console.warn("No QtMultimedia: Remote Play has nowhere to draw.")
        }
    }

    readonly property bool videoReady: videoLoader.status === Loader.Ready

    onStreamingChanged: {
        if (streaming) {
            if (videoReady)
                videoLoader.item.forceActiveFocus()
            if (built && stream.fullscreenOnConnect)
                window.setStreamFullscreen(true)
        } else {
            if (built)
                stream.releaseAllKeys()
            window.setStreamFullscreen(false)
        }
    }

    // ───────────────────────────── the consoles, when there is no picture
    //
    // The saved consoles side by side from the top left, and the card to add
    // one more; they wrap onto more rows when the window is narrow. The one
    // in use is the usual one; the others show their state and a click
    // switches to them. Below, only the explanation the state calls for (the
    // registration PIN, the reason for a failure, the game running).
    Flickable {
        id: cardScroll
        anchors.fill: videoStage
        anchors.margins: 10
        anchors.bottomMargin: helpRow.visible ? helpRow.height + 26 : 10
        visible: !root.streaming
        clip: true
        contentWidth: width
        contentHeight: consoleRow.height + 2 * consoleRow.y
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        // Centred, as many to a row as fit; the room around them keeps the
        // glow and the lift on hover inside the clip.
        Flow {
            id: consoleRow
            readonly property int cell: 256 + spacing
            readonly property int fits: Math.max(1, Math.floor((cardScroll.width - 28 + spacing) / cell))
            readonly property int count: app.consoles.length + 1
            x: Math.round((cardScroll.width - width) / 2)
            y: 14
            width: Math.min(fits, count) * cell - spacing
            spacing: 16

            Repeater {
                model: app.consoles

                ConsoleCard {
                    readonly property var other: root.built && !modelData.active
                                                 ? stream.consoleStates[modelData.address] : undefined
                    current: modelData.active
                    ftpAvailable: root.ftpOk(modelData)
                    jailbreak: app.jailbreaks[modelData.address] || ""
                    startMode: modelData.startMode
                    available: root.built
                    address: modelData.address
                    name: modelData.active && root.built && stream.consoleName.length > 0
                          ? stream.consoleName : modelData.name
                    status: modelData.active ? root.consoleState
                          : (other ? other.state : "unknown")
                    registered: modelData.active ? root.registered : (other ? other.registered : false)
                    // What the console said just now, if it answered; otherwise,
                    // what was stored the last time it answered.
                    kind: {
                        var answered = modelData.active
                            ? (root.consoleState === "ready" || root.consoleState === "standby")
                            : (other !== undefined && other.state !== "offline"
                               && other.state !== "unknown")
                        if (!answered)
                            return modelData.type
                        var ps5 = modelData.active ? stream.consolePs5 : other.ps5
                        return ps5 ? "ps5" : "ps4"
                    }
                    connecting: modelData.active && root.sessionState === "connecting"
                    searching: modelData.active && root.built && stream.searching
                    stage: modelData.active && root.built ? stream.connectStage : ""
                    onConnect: root.connectConsole()
                    onCancel: {
                        if (stream.connectStage.length > 0)
                            stream.cancelOneClick()
                        else
                            stream.stopStream()
                    }
                    onEdit: registerDialog.open()
                    // Another console: it becomes the console in use and connects
                    // right away, in the same click (see chooseAndConnect).
                    onChoose: root.chooseAndConnect(modelData.address)
                    onRemove: app.removeConsole(modelData.address)
                    onOpenFtp: root.openFtp(modelData.address)
                    spotlight: root.spotlightAddress.length > 0
                               && root.spotlightAddress === modelData.address
                }
            }

            AddConsoleCard {
                onAdd: addConsoleDialog.open()
            }
        }
    }

    Row {
        id: helpRow
        anchors.left: videoStage.left
        anchors.bottom: videoStage.bottom
        anchors.leftMargin: 26
        anchors.bottomMargin: 22
        spacing: 12
        visible: !root.streaming && helpText.text.length > 0

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: "info"
            size: 20
            color: Theme.onIdleStageMuted
        }
        Text {
            id: helpText
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, videoStage.width - 100)
            wrapMode: Text.WordWrap
            color: Theme.onIdleStageMuted
            font.pixelSize: 12
            lineHeight: 1.15
            text: {
                if (!root.built)
                    return qsTr("This package was built without chiaki-ng. Everything else — "
                                + "installing pkg files and FTP — still works.")
                if (root.sessionState === "failed" && stream.sessionDetail.length > 0)
                    return stream.sessionDetail
                if (root.consoleState === "offline")
                    return qsTr("Check the IP in the settings, and that the console is on the "
                                + "same network.")
                if (!root.registered && root.consoleState !== "unknown")
                    return stream.consolePs5
                        ? qsTr("On the PS5: Settings → System → Remote Play → Link Device. An "
                               + "8-digit PIN appears.")
                        : qsTr("On the console: Settings → Remote Play Connection Settings → Add "
                               + "Device. An 8-digit PIN appears.")
                if (stream.runningApp.length > 0)
                    return qsTr("Running: %1").arg(stream.runningApp)
                return ""
            }
        }
    }

    // ───────────────────────────── bar during the stream
    //
    // Floats over the top of the picture, in glass, and fades in when the
    // mouse moves instead of jumping onto the screen. In full screen it stays
    // out of the picture and only comes back when the mouse nears the top.
    Rectangle {
        id: streamToolbar
        anchors.top: videoStage.top
        anchors.left: videoStage.left
        anchors.right: videoStage.right
        anchors.margins: 14
        readonly property bool wanted: root.streaming
            && (toolbarHover.hovered
                || (window.streamFullscreen
                    ? streamBar.containsMouse && streamBar.mouseY < 110
                    : streamBar.containsMouse || streamHover.hovered))
        property bool shown: false
        onWantedChanged: {
            if (wanted) {
                hideDelay.stop()
                shown = true
            } else if (!root.streaming) {
                hideDelay.stop()
                shown = false
            } else {
                hideDelay.restart()
            }
        }
        // A short grace, so the bar does not flicker when the mouse leaves
        // it for a moment.
        Timer {
            id: hideDelay
            interval: 800
            onTriggered: streamToolbar.shown = false
        }
        HoverHandler { id: toolbarHover }
        visible: opacity > 0.01
        opacity: shown ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
        color: Theme.hudFill
        radius: 16
        border.width: 1
        border.color: Theme.hudEdge
        implicitHeight: 52

        // What is really arriving: the picture size and the counted fps.
        // Not what was asked in the settings — the console may send less
        // without saying.
        Text {
            id: frameInfo
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            text: {
                if (!root.built || stream.frameWidth <= 0)
                    return ""
                var message = qsTr("%1×%2").arg(stream.frameWidth).arg(stream.frameHeight)
                if (stream.measuredFps > 0)
                    message += qsTr(" · %1 fps").arg(stream.measuredFps)
                return message
            }
            color: Theme.onStage
            font.pixelSize: 14
            font.weight: Font.Medium
            HoverHandler { id: frameHover }
            ToolTip.visible: frameHover.hovered && text.length > 0
            ToolTip.text: root.built && stream.hardwareDecoder
                          ? qsTr("graphics card") : qsTr("processor")
        }

        RowLayout {
            id: streamRow
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            // The controls: the controller (green when one is connected) and the
            // keyboard, in one button that opens the key map.
            HudButton {
                iconName: "gamepad"
                secondIconName: "keyboard"
                tone: root.built && stream.gamepadName.length > 0 ? Theme.ok : Theme.onStage
                tip: root.built && stream.gamepadName.length > 0
                     ? qsTr("Controller: %1. Click for the keyboard map.").arg(stream.gamepadName)
                     : qsTr("No controller connected. Click for the keyboard map.")
                onClicked: keysDialog.open()
            }

            // Sound: a pill with its state, and a click mutes it.
            Rectangle {
                id: soundPill
                readonly property string audioStatus: root.built ? stream.audioState : "stopped"
                readonly property bool broken: audioStatus === "error" || audioStatus === "no-device"
                implicitHeight: 36
                implicitWidth: soundRow.implicitWidth + 24
                radius: 10
                color: soundArea.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06)
                border.width: 1
                border.color: Theme.hudEdge
                Row {
                    id: soundRow
                    anchors.centerIn: parent
                    spacing: 8
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8; radius: 4
                        color: soundPill.broken ? Theme.error
                             : root.built && stream.muted ? Theme.onStageMuted : Theme.ok
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: !root.built ? qsTr("Sound")
                             : soundPill.broken ? qsTr("No sound")
                             : stream.muted ? qsTr("Sound: off")
                             : qsTr("Sound: on")
                        color: Theme.onStage
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                }
                MouseArea {
                    id: soundArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: stream.muted = !stream.muted
                }
                ToolTip.visible: soundArea.containsMouse
                ToolTip.text: audioStatus === "no-device"
                        ? qsTr("This PC has no active sound output.")
                    : audioStatus === "error"
                        ? qsTr("The sound card refused the stream — see Ctrl+L.")
                    : audioStatus === "playing"
                        ? qsTr("Coming out of %1").arg(stream.audioDevice)
                        : qsTr("No sound has arrived from the console yet.")
            }

            // The microphone must be visible. While it is capturing, the button
            // stays lit — nobody can be heard without noticing.
            HudButton {
                readonly property string micStatus: root.built ? stream.microphoneState : "off"
                iconName: micStatus === "muted" ? "mic-off" : "mic"
                lit: micStatus === "talking"
                tip: micStatus === "off"
                    ? qsTr("Send your microphone to the console")
                    : qsTr("Capturing from %1. Click to mute, or right-click to turn it off.").arg(stream.microphoneDevice)
                onClicked: {
                    if (micStatus === "off")
                        stream.setMicrophoneEnabled(true)
                    else
                        stream.setMicrophoneMuted(micStatus === "talking")
                }
                // Right click turns it off entirely, instead of just muting.
                onRightClicked: stream.setMicrophoneEnabled(false)
            }
            // The picture's shape: as sent (16:9), 4:3 for PS1/PS2 games, or
            // stretched to the window. Each click moves to the next.
            Rectangle {
                id: aspectPill
                readonly property var modes: ["fit", "4:3", "fill"]
                implicitHeight: 36
                implicitWidth: aspectRow.implicitWidth + 24
                radius: 10
                color: aspectArea.pressed ? Qt.rgba(1, 1, 1, 0.18)
                     : aspectArea.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06)
                border.width: 1
                border.color: Theme.hudEdge
                Row {
                    id: aspectRow
                    anchors.centerIn: parent
                    spacing: 7
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "ratio"
                        size: 16
                        color: Theme.onStage
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: app.streamAspect === "4:3" ? "4:3"
                            : app.streamAspect === "fill" ? qsTr("Fill") : "16:9"
                        color: Theme.onStage
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                }
                MouseArea {
                    id: aspectArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        var i = aspectPill.modes.indexOf(app.streamAspect)
                        app.streamAspect = aspectPill.modes[(i + 1) % aspectPill.modes.length]
                    }
                }
                ToolTip.visible: aspectArea.containsMouse
                ToolTip.text: qsTr("Picture shape: 16:9 as the console sends it, 4:3 for PS1/PS2 games, or Fill to stretch it to the window. Click to change.")
            }
            HudButton {
                iconName: window.streamFullscreen ? "minimize" : "maximize"
                tip: window.streamFullscreen ? qsTr("Leave full screen") : qsTr("Full screen")
                onClicked: window.setStreamFullscreen(!window.streamFullscreen)
            }
            Item { implicitWidth: 4 }
            StyledButton {
                text: qsTr("End the session")
                danger: true
                solid: true
                implicitHeight: 36
                onClicked: stream.stopStream()
            }
        }
    }

    HoverHandler { id: streamHover }
    MouseArea { id: streamBar; anchors.fill: videoStage; hoverEnabled: true; acceptedButtons: Qt.NoButton }

    // Keyboard map, because nobody guesses that V is the triangle.
    //
    // NOTE: a Dialog without its own "background" uses the Basic style's,
    // which is white, and the theme's text on top is light. So it has its own
    // background, like every dialog (scripts/check-qml.py checks this).
    Dialog {
        id: keysDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 60 : 780, 780)
        height: Math.min(parent ? parent.height - 60 : 680, 680)
        modal: true
        padding: 0
        // While waiting for a new key, Esc cancels the choice and does not
        // close the window.
        closePolicy: keyboardMap.chosen.length > 0 || keyboardMap.padChosen.length > 0
                     ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        // Open, the window reads the controller (even without a session) and
        // the keyboard, so what is pressed lights up in the drawings.
        onOpened: {
            if (root.built)
                stream.setInputPreview(true)
            keyboardMap.forceActiveFocus()
        }
        onClosed: {
            keyboardMap.editing = false
            keyboardMap.pressedKeys = ({})
            if (root.built)
                stream.setInputPreview(false)
        }

        Overlay.modal: Rectangle { color: Theme.scrim }

        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }

        header: DialogHeader {
            title: qsTr("Keyboard map")
            dialog: keysDialog
        }

        footer: Rectangle {
            implicitHeight: Theme.dialogFooter
            color: "transparent"
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 10
                StyledButton {
                    visible: root.built
                    text: keyboardMap.editing ? qsTr("Done") : qsTr("Change keys")
                    primary: keyboardMap.editing
                    minimumWidth: 130
                    onClicked: keyboardMap.editing = !keyboardMap.editing
                }
                StyledButton {
                    visible: root.built && keyboardMap.editing
                    text: qsTr("Reset")
                    minimumWidth: 100
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Go back to the default keys and controller buttons")
                    onClicked: {
                        keyboardMap.chosen = ""
                        keyboardMap.padChosen = ""
                        stream.resetKeyBindings()
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }

        contentItem: ScrollView {
            id: keysScroll
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth
            // Told outright, so the end is always in reach (see SettingsPage).
            contentHeight: keysColumn.implicitHeight
            ScrollBar.vertical.policy: contentHeight > height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded

            ColumnLayout {
                id: keysColumn
                width: keysScroll.availableWidth - Theme.dialogMargin * 2
                x: Theme.dialogMargin
                spacing: 18

                Item { Layout.preferredHeight: Theme.dialogInner - 18 }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    text: {
                        if (!root.built)
                            return ""
                        if (stream.gamepadName.length > 0)
                            return qsTr("Controller connected: %1. The keyboard works too:")
                                .arg(stream.gamepadName)
                        return qsTr("No controller connected — plug one in over USB and it is "
                                    + "picked up on its own. Meanwhile, the keyboard:")
                    }
                }

                // The keyboard above; below it the controller and, beside it,
                // what the key does: the eye goes down from the key to the
                // button and across to the text, and nothing hides under the
                // dialog's edge.
                KeyboardMap {
                    id: keyboardMap
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Math.min(parent.width, implicitWidth)
                    Layout.preferredHeight: implicitHeight
                }

                Item {
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: sketch.width + 28 + explanation.width
                    implicitHeight: sketch.height

                    ControllerSketch {
                        id: sketch
                        width: 280
                        height: 211
                        highlight: keyboardMap.highlight
                        lit: keyboardMap.litTargets
                        // With a controller connected, its buttons can be moved
                        // by clicking one here and pressing the new one on it.
                        clickable: keyboardMap.editing && root.built && stream.gamepadName.length > 0
                        onActionClicked: (action) => keyboardMap.choosePad(action)
                    }

                    Rectangle {
                        id: explanation
                        x: sketch.width + 28
                        anchors.verticalCenter: parent.verticalCenter
                        width: 340
                        // Fixed height for a few lines: the box must not jump in
                        // size every time the mouse moves from key to key.
                        height: 96
                        radius: Theme.radiusSmall
                        readonly property bool full: keyboardMap.message.length > 0
                        color: full ? Theme.accentFill
                             : Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b,
                                       Theme.light ? 1.0 : 0.6)
                        border.width: 1
                        border.color: full ? Theme.accent
                                    : Theme.light ? Qt.rgba(0, 0, 0, 0.12) : Theme.glassEdge
                        Behavior on color { ColorAnimation { duration: Theme.fast } }

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WordWrap
                            color: parent.full ? Theme.text : Theme.textSecondary
                            font.pixelSize: parent.full ? 14 : 12
                            font.bold: parent.full
                            text: parent.full ? keyboardMap.message
                                : keyboardMap.editing
                                  ? (root.built && stream.gamepadName.length > 0
                                     ? qsTr("Keyboard: click a key, then press the new one. Controller: "
                                            + "click a button on the drawing, then press it on your "
                                            + "controller. Taken already? The two swap.")
                                     : qsTr("Click the key you want to change, then press the new key. "
                                            + "If it already does something, the two swap."))
                                  : qsTr("Press a key or a controller button and both light up. Hover "
                                         + "over a key to see which button it presses.")
                        }
                    }
                }

                Text {
                    visible: keyboardMap.padChanges.length > 0
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("Controller buttons changed: %1").arg(keyboardMap.padChanges)
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }

                Item { Layout.preferredHeight: 4 }
            }
        }
    }

    // Screenshots only.
    Timer {
        running: typeof demoKeys !== "undefined" && demoKeys
        interval: 1500
        // As if the mouse were over P: the screenshot shows the link between
        // the key and the lit button on the controller.
        onTriggered: {
            keysDialog.open()
            keyboardMap.hoverHighlight = "ps"
            keyboardMap.description = "P — " + keyboardMap.actions["ps"].name
        }
    }

    // The other consoles in the list: ask how they are from time to time,
    // while there is no session (the one in use is already watched by the stream).
    Timer {
        running: root.built && !root.streaming && app.consoles.length > 1
        interval: 8000
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            var others = []
            var items = app.consoles
            for (var i = 0; i < items.length; ++i)
                if (!items[i].active)
                    others.push(items[i].address)
            stream.probeConsoles(others)
        }
    }

    // Every console's FTP, from time to time: the other consoles' cards
    // learn whether to offer it (the one in use is checked by the
    // services), and each card which enabler its console runs.
    Timer {
        running: !root.streaming && app.consoles.length > 0
        interval: 10000
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            var all = []
            var items = app.consoles
            for (var i = 0; i < items.length; ++i)
                all.push(items[i].address)
            app.probeFtp(all)
        }
    }

    AddConsoleDialog { id: addConsoleDialog }

    // Each console that answers gets its type stored (PS4 or PS5), so
    // the card shows it even when the console is off.
    Connections {
        target: root.built ? stream : null
        function onConsoleChanged() {
            if (root.consoleState === "ready" || root.consoleState === "standby")
                app.rememberConsoleType(app.consoleAddress, stream.consolePs5, stream.hostId)
        }
        // One-click connect found the console unregistered: registration is
        // opened instead of a message telling you to open it.
        function onRegistrationNeeded() { registerDialog.open() }
        // Registered: nothing more to do in the dialog. Back to the cards,
        // with the console just registered lit for a moment.
        function onRegistrationSucceeded() {
            registerDialog.close()
            root.spotlightAddress = app.consoleAddress
            spotlightTimer.restart()
            root.registeredHere()
        }
        function onConsoleStatesChanged() {
            var stateList = stream.consoleStates
            for (var address in stateList) {
                var e = stateList[address]
                if (e.state === "ready" || e.state === "standby")
                    app.rememberConsoleType(address, e.ps5, e.hostId || "")
            }
        }
    }

    // ───────────────────────────── registration
    StreamRegisterDialog { id: registerDialog }

    AccountPickerDialog {
        id: accountPicker
        onChosen: function (accountId) {
            stream.useAccount(accountId)
            stream.connectOneClick()
        }
    }

    // The card the eye should go to after registering.
    property string spotlightAddress: ""
    Timer {
        id: spotlightTimer
        interval: 4000
        onTriggered: root.spotlightAddress = ""
    }

    // Screenshots only.
    Timer {
        running: typeof demoRegister !== "undefined" && demoRegister
        interval: 1500
        onTriggered: registerDialog.open()
    }

    // The PIN the console asks for to sign in to the account — not the
    // registration one. It has its own background for the same reason as the
    // key map: in a dialog blocking the connection, unreadable is worse than ugly.
    Dialog {
        id: loginPinDialog
        property bool incorrect: false
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 420
        modal: true
        padding: 0

        Overlay.modal: Rectangle { color: Theme.scrim }

        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }

        header: DialogHeader {
            title: qsTr("Console PIN")
            dialog: loginPinDialog
        }

        footer: Rectangle {
            implicitHeight: Theme.dialogFooter
            color: "transparent"
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 10
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancel")
                    minimumWidth: 110
                    onClicked: loginPinDialog.close()
                }
                StyledButton {
                    text: qsTr("Send")
                    minimumWidth: 110
                    primary: true
                    enabled: loginPinField.text.length > 0
                    onClicked: {
                        stream.sendLoginPin(loginPinField.text)
                        loginPinDialog.close()
                    }
                }
            }
        }

        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: Theme.dialogInner
                wrapMode: Text.WordWrap
                text: loginPinDialog.incorrect
                      ? (root.built && stream.accountPin(stream.savedAccountId).length === 4
                         ? qsTr("The console refused that PIN. If the one saved for this account changed, "
                                + "correct it in Settings → Account IDs.")
                         : qsTr("That PIN was wrong. Try again."))
                      : root.built && stream.accountName.length > 0
                        ? qsTr("The console is asking for the login PIN of %1.").arg(stream.accountName)
                        : qsTr("The console is asking for the account's login PIN.")
                color: loginPinDialog.incorrect ? Theme.error : Theme.textSecondary
                font.pixelSize: 12
            }
            StyledField {
                id: loginPinField
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.bottomMargin: Theme.dialogInner
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhDigitsOnly
                onAccepted: {
                    if (text.length > 0) {
                        stream.sendLoginPin(text)
                        loginPinDialog.close()
                    }
                }
            }
        }
    }

    Connections {
        target: root.built ? stream : null
        function onLoginPinRequested(incorrect) {
            loginPinDialog.incorrect = incorrect
            loginPinField.text = ""
            loginPinDialog.open()
        }
    }
}
