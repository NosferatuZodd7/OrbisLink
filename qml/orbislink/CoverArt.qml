// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A game's picture with its corners rounded, like every card. It fills the
// frame (crop), or, when its shape is not the frame's (a PS2 box in a square)
// and it is not asked to fill, sits whole over a dimmed copy of itself; with
// none, or while it loads, a disc stands in (DiscPlaceholder). The picture
// is made by the cover image provider (cover_provider.h) at the screen's own
// pixels — scaled down smoothly, its corners cut — and drawn 1:1, so it
// stays sharp on any screen and at any scale.
import QtQuick
import QtQuick.Window

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

    // The picture is there — or was, at another size, and shows until the
    // new size is made.
    readonly property bool ready: picture.length > 0 && (image.status === Image.Ready || drawn === picture)
    // The picture last made, and its image URL.
    property string drawn: ""
    property string drawnSource: ""

    // The frame in the screen's pixels, settled: while a window is being
    // resized the picture is stretched, and made again once it stops.
    readonly property real pixelRatio: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    property size frame: Qt.size(0, 0)
    function settle() {
        frame = Qt.size(Math.round(width * pixelRatio), Math.round(height * pixelRatio))
    }
    Timer {
        id: settleTimer
        interval: 150
        onTriggered: art.settle()
    }
    onWidthChanged: frame.width > 0 ? settleTimer.restart() : settle()
    onHeightChanged: frame.height > 0 ? settleTimer.restart() : settle()
    onPixelRatioChanged: settle()
    Component.onCompleted: settle()

    DiscPlaceholder {
        anchors.fill: parent
        visible: !art.ready
        variant: art.placeholder
        label: art.label
        radius: art.radius
    }

    // The last size made, while the new one is.
    Image {
        anchors.fill: parent
        visible: image.status !== Image.Ready && art.drawn === art.picture
        asynchronous: true
        smooth: true
        source: art.drawnSource
    }

    Image {
        id: image
        anchors.fill: parent
        visible: status === Image.Ready
        asynchronous: true
        smooth: true
        source: art.picture.length > 0 && art.frame.width > 0 && art.frame.height > 0
                ? "image://cover/" + art.frame.width + "x" + art.frame.height + "/"
                  + Math.round(art.radius * art.pixelRatio) + "/" + (art.crop ? "crop" : "fit") + "/"
                  + encodeURIComponent(String(Theme.cardBottom)) + "/" + encodeURIComponent(art.picture)
                : ""
        onStatusChanged: {
            if (status !== Image.Ready)
                return
            art.drawn = art.picture
            art.drawnSource = source
        }
    }

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
