// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The installs panel folded away while jobs go on: a narrow column with the
// whole batch's progress on top and, under it, each job under way or
// waiting — its picture with the same ring as its card, moving as the card
// does. A click opens the panel again; once nothing is left to do, the
// column goes. The spine it sits in draws the ground, so it draws none.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: rail

    // The TransferPanel whose cards it shows.
    property var panel: null
    signal openRequested()

    clip: true

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Show the transfers")

    // Anywhere on it: the panel again.
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onClicked: rail.openRequested()
        ToolTip.visible: containsMouse && !thumbs.anyHovered
        ToolTip.delay: 500
        ToolTip.text: qsTr("Show the transfers")
    }

    Column {
        id: thumbs
        property bool anyHovered: false
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 16
        spacing: 14

        // The batch: how far it is, in a ring with a brass bead at its end.
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 48
            height: 48
            readonly property real fraction: rail.panel ? rail.panel.fraction : 0
            onFractionChanged: total.requestPaint()
            Canvas {
                id: total
                anchors.fill: parent
                Connections {
                    target: Theme
                    function onNameChanged() { total.requestPaint() }
                }
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    var r = width / 2 - 4
                    var cx = width / 2, cy = height / 2
                    ctx.lineWidth = 3
                    ctx.lineCap = "round"
                    ctx.strokeStyle = Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                    ctx.beginPath()
                    ctx.arc(cx, cy, r, 0, 2 * Math.PI)
                    ctx.stroke()
                    var f = Math.min(1, parent.fraction)
                    if (f <= 0)
                        return
                    var end = -Math.PI / 2 + 2 * Math.PI * f
                    ctx.strokeStyle = Theme.accent
                    ctx.beginPath()
                    ctx.arc(cx, cy, r, -Math.PI / 2, end)
                    ctx.stroke()
                    if (f < 1) {
                        ctx.fillStyle = Theme.brass
                        ctx.beginPath()
                        ctx.arc(cx + r * Math.cos(end), cy + r * Math.sin(end), 3, 0, 2 * Math.PI)
                        ctx.fill()
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                text: Math.floor(parent.fraction * 100) + "%"
                color: Theme.ink
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                font.weight: Font.DemiBold
            }
        }

        Sparkle {
            anchors.horizontalCenter: parent.horizontalCenter
            size: 10
            color: Theme.brass
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
                    anchors.margins: -4
                    radius: 14
                    visible: slot.lit
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.alpha(Theme.accent, 0.6)
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 10
                    color: Theme.controlFill
                    border.color: Theme.glassEdge
                    opacity: slot.lit ? 1.0 : 0.7
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
                    color: Theme.sidebarFill
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
                HoverHandler {
                    id: hover
                    onHoveredChanged: thumbs.anyHovered = hovered
                }
                ToolTip.visible: hover.hovered
                ToolTip.delay: 300
                ToolTip.text: slot.thumb ? slot.thumb.title + (slot.thumb.text ? "\n" + slot.thumb.text : "") : ""
            }
        }
    }
}
