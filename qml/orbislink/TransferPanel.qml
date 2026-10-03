// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    // How many cards the queue shows (a converted game is one card).
    readonly property int cardCount: {
        app.queue.totalText  // re-read on every queue change
        return games.conversions.length + app.queue.shownCount(list.followed)
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
                    text: app.waitingForInstaller
                          ? qsTr("Waiting for Remote Package Installer: open it on the console and the "
                                 + "install starts by itself.")
                          : app.pauseReason.length > 0 ? app.pauseReason : qsTr("Queue paused.")
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

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 0
            model: app.queue

            ScrollBar.vertical: ScrollBar { }

            // The packages a conversion card already follows.
            readonly property var followed: {
                var keys = {}
                for (var i = 0; i < games.conversions.length; ++i) {
                    var k = games.conversions[i].pkgKey
                    if (k && k.length > 0)
                        keys[k] = true
                }
                return keys
            }

            header: conversionsHeader

            delegate: Item {
                id: taskSlot
                width: list.width
                readonly property bool followed: list.followed[model.localKey] === true || model.superseded
                visible: !followed
                height: followed ? 0 : taskCard.implicitHeight + 10

            Rectangle {
                id: taskCard
                width: list.width
                radius: 16
                color: taskHover.hovered ? Qt.lighter(Theme.panelAltFill, Theme.light ? 1.0 : 1.15)
                                         : Theme.panelAltFill
                border.color: model.active ? Theme.alpha(Theme.accent, 0.6)
                            : taskHover.hovered ? Theme.alpha(Theme.accent, 0.35) : Theme.border
                Behavior on color { ColorAnimation { duration: Theme.fast } }
                HoverHandler { id: taskHover }
                border.width: 1
                implicitHeight: content.implicitHeight + 24

                ColumnLayout {
                    id: content
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            width: 44; height: 44; radius: 10
                            color: Theme.controlFill
                            border.color: Theme.border
                            clip: true
                            Image {
                                anchors.fill: parent
                                anchors.margins: 1
                                source: model.iconSource ? model.iconSource : ""
                                fillMode: Image.PreserveAspectCrop
                                visible: source != ""
                            }
                            Icon {
                                anchors.centerIn: parent
                                visible: !model.iconSource
                                name: "package"
                                size: 22
                                color: Theme.textSecondary
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                text: model.title
                                color: Theme.text
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: [model.titleId, model.category, model.sizeText].filter(function (part) {
                                    return part && part.length > 0
                                }).join("  ·  ")
                                color: Theme.textMuted
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }

                        Rectangle {
                            radius: 11
                            color: Theme.alpha(Theme.taskColor(model.state), 0.12)
                            border.color: Theme.alpha(Theme.taskColor(model.state), 0.35)
                            implicitWidth: stateText.implicitWidth + 18
                            implicitHeight: 22
                            Text {
                                id: stateText
                                anchors.centerIn: parent
                                text: model.stateLabel
                                color: Theme.taskColor(model.state)
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
                            width: parent.width * Math.max(0, Math.min(1, model.percent / 100))
                            height: parent.height
                            radius: 4
                            color: Theme.taskColor(model.state)
                            Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: model.percent.toFixed(1) + "%"
                            color: Theme.textMuted
                            font.pixelSize: 11
                        }
                        Text {
                            text: "↓ " + model.speedText
                            color: Theme.textMuted
                            font.pixelSize: 11
                            visible: model.active
                        }
                        Text {
                            text: "⏱ " + model.etaText
                            color: Theme.textMuted
                            font.pixelSize: 11
                            visible: model.active
                        }
                        Item { Layout.fillWidth: true }

                        StyledToolButton {
                            iconName: "arrow-up"
                            iconSize: 14
                            implicitWidth: 28; implicitHeight: 28
                            onClicked: app.moveTaskUp(model.taskId)
                            visible: model.state === "pending"
                        }
                        StyledToolButton {
                            iconName: "arrow-down"
                            iconSize: 14
                            implicitWidth: 28; implicitHeight: 28
                            onClicked: app.moveTaskDown(model.taskId)
                            visible: model.state === "pending"
                        }
                        StyledToolButton {
                            iconName: "rotate-ccw"
                            iconSize: 14
                            implicitWidth: 28; implicitHeight: 28
                            onClicked: app.retryTask(model.taskId)
                            visible: model.state === "error" || model.state === "cancelled"
                        }
                        StyledToolButton {
                            iconName: "close"
                            iconSize: 14
                            implicitWidth: 28; implicitHeight: 28
                            onClicked: model.state === "pending" || model.active
                                       ? app.cancelTask(model.taskId)
                                       : app.removeTask(model.taskId)
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: model.message
                        color: model.state === "error" ? Theme.error : Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        visible: model.message.length > 0
                    }
                }
            }
            }

            // Empty
            Item {
                anchors.centerIn: parent
                width: parent.width - 40
                visible: games.conversions.length === 0 && (list.count === 0 || list.contentHeight < 2)
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

        // Footer with the summary.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
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
                    value: app.queue.totalSpeedText
                }
                Figure {
                    width: parent.width - parent.spacing - Math.min(84, (parent.width - parent.spacing) * 0.4)
                    label: qsTr("Left")
                    value: app.queue.totalText === "—" ? app.queue.remainingText
                           : qsTr("%1 of %2").arg(app.queue.remainingText).arg(app.queue.totalText)
                }
            }
        }
    }

    // PS1/PS2 conversions, one card each from the disc to the console: the
    // conversion, the upload and the install, each with its own bar. The
    // queue's own cards for that package are not shown twice.
    Component {
        id: conversionsHeader
        ColumnLayout {
            width: list.width
            spacing: 10
            visible: games.conversions.length > 0
            height: visible ? implicitHeight + 10 : 0

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("PS1/PS2 games")
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
                StyledButton {
                    chip: true
                    visible: {
                        for (var i = 0; i < games.conversions.length; ++i)
                            if (root.journey(games.conversions[i]).finished)
                                return true
                        return false
                    }
                    text: qsTr("Clear finished")
                    onClicked: games.clearFinishedConversions()
                }
            }

            Repeater {
                // By position, not by value: every progress tick hands over a
                // new list, and a value model would make each card (and its
                // bars) again from zero — the bars would flash.
                model: games.conversions.length
                delegate: Rectangle {
                    id: conv
                    required property int index
                    readonly property var modelData: games.conversions[index] || ({})
                    readonly property var j: root.journey(modelData)
                    Layout.fillWidth: true
                    radius: 16
                    color: Theme.alpha(loader.tone, Theme.light ? 0.06 : 0.08)
                    border.width: 1
                    border.color: Theme.alpha(loader.tone, 0.45)
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
                            StageLoader {
                                id: loader
                                stage: conv.j.stage
                                percent: conv.j.percent
                                waiting: conv.j.waiting
                                implicitWidth: 44
                                implicitHeight: 44
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: conv.modelData.title
                                    color: Theme.text
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: (conv.modelData.platform === "ps2" ? "PS2" : "PS1") + "  ·  " + conv.j.text
                                    color: conv.j.stage === "error" ? Theme.error : Theme.textSecondary
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                            StyledToolButton {
                                visible: conv.j.stage === "error" && conv.j.taskId.length > 0
                                iconName: "rotate-ccw"
                                iconSize: 15
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Try again")
                                onClicked: app.retryTask(conv.j.taskId)
                            }
                            StyledToolButton {
                                visible: conv.modelData.state === "done"
                                iconName: "folder-open"
                                iconSize: 15
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Open the folder")
                                onClicked: games.openOutputFolder()
                            }
                            StyledToolButton {
                                iconName: "close"
                                iconSize: 15
                                danger: !conv.j.finished
                                ToolTip.visible: hovered
                                ToolTip.text: conv.j.finished ? qsTr("Remove") : qsTr("Cancel")
                                onClicked: {
                                    if (conv.modelData.state === "waiting" || conv.modelData.state === "converting")
                                        games.cancelConversion(conv.modelData.id)
                                    else if (!conv.j.finished && conv.j.taskId.length > 0)
                                        app.cancelTask(conv.j.taskId)
                                    else
                                        games.removeConversion(conv.modelData.id)
                                }
                            }
                        }

                        // The steps, side by side: convert, send, install.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: conv.j.steps.length
                                ColumnLayout {
                                    required property int index
                                    readonly property var modelData: conv.j.steps[index]
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    spacing: 4
                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 6
                                        radius: 3
                                        color: Theme.alpha(modelData.tone, 0.16)
                                        Rectangle {
                                            width: parent.width * Math.max(0, Math.min(1, modelData.percent / 100))
                                            height: parent.height
                                            radius: parent.radius
                                            color: modelData.tone
                                            Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        // The step under way says how far it is.
                                        text: modelData.current && modelData.percent > 0 && modelData.percent < 100
                                              ? modelData.label + "  ·  " + Math.floor(modelData.percent) + "%"
                                              : modelData.label
                                        color: modelData.current ? Theme.text : Theme.textMuted
                                        font.pixelSize: 10
                                        font.weight: modelData.current ? Font.DemiBold : Font.Normal
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }

                        Text {
                            visible: conv.j.message.length > 0
                            Layout.fillWidth: true
                            text: conv.j.message
                            color: Theme.error
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
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
            text = c.stage === "download" ? qsTr("Downloading the emulator files… %1%").arg(pct(c.percent))
                 : c.stage === "unpack" ? qsTr("Unpacking the emulator files… %1%").arg(pct(c.percent))
                 : c.stage === "cover" ? qsTr("Getting the cover…")
                 : c.stage === "digest" || c.stage === "finish"
                 ? qsTr("Signing… %1%").arg(pct(c.percent))
                 : qsTr("Converting… %1%").arg(pct(c.percent)); break
        case "converted": text = c.install ? qsTr("Converted — sending next") : qsTr("Package ready"); break
        case "sending":
            text = t.waiting ? qsTr("Waiting to send") : qsTr("Sending to the console… %1%").arg(pct(t.percent)); break
        case "sent": text = qsTr("On the console"); break
        case "installing":
            text = t.waiting ? qsTr("Waiting to install") : qsTr("Installing… %1%").arg(pct(t.percent)); break
        case "installed": text = qsTr("Installed"); break
        case "cancelled": text = qsTr("Cancelled"); break
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
        var labels = [qsTr("Convert"), qsTr("Send"), qsTr("Install")]
        var tones = [Theme.warn, Theme.accent, Theme.ok]
        var steps = []
        var count = c.install || stage === "installing" || stage === "installed" ? 3 : 2
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
                         current: i === reached || i === failedAt })
        }
        var finished = stage === "installed" || stage === "error" || stage === "cancelled"
                     || (stage === "converted" && !c.install)
                     || stage === "sent"
        return { stage: stage === "converted" ? "converted" : stage,
                 percent: stage === "converting" ? (c.percent || 0) : (t.percent || 0),
                 waiting: stage === "waiting" || t.waiting === true,
                 text: text, steps: steps, finished: finished,
                 taskId: t.taskId || "",
                 message: c.message && c.message.length > 0 ? c.message : (stage === "error" ? (t.message || "") : "") }
    }
}
