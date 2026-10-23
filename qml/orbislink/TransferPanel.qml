// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The installs panel: every job of the app as one list of cards — the
// queue's packages, the PS1/PS2 conversions (one card each from the disc to
// the console), the homebrew store's installs, the console library's jobs
// and the copies to this PC. What is under way comes first, lit, then what
// waits its turn, then what is over; the footer adds them all up.
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Paused queue notice.
        Rectangle {
            Layout.fillWidth: true
            Layout.bottomMargin: 8
            visible: app.queuePaused
            radius: Theme.radiusSmall
            color: Theme.alpha(Theme.warn, 0.12)
            border.color: Theme.alpha(Theme.warn, 0.4)
            implicitHeight: pauseRow.implicitHeight + 18

            RowLayout {
                id: pauseRow
                anchors.fill: parent
                anchors.margins: 9
                spacing: 8

                Icon {
                    name: app.waitingForInstaller ? "refresh" : "pause"
                    spinning: app.waitingForInstaller
                    size: 16
                    color: Theme.warn
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
                    font.pixelSize: 12
                }
                StyledButton {
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
                contentHeight: column.implicitHeight + 10
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }

                // The cards in a column: placed again whenever one changes size or
                // place — a card that moves slides there.
                Column {
                    id: column
                    // Room for the lit cards' halo.
                    x: 4
                    y: 3
                    width: list.width - 8
                    spacing: 10
                    move: Transition { NumberAnimation { properties: "y"; duration: 260; easing.type: Easing.OutCubic } }

                    Repeater {
                        model: cards

                        delegate: Item {
                            id: entry
                            required property string key
                            required property string kind
                            required property string section
                            required property bool head
                            readonly property var d: root.cardData[key] || ({})
                            readonly property bool lit: section === "running"
                            width: column.width
                            height: (head ? header.height : 0) + (body.item ? body.item.implicitHeight : 0)
                            // A new card fades in.
                            opacity: 0
                            Behavior on opacity { NumberAnimation { duration: 220 } }
                            Component.onCompleted: opacity = 1

                            // The first card of a section carries its title: under way,
                            // waiting, finished (with the button that clears them).
                            Item {
                                id: header
                                visible: entry.head
                                width: parent.width
                                height: 34

                                RowLayout {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 8
                                    anchors.leftMargin: 2
                                    spacing: 8

                                    // Under way: a dot that breathes.
                                    Rectangle {
                                        width: 7
                                        height: 7
                                        radius: 3.5
                                        color: entry.lit ? Theme.accent
                                             : entry.section === "waiting" ? Theme.textSecondary : Theme.ok
                                        SequentialAnimation on opacity {
                                            running: entry.lit && entry.head && entry.visible
                                            loops: Animation.Infinite
                                            NumberAnimation { from: 1; to: 0.3; duration: 800; easing.type: Easing.InOutSine }
                                            NumberAnimation { from: 0.3; to: 1; duration: 800; easing.type: Easing.InOutSine }
                                        }
                                    }
                                    Text {
                                        text: (entry.section === "running" ? qsTr("Under way")
                                              : entry.section === "waiting" ? qsTr("Waiting") : qsTr("Finished")).toUpperCase()
                                        color: entry.lit ? Theme.accent : Theme.textSecondary
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.8
                                    }
                                    Text {
                                        text: entry.section === "running" ? root.runningCount
                                            : entry.section === "waiting" ? root.waitingCount : root.finishedCount
                                        color: Theme.textMuted
                                        font.pixelSize: 11
                                    }
                                    Item { Layout.fillWidth: true }
                                    StyledButton {
                                        visible: entry.section === "finished"
                                        chip: true
                                        text: qsTr("Clear finished")
                                        onClicked: root.clearFinished()
                                    }
                                }
                            }

                            // Under way: a soft halo around the card.
                            Repeater {
                                model: entry.lit ? 2 : 0
                                Rectangle {
                                    required property int index
                                    anchors.fill: body
                                    anchors.margins: -(index + 1) * 2
                                    radius: 16 + (index + 1) * 2
                                    color: "transparent"
                                    border.width: 2
                                    border.color: Theme.alpha(Theme.accent, index === 0 ? 0.22 : 0.09)
                                }
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
                                Rectangle {
                                    readonly property var r: entry.d.r || ({})
                                    radius: 16
                                    color: taskHover.hovered ? Qt.lighter(Theme.panelAltFill, Theme.light ? 1.0 : 1.15)
                                                             : Theme.panelAltFill
                                    border.width: 1
                                    border.color: entry.lit ? Theme.alpha(Theme.accent, 0.7)
                                                : taskHover.hovered ? Theme.alpha(Theme.accent, 0.35) : Theme.border
                                    Behavior on color { ColorAnimation { duration: Theme.fast } }
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
                                            spacing: 10

                                            Rectangle {
                                                width: Theme.gameIcon; height: Theme.gameIcon; radius: 8
                                                color: Theme.controlFill
                                                border.color: Theme.border
                                                CoverArt {
                                                    anchors.fill: parent
                                                    visible: ready
                                                    crop: true
                                                    edge: false
                                                    radius: parent.radius
                                                    picture: r.iconSource ? r.iconSource : ""
                                                }
                                                Icon {
                                                    anchors.centerIn: parent
                                                    visible: !r.iconSource
                                                    name: "package"
                                                    size: 22
                                                    color: Theme.textSecondary
                                                }
                                                Rectangle {
                                                    anchors.fill: taskStage
                                                    anchors.margins: -2
                                                    radius: width / 2
                                                    color: Theme.panelFill
                                                }
                                                StageLoader {
                                                    id: taskStage
                                                    stage: entry.d.thumb ? entry.d.thumb.stage : ""
                                                    percent: r.percent || 0
                                                    waiting: r.state === "pending" || r.state === "validating"
                                                    width: 22
                                                    height: 22
                                                    anchors.right: parent.right
                                                    anchors.bottom: parent.bottom
                                                    anchors.rightMargin: -5
                                                    anchors.bottomMargin: -5
                                                }
                                            }

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: r.title || ""
                                                    color: Theme.text
                                                    font.pixelSize: 14
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: [r.titleId, r.category, r.sizeText,
                                                           r.firmware ? qsTr("FW %1").arg(r.firmware) : ""].filter(function (part) {
                                                        return part && part.length > 0
                                                    }).join("  ·  ")
                                                    color: Theme.textMuted
                                                    font.pixelSize: 11
                                                    elide: Text.ElideRight
                                                }
                                                // An official package, or a firmware above the PS4's.
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    visible: !!r.warning
                                                    spacing: 5
                                                    Icon {
                                                        Layout.alignment: Qt.AlignTop
                                                        Layout.topMargin: 1
                                                        name: "warning"
                                                        size: 12
                                                        color: Theme.warn
                                                    }
                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: r.warning ? r.warning : ""
                                                        color: Theme.warn
                                                        font.pixelSize: 11
                                                        wrapMode: Text.WordWrap
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                radius: 11
                                                color: Theme.alpha(Theme.taskColor(r.state || ""), 0.12)
                                                border.color: Theme.alpha(Theme.taskColor(r.state || ""), 0.35)
                                                implicitWidth: stateText.implicitWidth + 18
                                                implicitHeight: 22
                                                Text {
                                                    id: stateText
                                                    anchors.centerIn: parent
                                                    text: r.stateLabel || ""
                                                    color: Theme.taskColor(r.state || "")
                                                    font.pixelSize: 11
                                                    font.weight: Font.Medium
                                                }
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            height: 8
                                            radius: 4
                                            color: Theme.controlFill
                                            Rectangle {
                                                width: parent.width * Math.max(0, Math.min(1, (r.percent || 0) / 100))
                                                height: parent.height
                                                radius: 4
                                                color: Theme.taskColor(r.state || "")
                                                Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10

                                            Text {
                                                text: (r.percent || 0).toFixed(1) + "%"
                                                color: Theme.textMuted
                                                font.pixelSize: 11
                                            }
                                            Text {
                                                text: "↓ " + (r.speedText || "")
                                                color: Theme.textMuted
                                                font.pixelSize: 11
                                                visible: r.active === true
                                            }
                                            Text {
                                                text: "⏱ " + (r.etaText || "")
                                                color: Theme.textMuted
                                                font.pixelSize: 11
                                                visible: r.active === true
                                            }
                                            Item { Layout.fillWidth: true }

                                            StyledToolButton {
                                                iconName: "arrow-up"
                                                iconSize: 14
                                                implicitWidth: 28; implicitHeight: 28
                                                onClicked: app.moveTaskUp(r.taskId)
                                                visible: r.state === "pending" && (r.canMoveUp || r.canMoveDown)
                                                enabled: r.canMoveUp === true
                                            }
                                            StyledToolButton {
                                                iconName: "arrow-down"
                                                iconSize: 14
                                                implicitWidth: 28; implicitHeight: 28
                                                onClicked: app.moveTaskDown(r.taskId)
                                                visible: r.state === "pending" && (r.canMoveUp || r.canMoveDown)
                                                enabled: r.canMoveDown === true
                                            }
                                            StyledToolButton {
                                                iconName: "rotate-ccw"
                                                iconSize: 14
                                                implicitWidth: 28; implicitHeight: 28
                                                onClicked: app.retryTask(r.taskId)
                                                visible: r.state === "error" || r.state === "cancelled"
                                            }
                                            StyledToolButton {
                                                iconName: "close"
                                                iconSize: 14
                                                implicitWidth: 28; implicitHeight: 28
                                                onClicked: r.state === "pending" || r.active
                                                           ? app.cancelTask(r.taskId)
                                                           : app.removeTask(r.taskId)
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: r.message || ""
                                            color: r.state === "error" ? Theme.error : Theme.textMuted
                                            font.pixelSize: 11
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
                                Rectangle {
                                    readonly property var c: entry.d.c || ({})
                                    readonly property var j: entry.d.j || ({ steps: [], text: "", stage: "", speed: "", eta: "",
                                                                             message: "", taskId: "", finished: false })
                                    radius: 16
                                    color: Theme.alpha(loader.tone, Theme.light ? 0.06 : 0.08)
                                    border.width: 1
                                    border.color: Theme.alpha(loader.tone, entry.lit ? 0.75 : 0.45)
                                    Behavior on border.color { ColorAnimation { duration: Theme.normal } }
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
                                            spacing: 10
                                            // The game's cover, with the step under way in its corner.
                                            Item {
                                                implicitWidth: Theme.gameIcon
                                                implicitHeight: Theme.gameIcon
                                                CoverArt {
                                                    id: convCover
                                                    anchors.fill: parent
                                                    visible: ready
                                                    crop: true
                                                    radius: 8
                                                    picture: entry.d.thumb ? entry.d.thumb.picture : ""
                                                }
                                                // No cover: the disc stands in, the step beside it.
                                                Rectangle {
                                                    anchors.fill: parent
                                                    visible: !convCover.ready
                                                    radius: 8
                                                    color: Theme.controlFill
                                                    border.color: Theme.border
                                                    Icon {
                                                        anchors.centerIn: parent
                                                        name: c.kind === "pack" ? "package" : "disc"
                                                        size: 22
                                                        color: Theme.textSecondary
                                                    }
                                                }
                                                Rectangle {
                                                    anchors.fill: loader
                                                    anchors.margins: -2
                                                    radius: width / 2
                                                    color: Theme.panelFill
                                                }
                                                // One size whether the cover is there or not: it
                                                // does not jump while a cover loads.
                                                StageLoader {
                                                    id: loader
                                                    stage: j.stage || ""
                                                    percent: j.percent || 0
                                                    waiting: j.waiting === true
                                                    width: 22
                                                    height: 22
                                                    anchors.right: parent.right
                                                    anchors.bottom: parent.bottom
                                                    anchors.rightMargin: -5
                                                    anchors.bottomMargin: -5
                                                }
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: c.title || ""
                                                    color: Theme.text
                                                    font.pixelSize: 14
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: (c.platform === "ps2" ? "PS2" : c.platform === "ps1" ? "PS1" : "PS4")
                                                          + "  ·  " + (j.text || "")
                                                    color: j.stage === "error" ? Theme.error : Theme.textSecondary
                                                    font.pixelSize: 11
                                                    elide: Text.ElideRight
                                                }
                                            }
                                            StyledToolButton {
                                                // The conversion itself again, or the send or install that failed.
                                                readonly property bool again: c.state === "error" || c.state === "cancelled"
                                                visible: again || (j.stage === "error" && (j.taskId || "").length > 0)
                                                iconName: "rotate-ccw"
                                                iconSize: 15
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Try again")
                                                onClicked: again ? games.retryConversion(c.id) : app.retryTask(j.taskId)
                                            }
                                            StyledToolButton {
                                                visible: c.state === "done"
                                                iconName: "folder-open"
                                                iconSize: 15
                                                ToolTip.visible: hovered
                                                ToolTip.text: qsTr("Open the folder")
                                                onClicked: games.openOutputFolder()
                                            }
                                            StyledToolButton {
                                                iconName: "close"
                                                iconSize: 15
                                                danger: !j.finished
                                                ToolTip.visible: hovered
                                                ToolTip.text: j.finished ? qsTr("Remove") : qsTr("Cancel")
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
                                            spacing: 6
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
                                                    Rectangle {
                                                        Layout.fillWidth: true
                                                        height: 6
                                                        radius: 3
                                                        color: Theme.alpha(step.tone, 0.16)
                                                        Rectangle {
                                                            width: parent.width * Math.max(0, Math.min(1, step.percent / 100))
                                                            height: parent.height
                                                            radius: parent.radius
                                                            color: step.tone
                                                            Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
                                                        }
                                                    }
                                                    Text {
                                                        Layout.fillWidth: true
                                                        // The step under way says how far it is.
                                                        text: step.current && step.percent > 0 && step.percent < 100
                                                              ? step.label + "  ·  " + Math.floor(step.percent) + "%"
                                                              : step.label
                                                        color: step.current ? Theme.text : Theme.textMuted
                                                        font.pixelSize: 10
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
                                                color: Theme.textMuted
                                            }
                                            Text {
                                                text: j.speed || ""
                                                color: Theme.textSecondary
                                                font.pixelSize: 11
                                            }
                                            Item { Layout.fillWidth: true }
                                            Icon {
                                                visible: (j.eta || "").length > 0
                                                name: "history"
                                                size: 12
                                                color: Theme.textMuted
                                            }
                                            Text {
                                                visible: (j.eta || "").length > 0
                                                text: qsTr("%1 left").arg(j.eta || "")
                                                color: Theme.textSecondary
                                                font.pixelSize: 11
                                            }
                                        }

                                        Text {
                                            visible: (j.message || "").length > 0
                                            Layout.fillWidth: true
                                            text: j.message || ""
                                            color: Theme.error
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: 11
                                        }
                                        // What became of the package made.
                                        Text {
                                            visible: (c.note || "").length > 0
                                            Layout.fillWidth: true
                                            text: c.note || ""
                                            color: Theme.textMuted
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: 11
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
                                                     : job.kind === "download" ? qsTr("In the queue") : qsTr("Installed"))
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

            // Empty
            Item {
                anchors.centerIn: parent
                width: parent.width - 40
                visible: cards.count === 0
                implicitHeight: emptyColumn.implicitHeight

                ColumnLayout {
                    id: emptyColumn
                    width: parent.width
                    spacing: 10
                    Icon {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.bottomMargin: 8
                        name: "download"
                        size: 36
                        strokeWidth: 1.5
                        color: Theme.textSecondary
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("The queue is empty")
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Drag .pkg files onto the window.")
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                }
            }
        }

        // ── footer: the whole batch ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        // How far the batch is, all of it: a bar, its percentage and the
        // time left.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 10
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            visible: root.busy && root.batchTotal > 0
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: Math.floor(root.fraction * 100) + "%"
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("%n job(s) to go", "", root.runningCount + root.waitingCount)
                    color: Theme.textMuted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Icon {
                    visible: root.secondsLeft > 0
                    name: "history"
                    size: 12
                    color: Theme.textMuted
                }
                Text {
                    text: root.secondsLeft > 0 ? qsTr("about %1 left").arg(app.queue.durationText(root.secondsLeft))
                                               : qsTr("working out the time…")
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 6
                radius: 3
                color: Theme.alpha(Theme.accent, 0.16)
                Rectangle {
                    width: parent.width * root.fraction
                    height: parent.height
                    radius: parent.radius
                    color: Theme.accent
                    Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
                }
            }
        }

        // Everything here keeps its place: the button is pinned to the right
        // with one width for Pause and Resume, and each figure has its own
        // column that shortens its text instead of pushing the others.
        Item {
            Layout.fillWidth: true
            implicitHeight: 54

            StyledButton {
                id: pauseButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: 3
                chip: true
                minimumWidth: Math.max(pauseWidth.implicitWidth, resumeWidth.implicitWidth) + 14 + 8 + 24
                iconName: app.queuePaused ? "play" : "pause"
                text: app.queuePaused ? qsTr("Resume") : qsTr("Pause")
                onClicked: app.queuePaused ? app.resumeQueue() : app.pauseQueue()
                enabled: app.queue.count > 0
                // The widest of the two labels sets the width.
                Text { id: pauseWidth; visible: false; text: qsTr("Pause"); font: pauseButton.font }
                Text { id: resumeWidth; visible: false; text: qsTr("Resume"); font: pauseButton.font }
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.right: pauseButton.left
                anchors.rightMargin: 12
                anchors.verticalCenter: pauseButton.verticalCenter
                spacing: 12

                component Figure: Column {
                    property alias label: figureLabel.text
                    property alias value: figureValue.text
                    spacing: 1
                    Text {
                        id: figureLabel
                        width: parent.width
                        color: Theme.textMuted
                        font.pixelSize: 10
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        id: figureValue
                        width: parent.width
                        color: Theme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }

                Figure {
                    width: Math.min(84, (parent.width - parent.spacing) * 0.4)
                    label: qsTr("Speed")
                    value: root.speed >= 1 ? app.queue.bytesText(root.speed) + "/s" : "—"
                }
                Figure {
                    width: parent.width - parent.spacing - Math.min(84, (parent.width - parent.spacing) * 0.4)
                    label: qsTr("Left")
                    value: !root.busy || root.batchTotal <= 0 ? "—"
                           : qsTr("%1 of %2").arg(app.queue.bytesText(Math.max(0, root.batchTotal - root.batchDone)))
                                             .arg(app.queue.bytesText(root.batchTotal))
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
