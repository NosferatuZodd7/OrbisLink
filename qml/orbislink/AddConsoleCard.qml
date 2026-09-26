// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The card next to the consoles for adding one more to the list: no fill,
// with a dashed border — an empty spot waiting for a console.
import QtQuick

Item {
    id: card

    signal add()

    // Follows the scale of the console cards (drawn 330 tall).
    readonly property real scaleFactor: height / 330
    implicitWidth: 250
    implicitHeight: 330

    // The same motion as the console cards: grows and lifts with the mouse
    // over it, sinks when pressed.
    scale: area.pressed ? Theme.pressScale : (area.containsMouse ? 1.03 : 1.0)
    Behavior on scale { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
    transform: Translate {
        y: area.containsMouse && !area.pressed ? -4 : 0
        Behavior on y { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
    }

    Rectangle {
        anchors.fill: parent
        radius: 30 * card.scaleFactor
        color: area.containsMouse ? Qt.rgba(Theme.cardGlow.r, Theme.cardGlow.g, Theme.cardGlow.b,
                                            Theme.light ? 0.06 : 0.08)
                                  : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.cardEase } }
    }

    // The dashed border, drawn dash by dash along the rounded outline
    // (Canvas does not support dashes in every Qt version).
    Canvas {
        id: outline
        anchors.fill: parent
        readonly property color tone: area.containsMouse
            ? Theme.cardGlow
            : Qt.rgba(Theme.cardTextMuted.r, Theme.cardTextMuted.g, Theme.cardTextMuted.b, 0.6)
        onToneChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var m = 1.5, w = width - 2 * m, h = height - 2 * m
            var r = Math.min(30 * card.scaleFactor, w / 2, h / 2)
            // The outline as a sequence of points, every 1 px.
            var dots = []
            function line(x0, y0, x1, y1) {
                var n = Math.max(1, Math.round(Math.hypot(x1 - x0, y1 - y0)))
                for (var i = 0; i < n; ++i)
                    dots.push([x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n])
            }
            function arc(cx, cy, a0, a1) {
                var n = Math.max(2, Math.round(r * Math.abs(a1 - a0)))
                for (var i = 0; i < n; ++i) {
                    var a = a0 + (a1 - a0) * i / n
                    dots.push([cx + r * Math.cos(a), cy + r * Math.sin(a)])
                }
            }
            line(m + r, m, m + w - r, m)
            arc(m + w - r, m + r, -Math.PI / 2, 0)
            line(m + w, m + r, m + w, m + h - r)
            arc(m + w - r, m + h - r, 0, Math.PI / 2)
            line(m + w - r, m + h, m + r, m + h)
            arc(m + r, m + h - r, Math.PI / 2, Math.PI)
            line(m, m + h - r, m, m + r)
            arc(m + r, m + r, Math.PI, 3 * Math.PI / 2)

            ctx.strokeStyle = tone
            ctx.lineWidth = 1.5
            ctx.lineCap = "round"
            var dash = 7, space = 6, ciclo = dash + space
            ctx.beginPath()
            for (var j = 0; j + 1 < dots.length; ++j) {
                if (j % ciclo < dash) {
                    ctx.moveTo(dots[j][0], dots[j][1])
                    ctx.lineTo(dots[j + 1][0], dots[j + 1][1])
                }
            }
            ctx.stroke()
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 20 * card.scaleFactor

        // The "+" inside a thin circle.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 84 * card.scaleFactor
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: area.containsMouse ? Theme.cardGlow : Theme.cardTextMuted
            Behavior on border.color { ColorAnimation { duration: Theme.cardEase } }
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.4; height: 2; radius: 1
                color: parent.border.color
            }
            Rectangle {
                anchors.centerIn: parent
                width: 2; height: parent.height * 0.4; radius: 1
                color: parent.border.color
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: card.width - 32
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Add console")
            color: area.containsMouse ? Theme.cardGlow : Theme.onIdleStage
            font.pixelSize: Math.max(11, Math.round(20 * card.scaleFactor))
            font.weight: Font.DemiBold
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: card.add()
    }
}
