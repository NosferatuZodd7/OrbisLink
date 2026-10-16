// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The installs panel folded away while jobs go on: a narrow column with the
// whole batch's progress on top and, under it, each job under way or
// waiting — its picture with the same ring as its card, moving as the card
// does. A click opens the panel again; once nothing is left to do, the
// column goes.
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: rail

    // The TransferPanel whose cards it shows.
    property var panel: null
    signal openRequested()

    radius: Theme.radius
    color: Theme.panelFill
    border.width: 1
    border.color: Theme.glassEdge
    clip: true

    // Anywhere on it: the panel again.
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: rail.openRequested()
    }

    Column {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 10
        spacing: 12

        // The batch: how far it is, in a ring.
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 40
            height: 40
            readonly property real fraction: rail.panel ? rail.panel.fraction : 0
            onFractionChanged: total.requestPaint()
            Canvas {
                id: total
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    var r = width / 2 - 3
                    ctx.lineWidth = 3
                    ctx.lineCap = "round"
                    ctx.strokeStyle = Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                    ctx.beginPath()
                    ctx.arc(width / 2, height / 2, r, 0, 2 * Math.PI)
                    ctx.stroke()
                    var f = parent.fraction
                    if (f <= 0)
                        return
                    ctx.strokeStyle = Theme.accent
                    ctx.beginPath()
                    ctx.arc(width / 2, height / 2, r, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * Math.min(1, f))
                    ctx.stroke()
                }
            }
            Text {
                anchors.centerIn: parent
                text: Math.floor(parent.fraction * 100) + "%"
                color: Theme.text
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 24
            height: 1
            color: Theme.border
        }

        Repeater {
            model: rail.panel ? rail.panel.model : 0
            delegate: Item {
                id: slot
                required property string key
                required property string section
                readonly property var thumb: rail.panel && rail.panel.cardData[key] ? rail.panel.cardData[key].thumb : null
                readonly property bool lit: section === "running"
                visible: section !== "finished" && thumb !== null
                anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
                width: 44
                height: 44

                // Under way: lit.
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    radius: 11
                    visible: slot.lit
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.alpha(Theme.accent, 0.55)
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 8
                    color: Theme.controlFill
                    border.color: Theme.border
                    opacity: slot.lit ? 1.0 : 0.75
                    CoverArt {
                        id: cover
                        anchors.fill: parent
                        visible: ready
                        crop: true
                        edge: false
                        radius: parent.radius
                        picture: slot.thumb ? slot.thumb.picture : ""
                    }
                    Icon {
                        anchors.centerIn: parent
                        visible: !cover.ready
                        name: slot.thumb ? slot.thumb.icon : "package"
                        size: 18
                        color: Theme.textSecondary
                    }
                }
                Rectangle {
                    anchors.fill: badge
                    anchors.margins: -2
                    radius: width / 2
                    color: Theme.panelFill
                }
                StageLoader {
                    id: badge
                    stage: slot.thumb ? slot.thumb.stage : ""
                    percent: slot.thumb ? slot.thumb.percent || 0 : 0
                    waiting: slot.thumb ? slot.thumb.waiting === true : false
                    width: 22
                    height: 22
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: -6
                    anchors.bottomMargin: -6
                }
                HoverHandler { id: hover }
                ToolTip.visible: hover.hovered
                ToolTip.delay: 300
                ToolTip.text: slot.thumb ? slot.thumb.title + (slot.thumb.text ? "\n" + slot.thumb.text : "") : ""
            }
        }
    }
}
