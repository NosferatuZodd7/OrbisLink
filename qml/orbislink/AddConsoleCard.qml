// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The card next to the consoles for adding one more to the list: no fill,
// with a dashed border — an empty spot waiting for a console.
import QtQuick

Item {
    id: card

    signal add()

    implicitWidth: 180
    implicitHeight: 312

    // The same motion as the console cards: lifts with the mouse over it,
    // sinks when pressed.
    scale: area.pressed ? Theme.pressScale : area.containsMouse ? 1.02 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
    transform: Translate {
        y: area.containsMouse && !area.pressed ? -6 : 0
        Behavior on y { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: area.containsMouse ? Theme.alpha(Theme.accent, Theme.light ? 0.10 : 0.14)
                                  : Theme.alpha(Theme.panel, Theme.light ? 0.5 : 0.25)
        Behavior on color { ColorAnimation { duration: Theme.cardEase } }
    }

    // A soft glow around it with the mouse over it, like the console cards.
    Repeater {
        model: 3
        Rectangle {
            anchors.fill: parent
            anchors.margins: -(index + 1) * 3
            radius: Theme.radius + (index + 1) * 3
            color: "transparent"
            border.width: 3
            border.color: Theme.alpha(Theme.accent, 0.16 - index * 0.045)
            opacity: area.containsMouse ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.cardEase } }
        }
    }

    // The dashed border, drawn dash by dash along the rounded outline
    // (Canvas does not support dashes in every Qt version).
    Canvas {
        id: outline
        anchors.fill: parent
        readonly property color tone: area.containsMouse
            ? Theme.cardGlow
            : Theme.alpha(Theme.cardTextMuted, 0.55)
        onToneChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var m = 1.5, w = width - 2 * m, h = height - 2 * m
            var r = Math.min(Theme.radius, w / 2, h / 2)
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
            var dash = 7, space = 6, period = dash + space
            ctx.beginPath()
            for (var j = 0; j + 1 < dots.length; ++j) {
                if (j % period < dash) {
                    ctx.moveTo(dots[j][0], dots[j][1])
                    ctx.lineTo(dots[j + 1][0], dots[j + 1][1])
                }
            }
            ctx.stroke()
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 14

        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "plus-circle"
            size: 48
            strokeWidth: 1.4
            color: area.containsMouse ? Theme.accent : Theme.text
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: card.width - 32
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Add console")
            color: area.containsMouse ? Theme.accent : Theme.text
            font.pixelSize: 15
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
