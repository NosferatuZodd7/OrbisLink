// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A point of light: a small champagne dot with a soft glow around it. It
// marks what is singled out — the page open in the navigation, the middle
// of a divider.
import QtQuick

Item {
    id: dot
    property color color: Theme.brass
    property real size: 6

    width: size
    height: size

    Rectangle {
        anchors.centerIn: parent
        width: dot.size * 2.6
        height: width
        radius: width / 2
        color: Theme.alpha(dot.color, 0.18)
    }
    Rectangle {
        anchors.centerIn: parent
        width: dot.size
        height: width
        radius: width / 2
        color: dot.color
    }
}
