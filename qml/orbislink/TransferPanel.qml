// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

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
                    name: "pause"
                    size: 16
                    color: Theme.warn
                }
                Text {
                    Layout.fillWidth: true
                    text: app.pauseReason.length > 0 ? app.pauseReason : qsTr("Queue paused.")
                    color: Theme.text
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                }
                StyledButton {
                    text: qsTr("Resume")
                    onClicked: app.resumeQueue()
                }
            }
        }

        // PS1/PS2 conversions: above the transfers and drawn apart from them,
        // in amber, while the package is being made. One meant for installing
        // then shows up below as an ordinary install.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: games.conversions.length > 0 ? 10 : 0
            visible: games.conversions.length > 0
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: qsTr("Conversions")
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
                StyledButton {
                    chip: true
                    visible: {
                        for (var i = 0; i < games.conversions.length; ++i) {
                            var s = games.conversions[i].state
                            if (s !== "waiting" && s !== "converting")
                                return true
                        }
                        return false
                    }
                    text: qsTr("Clear finished")
                    onClicked: games.clearFinishedConversions()
                }
            }

            Repeater {
                model: games.conversions
                delegate: Rectangle {
                    id: conv
                    required property var modelData
                    readonly property bool working: modelData.state === "converting"
                    readonly property bool waiting: modelData.state === "waiting"
                    readonly property color tone: modelData.state === "error" ? Theme.error
                                                : modelData.state === "done" ? Theme.ok
                                                : modelData.state === "cancelled" ? Theme.textSecondary
                                                : Theme.warn
                    Layout.fillWidth: true
                    radius: 16
                    color: Theme.alpha(tone, Theme.light ? 0.07 : 0.09)
                    border.width: 1
                    border.color: Theme.alpha(tone, 0.45)
                    implicitHeight: convColumn.implicitHeight + 24

                    ColumnLayout {
                        id: convColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Rectangle {
                                width: 44; height: 44; radius: 10
                                color: Theme.alpha(conv.tone, 0.14)
                                Icon {
                                    anchors.centerIn: parent
                                    name: conv.modelData.state === "done" ? "check" : "disc"
                                    spinning: conv.working
                                    size: 22
                                    color: conv.tone
                                }
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
                                    text: {
                                        var d = conv.modelData
                                        var p = (d.platform === "ps2" ? "PS2" : "PS1") + "  ·  "
                                        if (d.state === "waiting")
                                            return p + qsTr("Waiting to convert")
                                        if (d.state === "converting")
                                            return p + (d.stage === "digest" || d.stage === "finish"
                                                        ? qsTr("Signing… %1%").arg(Math.floor(d.percent))
                                                        : qsTr("Converting… %1%").arg(Math.floor(d.percent)))
                                        if (d.state === "done")
                                            return p + (d.install ? qsTr("Ready — installing")
                                                                  : qsTr("Package ready"))
                                        if (d.state === "cancelled")
                                            return p + qsTr("Cancelled")
                                        return p + qsTr("Failed")
                                    }
                                    color: conv.modelData.state === "error" ? Theme.error : Theme.textSecondary
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                            StyledToolButton {
                                visible: conv.working || conv.waiting
                                iconName: "close"
                                iconSize: 15
                                danger: true
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Cancel")
                                onClicked: games.cancelConversion(conv.modelData.id)
                            }
                            StyledToolButton {
                                visible: conv.modelData.state === "done"
                                iconName: "folder-open"
                                iconSize: 15
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Open the folder")
                                onClicked: games.openOutputFolder()
                            }
                        }

                        // A bar with the amber of a conversion, not the blue of a transfer.
                        Rectangle {
                            visible: conv.working || conv.waiting
                            Layout.fillWidth: true
                            height: 4
                            radius: 2
                            color: Theme.alpha(conv.tone, 0.18)
                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1, conv.modelData.percent / 100))
                                height: parent.height
                                radius: parent.radius
                                color: conv.tone
                                Behavior on width { NumberAnimation { duration: 250 } }
                            }
                        }
                        Text {
                            visible: conv.modelData.message.length > 0
                            Layout.fillWidth: true
                            text: conv.modelData.message
                            color: Theme.error
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            model: app.queue

            ScrollBar.vertical: ScrollBar { }

            delegate: Rectangle {
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
                            Behavior on width { NumberAnimation { duration: 180 } }
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

            // Empty
            Item {
                anchors.centerIn: parent
                width: parent.width - 40
                visible: list.count === 0 && games.conversions.length === 0
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
        Item {
            Layout.fillWidth: true
            implicitHeight: 48

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 0
                anchors.topMargin: 8
                spacing: 14

                Text {
                    text: qsTr("Speed: %1").arg(app.queue.totalSpeedText)
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
                Text {
                    text: qsTr("Left: %1").arg(app.queue.remainingText)
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
                Item { Layout.fillWidth: true }
                StyledButton {
                    chip: true
                    iconName: app.queuePaused ? "play" : "pause"
                    text: app.queuePaused ? qsTr("Resume") : qsTr("Pause")
                    onClicked: app.queuePaused ? app.resumeQueue() : app.pauseQueue()
                    enabled: app.queue.count > 0
                }
            }
        }
    }
}
