// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Aviso de fila em pausa (§6.3)
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 10
            visible: app.queuePaused
            radius: 8
            color: "#3a2a12"
            border.color: Theme.warn
            implicitHeight: pauseRow.implicitHeight + 18

            RowLayout {
                id: pauseRow
                anchors.fill: parent
                anchors.margins: 9
                spacing: 8

                Text {
                    text: "⏸"
                    color: Theme.warn
                    font.pixelSize: 16
                }
                Text {
                    Layout.fillWidth: true
                    text: app.pauseReason.length > 0 ? app.pauseReason : qsTr("Fila em pausa.")
                    color: Theme.text
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                }
                StyledButton {
                    text: qsTr("Retomar")
                    onClicked: app.resumeQueue()
                }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 10
            clip: true
            spacing: 8
            model: app.queue

            ScrollBar.vertical: ScrollBar { }

            delegate: Rectangle {
                width: list.width
                radius: Theme.radius
                color: Theme.panelAltFill
                border.color: model.active ? Theme.accent : Theme.border
                border.width: model.active ? 1 : 1
                implicitHeight: content.implicitHeight + 20

                ColumnLayout {
                    id: content
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 7

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            width: 44; height: 44; radius: 6
                            color: Theme.panelFill
                            border.color: Theme.border
                            Image {
                                anchors.fill: parent
                                anchors.margins: 1
                                source: model.iconSource ? model.iconSource : ""
                                fillMode: Image.PreserveAspectCrop
                                visible: source != ""
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: !model.iconSource
                                text: "▣"
                                color: Theme.border
                                font.pixelSize: 20
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
                                font.bold: true
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
                            radius: 10
                            color: "transparent"
                            border.color: Theme.taskColor(model.state)
                            implicitWidth: stateText.implicitWidth + 16
                            implicitHeight: 20
                            Text {
                                id: stateText
                                anchors.centerIn: parent
                                text: model.stateLabel
                                color: Theme.taskColor(model.state)
                                font.pixelSize: 11
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 6
                        radius: 3
                        color: Theme.panelFill
                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(1, model.percent / 100))
                            height: parent.height
                            radius: 3
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
                            text: "▲"
                            implicitWidth: 26; implicitHeight: 22
                            onClicked: app.moveTaskUp(model.taskId)
                            visible: model.state === "pending"
                        }
                        StyledToolButton {
                            text: "▼"
                            implicitWidth: 26; implicitHeight: 22
                            onClicked: app.moveTaskDown(model.taskId)
                            visible: model.state === "pending"
                        }
                        StyledToolButton {
                            text: "↻"
                            implicitWidth: 26; implicitHeight: 22
                            onClicked: app.retryTask(model.taskId)
                            visible: model.state === "error" || model.state === "cancelled"
                        }
                        StyledToolButton {
                            text: "✕"
                            implicitWidth: 26; implicitHeight: 22
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

            // Estado vazio
            Item {
                anchors.centerIn: parent
                width: parent.width - 40
                visible: list.count === 0
                implicitHeight: emptyColumn.implicitHeight

                ColumnLayout {
                    id: emptyColumn
                    width: parent.width
                    spacing: 8
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "⤓"
                        color: Theme.border
                        font.pixelSize: 40
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("A fila está vazia")
                        color: Theme.textMuted
                        font.pixelSize: 13
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Arrasta ficheiros .pkg para a janela.")
                        color: Theme.border
                        font.pixelSize: 11
                    }
                }
            }
        }

        // Rodapé com o resumo (§5.7)
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 34
            color: Theme.panelFill
            border.color: Theme.border
            border.width: 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 12

                Text {
                    text: qsTr("Velocidade: %1").arg(app.queue.totalSpeedText)
                    color: Theme.textMuted
                    font.pixelSize: 11
                }
                Text {
                    text: qsTr("Em falta: %1").arg(app.queue.remainingText)
                    color: Theme.textMuted
                    font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
                StyledToolButton {
                    text: app.queuePaused ? qsTr("Retomar") : qsTr("Pausar")
                    onClicked: app.queuePaused ? app.resumeQueue() : app.pauseQueue()
                    enabled: app.queue.count > 0
                }
            }
        }
    }
}
