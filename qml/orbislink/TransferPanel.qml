// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The installs panel: every job of the app as one list of cards — the
// queue's packages, the PS1/PS2 conversions (one card each from the disc to
// the console), the homebrew store's installs, the console library's jobs
// and the copies to this PC. What is under way comes first, each job on a
// card of brighter glass, then what waits its turn and what is over, as rows
// between hairlines (a glass pill under the mouse) under champagne section
// labels with their count. At the foot a glass readout adds them all up: the
// percentage large and bold, the bar, the figures in mono and Pause.
// It sits in the transfers pane, which draws the glass: no panel here.
//
// The list's rows only say which card goes where ({ key, kind, section,
// head — the first of its section, which carries the section's title });
// what each card shows is in `cardData`, made again at every change. When
// the order changes the rows move, so a card — and its animations — is
// never made again because another one changed.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    ListModel { id: cards }
    readonly property alias model: cards
    // key → { kind, group, weight, fraction, failed, thumb, and the job's own
    // data: r (a queue task), c and j (a conversion and its journey), job }
    property var cardData: ({})
    property int runningCount: 0
    property int waitingCount: 0
    property int finishedCount: 0
    readonly property int cardCount: cards.count
    // Something under way or waiting.
    readonly property bool busy: runningCount + waitingCount > 0

    // The order the cards came in, and the order they finished in.
    property var firstSeen: ({})
    property int seenCounter: 0
    property var finishedOrder: ({})
    property int finishCounter: 0

    // The footer's figures: the batch is every card since the queue last had
    // nothing to do — the ones done since count too, so its size stays put
    // while it goes (one that fails or is cancelled leaves it).
    property var batch: ({})
    property real batchTotal: 0
    property real batchDone: 0
    property real speed: 0
    // The batch's bytes done per second, smoothed over the last seconds: the
    // time left comes from it.
    property real rate: 0
    property real sampledDone: -1
    property real sampledAt: 0
    readonly property real fraction: batchTotal > 0 ? Math.max(0, Math.min(1, batchDone / batchTotal)) : 0
    readonly property real secondsLeft: rate > 1 && batchTotal > batchDone ? (batchTotal - batchDone) / rate : -1

    Timer {
        id: rebuildTimer
        interval: 40
        onTriggered: root.rebuild()
    }
    function scheduleRebuild() {
        if (!rebuildTimer.running)
            rebuildTimer.start()
    }
    Connections {
        target: app.queue
        function onRowsChanged() { root.scheduleRebuild() }
    }
    Connections {
        target: games
        function onConversionsChanged() { root.scheduleRebuild() }
    }
    Connections {
        target: consoleLibrary
        function onJobsChanged() { root.scheduleRebuild() }
    }
    Connections {
        target: typeof store !== "undefined" ? store : null
        ignoreUnknownSignals: true
        function onJobsChanged() { root.scheduleRebuild() }
    }
    Connections {
        target: shelf
        function onCoversChanged() { root.scheduleRebuild() }
    }
    Component.onCompleted: rebuild()

    // How fast the batch goes: sampled each second while there is work.
    Timer {
        running: root.busy
        interval: 1000
        repeat: true
        onTriggered: root.sample()
    }
    onBusyChanged: {
        rate = 0
        sampledDone = -1
    }
    function sample() {
        var now = Date.now()
        if (sampledDone >= 0 && now > sampledAt) {
            var instant = (batchDone - sampledDone) / ((now - sampledAt) / 1000)
            if (instant >= 0)
                rate = rate > 0 ? 0.85 * rate + 0.15 * instant : instant
        }
        sampledDone = batchDone
        sampledAt = now
    }

    // ── the cards, from every job ────────────────────────────────────────
    function rebuild() {
        var items = []
        var data = {}
        var followed = {}
        var speed = 0
        var i, key

        var convs = games.conversions
        for (i = 0; i < convs.length; ++i) {
            var c = convs[i]
            var j = journey(c)
            if (c.pkgKey && c.pkgKey.length > 0)
                followed[c.pkgKey] = true
            var cRunning = !j.finished && (c.state === "converting"
                           || ((j.stage === "sending" || j.stage === "installing") && !j.waiting))
            key = "c" + c.id
            if (c.state === "converting")
                speed += c.bytesPerSecond || 0
            data[key] = {
                kind: "conversion", c: c, j: j,
                group: j.finished ? 2 : cRunning ? 0 : 1, source: 0, order: i,
                weight: c.size > 0 ? c.size : (c.bytesTotal || 0),
                fraction: j.done ? 1 : stepsFraction(j.steps),
                failed: j.stage === "error" || j.stage === "cancelled",
                thumb: { title: c.title || "", picture: shelf.coverRevision >= 0
                                ? shelf.cover(c.platform || "", c.serial || "") : "",
                         icon: c.kind === "pack" ? "package" : "disc",
                         stage: j.stage, percent: j.percent, waiting: j.waiting, text: j.text }
            }
            items.push(key)
        }

        var rows = app.queue.rows
        for (i = 0; i < rows.length; ++i) {
            var r = rows[i]
            speed += r.active ? (r.bytesPerSecond || 0) : 0
            if (r.superseded || followed[r.localKey])
                continue
            key = "t" + r.taskId
            var tStage = taskStage(r)
            data[key] = {
                kind: "task", r: r,
                group: r.active ? 0 : r.state === "pending" ? 1 : 2, source: 1, order: i,
                weight: r.totalBytes || 0,
                fraction: r.state === "completed" ? 1 : (r.percent || 0) / 100,
                failed: r.state === "error" || r.state === "cancelled",
                thumb: { title: r.title, picture: r.iconSource || "", icon: "package",
                         stage: tStage, percent: r.percent, waiting: r.state === "pending",
                         text: r.stateLabel }
            }
            items.push(key)
        }

        var library = consoleLibrary.jobs
        for (i = 0; i < library.length; ++i) {
            var l = library[i]
            var lWorking = l.state === "working"
            key = "l" + l.id
            if (lWorking)
                speed += l.bytesPerSecond || 0
            data[key] = {
                kind: "library", job: l,
                group: lWorking ? (l.queued ? 1 : 0) : 2, source: 2, order: i,
                weight: l.kind === "copy" ? (l.bytesTotal || 0) : 0,
                fraction: l.state === "done" ? 1 : (l.percent || 0) / 100,
                failed: l.state === "error" || l.state === "cancelled",
                thumb: { title: l.name || "", picture: l.icon ? l.icon.toString() : "", icon: libraryIcon(l),
                         stage: jobStage(l, l.kind === "copy" ? "receiving" : l.kind === "run" ? "sending" : "installing"),
                         percent: l.percent || 0, waiting: lWorking && l.queued === true, text: l.stageText || "" }
            }
            items.push(key)
        }

        var storeJobs = typeof store !== "undefined" && store ? store.jobs : []
        for (i = 0; i < storeJobs.length; ++i) {
            var s = storeJobs[i]
            key = "s" + s.id
            if (s.state === "working")
                speed += s.bytesPerSecond || 0
            data[key] = {
                kind: "store", job: s,
                group: s.state === "working" ? 0 : 2, source: 3, order: i,
                weight: s.bytesTotal || 0,
                fraction: s.state === "done" ? 1 : (s.percent || 0) / 100,
                failed: s.state === "error" || s.state === "cancelled",
                thumb: { title: s.name || "", picture: s.icon ? s.icon.toString() : "", icon: "store",
                         stage: jobStage(s, "installing"), percent: s.percent || 0, waiting: false,
                         text: s.stageText || "" }
            }
            items.push(key)
        }

        // When each came, and when each finished.
        var seen = firstSeen
        var finished = finishedOrder
        var present = {}
        var running = 0, waiting = 0, over = 0
        for (i = 0; i < items.length; ++i) {
            key = items[i]
            present[key] = true
            if (seen[key] === undefined)
                seen[key] = ++seenCounter
            var group = data[key].group
            if (group === 2 && finished[key] === undefined)
                finished[key] = ++finishCounter
            else if (group !== 2 && finished[key] !== undefined)
                delete finished[key]
            if (group === 0) ++running
            else if (group === 1) ++waiting
            else ++over
        }
        for (key in seen)
            if (!present[key])
                delete seen[key]
        for (key in finished)
            if (!present[key])
                delete finished[key]

        // Under way first, then waiting (each kind of job in its own order),
        // then the finished ones, the latest first.
        items.sort(function (a, b) {
            var da = data[a], db = data[b]
            if (da.group !== db.group)
                return da.group - db.group
            if (da.group === 2)
                return finished[b] - finished[a]
            if (da.source !== db.source)
                return da.source - db.source
            return da.order - db.order
        })

        // The batch.
        var members = batch
        if (running + waiting === 0)
            members = {}
        else {
            for (i = 0; i < items.length; ++i)
                if (data[items[i]].group !== 2)
                    members[items[i]] = true
            for (key in members)
                if (!present[key] || data[key].failed)
                    delete members[key]
        }
        var total = 0, done = 0
        for (key in members) {
            var d = data[key]
            total += d.weight
            done += d.weight * Math.max(0, Math.min(1, d.fraction))
        }

        cardData = data
        runningCount = running
        waitingCount = waiting
        finishedCount = over
        batch = members
        batchTotal = total
        batchDone = done
        root.speed = speed

        // The rows: those gone go, the others move to their place.
        for (i = cards.count - 1; i >= 0; --i)
            if (!present[cards.get(i).key])
                cards.remove(i)
        var previous = ""
        for (i = 0; i < items.length; ++i) {
            key = items[i]
            var section = data[key].group === 0 ? "running" : data[key].group === 1 ? "waiting" : "finished"
            var head = section !== previous
            previous = section
            var at = -1
            for (var k = i; k < cards.count; ++k)
                if (cards.get(k).key === key) {
                    at = k
                    break
                }
            if (at < 0)
                cards.insert(i, { key: key, kind: data[key].kind, section: section, head: head })
            else {
                if (at !== i)
                    cards.move(at, i, 1)
                if (cards.get(i).section !== section)
                    cards.setProperty(i, "section", section)
                if (cards.get(i).head !== head)
                    cards.setProperty(i, "head", head)
            }
        }
    }

    // How far through its steps a conversion is, from 0 to 1.
    function stepsFraction(steps) {
        if (!steps || steps.length === 0)
            return 0
        var sum = 0
        for (var i = 0; i < steps.length; ++i)
            sum += Math.max(0, Math.min(100, steps[i].percent || 0))
        return sum / (100 * steps.length)
    }
    // A queue task's place on the way, as StageLoader says it.
    function taskStage(r) {
        if (r.state === "completed")
            return "done"
        if (r.state === "error" || r.state === "cancelled")
            return r.state
        return r.install ? "installing" : "sending"
    }
    function jobStage(job, under) {
        return job.state === "done" ? "done" : job.state === "error" ? "error"
             : job.state === "cancelled" ? "cancelled" : under
    }
    function libraryIcon(job) {
        return job.kind === "run" ? "zap"
             : job.kind === "copy" ? (job.itemKind === "folder" ? "folder" : job.itemKind === "file" ? "file" : "laptop")
             : job.kind === "uninstall" ? "trash"
             : job.itemKind === "folder" ? "folder" : "package"
    }

    // Every finished card, of every kind, off the panel.
    function clearFinished() {
        app.clearFinishedTasks()
        games.clearFinishedConversions()
        consoleLibrary.clearFinishedJobs()
        if (typeof store !== "undefined" && store)
            store.clearFinishedJobs()
    }

    // A thin bar: a recessed glass track, the fill in the job's colour and,
    // while it moves, a small lit bead where it has got to.
    component ThinBar: Item {
        id: thin
        property real fraction: 0
        property color tone: Theme.accent
        property bool bead: false
        property int thickness: 3
        implicitHeight: 6
        readonly property real clamped: Math.max(0, Math.min(1, fraction))
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: thin.thickness
            radius: height / 2
            color: Theme.light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.09)
            Rectangle {
                width: parent.width * thin.clamped
                height: parent.height
                radius: parent.radius
                color: thin.tone
                Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
            }
        }
        Rectangle {
            visible: thin.bead && thin.clamped > 0 && thin.clamped < 1
            width: 12; height: 12; radius: 6
            anchors.verticalCenter: parent.verticalCenter
            x: Math.max(0, thin.width * thin.clamped - 6)
            color: Theme.alpha(thin.tone, 0.25)
            Behavior on x { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
            Rectangle {
                anchors.centerIn: parent
                width: 6; height: 6; radius: 3
                color: "#FFFFFF"
            }
        }
    }

    // A queue row's picture: the cover (or an icon standing in) with the
    // step under way in its corner, in a ring that matches the rail's.
    component Thumb: Rectangle {
        property string picture: ""
        property string icon: "package"
        property string stage: ""
        property real percent: 0
        property bool waiting: false
        implicitWidth: Theme.gameIcon
        implicitHeight: Theme.gameIcon
        radius: 12
        color: Theme.controlFill
        border.color: Theme.light ? Qt.rgba(1, 1, 1, 0.95) : Theme.glassEdge
        CoverArt {
            id: thumbCover
            anchors.fill: parent
            visible: ready
            crop: true
            edge: false
            radius: parent.radius
            picture: parent.picture
        }
        Icon {
            anchors.centerIn: parent
            visible: !thumbCover.ready
            name: parent.icon
            size: 20
            color: Theme.textSecondary
        }
        Rectangle {
            anchors.fill: thumbStage
            anchors.margins: -2
            radius: width / 2
            color: Theme.menuFill
            border.width: 1
            border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Theme.glassEdge
        }
        StageLoader {
            id: thumbStage
            stage: parent.stage
            percent: parent.percent
            waiting: parent.waiting
            width: 22
            height: 22
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: -5
            anchors.bottomMargin: -5
        }
    }

    // The glass behind a row: a card of brighter glass, lit from above, for
    // a job under way; otherwise nothing but a glass pill under the mouse.
    component RowGlass: Rectangle {
        property bool lit: false
        property bool hovered: false
        radius: 16
        color: lit ? (Theme.light ? Qt.rgba(1, 1, 1, 0.86) : Qt.rgba(1, 1, 1, 0.09))
             : hovered ? Theme.controlFill : "transparent"
        border.width: lit ? 1 : 0
        border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.08)
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        Rectangle {
            visible: parent.lit
            anchors.fill: parent
            anchors.margins: 1
            radius: parent.radius - 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.5 : 0.07) }
                GradientStop { position: 0.4; color: Qt.rgba(1, 1, 1, 0) }
            }
        }
        // The lit upper edge.
        Rectangle {
            visible: parent.lit
            anchors.top: parent.top
            anchors.topMargin: 1
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 2 * parent.radius
            height: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0) }
                GradientStop { position: 0.5; color: Theme.glassStrokeTop }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0) }
            }
        }
    }

    // The small round buttons of a row.
    component RowButton: StyledToolButton {
        property string hint: ""
        iconSize: 15
        implicitWidth: 32
        implicitHeight: 32
        Accessible.name: hint
        ToolTip.visible: hovered && hint.length > 0
        ToolTip.delay: 400
        ToolTip.text: hint
    }

    // Pause and Resume, one width for both labels so it never jumps.
    component PauseButton: StyledButton {
        id: pause
        chip: true
        minimumWidth: Math.max(pauseWidth.implicitWidth, resumeWidth.implicitWidth) + 14 + 8 + 24
        iconName: app.queuePaused ? "play" : "pause"
        text: app.queuePaused ? qsTr("Resume") : qsTr("Pause")
        onClicked: app.queuePaused ? app.resumeQueue() : app.pauseQueue()
        enabled: app.queue.count > 0
        // The widest of the two labels sets the width.
        Text { id: pauseWidth; visible: false; text: qsTr("Pause"); font: pause.font }
        Text { id: resumeWidth; visible: false; text: qsTr("Resume"); font: pause.font }
    }

    // A figure of the readout: its name in small capitals, its value in mono.
    component Figure: Column {
        property alias label: figureLabel.text
        property alias value: figureValue.text
        spacing: 3
        Eyebrow {
            id: figureLabel
            width: parent.width
            color: Theme.textSecondary
        }
        Text {
            id: figureValue
            width: parent.width
            color: Theme.ink
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSmall
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Paused queue notice: a glass card with a faint amber wash.
        Plate {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.bottomMargin: 4
            visible: app.queuePaused
            radius: 16
            color: Theme.alpha(Theme.warn, Theme.light ? 0.12 : 0.10)
            border.color: Theme.alpha(Theme.warn, 0.40)
            implicitHeight: pauseRow.implicitHeight + 24

            RowLayout {
                id: pauseRow
                anchors.fill: parent
                anchors.margins: 12
                anchors.leftMargin: 14
                spacing: 10

                Icon {
                    Layout.alignment: Qt.AlignTop
                    name: app.waitingForInstaller ? "refresh" : "pause"
                    spinning: app.waitingForInstaller
                    size: 16
                    color: Theme.light ? Qt.darker(Theme.warn, 1.3) : Theme.warn
                }
                Text {
                    Layout.fillWidth: true
                    // A PS5's installer is a payload (Prospero Manager) or
                    // an etaHEN setting (DPI v2), not an app to open.
                    text: !app.waitingForInstaller
                          ? (app.pauseReason.length > 0 ? app.pauseReason : qsTr("Queue paused."))
                          : app.installsFromConsole
                            ? qsTr("Waiting for an installer: in Payloads, press ▶ on Prospero Manager, or turn "
                                   + "on etaHEN's DPI v2. The install starts by itself.")
                            : qsTr("Waiting for Remote Package Installer: open it on the console and the "
                                   + "install starts by itself.")
                    color: Theme.text
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontSmall
                    lineHeight: 1.15
                }
                StyledButton {
                    Layout.alignment: Qt.AlignVCenter
                    chip: true
                    text: app.waitingForInstaller ? qsTr("Try now") : qsTr("Resume")
                    onClicked: app.resumeQueue()
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Flickable {
                id: list
                anchors.fill: parent
                clip: true
                contentWidth: width
                contentHeight: column.implicitHeight + 12
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }

                // The cards in a column: placed again whenever one changes size or
                // place — a card that moves slides there.
                Column {
                    id: column
                    width: list.width
                    spacing: 0
                    move: Transition { NumberAnimation { properties: "y"; duration: 260; easing.type: Easing.OutCubic } }

                    Repeater {
                        model: cards

                        delegate: Item {
                            id: entry
                            required property int index
                            required property string key
                            required property string kind
                            required property string section
                            required property bool head
                            readonly property var d: root.cardData[key] || ({})
                            readonly property bool lit: section === "running"
                            width: column.width
                            height: (head ? header.height : 0) + (body.item ? body.item.implicitHeight : 0)
                                    + (lit ? 6 : 0)
                            // A new card fades in.
                            opacity: 0
                            Behavior on opacity { NumberAnimation { duration: 220 } }
                            Component.onCompleted: opacity = 1

                            // The first card of a section carries its label:
                            // under way, waiting, finished (with the button
                            // that clears them).
                            Item {
                                id: header
                                visible: entry.head
                                width: parent.width
                                height: entry.index === 0 ? 36 : 48

                                RowLayout {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 8
                                    anchors.leftMargin: 2
                                    spacing: 8

                                    // Under way: a dot that breathes.
                                    Rectangle {
                                        visible: entry.lit
                                        width: 6
                                        height: 6
                                        radius: 3
                                        color: Theme.accent
                                        SequentialAnimation on opacity {
                                            running: entry.lit && entry.head && entry.visible
                                            loops: Animation.Infinite
                                            NumberAnimation { from: 1; to: 0.3; duration: 800; easing.type: Easing.InOutSine }
                                            NumberAnimation { from: 0.3; to: 1; duration: 800; easing.type: Easing.InOutSine }
                                        }
                                    }
                                    Eyebrow {
                                        text: entry.section === "running" ? qsTr("Under way")
                                            : entry.section === "waiting" ? qsTr("Waiting") : qsTr("Finished")
                                    }
                                    // The count, on a small glass pill.
                                    Rectangle {
                                        implicitWidth: Math.max(20, countText.implicitWidth + 12)
                                        implicitHeight: 18
                                        radius: 9
                                        color: Theme.controlFill
                                        border.width: 1
                                        border.color: Theme.light ? Qt.rgba(1, 1, 1, 0.95) : Theme.glassEdge
                                        Text {
                                            id: countText
                                            anchors.centerIn: parent
                                            text: entry.section === "running" ? root.runningCount
                                                : entry.section === "waiting" ? root.waitingCount : root.finishedCount
                                            color: Theme.text
                                            font.family: Theme.fontMono
                                            font.pixelSize: Theme.fontCaption
                                            font.weight: Font.Medium
                                        }
                                    }
                                    // The label's hairline runs on, fading, to the edge.
                                    EngravedRule {
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignVCenter
                                    }
                                    StyledButton {
                                        visible: entry.section === "finished"
                                        chip: true
                                        iconName: "check-check"
                                        text: qsTr("Clear finished")
                                        onClicked: root.clearFinished()
                                    }
                                }
                            }

                            // Between two rows of a section: a hairline (none
                            // around the glass cards of the jobs under way).
                            Rectangle {
                                visible: !entry.head && !entry.lit
                                y: header.visible ? header.height : 0
                                x: 14
                                width: parent.width - 28
                                height: 1
                                color: Theme.rule
                                opacity: 0.7
                            }

                            Loader {
                                id: body
                                y: entry.head ? header.height : 0
                                width: parent.width
                                sourceComponent: entry.kind === "conversion" ? conversionCard
                                               : entry.kind === "task" ? taskCard : jobCard
                            }

                            // A package of the queue.
                            Component {
                                id: taskCard
                                RowGlass {
                                    readonly property var r: entry.d.r || ({})
                                    readonly property string stateTone: r.state === "completed" ? "ok"
                                                                      : r.state === "error" ? "error"
                                                                      : r.state === "cancelled" || r.state === "pending" ? "neutral"
                                                                      : "accent"
                                    lit: entry.lit
                                    hovered: taskHover.hovered
                                    HoverHandler { id: taskHover }
                                    implicitHeight: taskColumn.implicitHeight + 24

                                    ColumnLayout {
                                        id: taskColumn
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 12
                                        spacing: 8

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 12

                                            Thumb {
                                                picture: r.iconSource ? r.iconSource : ""
                                                icon: "package"
                                                stage: entry.d.thumb ? entry.d.thumb.stage : ""
                                                percent: r.percent || 0
                                                waiting: r.state === "pending" || r.state === "validating"
                                            }

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 3
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: r.title || ""
                                                    color: Theme.ink
                                                    font.pixelSize: Theme.fontBody
                                                    font.weight: Font.Bold
                                                    elide: Text.ElideRight
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: [r.titleId, r.category, r.sizeText,
                                                           r.firmware ? qsTr("FW %1").arg(r.firmware) : ""].filter(function (part) {
                                                        return part && part.length > 0
                                                    }).join(" · ")
                                                    color: Theme.textSecondary
                                                    font.family: Theme.fontMono
                                                    font.pixelSize: Theme.fontCaption
                                                    elide: Text.ElideRight
                                                }
                                            }

                                        }

                                        // An official package, or a firmware above the PS4's.
                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: !!r.warning
                                            spacing: 6
                                            Icon {
                                                Layout.alignment: Qt.AlignTop
                                                Layout.topMargin: 1
                                                name: "warning"
                                                size: 12
                                                color: Theme.light ? Qt.darker(Theme.warn, 1.3) : Theme.warn
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: r.warning ? r.warning : ""
                                                color: Theme.light ? Qt.darker(Theme.warn, 1.3) : Theme.warn
                                                font.pixelSize: Theme.fontCaption
                                                wrapMode: Text.WordWrap
                                            }
                                        }

                                        ThinBar {
                                            Layout.fillWidth: true
                                            fraction: (r.percent || 0) / 100
                                            tone: Theme.taskColor(r.state || "")
                                            bead: r.active === true
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8

                                            // The state leads the figures, so the
                                            // name and its facts keep the full width.
                                            Tag {
                                                text: r.stateLabel || ""
                                                tone: stateTone
                                            }
                                            Text {
                                                text: (r.percent || 0).toFixed(1) + "%"
                                                color: Theme.text
                                                font.family: Theme.fontMono
                                                font.pixelSize: Theme.fontCaption
                                            }
                                            Text {
                                                visible: r.active === true && (r.speedText || "").length > 0
                                                text: r.speedText || ""
                                                color: Theme.textSecondary
                                                font.family: Theme.fontMono
                                                font.pixelSize: Theme.fontCaption
                                            }
                                            // The time left, after a small clock: the
                                            // row is narrow, a word would not fit.
                                            Icon {
                                                visible: r.active === true && (r.etaText || "").length > 0
                                                name: "history"
                                                size: 12
                                                color: Theme.textSecondary
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                Layout.leftMargin: -4
                                                visible: r.active === true
                                                text: r.etaText || ""
                                                color: Theme.textSecondary
                                                font.family: Theme.fontMono
                                                font.pixelSize: Theme.fontCaption
                                                elide: Text.ElideRight
                                            }
                                            Item { Layout.fillWidth: r.active !== true }

                                            RowButton {
                                                iconName: "arrow-up"
                                                hint: qsTr("Move up")
                                                onClicked: app.moveTaskUp(r.taskId)
                                                visible: r.state === "pending" && (r.canMoveUp || r.canMoveDown)
                                                enabled: r.canMoveUp === true
                                            }
                                            RowButton {
                                                iconName: "arrow-down"
                                                hint: qsTr("Move down")
                                                onClicked: app.moveTaskDown(r.taskId)
                                                visible: r.state === "pending" && (r.canMoveUp || r.canMoveDown)
                                                enabled: r.canMoveDown === true
                                            }
                                            RowButton {
                                                iconName: "rotate-ccw"
                                                hint: qsTr("Try again")
                                                onClicked: app.retryTask(r.taskId)
                                                visible: r.state === "error" || r.state === "cancelled"
                                            }
                                            RowButton {
                                                readonly property bool stops: r.state === "pending" || r.active === true
                                                iconName: "close"
                                                danger: r.active === true
                                                hint: stops ? qsTr("Cancel") : qsTr("Remove")
                                                onClicked: stops ? app.cancelTask(r.taskId) : app.removeTask(r.taskId)
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: r.message || ""
                                            color: r.state === "error" ? (Theme.light ? Qt.darker(Theme.error, 1.3) : Theme.error)
                                                                       : Theme.textSecondary
                                            font.pixelSize: Theme.fontCaption
                                            wrapMode: Text.WordWrap
                                            visible: (r.message || "").length > 0
                                        }
                                    }
                                }
                            }

                            // A PS1/PS2 conversion (or a folder packed), from the disc to
                            // the console: the conversion, the upload and the install,
                            // each with its own bar.
                            Component {
                                id: conversionCard
                                RowGlass {
                                    readonly property var c: entry.d.c || ({})
                                    readonly property var j: entry.d.j || ({ steps: [], text: "", stage: "", speed: "", eta: "",
                                                                             message: "", taskId: "", finished: false })
                                    lit: entry.lit
                                    hovered: convHover.hovered
                                    HoverHandler { id: convHover }
                                    implicitHeight: convColumn.implicitHeight + 24

                                    ColumnLayout {
                                        id: convColumn
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 12
                                        spacing: 10

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 12
                                            // The game's cover, with the step under way in its
                                            // corner; one size whether the cover is there or not.
                                            Thumb {
                                                picture: entry.d.thumb ? entry.d.thumb.picture : ""
                                                icon: c.kind === "pack" ? "package" : "disc"
                                                stage: j.stage || ""
                                                percent: j.percent || 0
                                                waiting: j.waiting === true
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 3
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: c.title || ""
                                                    color: Theme.ink
                                                    font.pixelSize: Theme.fontBody
                                                    font.weight: Font.Bold
                                                    elide: Text.ElideRight
                                                }
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 6
                                                    Text {
                                                        text: c.platform === "ps2" ? "PS2" : c.platform === "ps1" ? "PS1" : "PS4"
                                                        color: Theme.textSecondary
                                                        font.family: Theme.fontMono
                                                        font.pixelSize: Theme.fontCaption
                                                    }
                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: j.text || ""
                                                        color: j.stage === "error"
                                                               ? (Theme.light ? Qt.darker(Theme.error, 1.3) : Theme.error)
                                                               : Theme.textSecondary
                                                        font.pixelSize: Theme.fontCaption
                                                        elide: Text.ElideRight
                                                    }
                                                }
                                            }
                                            RowButton {
                                                Layout.alignment: Qt.AlignTop
                                                // The conversion itself again, or the send or install that failed.
                                                readonly property bool again: c.state === "error" || c.state === "cancelled"
                                                visible: again || (j.stage === "error" && (j.taskId || "").length > 0)
                                                iconName: "rotate-ccw"
                                                hint: qsTr("Try again")
                                                onClicked: again ? games.retryConversion(c.id) : app.retryTask(j.taskId)
                                            }
                                            RowButton {
                                                Layout.alignment: Qt.AlignTop
                                                visible: c.state === "done"
                                                iconName: "folder-open"
                                                hint: qsTr("Open the folder")
                                                onClicked: games.openOutputFolder()
                                            }
                                            RowButton {
                                                Layout.alignment: Qt.AlignTop
                                                iconName: "close"
                                                danger: !j.finished
                                                hint: j.finished ? qsTr("Remove") : qsTr("Cancel")
                                                onClicked: {
                                                    if (c.state === "waiting" || c.state === "converting")
                                                        games.cancelConversion(c.id)
                                                    else if (!j.finished && (j.taskId || "").length > 0)
                                                        app.cancelTask(j.taskId)
                                                    else
                                                        games.removeConversion(c.id)
                                                }
                                            }
                                        }

                                        // The steps, side by side: convert, send, install.
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            Repeater {
                                                model: j.steps ? j.steps.length : 0
                                                ColumnLayout {
                                                    required property int index
                                                    readonly property var step: j.steps[index] || ({ label: "", percent: 0,
                                                                                                    tone: Theme.textSecondary,
                                                                                                    current: false })
                                                    Layout.fillWidth: true
                                                    Layout.preferredWidth: 1
                                                    spacing: 4
                                                    ThinBar {
                                                        Layout.fillWidth: true
                                                        fraction: step.percent / 100
                                                        tone: step.tone
                                                        bead: step.current
                                                    }
                                                    Text {
                                                        Layout.fillWidth: true
                                                        // The step under way says how far it is.
                                                        text: step.current && step.percent > 0 && step.percent < 100
                                                              ? step.label + " · " + Math.floor(step.percent) + "%"
                                                              : step.label
                                                        color: step.current ? Theme.ink : Theme.textSecondary
                                                        font.pixelSize: Theme.fontEyebrow
                                                        font.weight: step.current ? Font.DemiBold : Font.Normal
                                                        elide: Text.ElideRight
                                                    }
                                                }
                                            }
                                        }

                                        // How fast it goes and how long it has left.
                                        RowLayout {
                                            visible: (j.speed || "").length > 0
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Icon {
                                                name: j.speedIcon || "cpu"
                                                size: 12
                                                color: Theme.textSecondary
                                            }
                                            Text {
                                                text: j.speed || ""
                                                color: Theme.text
                                                font.family: Theme.fontMono
                                                font.pixelSize: Theme.fontCaption
                                            }
                                            Item { Layout.fillWidth: true }
                                            Icon {
                                                visible: (j.eta || "").length > 0
                                                name: "history"
                                                size: 12
                                                color: Theme.textSecondary
                                            }
                                            Text {
                                                visible: (j.eta || "").length > 0
                                                text: qsTr("%1 left").arg(j.eta || "")
                                                color: Theme.textSecondary
                                                font.family: Theme.fontMono
                                                font.pixelSize: Theme.fontCaption
                                            }
                                        }

                                        Text {
                                            visible: (j.message || "").length > 0
                                            Layout.fillWidth: true
                                            text: j.message || ""
                                            color: Theme.light ? Qt.darker(Theme.error, 1.3) : Theme.error
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: Theme.fontCaption
                                        }
                                        // What became of the package made.
                                        Text {
                                            visible: (c.note || "").length > 0
                                            Layout.fillWidth: true
                                            text: c.note || ""
                                            color: Theme.textSecondary
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: Theme.fontCaption
                                        }
                                    }
                                }
                            }

                            // A homebrew store install, or what the console library set
                            // going (a copy to this PC, a move, a run, an uninstall).
                            Component {
                                id: jobCard
                                JobCard {
                                    readonly property bool fromStore: entry.kind === "store"
                                    job: entry.d.job || ({})
                                    highlighted: entry.lit
                                    stage: entry.d.thumb ? entry.d.thumb.stage : ""
                                    waiting: entry.d.thumb ? entry.d.thumb.waiting === true : false
                                    fallbackIcon: fromStore ? "store" : root.libraryIcon(job)
                                    showBar: !fromStore || job.kind !== "remove"
                                    // A store's package is downloaded here, then the queue installs it.
                                    stateText: fromStore
                                               ? (job.state === "done"
                                                  ? (job.kind === "remove" ? qsTr("Removed")
                                                     : job.kind === "download" ? qsTr("Downloaded") : qsTr("Installed"))
                                                  : job.state === "error" ? qsTr("Failed")
                                                  : job.state === "cancelled" ? qsTr("Cancelled")
                                                  : job.kind === "remove" ? qsTr("Removing")
                                                  : job.kind === "download" ? qsTr("Downloading") : qsTr("Installing"))
                                               : (job.state === "error" ? qsTr("Failed")
                                                  : job.state === "cancelled" ? (job.interrupted ? qsTr("Stopped") : qsTr("Cancelled"))
                                                  : job.kind === "run" ? (job.state === "done" ? qsTr("Sent") : qsTr("Sending"))
                                                  : job.kind === "copy" ? (job.state === "done" ? qsTr("On this PC")
                                                                           : job.queued ? qsTr("Waiting") : qsTr("Copying"))
                                                  : job.kind === "uninstall" ? (job.state === "done" ? qsTr("Uninstalled") : qsTr("Uninstalling"))
                                                  : job.state === "done" ? qsTr("On the home screen") : qsTr("Moving"))
                                    // A store install, or a copy to this PC, stops from here.
                                    cancellable: fromStore || job.kind === "copy"
                                    // Any library job that failed goes again from its card.
                                    retryable: !fromStore && (job.kind === "copy" || job.kind === "uninstall"
                                                              || job.kind === "mount" || job.kind === "run")
                                    doneIcon: !fromStore && job.kind === "copy" ? "folder-open" : ""
                                    doneHint: qsTr("Open the folder")
                                    onCancelClicked: fromStore ? store.cancel() : consoleLibrary.cancelJob(job.id)
                                    onRetryClicked: consoleLibrary.retryJob(job.id)
                                    onDoneClicked: consoleLibrary.openJobFolder(job.id)
                                    onRemoveClicked: fromStore ? store.removeJob(job.id) : consoleLibrary.removeJob(job.id)
                                }
                            }
                        }
                    }
                }
            }

            // Empty: the orb on a disc of glass, a bold heading, one line.
            Item {
                anchors.centerIn: parent
                width: parent.width - 40
                visible: cards.count === 0
                implicitHeight: emptyColumn.implicitHeight

                ColumnLayout {
                    id: emptyColumn
                    width: parent.width
                    spacing: 8
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.bottomMargin: 10
                        implicitWidth: 96
                        implicitHeight: 96
                        radius: 48
                        color: Theme.controlFill
                        border.width: 1
                        border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.04)
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 1
                            radius: width / 2
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.6 : 0.10) }
                                GradientStop { position: 0.6; color: Qt.rgba(1, 1, 1, 0) }
                            }
                        }
                        Image {
                            anchors.centerIn: parent
                            source: "qrc:/icons/planet.png"
                            sourceSize.width: 128
                            sourceSize.height: 128
                            width: 60
                            height: 60
                            fillMode: Image.PreserveAspectFit
                            opacity: Theme.light ? 0.55 : 0.6
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("The queue is empty")
                        color: Theme.ink
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.fontHeading
                        font.weight: Font.Bold
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Drag .pkg files onto the window.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }
        }

        // ── footer: the whole batch, on a glass readout card ──
        Plate {
            Layout.fillWidth: true
            Layout.topMargin: 10
            Layout.bottomMargin: 2
            radius: Theme.radius
            implicitHeight: readout.implicitHeight + 24

        ColumnLayout {
            id: readout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            anchors.leftMargin: 16
            anchors.rightMargin: 12
            spacing: 4

        // How far the batch is, all of it: its percentage large and bold,
        // the jobs to go and the time left beside it, a bar under them.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 4
            visible: root.busy && root.batchTotal > 0
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text {
                    Layout.alignment: Qt.AlignBaseline
                    text: Math.floor(root.fraction * 100) + "%"
                    color: Theme.ink
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.Bold
                    font.letterSpacing: -0.5
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("%n job(s) to go", "", root.runningCount + root.waitingCount)
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.secondsLeft > 0 ? qsTr("about %1 left").arg(app.queue.durationText(root.secondsLeft))
                                                   : qsTr("working out the time…")
                        color: Theme.textSecondary
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                    }
                }
                // While the batch goes, Pause sits up here beside its
                // percentage, leaving the figures below the whole width.
                PauseButton { Layout.alignment: Qt.AlignVCenter }
            }
            ThinBar {
                Layout.fillWidth: true
                fraction: root.fraction
                tone: Theme.accent
                bead: true
                thickness: 4
                implicitHeight: 8
            }
        }

        // Everything here keeps its place: the button is pinned to the right
        // with one width for Pause and Resume, and each figure has its own
        // column that shortens its text instead of pushing the others.
        Item {
            Layout.fillWidth: true
            implicitHeight: 44

            PauseButton {
                id: pauseButton
                visible: !(root.busy && root.batchTotal > 0)
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
            }

            Row {
                anchors.left: parent.left
                anchors.right: pauseButton.visible ? pauseButton.left : parent.right
                anchors.rightMargin: pauseButton.visible ? 12 : 0
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12

                Figure {
                    width: Math.min(96, (parent.width - parent.spacing) * 0.4)
                    label: qsTr("Speed")
                    value: root.speed >= 1 ? app.queue.bytesText(root.speed) + "/s" : "—"
                }
                Figure {
                    width: parent.width - parent.spacing - Math.min(96, (parent.width - parent.spacing) * 0.4)
                    label: qsTr("Left")
                    value: !root.busy || root.batchTotal <= 0 ? "—"
                           : qsTr("%1 of %2").arg(app.queue.bytesText(Math.max(0, root.batchTotal - root.batchDone)))
                                             .arg(app.queue.bytesText(root.batchTotal))
                }
            }
        }
        }
        }
    }

    // Where a conversion is on its way: the step under way, its text, the
    // bars of every step and whether it is over.
    function journey(c) {
        var t = c.transfer || {}
        var stage = c.state === "done" ? (t.stage || "converted") : c.state
        var pct = function (v) { return Math.floor(v || 0) }
        var text = ""
        switch (stage) {
        case "waiting": text = qsTr("Waiting to convert"); break
        case "converting":
            text = c.stage === "fetch" ? qsTr("Bringing it from the console… %1%").arg(pct(c.percent))
                 : c.stage === "download" ? qsTr("Downloading the emulator files… %1%").arg(pct(c.percent))
                 : c.stage === "unpack" ? qsTr("Unpacking the emulator files… %1%").arg(pct(c.percent))
                 : c.stage === "cover" ? qsTr("Getting the cover…")
                 : c.stage === "digest" || c.stage === "finish"
                 ? qsTr("Signing… %1%").arg(pct(c.percent))
                 : c.kind === "pack" ? qsTr("Packing… %1%").arg(pct(c.percent))
                 : qsTr("Converting… %1%").arg(pct(c.percent)); break
        case "converted": text = !c.send ? qsTr("Package ready")
                                : c.kind === "pack" ? qsTr("Packed — sending next") : qsTr("Converted — sending next"); break
        case "sending":
            text = t.waiting ? qsTr("Waiting to send") : qsTr("Sending to the console… %1%").arg(pct(t.percent)); break
        case "sent": text = qsTr("On the console"); break
        case "installing":
            text = t.waiting ? qsTr("Waiting to install") : qsTr("Installing… %1%").arg(pct(t.percent)); break
        case "installed": text = qsTr("Installed"); break
        case "cancelled": text = c.interrupted ? qsTr("Stopped when the app closed") : qsTr("Cancelled"); break
        default: text = qsTr("Failed")
        }
        // A step's bar is full once a later step has started.
        var order = ["converting", "sending", "installing"]
        var reached = stage === "waiting" || stage === "converting" ? 0
                    : stage === "converted" || stage === "sending" ? 1
                    : stage === "sent" || stage === "installing" ? 2
                    : stage === "installed" ? 3 : -1
        var failedAt = c.state === "error" ? 0 : c.state === "cancelled" ? 0
                     : (t.stage === "error" || t.stage === "cancelled") ? (t.mode === "install" ? 2 : 1) : -1
        var labels = [c.kind === "pack" ? qsTr("Pack") : qsTr("Convert"), qsTr("Send"), qsTr("Install")]
        // Over, and well: the package made (and no more asked), on the
        // console, or installed. The card goes green.
        var done = stage === "installed" || stage === "sent" || (stage === "converted" && !c.send)
        var tones = done ? [Theme.ok, Theme.ok, Theme.ok] : [Theme.warn, Theme.accent, Theme.ok]
        var steps = []
        // Only the steps asked for: making it, sending it, installing it.
        var count = c.install || stage === "installing" || stage === "installed" ? 3
                  : c.send || stage === "sending" || stage === "sent" ? 2 : 1
        for (var i = 0; i < count; ++i) {
            var p = 0
            if (i < reached)
                p = 100
            else if (i === reached && i === 0)
                p = c.percent || 0
            else if (i === reached && stage === order[i])
                p = t.waiting ? 0 : (t.percent || 0)
            steps.push({ label: labels[i], percent: p,
                         tone: i === failedAt ? Theme.error : tones[i],
                         current: !done && (i === reached || i === failedAt) })
        }
        var finished = done || stage === "error" || stage === "cancelled"
        // How fast the step under way goes, where it moves bytes: from the
        // console, the emulator files, the image, the upload, the install.
        var moving = stage === "converting" || ((stage === "sending" || stage === "installing") && !t.waiting)
        var speed = !moving ? "" : stage === "converting" ? (c.speedText || "") : (t.speedText || "")
        var eta = !moving ? "" : stage === "converting" ? (c.etaText || "") : (t.etaText || "")
        var speedIcon = stage === "sending" ? "upload" : stage === "installing" ? "package"
                      : c.stage === "fetch" || c.stage === "download" ? "download" : "cpu"
        // Converted and on the way to the queue: it waits its turn there.
        var waiting = stage === "waiting" || t.waiting === true || (stage === "converted" && !!c.send)
        return { stage: done ? "done" : stage === "converted" && c.send ? "sending" : stage,
                 percent: stage === "converting" ? (c.percent || 0) : (t.percent || 0),
                 waiting: waiting,
                 text: text, steps: steps, finished: finished, done: done,
                 speed: speed, eta: eta, speedIcon: speedIcon,
                 taskId: t.taskId || "",
                 message: c.message && c.message.length > 0 ? c.message : (stage === "error" ? (t.message || "") : "") }
    }
}
