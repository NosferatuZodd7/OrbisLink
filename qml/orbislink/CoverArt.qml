// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A game's picture with its corners rounded, like every card. It fills the
// frame (crop), or, when its shape is not the frame's (a PS2 box in a square)
// and it is not asked to fill, sits whole over a dimmed copy of itself; with
// none, or while it loads, a disc stands in (DiscPlaceholder). Drawn on a
// canvas so the corners are cut for real, whatever is behind.
import QtQuick

Item {
    id: art

    property string picture: ""
    property string placeholder: "bd"
    property string label: ""
    property real radius: 6
    // Fill the frame whatever the picture's shape.
    property bool crop: false
    // The thin edge around it.
    property bool edge: true

    // The picture's shape, and whether it loads, from a small copy never drawn.
    readonly property bool ready: shape.status === Image.Ready
    readonly property real ratio: shape.implicitHeight > 0 ? shape.implicitWidth / shape.implicitHeight : 1
    readonly property bool square: crop || Math.abs(ratio - width / Math.max(1, height)) < 0.12

    Image {
        id: shape
        visible: false
        source: art.picture
        sourceSize.width: 64
        asynchronous: true
    }

    DiscPlaceholder {
        anchors.fill: parent
        visible: !art.ready
        variant: art.placeholder
        label: art.label
        radius: art.radius
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        visible: art.ready

        function load() {
            if (art.picture.length > 0 && !isImageLoaded(art.picture))
                loadImage(art.picture)
            requestPaint()
        }
        // Where the picture goes: covering the frame, or whole inside it.
        function place(cover) {
            var w = width, h = height
            var wider = art.ratio > w / Math.max(1, h)
            var dw, dh
            if (wider === cover) {
                dh = h
                dw = h * art.ratio
            } else {
                dw = w
                dh = w / Math.max(0.01, art.ratio)
            }
            return [(w - dw) / 2, (h - dh) / 2, dw, dh]
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            if (!art.ready || art.picture.length === 0 || !isImageLoaded(art.picture))
                return
            var w = width, h = height
            var r = Math.max(0, Math.min(art.radius, w / 2, h / 2))
            ctx.beginPath()
            ctx.moveTo(r, 0)
            ctx.lineTo(w - r, 0)
            ctx.arcTo(w, 0, w, r, r)
            ctx.lineTo(w, h - r)
            ctx.arcTo(w, h, w - r, h, r)
            ctx.lineTo(r, h)
            ctx.arcTo(0, h, 0, h - r, r)
            ctx.lineTo(0, r)
            ctx.arcTo(0, 0, r, 0, r)
            ctx.closePath()
            ctx.clip()
            ctx.fillStyle = Theme.cardBottom
            ctx.fillRect(0, 0, w, h)
            var cover = place(true)
            if (art.square) {
                ctx.drawImage(art.picture, cover[0], cover[1], cover[2], cover[3])
                return
            }
            ctx.globalAlpha = 0.3
            ctx.drawImage(art.picture, cover[0], cover[1], cover[2], cover[3])
            ctx.globalAlpha = 1
            var whole = place(false)
            ctx.drawImage(art.picture, whole[0], whole[1], whole[2], whole[3])
        }
        onImageLoaded: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Component.onCompleted: load()
    }

    onPictureChanged: canvas.load()
    onReadyChanged: canvas.requestPaint()
    onSquareChanged: canvas.requestPaint()
    onRadiusChanged: canvas.requestPaint()

    // The tile's own edge over the picture.
    Rectangle {
        anchors.fill: parent
        visible: art.ready && art.edge
        radius: art.radius
        color: "transparent"
        border.width: 1
        border.color: Theme.cardEdge
    }
}
