// SPDX-License-Identifier: AGPL-3.0-or-later
//
// "Neon-Haus": a word lit like a neon sign whose tube is unstable — it
// buzzes, drops out twice, fades, catches again. The CSS it follows:
//
//   animation: 3.2s linear infinite
//   0%, 18%, 21%, 24%, 100%  opacity 1,   glow 4px ink + 11/24/48px ink-2
//   19%, 22%, 62%            opacity .35, no glow
//   63%, 64.5%               opacity .6,  glow 4px ink-2
//
// Between those, everything goes in a straight line, as CSS interpolates.
// The glow is drawn by Canvas shadows (QtQuick.Effects is not needed): each
// layer is the text's shadow only, its weight following the keyframes.
import QtQuick

Item {
    id: root

    property string text: ""
    // The colour tokens: the text, the main glow and the second accent.
    property color ink: Theme.text
    property color ink2: "#FF4FD8"
    property color ink3: "#4FF8FF"
    property int pixelSize: 32
    // In ems, as letter-spacing: .14em.
    property real tracking: 0.14
    property bool running: visible
    // The room the widest glow (48px) needs around the text.
    readonly property int pad: 52

    implicitWidth: Math.ceil(textMetrics.advanceWidth) + pad * 2
    implicitHeight: Math.round(pixelSize * 1.1) + pad * 2

    // Where the animation is (0 to 1), and the keyframes as
    // [at, opacity, full glow, small glow].
    property real progress: 0
    readonly property var frames: [[0, 1, 1, 0], [0.18, 1, 1, 0], [0.19, 0.35, 0, 0], [0.21, 1, 1, 0],
        [0.22, 0.35, 0, 0], [0.24, 1, 1, 0], [0.62, 0.35, 0, 0], [0.63, 0.6, 0, 1], [0.645, 0.6, 0, 1],
        [1, 1, 1, 0]]
    function frame(t) {
        for (var i = 1; i < frames.length; ++i) {
            var a = frames[i - 1], b = frames[i]
            if (t <= b[0]) {
                var k = b[0] > a[0] ? (t - a[0]) / (b[0] - a[0]) : 1
                return [a[1] + (b[1] - a[1]) * k, a[2] + (b[2] - a[2]) * k, a[3] + (b[3] - a[3]) * k]
            }
        }
        return [1, 1, 0]
    }
    readonly property var now: running ? frame(progress) : [1, 1, 0]

    NumberAnimation on progress {
        from: 0
        to: 1
        duration: 3200
        loops: Animation.Infinite
        running: root.running
    }

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
    function measure(ctx) {
        var width = 0
        for (var i = 0; i < text.length; ++i)
            width += ctx.measureText(text[i]).width + (i < text.length - 1 ? tracking * pixelSize : 0)
        return width
    }
    // Draws the text letter by letter (Canvas has no letter-spacing), with
    // a shadow and nothing else when `shadowOnly`: the letters are drawn far
    // off to the left and only their shadow is moved back into place.
    function draw(ctx, fill, blur, shadow, shadowOnly) {
        ctx.font = "600 " + pixelSize + "px \"" + Theme.fontMono + "\""
        ctx.textBaseline = "middle"
        var width = measure(ctx)
        var x = (ctx.canvas.width - width) / 2
        var y = ctx.canvas.height / 2
        var away = shadowOnly ? 10000 : 0
        ctx.shadowColor = shadow
        ctx.shadowBlur = blur
        ctx.shadowOffsetX = away
        ctx.shadowOffsetY = 0
        ctx.fillStyle = fill
        for (var i = 0; i < text.length; ++i) {
            ctx.fillText(text[i], x - away, y)
            x += ctx.measureText(text[i]).width + tracking * pixelSize
        }
    }

    // The full glow: 0 0 11/24/48px ink-2, and 0 0 4px ink.
    Canvas {
        id: fullGlow
        anchors.fill: parent
        opacity: root.now[0] * root.now[1]
        visible: opacity > 0.01
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            // The wide layers twice: a Canvas shadow is fainter than CSS's.
            root.draw(ctx, root.ink2, 48, root.ink2, true)
            root.draw(ctx, root.ink2, 48, root.ink2, true)
            root.draw(ctx, root.ink2, 24, root.ink2, true)
            root.draw(ctx, root.ink2, 24, root.ink2, true)
            root.draw(ctx, root.ink2, 11, root.ink2, true)
            root.draw(ctx, root.ink, 4, root.ink, true)
        }
    }
    // The tube catching again: 0 0 4px ink-2.
    Canvas {
        id: smallGlow
        anchors.fill: parent
        opacity: root.now[0] * root.now[2]
        visible: opacity > 0.01
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            root.draw(ctx, root.ink2, 4, root.ink2, true)
        }
    }
    // The letters themselves.
    Canvas {
        id: letters
        anchors.fill: parent
        opacity: root.now[0]
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            root.draw(ctx, root.ink, 0, "transparent", false)
        }
    }

    function repaint() {
        fullGlow.requestPaint()
        smallGlow.requestPaint()
        letters.requestPaint()
    }
    onTextChanged: repaint()
    onInkChanged: repaint()
    onInk2Changed: repaint()
    onPixelSizeChanged: repaint()
    onWidthChanged: repaint()
    onHeightChanged: repaint()
}
