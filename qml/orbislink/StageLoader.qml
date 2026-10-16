// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Where a game is on its way to the console, as a ring that fills up with a
// symbol in the middle. Each step has its own colour and its own motion, so
// a glance says which one it is:
//
// * converting — amber, a disc that spins;
// * sending    — blue, arrows gliding up through the ring, one after another;
// * receiving  — blue, arrows gliding down (a copy coming to this PC);
// * installing — green, a box that breathes.
//
// Waiting for its turn (another game is being converted, sent or
// installed) it stands by: a quiet grey ring and the step's symbol slowly
// fading in and out — nothing turns, so what moves on screen is what is
// under way.
//
// Finished steps keep the colour with a still symbol (✓, or ! on failure,
// ✕ in grey when cancelled); a journey over and done ("done") is green
// with a ✓.
import QtQuick

Item {
    id: loader

    property string stage: ""
    property real percent: 0
    // Queued, not started: it stands by.
    property bool waiting: false

    readonly property bool converting: stage === "converting" || stage === "converted"
    readonly property bool sending: stage === "sending" || stage === "sent"
    readonly property bool receiving: stage === "receiving" || stage === "received"
    readonly property bool installing: stage === "installing" || stage === "installed"
    readonly property bool active: stage === "waiting" || stage === "converting"
                                   || stage === "sending" || stage === "receiving" || stage === "installing"
    readonly property bool standing: stage === "waiting" || (waiting && active)
    readonly property color tone: stage === "error" ? Theme.error
                                : stage === "done" ? Theme.ok
                                : standing ? Theme.textSecondary
                                : converting ? Theme.warn
                                : sending || receiving ? Theme.accent
                                : installing ? Theme.ok
                                : Theme.textSecondary

    implicitWidth: 40
    implicitHeight: 40

    // The track and the filled part.
    Canvas {
        id: ring
        anchors.fill: parent
        readonly property real fill: loader.standing ? 0
                                   : loader.active ? Math.max(0.04, Math.min(1, loader.percent / 100))
                                   : 1
        onFillChanged: requestPaint()
        Connections {
            target: loader
            function onToneChanged() { ring.requestPaint() }
            function onStandingChanged() { ring.requestPaint() }
        }
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var r = width / 2 - 3
            ctx.lineWidth = 3
            ctx.lineCap = "round"
            ctx.strokeStyle = Qt.rgba(loader.tone.r, loader.tone.g, loader.tone.b, loader.standing ? 0.32 : 0.18)
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, r, 0, 2 * Math.PI)
            ctx.stroke()
            if (fill <= 0)
                return
            ctx.strokeStyle = loader.tone
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, r, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * fill)
            ctx.stroke()
        }
    }

    Icon {
        id: glyph
        anchors.centerIn: parent
        visible: !loader.gliding
        name: loader.stage === "error" ? "alert"
            : loader.stage === "cancelled" ? "close"
            : !loader.active ? "check"
            : loader.stage === "waiting" ? "history"
            : loader.converting ? "disc"
            : loader.sending ? "upload"
            : loader.receiving ? "download"
            : "package"
        // In step with the ring, at any size.
        size: Math.max(10, Math.round(loader.width * 0.45))
        strokeWidth: 2
        color: loader.tone
    }

    // Standing by: the symbol fades in and out, slowly.
    SequentialAnimation {
        running: loader.standing && loader.visible
        loops: Animation.Infinite
        onStopped: glyph.opacity = 1
        NumberAnimation { target: glyph; property: "opacity"; from: 1; to: 0.35; duration: 900; easing.type: Easing.InOutSine }
        NumberAnimation { target: glyph; property: "opacity"; from: 0.35; to: 1; duration: 900; easing.type: Easing.InOutSine }
    }
    // Converting: the disc spins.
    RotationAnimation {
        target: glyph
        property: "rotation"
        running: loader.stage === "converting" && !loader.standing && loader.visible
        from: 0; to: 360
        duration: 1400
        loops: Animation.Infinite
        onStopped: glyph.rotation = 0
    }
    // Sending (or receiving): arrows glide at an even pace, each leaving
    // through one side of a window inside the ring as the next comes in from
    // the other — one steady motion, with no jump back.
    readonly property bool gliding: (stage === "sending" || stage === "receiving") && !standing
    Item {
        id: lane
        visible: loader.gliding
        anchors.centerIn: parent
        width: glyph.size
        height: glyph.size
        clip: true
        Column {
            id: arrows
            readonly property real step: lane.height + spacing
            // What comes to this PC goes down.
            readonly property bool down: loader.stage === "receiving"
            property real slide: 0
            y: slide
            spacing: Math.round(lane.height * 0.35)
            Repeater {
                model: 2
                Icon {
                    required property int index
                    name: arrows.down ? "arrow-down" : "arrow-up"
                    size: lane.width
                    strokeWidth: 2
                    color: loader.tone
                    // Faded out as it leaves and in as it comes in, instead
                    // of being cut off.
                    opacity: Math.max(0, 1 - Math.abs(arrows.slide + index * arrows.step) / (lane.height * 0.9))
                }
            }
            NumberAnimation on slide {
                running: loader.gliding && loader.visible
                from: arrows.down ? -arrows.step : 0
                to: arrows.down ? 0 : -arrows.step
                duration: 900
                loops: Animation.Infinite
            }
        }
    }
    // Installing: the box breathes.
    SequentialAnimation {
        running: loader.stage === "installing" && !loader.standing && loader.visible
        loops: Animation.Infinite
        onStopped: glyph.scale = 1
        NumberAnimation { target: glyph; property: "scale"; from: 0.82; to: 1.12; duration: 600; easing.type: Easing.InOutSine }
        NumberAnimation { target: glyph; property: "scale"; from: 1.12; to: 0.82; duration: 600; easing.type: Easing.InOutSine }
    }
}
