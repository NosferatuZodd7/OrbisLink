// SPDX-License-Identifier: AGPL-3.0-or-later
//
// "Tilt-Shift": a word out of focus, with a sharper strip of it travelling
// up and down through it, as through a tilt-shift lens. The CSS it follows:
//
//   the word        ink at 28%, the whole element blurred 2px
//   ::after         ink-3 at 30%, blurred 6px more
//   ::before        ink, a strip of 36% of its height
//                   (clip-path: inset(0 0 64% 0) → inset(64% 0 0 0),
//                   translateY(-2px) → (2px)), 2.8s ease-in-out, alternate
//
// The blur is drawn by Canvas shadows (QtQuick.Effects is not needed).
import QtQuick

Item {
    id: root

    property string text: ""
    property color ink: Theme.text
    property color ink2: "#FF4FD8"
    property color ink3: "#4FF8FF"
    property int pixelSize: 32
    // In ems, as letter-spacing: .1em.
    property real tracking: 0.1
    property bool running: visible
    // Room for the widest blur (6px + 2px) around the text.
    readonly property int pad: 16
    readonly property real lineHeight: Math.round(pixelSize * 1.1)

    implicitWidth: Math.ceil(textMetrics.advanceWidth) + pad * 2
    implicitHeight: lineHeight + pad * 2

    // The text's width, known before anything is drawn (the Canvas is sized
    // from it; measuring while painting would resize it mid-paint).
    TextMetrics {
        id: textMetrics
        text: root.text
        font.family: Theme.fontMono
        font.pixelSize: root.pixelSize
        font.weight: Font.DemiBold
        font.letterSpacing: root.tracking * root.pixelSize
    }
    // 0: the strip at the top; 1: at the bottom.
    property real stripAt: 0

    SequentialAnimation on stripAt {
        running: root.running
        loops: Animation.Infinite
        // CSS's ease-in-out, both ways (alternate).
        NumberAnimation { from: 0; to: 1; duration: 2800; easing.type: Easing.Bezier; easing.bezierCurve: [0.42, 0, 0.58, 1, 1, 1] }
        NumberAnimation { from: 1; to: 0; duration: 2800; easing.type: Easing.Bezier; easing.bezierCurve: [0.42, 0, 0.58, 1, 1, 1] }
    }

    function draw(ctx, color, blur) {
        ctx.reset()
        ctx.font = "600 " + pixelSize + "px \"" + Theme.fontMono + "\""
        ctx.textBaseline = "middle"
        var width = 0
        for (var i = 0; i < text.length; ++i)
            width += ctx.measureText(text[i]).width + (i < text.length - 1 ? tracking * pixelSize : 0)
        var x = (ctx.canvas.width - width) / 2
        var y = pad + lineHeight / 2
        // Only the shadow, moved back into place: a blurred copy of the text.
        var away = 10000
        ctx.shadowColor = color
        ctx.shadowBlur = blur
        ctx.shadowOffsetX = away
        ctx.fillStyle = color
        for (var j = 0; j < text.length; ++j) {
            ctx.fillText(text[j], x - away, y)
            x += ctx.measureText(text[j]).width + tracking * pixelSize
        }
    }

    // The word, faint and out of focus.
    Canvas {
        id: base
        anchors.fill: parent
        onPaint: root.draw(getContext("2d"), Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.28), 4)
    }
    // Its haze in the second accent.
    Canvas {
        id: haze
        anchors.fill: parent
        opacity: 0.3
        onPaint: root.draw(getContext("2d"), root.ink3, 16)
    }
    // The strip in focus, travelling through it.
    Item {
        id: strip
        x: 0
        width: parent.width
        y: root.pad + root.lineHeight * 0.64 * root.stripAt + (root.stripAt * 4 - 2)
        height: root.lineHeight * 0.36
        clip: true
        Canvas {
            id: sharp
            x: 0
            y: -strip.y + (root.stripAt * 4 - 2)
            width: root.width
            height: root.height
            onPaint: root.draw(getContext("2d"), root.ink, 4)
        }
    }

    function repaint() {
        base.requestPaint()
        haze.requestPaint()
        sharp.requestPaint()
    }
    onTextChanged: repaint()
    onInkChanged: repaint()
    onInk3Changed: repaint()
    onPixelSizeChanged: repaint()
    onWidthChanged: repaint()
    onHeightChanged: repaint()
}
