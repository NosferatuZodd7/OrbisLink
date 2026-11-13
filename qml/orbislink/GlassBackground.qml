// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The window background: the room the glass floats in.
//
// A real-looking interior, completely out of focus — a window of daylight,
// a lamp, a sofa, wooden slats — painted once and shipped as a picture
// (scripts/make-room.py makes it). It is what makes the panes read as
// glass: a translucent surface only looks like glass when what is behind it
// has colour and light of its own. There is no live blur to pay for: the
// room is already blurred.
//
// Daylight has a bright room; Graphite and Ambient a warm evening one, a
// little darker under Graphite so the glass keeps its contrast.
import QtQuick

Item {
    id: room

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    Image {
        anchors.fill: parent
        source: Theme.light ? "qrc:/icons/room-light.jpg" : "qrc:/icons/room-dark.jpg"
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        smooth: true
        cache: true
    }

    // A veil in the appearance's own colour: keeps the room from competing
    // with what is on the glass.
    Rectangle {
        anchors.fill: parent
        color: Theme.background
        opacity: Theme.light ? 0.15 : Theme.glass ? 0.04 : 0.20
    }
}
