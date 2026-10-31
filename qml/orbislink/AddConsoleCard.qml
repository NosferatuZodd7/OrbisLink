// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The plate next to the consoles for adding one more to the list: no fill,
// a dashed outline — an empty place on the atlas page, waiting for a console.
import QtQuick

Item {
    id: card

    signal add()

    // The page sets the size, the same as a console plate's.
    implicitWidth: 280
    implicitHeight: 240

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Add console")

    // The same motion as the console plates: a small lift with the mouse
    // over it, sinking when pressed.
    scale: area.pressed ? 0.99 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
    transform: Translate {
        y: area.containsMouse && !area.pressed ? -3 : 0
        Behavior on y { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
    }

    // A soft ink wash with the mouse over it, as the lists have.
    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: area.containsMouse ? Theme.alpha(Theme.ink, 0.05) : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }

    // The dashed border, drawn dash by dash along the rounded outline
    // (Canvas does not support dashes in every Qt version).
    Canvas {
        id: outline
        anchors.fill: parent
        readonly property color tone: area.containsMouse ? Theme.alpha(Theme.accent, 0.9)
                                                         : Theme.alpha(Theme.ink, Theme.light ? 0.32 : 0.28)
        onToneChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var m = 1, w = width - 2 * m, h = height - 2 * m
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
            ctx.lineWidth = 1.2
            ctx.lineCap = "round"
            var dash = 6, space = 6, period = dash + space
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
        width: card.width - 40
        spacing: 10

        // The seal: a plus inside a brass ring, a star where the ring is
        // engraved — the atlas's mark for "a place for one more".
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 56
            height: 56
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: area.containsMouse ? Theme.accentFill : Theme.alpha(Theme.ink, 0.04)
                border.width: 1
                border.color: Theme.alpha(Theme.brass, area.containsMouse ? 0.8 : 0.5)
                Behavior on color { ColorAnimation { duration: Theme.fast } }
            }
            Icon {
                anchors.centerIn: parent
                name: "plus"
                size: 24
                color: area.containsMouse ? Theme.accent : Theme.ink
            }
            Sparkle {
                x: parent.width - width * 0.75
                y: -size * 0.25
                size: 12
                color: Theme.brass
            }
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Add console")
            color: area.containsMouse ? (Theme.light ? Qt.darker(Theme.accent, 1.3) : Theme.accent) : Theme.ink
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontCardTitle
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("A PS4 or a PS5 on this network")
            color: Theme.textSecondary
            font.pixelSize: 12
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
