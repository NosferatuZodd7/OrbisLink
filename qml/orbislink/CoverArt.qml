// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A game's picture, square like the console's own tiles. A picture that is
// not square (a PS2 box) sits whole over a dimmed copy of itself filling the
// tile; with none, or while it loads, a disc stands in (DiscPlaceholder).
import QtQuick

Item {
    id: art

    property string picture: ""
    property string placeholder: "bd"
    property string label: ""
    property real radius: 6
    // Fill the frame whatever the picture's shape (a box cover in a box).
    property bool crop: false

    readonly property bool ready: image.status === Image.Ready
    // The picture's own shape, read from a small copy that is never drawn
    // (the drawn one changes size with the way it is fitted).
    readonly property bool square: crop || shape.implicitHeight <= 0
                                   || Math.abs(shape.implicitWidth / shape.implicitHeight
                                               - width / Math.max(1, height)) < 0.12

    Image {
        id: shape
        visible: false
        source: art.crop ? "" : art.picture
        sourceSize.width: 48
        asynchronous: true
    }

    DiscPlaceholder {
        anchors.fill: parent
        visible: !art.ready
        variant: art.placeholder
        label: art.label
        radius: art.radius
    }

    Rectangle {
        anchors.fill: parent
        visible: art.ready
        radius: art.radius
        color: Theme.cardBottom
        clip: true
        Image {
            anchors.fill: parent
            visible: !art.square
            source: art.square ? "" : art.picture
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: 96
            asynchronous: true
            opacity: 0.3
        }
        Image {
            id: image
            anchors.fill: parent
            source: art.picture
            fillMode: art.square ? Image.PreserveAspectCrop : Image.PreserveAspectFit
            sourceSize.width: Math.max(160, Math.round(art.width * 2))
            asynchronous: true
            smooth: true
            mipmap: true
        }
    }
    // The tile's own edge over the picture.
    Rectangle {
        anchors.fill: parent
        visible: art.ready
        radius: art.radius
        color: "transparent"
        border.width: 1
        border.color: Theme.cardEdge
    }
}
