// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Where a game is on its way to the console, as a ring that fills up with a
// symbol in the middle. Each step has its own colour and its own motion, so
// a glance says which one it is:
//
// * converting — amber, a disc that spins;
// * sending    — blue, an arrow that keeps rising;
// * installing — green, a box that breathes;
// * waiting    — grey, a ring that turns with no fill.
//
// Finished steps keep the colour with a still symbol (✓, or ! on failure).
import QtQuick

Item {
    id: loader

    property string stage: ""
    property real percent: 0
    // Queued, not started: the ring turns instead of filling.
    property bool waiting: false

    readonly property bool converting: stage === "converting" || stage === "converted"
    readonly property bool sending: stage === "sending" || stage === "sent"
    readonly property bool installing: stage === "installing" || stage === "installed"
    readonly property bool active: stage === "waiting" || stage === "converting"
                                   || stage === "sending" || stage === "installing"
    readonly property bool indeterminate: stage === "waiting" || waiting
    readonly property color tone: stage === "error" ? Theme.error
                                : converting ? Theme.warn
                                : sending ? Theme.accent
                                : installing ? Theme.ok
                                : Theme.textSecondary

    implicitWidth: 40
    implicitHeight: 40

    // The track and the filled part.
    Canvas {
        id: ring
        anchors.fill: parent
        readonly property real fill: loader.indeterminate ? 0.28
                                   : loader.active ? Math.max(0.04, Math.min(1, loader.percent / 100))
                                   : 1
        onFillChanged: requestPaint()
        Connections {
            target: loader
            function onToneChanged() { ring.requestPaint() }
        }
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var r = width / 2 - 3
            ctx.lineWidth = 3
            ctx.lineCap = "round"
            ctx.strokeStyle = Qt.rgba(loader.tone.r, loader.tone.g, loader.tone.b, 0.18)
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, r, 0, 2 * Math.PI)
            ctx.stroke()
            ctx.strokeStyle = loader.tone
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, r, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * fill)
            ctx.stroke()
        }
        // With no progress to show, the arc goes round.
        RotationAnimation on rotation {
            running: loader.indeterminate
            from: 0; to: 360
            duration: 1100
            loops: Animation.Infinite
            onStopped: ring.rotation = 0
        }
    }

    Icon {
        id: glyph
        anchors.centerIn: parent
        name: loader.stage === "error" ? "alert"
            : !loader.active ? "check"
            : loader.stage === "waiting" ? "loader"
            : loader.converting ? "disc"
            : loader.sending ? "upload"
            : "package"
        size: 18
        strokeWidth: 2
        color: loader.tone
    }

    // Converting: the disc spins.
    RotationAnimation {
        target: glyph
        property: "rotation"
        running: loader.stage === "converting"
        from: 0; to: 360
        duration: 1400
        loops: Animation.Infinite
        onStopped: glyph.rotation = 0
    }
    // Sending: the arrow keeps rising.
    SequentialAnimation {
        running: loader.stage === "sending"
        loops: Animation.Infinite
        onStopped: { glyph.anchors.verticalCenterOffset = 0; glyph.opacity = 1 }
        ParallelAnimation {
            NumberAnimation { target: glyph; property: "anchors.verticalCenterOffset"; from: 4; to: -4; duration: 700; easing.type: Easing.OutCubic }
            NumberAnimation { target: glyph; property: "opacity"; from: 0.2; to: 1; duration: 350 }
        }
        NumberAnimation { target: glyph; property: "opacity"; to: 0; duration: 250 }
    }
    // Installing: the box breathes.
    SequentialAnimation {
        running: loader.stage === "installing"
        loops: Animation.Infinite
        onStopped: glyph.scale = 1
        NumberAnimation { target: glyph; property: "scale"; from: 0.82; to: 1.12; duration: 600; easing.type: Easing.InOutSine }
        NumberAnimation { target: glyph; property: "scale"; from: 1.12; to: 0.82; duration: 600; easing.type: Easing.InOutSine }
    }
    // Waiting: the loader glyph turns too.
    RotationAnimation {
        target: glyph
        property: "rotation"
        running: loader.stage === "waiting"
        from: 0; to: 360
        duration: 1100
        loops: Animation.Infinite
        onStopped: glyph.rotation = 0
    }
}
