// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The video item, in a file of its own.
//
// The "import QtMultimedia" is isolated here on purpose: when OrbisLink
// is built without Remote Play, or when QtMultimedia's QML module is not
// installed, this file is never loaded and the rest of the window opens
// anyway. Having the import in StreamArea.qml would make the whole
// interface fail to load on those machines.
import QtQuick
import QtMultimedia

VideoOutput {
    id: video
    fillMode: VideoOutput.PreserveAspectFit

    // The keyboard acts as the controller while there is a stream.
    Keys.onPressed: function(event) {
        if (event.isAutoRepeat)
            return
        if (event.key === Qt.Key_Escape) {
            // In full screen, Esc leaves full screen; outside it, it ends
            // the session. It is what anyone expects.
            if (window.streamFullscreen)
                window.setStreamFullscreen(false)
            else
                stream.stopStream()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_F11) {
            window.setStreamFullscreen(!window.streamFullscreen)
            event.accepted = true
            return
        }
        event.accepted = stream.keyPressed(event.key)
    }
    Keys.onReleased: function(event) {
        if (event.isAutoRepeat)
            return
        event.accepted = stream.keyReleased(event.key)
    }
    onActiveFocusChanged: if (!activeFocus) stream.releaseAllKeys()

    // The mouse acts as the touchpad: dragging over the picture is the same
    // as dragging a finger on the controller. Clicking is a tap.
    MouseArea {
        anchors.fill: parent
        enabled: stream.touchpadFromMouse()
        acceptedButtons: Qt.LeftButton
        cursorShape: enabled ? Qt.BlankCursor : Qt.ArrowCursor
        hoverEnabled: false

        function normalised(dot) {
            // The video keeps its aspect ratio, so the drawn area may be
            // smaller than the item. The real area is used, otherwise the touch
            // would land offset in the black bars.
            var r = video.contentRect
            if (r.width <= 0 || r.height <= 0)
                return null
            var x = (dot.x - r.x) / r.width
            var y = (dot.y - r.y) / r.height
            if (x < 0 || x > 1 || y < 0 || y > 1)
                return null
            return { "x": x, "y": y }
        }

        onPressed: function(mouse) {
            var p = normalised(mouse)
            if (p)
                stream.touchBegin(p.x, p.y)
            video.forceActiveFocus()
        }
        onPositionChanged: function(mouse) {
            var p = normalised(mouse)
            if (p)
                stream.touchMove(p.x, p.y)
        }
        onReleased: stream.touchEnd()
        onCanceled: stream.touchEnd()
    }

    Component.onCompleted: stream.video.setVideoSink(video.videoSink)
}
