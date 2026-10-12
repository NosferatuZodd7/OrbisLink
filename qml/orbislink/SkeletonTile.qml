// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A placeholder game card while a place is being read: a soft square where
// the cover will be and two lines where its name and details will be, with
// a band of light sweeping across, as SkeletonRow does for lists.
import QtQuick

Item {
    id: tile

    property int artSize: 148
    // Cards side by side start their shine a little apart, like a wave.
    property int delay: 0

    width: artSize
    height: artSize + 44

    readonly property color block: Theme.light ? "#E4E9F0" : Qt.rgba(1, 1, 1, 0.07)

    Rectangle {
        id: art
        width: tile.artSize
        height: tile.artSize
        radius: 6
        color: tile.block
        clip: true

        Rectangle {
            id: shine
            width: parent.width * 0.6
            height: parent.height
            x: -width
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: Theme.light ? Qt.rgba(1, 1, 1, 0.75) : Qt.rgba(1, 1, 1, 0.07) }
                GradientStop { position: 1.0; color: "transparent" }
            }
            SequentialAnimation on x {
                running: tile.visible
                loops: Animation.Infinite
                PauseAnimation { duration: tile.delay }
                NumberAnimation { from: -shine.width; to: art.width; duration: 1100; easing.type: Easing.InOutQuad }
                PauseAnimation { duration: 500 }
            }
        }
    }
    Rectangle {
        anchors.top: art.bottom
        anchors.topMargin: 12
        width: tile.artSize * 0.72
        height: 11
        radius: 5
        color: tile.block
    }
    Rectangle {
        anchors.top: art.bottom
        anchors.topMargin: 30
        width: tile.artSize * 0.38
        height: 9
        radius: 4
        color: tile.block
    }
}
