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

    // The video stage floats like everything else: margins, wide corners and
    // a glass frame. In full screen all of that goes away — there the
    // picture rules.
    readonly property bool solto: typeof window !== "undefined" && window
                                  && window.streamFullscreen

    Rectangle {
        id: palco
        anchors.fill: parent
        anchors.leftMargin: root.solto ? 0 : Theme.gutter
        anchors.topMargin: root.solto ? 0 : 6
        anchors.bottomMargin: root.solto ? 0 : Theme.gutter
        anchors.rightMargin: root.solto ? 0 : 6
        radius: root.solto ? 0 : Theme.radius
        // Black whenever there is (or will be) a picture; idle, it follows the theme.
        color: root.streaming ? "#000000" : Theme.stageIdle
        Behavior on color { ColorAnimation { duration: Theme.normal } }
        border.width: root.solto ? 0 : 1
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
        anchors.fill: palco
        anchors.margins: 1
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

    // ───────────────────────────── background, when there is no picture
    Image {
        anchors.fill: palco
        anchors.margins: 1
        source: Theme.stageBackdrop
        fillMode: Image.PreserveAspectFit
        opacity: root.streaming ? 0.0 : Theme.stageBackdropOpacity
        visible: opacity > 0
        asynchronous: true
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }

    Canvas {
        id: grelha
        anchors.fill: palco
        anchors.margins: 1
        visible: !root.streaming
        opacity: 0.25
        // The Canvas does not repaint by itself when the colour changes.
        readonly property color cor: Theme.stageGrid
        onCorChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = cor
            ctx.lineWidth = 1
            for (var x = 0; x < width; x += 40) {
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
            }
            for (var y = 0; y < height; y += 40) {
                ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
            }
        }
    }

    // The console on a card in the centre: one click connects, wakes,
    // registers or searches, depending on the state. Below, only the
    // explanation the state calls for (the registration PIN, the reason for a failure, the game running).
    Column {
        anchors.centerIn: palco
        spacing: 18
        visible: !root.streaming

        // The saved consoles side by side, and the card to add one more.
        // The one in use is the usual one; the others show their state and
        // a click switches to them.
        Row {
            id: filaConsolas
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 18
            readonly property var lista: app.consoles
            // A little below the drawing's size, so they do not dominate the
            // stage; and they shrink together, without changing proportions,
            // when they do not fit.
            readonly property real fator: Math.max(0.42, Math.min(0.72,
                (palco.width - 60 - spacing * lista.length) / (360 * lista.length + 250)))

            Repeater {
                model: filaConsolas.lista

                ConsoleCard {
                    readonly property var outra: root.built && !modelData.active
                                                 ? stream.consoleStates[modelData.address] : undefined
                    fator: filaConsolas.fator
                    ativa: modelData.active
                    disponivel: root.built
                    endereco: modelData.address
                    nome: modelData.active && root.built && stream.consoleName.length > 0
                          ? stream.consoleName : modelData.name
                    estado: modelData.active ? root.consoleState
                          : (outra ? outra.state : "unknown")
                    registada: modelData.active ? root.registered : (outra ? outra.registered : false)
                    // What the console said just now, if it answered; otherwise,
                    // what was stored the last time it answered.
                    tipo: {
                        var respondeu = modelData.active
                            ? (root.consoleState === "ready" || root.consoleState === "standby")
                            : (outra !== undefined && outra.state !== "offline"
                               && outra.state !== "unknown")
                        if (!respondeu)
                            return modelData.type
                        var ps5 = modelData.active ? stream.consolePs5 : outra.ps5
                        return ps5 ? "ps5" : "ps4"
                    }
                    aLigar: modelData.active && root.sessionState === "connecting"
                    aProcurar: modelData.active && root.built && stream.searching
                    etapa: modelData.active && root.built ? stream.connectStage : ""
                    onLigar: stream.connectOneClick()
                    onCancelar: {
                        if (stream.connectStage.length > 0)
                            stream.cancelOneClick()
                        else
                            stream.stopStream()
                    }
                    onEditar: registerDialog.open()
                    // Another console: it becomes the console in use and connects
                    // right away, in the same click.
                    onEscolher: {
                        app.selectConsole(modelData.address)
                        stream.connectOneClick()
                    }
                    onRemover: app.removeConsole(modelData.address)
                }
            }

            AddConsoleCard {
                width: 250 * filaConsolas.fator
                height: 330 * filaConsolas.fator
                onAdicionar: addConsoleDialog.open()
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(palco.width - 80, 440)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: text.length > 0
            color: Theme.onIdleStageMuted
            font.pixelSize: 12
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
    // Floats over the picture, in a glass capsule, and fades in
    // instead of jumping onto the screen.
    Rectangle {
        id: barraStream
        anchors.top: palco.top
        anchors.horizontalCenter: palco.horizontalCenter
        anchors.topMargin: 16
        readonly property bool mostrar: root.streaming
                                        && (streamBar.containsMouse || streamHover.hovered)
        visible: opacity > 0.01
        opacity: mostrar ? 1.0 : 0.0
        scale: mostrar ? 1.0 : 0.96
        Behavior on opacity { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
        Behavior on scale {
            NumberAnimation { duration: Theme.normal; easing.type: Theme.easeSpring; easing.overshoot: 1.05 }
        }
        color: Qt.rgba(0.04, 0.04, 0.06, 0.72)
        radius: height / 2
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.14)
        implicitWidth: streamRow.implicitWidth + 32
        implicitHeight: 44

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.10) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        RowLayout {
            id: streamRow
            anchors.centerIn: parent
            spacing: 12
            // What is really arriving: the picture size and the counted fps.
            // Not what was asked in the settings — the console may send less
            // without saying.
            Text {
                text: {
                    if (!root.built || stream.frameWidth <= 0)
                        return ""
                    var texto = qsTr("%1×%2").arg(stream.frameWidth).arg(stream.frameHeight)
                    if (stream.measuredFps > 0)
                        texto += qsTr(" · %1 fps").arg(stream.measuredFps)
                    return texto
                }
                color: Theme.onStageMuted
                font.pixelSize: 11
            }
            Text {
                visible: root.built && stream.gamepadName.length > 0
                text: root.built ? stream.gamepadName : ""
                color: Theme.ok
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.maximumWidth: 160
            }
            StyledButton {
                readonly property string somEstado: root.built ? stream.audioState : "stopped"
                text: !root.built ? qsTr("Sound")
                     : somEstado === "error" || somEstado === "no-device" ? qsTr("No sound")
                     : stream.muted ? qsTr("Sound: off")
                     : qsTr("Sound: on")
                danger: somEstado === "error" || somEstado === "no-device"
                implicitHeight: 30
                font.pixelSize: 11
                ToolTip.visible: hovered
                ToolTip.text: somEstado === "no-device"
                        ? qsTr("This PC has no active sound output.")
                    : somEstado === "error"
                        ? qsTr("The sound card refused the stream — see Ctrl+L.")
                    : somEstado === "playing"
                        ? qsTr("Coming out of %1").arg(stream.audioDevice)
                        : qsTr("No sound has arrived from the console yet.")
                onClicked: stream.muted = !stream.muted
            }
            Text {
                text: root.built && stream.hardwareDecoder
                      ? qsTr("graphics card") : qsTr("processor")
                color: Theme.onStageMuted
                font.pixelSize: 10
            }
            // The microphone must be visible. While it is capturing, the button
            // stays lit — nobody can be heard without noticing.
            StyledButton {
                readonly property string micEstado: root.built ? stream.microphoneState
                                                               : "off"
                text: micEstado === "talking" ? qsTr("🎤 Talking")
                     : micEstado === "muted" ? qsTr("🎤 Muted")
                     : qsTr("Microphone")
                implicitHeight: 30
                font.pixelSize: 11
                danger: micEstado === "talking"
                ToolTip.visible: hovered
                ToolTip.text: micEstado === "off"
                    ? qsTr("Send your microphone to the console")
                    : qsTr("Capturing from %1. Click to mute, or right-click to turn it off.").arg(stream.microphoneDevice)
                onClicked: {
                    if (micEstado === "off")
                        stream.setMicrophoneEnabled(true)
                    else
                        stream.setMicrophoneMuted(micEstado === "talking")
                }
                // Right click turns it off entirely, instead of just muting.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: stream.setMicrophoneEnabled(false)
                }
            }
            StyledButton {
                text: window.streamFullscreen ? qsTr("Leave full screen")
                                              : qsTr("Full screen")
                implicitHeight: 30
                font.pixelSize: 11
                onClicked: window.setStreamFullscreen(!window.streamFullscreen)
            }
            StyledButton {
                text: qsTr("Keys")
                implicitHeight: 30
                font.pixelSize: 11
                onClicked: keysDialog.open()
            }
            StyledButton {
                text: qsTr("End the session")
                implicitHeight: 30
                font.pixelSize: 11
                onClicked: stream.stopStream()
            }
        }
    }

    HoverHandler { id: streamHover }
    MouseArea { id: streamBar; anchors.fill: palco; hoverEnabled: true; acceptedButtons: Qt.NoButton }

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
        closePolicy: keyboardMap.escolhida.length > 0
                     ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: keyboardMap.editando = false

        Overlay.modal: Rectangle { color: Theme.scrim }

        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radius
        }

        header: Rectangle {
            implicitHeight: Theme.dialogHeader
            color: "transparent"
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: Theme.dialogMargin
                text: qsTr("The keyboard as a controller")
                color: Theme.text
                font.pixelSize: 15
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
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 10
                StyledButton {
                    visible: root.built
                    text: keyboardMap.editando ? qsTr("Done") : qsTr("Change keys")
                    larguraMinima: 130
                    onClicked: keyboardMap.editando = !keyboardMap.editando
                }
                StyledButton {
                    visible: root.built && keyboardMap.editando
                    text: qsTr("Reset")
                    larguraMinima: 100
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Go back to the default keys")
                    onClicked: {
                        keyboardMap.escolhida = ""
                        stream.resetKeyBindings()
                    }
                }
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Close")
                    larguraMinima: 110
                    primary: true
                    onClicked: keysDialog.close()
                }
            }
        }

        contentItem: ScrollView {
            id: keysScroll
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            contentWidth: availableWidth

            ColumnLayout {
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

                // Both in the centre, one above the other, and the explanation
                // in a box below: the eye goes down from the key to the button
                // and from it to the text.
                KeyboardMap {
                    id: keyboardMap
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Math.min(parent.width, implicitWidth)
                    Layout.preferredHeight: implicitHeight
                }

                ControllerSketch {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 236
                    Layout.preferredHeight: 178
                    destaque: keyboardMap.destaque
                }

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Math.min(parent.width, 520)
                    // Fixed height for two lines: the box must not jump in size
                    // every time the mouse moves from key to key.
                    Layout.preferredHeight: 56
                    radius: Theme.radiusSmall
                    readonly property bool cheia: keyboardMap.texto.length > 0
                    color: cheia ? Theme.accentFill
                         : Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b,
                                   Theme.claro ? 1.0 : 0.6)
                    border.width: 1
                    border.color: cheia ? Theme.accent
                                : Theme.claro ? Qt.rgba(0, 0, 0, 0.12) : Theme.glassEdge
                    Behavior on color { ColorAnimation { duration: Theme.fast } }

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                        color: parent.cheia ? Theme.text : Theme.textSecondary
                        font.pixelSize: parent.cheia ? 14 : 12
                        font.bold: parent.cheia
                        text: parent.cheia ? keyboardMap.texto
                            : keyboardMap.editando
                              ? qsTr("Click the key you want to change, then press the new key. "
                                     + "If it already does something, the two swap.")
                              : qsTr("Hover over a key to see on the controller which button it "
                                     + "presses. Greyed-out keys do nothing.")
                    }
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
            keyboardMap.destaqueRato = "ps"
            keyboardMap.descricao = "P — " + keyboardMap.acoes["ps"].nome
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
            var outras = []
            var lista = app.consoles
            for (var i = 0; i < lista.length; ++i)
                if (!lista[i].active)
                    outras.push(lista[i].address)
            stream.probeConsoles(outras)
        }
    }

    AddConsoleDialog { id: addConsoleDialog }

    // Each console that answers gets its type stored (PS4 or PS5), so
    // the card shows it even when the console is off.
    Connections {
        target: root.built ? stream : null
        function onConsoleChanged() {
            if (root.consoleState === "ready" || root.consoleState === "standby")
                app.rememberConsoleType(app.consoleAddress, stream.consolePs5)
        }
        // One-click connect found the console unregistered: registration is
        // opened instead of a message telling you to open it.
        function onRegistrationNeeded() { registerDialog.open() }
                function onConsoleStatesChanged() {
            var estados = stream.consoleStates
            for (var endereco in estados) {
                var e = estados[endereco]
                if (e.state === "ready" || e.state === "standby")
                    app.rememberConsoleType(endereco, e.ps5)
            }
        }
    }

    // ───────────────────────────── registration
    StreamRegisterDialog { id: registerDialog }

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
            radius: Theme.radius
        }

        header: Rectangle {
            implicitHeight: Theme.dialogHeader
            color: "transparent"
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: Theme.dialogMargin
                text: qsTr("Console PIN")
                color: Theme.text
                font.pixelSize: 15
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
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 10
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancel")
                    larguraMinima: 110
                    onClicked: loginPinDialog.close()
                }
                StyledButton {
                    text: qsTr("Send")
                    larguraMinima: 110
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
                      ? qsTr("That PIN was wrong. Try again.")
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
