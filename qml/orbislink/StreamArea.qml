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
    // Every connection starts here, with the account shown on the card; with
    // two or more saved accounts and none chosen for this console, which one
    // is asked first. The session then uses that account's own registration
    // and PIN (never another account's).
    function connectConsole() {
        if (stream.streaming || stream.sessionState === "connecting" || stream.connectStage.length > 0)
            return
        var accounts = app.accounts
        // The account on the card goes in; the question comes only when the
        // console has none of the saved ones yet.
        var chosen = false
        for (var i = 0; i < accounts.length; ++i)
            if (accounts[i].accountId === stream.savedAccountId)
                chosen = true
        if (accounts.length >= 2 && !chosen) {
            accountPicker.ask()
            return
        }
        if (accounts.length >= 1 && !chosen)
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

    // Where things go. Without a picture this is a page like the others: it
    // draws no panel of its own and keeps the sheet's margins. With one, the
    // picture sits in a softly rounded frame; in full screen ("free") all of
    // that goes away — there the picture rules.
    readonly property bool free: typeof window !== "undefined" && window
                                  && window.streamFullscreen

    Rectangle {
        id: videoStage
        anchors.fill: parent
        anchors.leftMargin: root.free ? 0 : root.streaming ? 16 : Theme.gutter
        anchors.rightMargin: root.free ? 0 : root.streaming ? 16 : Theme.gutter
        anchors.topMargin: root.free ? 0 : root.streaming ? 14 : 26
        anchors.bottomMargin: root.free ? 0 : 12
        radius: root.free || !root.streaming ? 0 : Theme.radius
        // Black whenever there is a picture; idle, the sheet shows through.
        color: root.streaming ? "#000000" : "transparent"
        border.width: root.streaming && !root.free ? 1 : 0
        border.color: Theme.glassEdge
        clip: true

        Behavior on anchors.leftMargin { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
        Behavior on radius { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
    }

    // Tests the object itself and not just the flag: when closing the window
    // the controller dies before the bindings, and without this the log fills
    // up with "Cannot read property of null".
    readonly property bool built: typeof stream !== "undefined" && stream !== null
    // A rate in bytes per second, in KB/s or MB/s; "—" when not known.
    function rate(bytes) {
        if (bytes < 0)
            return "—"
        if (bytes < 1024 * 1024)
            return qsTr("%1 KB/s").arg((bytes / 1024).toFixed(bytes < 10 * 1024 ? 1 : 0))
        return qsTr("%1 MB/s").arg((bytes / (1024 * 1024)).toFixed(1))
    }
    // The two sessions: Remote Play's first one (stream) and a second one at
    // once on another console (stream2). Starting again with another picture
    // counts as live: the stage, its bar and the full screen stay through the
    // console's pause.
    readonly property bool has2: typeof stream2 !== "undefined" && stream2 !== null
    readonly property bool live1: built && window.live1
    readonly property bool live2: has2 && window.live2
    readonly property bool streaming: live1 || live2
    readonly property bool both: live1 && live2
    // The session the bar acts on and the keyboard goes to: the one picked
    // on screen with two, else the one there is.
    readonly property var cur: !built ? null
        : both ? (window.focusedSession === 2 ? stream2 : stream)
        : live2 ? stream2 : stream
    readonly property string consoleState: built ? stream.consoleState : "unknown"
    readonly property bool registered: built && stream.registered
    readonly property string sessionState: built ? stream.sessionState : "idle"

    // ───────────────────────────── video
    //
    // Each picture's place on the stage: the whole of it with one session;
    // with two, side by side, one over the other, or the second small in a
    // corner over the first (app.streamLayout).
    readonly property real stageX: videoStage.x + 1
    readonly property real stageY: videoStage.y + 1
    readonly property real stageW: videoStage.width - 2
    readonly property real stageH: videoStage.height - 2
    readonly property string layout: app.streamLayout
    readonly property real gap: 4
    readonly property rect rect1: {
        if (!both)
            return Qt.rect(stageX, stageY, stageW, stageH)
        if (layout === "stacked")
            return Qt.rect(stageX, stageY, stageW, (stageH - gap) / 2)
        if (layout === "pip")
            return Qt.rect(stageX, stageY, stageW, stageH)
        return Qt.rect(stageX, stageY, (stageW - gap) / 2, stageH)
    }
    readonly property rect rect2: {
        if (!both)
            return Qt.rect(stageX, stageY, stageW, stageH)
        if (layout === "stacked")
            return Qt.rect(stageX, stageY + (stageH + gap) / 2, stageW, (stageH - gap) / 2)
        if (layout === "pip") {
            var w = Math.max(220, stageW * 0.3)
            var h = w * 9 / 16
            return Qt.rect(stageX + stageW - w - 16, stageY + stageH - h - 16, w, h)
        }
        return Qt.rect(stageX + (stageW + gap) / 2, stageY, (stageW - gap) / 2, stageH)
    }

    // One picture: its box in its place (a centred 4:3 box for PS1/PS2 games),
    // the session's notes over it, and — with two — the ring of the one
    // played; a press anywhere on it picks it.
    //
    // The video is loaded apart because it is the only place with "import
    // QtMultimedia": where that module does not exist, this part fails on its
    // own and the rest of the window opens anyway.
    component Stage: Item {
        id: stage
        property var ctrl: null
        property int number: 1
        property bool live: false
        property rect place: Qt.rect(0, 0, 0, 0)
        readonly property bool picked: root.both && window.focusedSession === number
        readonly property alias loader: picture
        x: place.x
        y: place.y
        width: place.width
        height: place.height
        visible: live
        // Over the first in a corner: a frame, a shadow of black around it.
        Rectangle {
            anchors.fill: parent
            color: "#000000"
            visible: root.both
            radius: root.layout === "pip" && stage.number === 2 ? 10 : 0
            // The small one stands out from the big one behind it.
            border.width: root.layout === "pip" && stage.number === 2 ? 1 : 0
            border.color: Qt.rgba(1, 1, 1, 0.35)
        }
        Loader {
            id: picture
            readonly property bool boxed: app.streamAspect === "4:3"
            width: boxed ? Math.min(stage.width, stage.height * 4 / 3) : stage.width
            height: boxed ? Math.min(stage.height, stage.width * 3 / 4) : stage.height
            x: (stage.width - width) / 2
            y: (stage.height - height) / 2
            active: root.built && stage.ctrl !== null
            Component.onCompleted: if (stage.ctrl) setSource("qrc:/qml/StreamVideo.qml", { ctrl: stage.ctrl })
            onStatusChanged: {
                if (status === Loader.Error)
                    console.warn("No QtMultimedia: Remote Play has nowhere to draw.")
            }
        }
        // Connecting (a second session has no card for it): what it is doing,
        // and the way out.
        Column {
            anchors.centerIn: parent
            visible: stage.live && stage.ctrl !== null && !stage.ctrl.streaming && !stage.ctrl.restarting
            spacing: 12
            Spinner {
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: !stage.ctrl ? ""
                    : stage.ctrl.connectStage === "waking" ? qsTr("Waking %1…").arg(root.consoleNameOf(stage.ctrl.address))
                    : qsTr("Connecting to %1…").arg(root.consoleNameOf(stage.ctrl.address))
                color: Theme.onStage
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            StyledButton {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Cancel")
                onClicked: {
                    if (stage.ctrl.connectStage.length > 0)
                        stage.ctrl.cancelOneClick()
                    else
                        stage.ctrl.stopStream()
                }
            }
        }
        // Starting again with another picture: what is happening, on the black.
        Column {
            anchors.centerIn: parent
            visible: stage.ctrl !== null && stage.ctrl.restarting
            spacing: 12
            Spinner {
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: stage.ctrl ? stage.ctrl.sessionDetail : ""
                color: Theme.onStage
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
        }
        // The console is showing something it does not stream (YouTube, a
        // film): the picture goes dark, and this says why instead of looking
        // frozen.
        Column {
            anchors.centerIn: parent
            width: Math.min(420, parent.width - 48)
            spacing: 10
            visible: stage.live && stage.ctrl !== null && stage.ctrl.cantDisplay
            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "monitor"
                size: 34
                color: Theme.onStageMuted
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.onStage
                font.pixelSize: 15
                font.weight: Font.DemiBold
                text: qsTr("The console is not streaming this screen")
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.onStageMuted
                font.pixelSize: 12
                lineHeight: 1.2
                text: qsTr("Video apps such as YouTube or Netflix are not sent over Remote Play. "
                           + "Close the app or press the PS button for the home screen; the "
                           + "picture comes back by itself.")
            }
        }
        // With two: its console's name in a corner, and a ring on the one
        // played.
        Rectangle {
            visible: root.both
            // At the foot: the bar takes the top.
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 10
            width: nameTag.implicitWidth + 20
            height: 26
            radius: 13
            color: Qt.rgba(0, 0, 0, 0.55)
            border.width: 1
            border.color: stage.picked ? Theme.alpha(Theme.accent, 0.8) : Qt.rgba(1, 1, 1, 0.15)
            Text {
                id: nameTag
                anchors.centerIn: parent
                text: root.consoleNameOf(stage.ctrl ? stage.ctrl.address : "")
                      + (stage.picked ? "  ·  " + qsTr("played") : "")
                color: Theme.onStage
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            visible: stage.picked
            border.width: 2
            border.color: Theme.alpha(Theme.accent, 0.85)
            radius: root.layout === "pip" && stage.number === 2 ? 10 : 0
        }
        // A press picks it, without taking the press from the picture (the
        // mouse can be the touchpad): on a layer over it, which only watches
        // (a passive grab) and lets the press go on to the picture.
        Item {
            anchors.fill: parent
            PointHandler {
                id: picker
                acceptedButtons: Qt.AllButtons
                onActiveChanged: {
                    if (!active || !root.both)
                        return
                    // Under the small one in a corner, the big one does not
                    // take the press: the small one is what was pressed.
                    if (stage.number === 1 && root.layout === "pip") {
                        var p = stage2.mapFromItem(stage, picker.point.position.x, picker.point.position.y)
                        if (stage2.contains(p))
                            return
                    }
                    window.focusedSession = stage.number
                }
            }
        }
    }

    // The consoles a second session can go to: registered on this PC (a
    // registration of theirs is known), saved in the list, and not the one
    // already streaming.
    readonly property var secondCandidates: {
        if (!built || !has2)
            return []
        // Re-read when the registrations change (canConnectTo reads them).
        var regs = stream.registrations
        var held = [stream.sessionAddress, stream2.sessionAddress]
        var out = []
        for (var i = 0; i < app.consoles.length; ++i) {
            var c = app.consoles[i]
            if (held.indexOf(c.address) >= 0)
                continue
            // The same rule connecting will use: a registration of this PC
            // there, for its account or another.
            if (regs !== undefined && stream2.canConnectTo(c.address))
                out.push(c)
        }
        return out
    }
    function startSecond(address) {
        // Whichever controller is free: the second while the first streams,
        // the first once it has ended and the second goes on.
        if (!live1)
            stream.connectTo(address)
        else
            stream2.connectTo(address)
    }
    Menu {
        id: secondMenu
        topPadding: 8
        bottomPadding: 8
        background: ShelfTile.MenuGlass { implicitWidth: 300 }
        Instantiator {
            model: root.secondCandidates
            delegate: StyledMenuItem {
                required property var modelData
                text: modelData.name + (modelData.type === "ps5" ? "  ·  PS5" : modelData.type === "ps4" ? "  ·  PS4" : "")
                iconName: modelData.type === "ps5" ? "monitor" : "gamepad"
                onTriggered: root.startSecond(modelData.address)
            }
            onObjectAdded: (index, object) => secondMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => secondMenu.removeItem(object)
        }
    }
    Menu {
        id: layoutMenu
        topPadding: 8
        bottomPadding: 8
        background: ShelfTile.MenuGlass { implicitWidth: 260 }
        Repeater {
            model: [["side", "columns", qsTr("Side by side")], ["stacked", "rows", qsTr("One over the other")],
                    ["pip", "picture-in-picture", qsTr("One small over the other")]]
            delegate: StyledMenuItem {
                required property var modelData
                text: modelData[2] + (app.streamLayout === modelData[0] ? "  ✓" : "")
                iconName: modelData[1]
                onTriggered: app.streamLayout = modelData[0]
            }
        }
        MenuSeparator {}
        // The controller, the keyboard and the sound to the other picture
        // (a click on it does the same).
        StyledMenuItem {
            text: qsTr("Play the other one")
            iconName: "gamepad"
            onTriggered: window.focusedSession = window.focusedSession === 1 ? 2 : 1
        }
    }

    function consoleNameOf(address) {
        for (var i = 0; i < app.consoles.length; ++i)
            if (app.consoles[i].address === address)
                return app.consoles[i].name
        return address
    }

    Stage {
        id: stage1
        ctrl: root.built ? stream : null
        number: 1
        live: root.live1
        place: root.rect1
    }
    Stage {
        id: stage2
        // Over the first, in a corner, in picture in picture.
        z: 1
        ctrl: root.has2 ? stream2 : null
        number: 2
        live: root.live2
        place: root.rect2
    }
    // The first picture's box (the bar and the help sit relative to it).
    readonly property Item videoLoader: stage1.loader

    readonly property bool videoReady: stage1.loader.status === Loader.Ready
    // The keyboard to the picture played (a page over it was closed, or
    // another picture was picked).
    function focusPicture() {
        var stage = cur === stream2 ? stage2 : stage1
        if (streaming && stage.loader.status === Loader.Ready)
            stage.loader.item.forceActiveFocus()
    }
    readonly property var curStream: cur
    onCurStreamChanged: Qt.callLater(focusPicture)

    onStreamingChanged: {
        if (streaming) {
            Qt.callLater(focusPicture)
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
    // The app's dashboard: the page's head (what is true right now — how
    // many consoles, the one in use with its jailbreak and firmware, once
    // they said them, and what the queue is doing), then the saved consoles
    // as glass cards in a grid that reflows with the window, and the place to add
    // one more. The one in use is the usual one; the others show their state
    // and a click switches to them. At the foot, only the explanation the
    // state calls for (the registration PIN, the reason for a failure, the
    // game running).
    Flickable {
        id: cardScroll
        anchors.fill: videoStage
        anchors.bottomMargin: helpRow.visible ? helpRow.height + 16 : 0
        visible: !root.streaming
        clip: true
        contentWidth: width
        contentHeight: homeColumn.height + 12
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: homeColumn
            width: cardScroll.width
            spacing: 22

            PageHeader {
                id: overview
                width: parent.width

                readonly property string jailbreak: app.jailbreaks[app.consoleAddress] || ""
                readonly property string henVersion: app.henVersions[app.consoleAddress] || ""
                readonly property var facts: {
                    if (app.consoles.length === 0)
                        return [qsTr("Add the first console to begin.")]
                    var list = [qsTr("%n console(s)", "", app.consoles.length)]
                    if (app.consoleName.length > 0)
                        list.push(qsTr("%1 in use").arg(app.consoleName))
                    if (jailbreak.length > 0)
                        list.push(henVersion.length > 0 ? jailbreak + " " + henVersion : jailbreak)
                    if (app.consoleFirmware.length > 0)
                        list.push(qsTr("firmware %1").arg(app.consoleFirmware))
                    return list
                }

                eyebrow: qsTr("Dashboard")
                title: qsTr("Your consoles")
                subtitle: facts.join("  ·  ")

                // What the queue is doing, when it has something.
                Tag {
                    visible: app.queue.count > 0
                    tone: "accent"
                    iconName: "package"
                    text: qsTr("%n in the queue", "", app.queue.count)
                          + (app.queue.totalSpeedText.length > 0 ? "  ·  " + app.queue.totalSpeedText : "")
                }
            }

            // As many cards to a row as fit at a comfortable width; they
            // share the row out between them, up to a width past which a
            // card would only be emptier.
            Flow {
                id: consoleGrid
                readonly property int minCell: 250
                readonly property int maxCell: 400
                readonly property int cols: Math.max(1, Math.floor((width + spacing) / (minCell + spacing)))
                readonly property int cell: Math.min(maxCell,
                                                     Math.floor((width - spacing * (cols - 1)) / cols))
                // As tall as the card that needs most room (tags on two
                // lines), so the cards of a row stay alike.
                readonly property int cellHeight: {
                    var tallest = 240
                    for (var i = 0; i < children.length; ++i) {
                        var needed = children[i].neededHeight
                        if (needed !== undefined && needed > tallest)
                            tallest = needed
                    }
                    return tallest
                }
                // Room for the ring of glass around the console in use.
                x: 6
                width: parent.width - 12
                spacing: 20

                Repeater {
                    model: app.consoles

                    ConsoleCard {
                        // Remote Play's own console: the one in use, except
                        // while a session holds another — what Remote Play says
                        // (state, name, stage) is that one's.
                        readonly property bool remote: root.built && modelData.address === stream.address
                        readonly property var other: root.built && !remote
                                                     ? stream.consoleStates[modelData.address] : undefined
                        width: consoleGrid.cell
                        height: consoleGrid.cellHeight
                        current: modelData.active
                        ftpAvailable: root.ftpOk(modelData)
                        jailbreak: app.jailbreaks[modelData.address] || ""
                        startMode: modelData.startMode
                        available: root.built
                        address: modelData.address
                        name: remote && stream.consoleName.length > 0
                              ? stream.consoleName : modelData.name
                        status: remote ? root.consoleState
                              : (other ? other.state : "unknown")
                        registered: remote ? root.registered : (other ? other.registered : false)
                        // It took a direct connection without answering the search.
                        direct: remote ? stream.directReachable
                                                 : modelData.connectsDirectly === true
                        // What the console said just now, if it answered; otherwise,
                        // what was stored the last time it answered.
                        kind: {
                            var answered = remote
                                ? (root.consoleState === "ready" || root.consoleState === "standby")
                                : (other !== undefined && other.state !== "offline"
                                   && other.state !== "unknown")
                            if (!answered)
                                return modelData.type
                            var ps5 = remote ? stream.consolePs5 : other.ps5
                            return ps5 ? "ps5" : "ps4"
                        }
                        connecting: remote && root.sessionState === "connecting"
                        searching: remote && stream.searching
                        stage: remote ? stream.connectStage : ""
                        onConnect: root.connectConsole()
                        onCancel: {
                            if (stream.connectStage.length > 0)
                                stream.cancelOneClick()
                            else
                                stream.stopStream()
                        }
                        onEdit: registerDialog.open()
                        accounts: app.accounts
                        account: root.built ? stream.savedAccountId : ""
                        registeredAccounts: root.built ? stream.registeredAccounts : []
                        // The choice is this console's from now on: the next
                        // connection goes in with it, without asking.
                        onChooseAccount: function (accountId) {
                            stream.useAccount(accountId)
                            app.rememberAccountId(modelData.address, accountId)
                        }
                        onManageAccounts: {
                            settingsDialog.loadValues()
                            settingsDialog.section = 1
                            settingsDialog.open()
                        }
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
                    width: consoleGrid.cell
                    height: consoleGrid.cellHeight
                    onAdd: addConsoleDialog.open()
                }
            }
        }
    }

    // The note at the foot of the page: what the state calls for, in the
    // page's quiet voice (the pane's status line already rules it off).
    Item {
        id: helpRow
        anchors.left: videoStage.left
        anchors.right: videoStage.right
        anchors.bottom: videoStage.bottom
        height: helpLine.height + 6
        visible: !root.streaming && helpText.text.length > 0

        Row {
            id: helpLine
            y: 0
            width: parent.width
            spacing: 10

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "info"
                size: 16
                color: Theme.light ? Qt.darker(Theme.accent, 1.3) : Theme.accent
            }
            Text {
                id: helpText
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 26
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 12
                lineHeight: 1.15
                text: {
                    if (!root.built)
                        return qsTr("This package was built without chiaki-ng. Everything else — "
                                    + "installing pkg files and FTP — still works.")
                    if (root.sessionState === "failed" && stream.sessionDetail.length > 0)
                        return stream.sessionDetail
                    if (root.consoleState === "offline" && stream.directReachable && app.ftpState === "available")
                        return qsTr("This console does not answer the network search, but takes a direct "
                                    + "connection: Remote Play connects all the same.")
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
    }

    // ───────────────────────────── bar during the stream
    //
    // A pill of dark glass floating over the top of the picture, only as wide
    // as what is on it, so the game stays the thing on screen. It fades in
    // and settles when the mouse moves instead of jumping onto the screen. In
    // full screen it stays out of the picture and only comes back when the
    // mouse nears the top. The picture is always dark, so the pill is the
    // same dark glass whatever the theme.
    Rectangle {
        id: streamToolbar
        // Over both pictures.
        z: 3
        anchors.top: videoStage.top
        anchors.topMargin: 16 + (shown ? 0 : -8)
        Behavior on anchors.topMargin { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
        anchors.horizontalCenter: videoStage.horizontalCenter
        // The readout on the left only when it fits beside the buttons: in a
        // narrow window the buttons come first.
        readonly property bool infoFits: frameInfo.text.length > 0
            && 22 + frameInfo.implicitWidth + 16 + 1 + 6 + streamRow.implicitWidth + 10 <= videoStage.width - 28
        width: Math.min(videoStage.width - 28,
                        (infoFits ? 22 + frameInfo.implicitWidth + 16 + 1 + 6 : 10)
                        + streamRow.implicitWidth + 10)
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
        // Round-ended, like the buttons on it.
        radius: height / 2
        border.width: 1
        border.color: Theme.hudEdge
        implicitHeight: 54

        // Its shadow on the picture, soft and close.
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: 8
            anchors.bottomMargin: -10
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            radius: height / 2
            color: Qt.rgba(0, 0, 0, 0.28)
        }
        // The light on the glass: a sheen from the top and a lit upper edge.
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: height / 2
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.12) }
                GradientStop { position: 0.55; color: Qt.rgba(1, 1, 1, 0) }
            }
        }
        Rectangle {
            anchors.top: parent.top
            anchors.topMargin: 1
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - parent.height
            height: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0) }
                GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.42) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0) }
            }
        }
        // Between the readout and the controls.
        Rectangle {
            visible: streamToolbar.infoFits
            x: frameInfo.x + frameInfo.implicitWidth + 16
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: 22
            color: Theme.hudEdge
        }

        // What is really arriving: the picture size and the counted fps.
        // Not what was asked in the settings — the console may send less
        // without saying.
        Text {
            id: frameInfo
            visible: streamToolbar.infoFits
            anchors.left: parent.left
            anchors.leftMargin: 22
            anchors.verticalCenter: parent.verticalCenter
            // With another console in use for the pages, the one the picture
            // comes from is named first.
            // With two sessions, always: the bar acts on that one.
            readonly property string elsewhere: {
                if (!root.built || (!root.both && root.cur.address === app.consoleAddress))
                    return ""
                for (var i = 0; i < app.consoles.length; ++i)
                    if (app.consoles[i].address === root.cur.address)
                        return app.consoles[i].name
                return root.cur.address
            }
            text: {
                if (!root.built || root.cur.frameWidth <= 0)
                    return elsewhere
                var message = (elsewhere.length > 0 ? elsewhere + "  ·  " : "")
                    + qsTr("%1×%2").arg(root.cur.frameWidth).arg(root.cur.frameHeight)
                if (root.cur.measuredFps > 0)
                    message += qsTr(" · %1 fps").arg(root.cur.measuredFps)
                return message
            }
            color: Theme.onStageMuted
            font.family: Theme.fontMono
            font.pixelSize: 12
            font.weight: Font.Medium
            HoverHandler { id: frameHover }
            ToolTip.visible: frameHover.hovered && text.length > 0
            ToolTip.text: root.built && root.cur.hardwareDecoder
                          ? qsTr("graphics card") : qsTr("processor")
        }

        RowLayout {
            id: streamRow
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            // A second session at once, on another registered console.
            HudButton {
                id: addButton
                visible: !root.both && root.secondCandidates.length > 0
                iconName: "plus"
                tip: qsTr("Remote Play on a second console at the same time")
                onClicked: secondMenu.popup(addButton, 0, addButton.height + 8)
            }
            // With two: how they share the stage.
            HudButton {
                id: layoutButton
                visible: root.both
                iconName: root.layout === "stacked" ? "rows" : root.layout === "pip" ? "picture-in-picture" : "columns"
                tip: qsTr("The two pictures: side by side, one over the other, or one small over the other")
                onClicked: layoutMenu.popup(layoutButton, 0, layoutButton.height + 8)
            }

            // The picture asked of the console: its height and fps. A click
            // picks others; the session starts again with them (the console
            // only takes them when connecting).
            Rectangle {
                id: qualityPill
                implicitHeight: 36
                implicitWidth: qualityRow.implicitWidth + 24
                radius: height / 2
                color: qualityArea.pressed ? Qt.rgba(1, 1, 1, 0.18)
                     : qualityArea.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06)
                border.width: 1
                border.color: Theme.hudEdge
                Row {
                    id: qualityRow
                    anchors.centerIn: parent
                    spacing: 7
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "monitor"
                        size: 16
                        color: Theme.onStage
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.built ? qsTr("%1p · %2").arg(root.cur.requestedResolution).arg(root.cur.requestedFps) : ""
                        color: Theme.onStage
                        font.family: Theme.fontMono
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                }
                // After the menu, the tip waits for the mouse to come back:
                // the menu takes the hover, and the leaving is not seen.
                property bool tipAllowed: true
                MouseArea {
                    id: qualityArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: qualityPill.tipAllowed = true
                    onClicked: {
                        qualityPill.tipAllowed = false
                        qualityMenu.popup(qualityPill, 0, -qualityMenu.implicitHeight - 8)
                    }
                }
                ToolTip.visible: qualityArea.containsMouse && qualityPill.tipAllowed && !qualityMenu.visible
                ToolTip.text: qsTr("The picture asked of the console. Click for another: the session starts again with it.")

                Menu {
                    id: qualityMenu
                    topPadding: 8
                    bottomPadding: 8
                    background: ShelfTile.MenuGlass { implicitWidth: 230 }
                    // Changing both at once would end the session twice.
                    function pick(resolution, fps) {
                        qualityPill.tipAllowed = false
                        app.setStreamQuality(resolution, fps)
                        root.cur.changeQuality(resolution, fps)
                    }
                    StyledMenuItem {
                        enabled: false
                        text: qsTr("Resolution")
                    }
                    Repeater {
                        model: [1080, 720, 540, 360]
                        delegate: StyledMenuItem {
                            required property int modelData
                            // 1080p: a PS5 or a PS4 Pro (a PS4 sends 720p; the
                            // search does not tell a Pro apart).
                            text: modelData + "p"
                                  + (modelData === 1080 && root.built && !root.cur.consolePs5 ? "  " + qsTr("(PS4 Pro)") : "")
                                  + (root.built && root.cur.requestedResolution === modelData ? "  ✓" : "")
                            onTriggered: qualityMenu.pick(modelData, root.cur.requestedFps)
                        }
                    }
                    MenuSeparator {}
                    StyledMenuItem {
                        enabled: false
                        text: qsTr("Frames per second")
                    }
                    Repeater {
                        model: [60, 30]
                        delegate: StyledMenuItem {
                            required property int modelData
                            text: qsTr("%1 fps").arg(modelData)
                                  + (root.built && root.cur.requestedFps === modelData ? "  ✓" : "")
                            onTriggered: qualityMenu.pick(root.cur.requestedResolution, modelData)
                        }
                    }
                }
            }

            // The connection: hovering shows what comes in and goes out and
            // how quickly the console answers; the colour says how it is.
            HudButton {
                readonly property double ping: root.built ? root.cur.netPing : -1
                iconName: "activity"
                readonly property string grade: root.built ? root.cur.netGrade : "unknown"
                tone: grade === "good" ? Theme.ok : grade === "fair" ? Theme.warn
                    : grade === "poor" ? Theme.error : Theme.onStage
                tip: !root.built ? "" : qsTr("Download: %1").arg(root.rate(root.cur.netDownload))
                     + (root.cur.netVideo > 0 ? "  " + qsTr("(picture: %1)").arg(root.rate(root.cur.netVideo)) : "")
                     + "\n" + qsTr("Upload: %1").arg(root.rate(root.cur.netUpload))
                     + "\n" + qsTr("Ping: %1").arg(ping < 0 ? "—" : qsTr("%1 ms").arg(ping < 10 ? ping.toFixed(1) : Math.round(ping)))
                     + "\n" + (root.cur.netInterface.length > 0
                                ? qsTr("This PC's connection (%1), everything it carries.").arg(root.cur.netInterface)
                                : qsTr("This PC's connection."))
            }

            // The controls: the controller (green when one is connected) and the
            // keyboard, in one button that opens the key map.
            HudButton {
                iconName: "gamepad"
                secondIconName: "keyboard"
                tone: root.built && root.cur.gamepadName.length > 0 ? Theme.ok : Theme.onStage
                tip: root.built && root.cur.gamepadName.length > 0
                     ? qsTr("Controller: %1. Click for the keyboard map.").arg(root.cur.gamepadName)
                     : qsTr("No controller connected. Click for the keyboard map.")
                onClicked: keysDialog.open()
            }

            // Sound: a pill with its state, and a click mutes it.
            Rectangle {
                id: soundPill
                readonly property string audioStatus: root.built ? root.cur.audioState : "stopped"
                readonly property bool broken: audioStatus === "error" || audioStatus === "no-device"
                implicitHeight: 36
                implicitWidth: soundRow.implicitWidth + 24
                radius: height / 2
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
                             : root.built && root.cur.muted ? Theme.onStageMuted : Theme.ok
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: !root.built ? qsTr("Sound")
                             : soundPill.broken ? qsTr("No sound")
                             : root.cur.muted ? qsTr("Sound: off")
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
                    onClicked: root.cur.muted = !root.cur.muted
                }
                ToolTip.visible: soundArea.containsMouse
                ToolTip.text: audioStatus === "no-device"
                        ? qsTr("This PC has no active sound output.")
                    : audioStatus === "error"
                        ? qsTr("The sound card refused the stream — see Ctrl+L.")
                    : audioStatus === "playing"
                        ? qsTr("Coming out of %1").arg(root.cur.audioDevice)
                        : qsTr("No sound has arrived from the console yet.")
            }

            // The microphone must be visible. While it is capturing, the button
            // stays lit — nobody can be heard without noticing.
            HudButton {
                readonly property string micStatus: root.built ? root.cur.microphoneState : "off"
                iconName: micStatus === "muted" ? "mic-off" : "mic"
                lit: micStatus === "talking"
                tip: micStatus === "off"
                    ? qsTr("Send your microphone to the console")
                    : qsTr("Capturing from %1. Click to mute, or right-click to turn it off.").arg(root.cur.microphoneDevice)
                onClicked: {
                    if (micStatus === "off")
                        root.cur.setMicrophoneEnabled(true)
                    else
                        root.cur.setMicrophoneMuted(micStatus === "talking")
                }
                // Right click turns it off entirely, instead of just muting.
                onRightClicked: root.cur.setMicrophoneEnabled(false)
            }
            // The picture's shape: as sent (16:9), 4:3 for PS1/PS2 games, or
            // stretched to the window. Each click moves to the next.
            Rectangle {
                id: aspectPill
                readonly property var modes: ["fit", "4:3", "fill"]
                implicitHeight: 36
                implicitWidth: aspectRow.implicitWidth + 24
                radius: height / 2
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
                        font.family: app.streamAspect === "fill" ? Theme.fontSans : Theme.fontMono
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
            // The transfers: over the picture in full screen, the side
            // panel otherwise (F9).
            HudButton {
                iconName: "panel"
                tip: window.streamFullscreen ? qsTr("Transfers over the picture (F9)") : qsTr("Transfers (F9)")
                onClicked: window.togglePanel()
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
                onClicked: root.cur.stopStream()
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

        // A sheet of glass floating over the window, lit along its upper edge;
        // it fades in and settles, without bouncing.
        background: Plate {
            level: "secondary"
            radius: Theme.radiusDialog
            color: Theme.dialogFill
            border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Theme.glassEdge
        }
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
            NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast; easing.type: Theme.easeOut }
        }

        header: DialogHeader {
            eyebrow: qsTr("Remote Play")
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
                        // A well in the dialog: sunken while it only explains,
                        // in the accent once it says what the key does.
                        radius: Theme.radius
                        readonly property bool full: keyboardMap.message.length > 0
                        color: full ? Theme.accentFill
                             : Theme.light ? Qt.rgba(1, 1, 1, 0.40) : Qt.rgba(0, 0, 0, 0.20)
                        border.width: 1
                        border.color: full ? Theme.alpha(Theme.accent, 0.6) : Theme.glassEdge
                        Behavior on color { ColorAnimation { duration: Theme.fast } }

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WordWrap
                            color: parent.full ? Theme.ink : Theme.textSecondary
                            font.family: parent.full ? Theme.fontDisplay : Theme.fontSans
                            font.pixelSize: parent.full ? 15 : 12
                            font.weight: parent.full ? Font.Bold : Font.Normal
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
                // Remote Play's console: while a session holds one, another
                // can be in use, and must not take this one's type and host-id.
                app.rememberConsoleType(stream.address, stream.consolePs5, stream.hostId)
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
        // The session that asked (the first or the second at once).
        property var asker: null
        // A 4-digit passcode is that console's from now on: the next
        // connection sends it by itself, without this dialog.
        function send(pin) {
            var ctrl = asker ? asker : stream
            ctrl.sendLoginPin(pin)
            if (/^[0-9]{4}$/.test(pin))
                app.setConsolePin(ctrl.address, pin)
            close()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 420
        modal: true
        padding: 0

        Overlay.modal: Rectangle { color: Theme.scrim }

        // A sheet of glass floating over the window, lit along its upper edge;
        // it fades in and settles, without bouncing.
        background: Plate {
            level: "secondary"
            radius: Theme.radiusDialog
            color: Theme.dialogFill
            border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Theme.glassEdge
        }
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
            NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast; easing.type: Theme.easeOut }
        }

        header: DialogHeader {
            eyebrow: qsTr("Remote Play")
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
                    onClicked: loginPinDialog.send(loginPinField.text)
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
                      ? qsTr("The console refused that PIN. Type the right one: it replaces the one saved "
                             + "for this console.")
                      : loginPinDialog.asker && loginPinDialog.asker.accountName.length > 0
                        ? qsTr("The console is asking for the login PIN of %1.").arg(loginPinDialog.asker.accountName)
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
                onAccepted: if (text.length > 0) loginPinDialog.send(text)
            }
        }
    }

    Connections {
        target: root.built ? stream : null
        function onLoginPinRequested(incorrect) {
            loginPinDialog.asker = stream
            loginPinDialog.incorrect = incorrect
            loginPinField.text = ""
            loginPinDialog.open()
        }
    }
    // The second session: its own console's PIN, through the same dialog; and
    // since it has no card, an unregistered console is said in a notice.
    Connections {
        target: root.has2 ? stream2 : null
        function onLoginPinRequested(incorrect) {
            loginPinDialog.asker = stream2
            loginPinDialog.incorrect = incorrect
            loginPinField.text = ""
            loginPinDialog.open()
        }
        function onRegistrationNeeded() {
            toast.show(qsTr("Register this PC on that console first, from its card, then connect a second session."), true)
        }
    }
}
