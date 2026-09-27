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

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            model: app.queue

            ScrollBar.vertical: ScrollBar { }

            delegate: Rectangle {
                width: list.width
                radius: 16
                color: Theme.panelAltFill
                border.color: model.active ? Theme.alpha(Theme.accent, 0.6) : Theme.border
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
                visible: list.count === 0
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
