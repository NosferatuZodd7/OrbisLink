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
// Or a picture of one's own (Settings → Personalisation), shown the same
// way: the app keeps a blurred copy of it (AppController::backgroundUrl).
//
// Daylight has a bright room; Graphite and Ambient a warm evening one, a
// little darker under Graphite so the glass keeps its contrast.
//
// Over it, when asked for, light moves as through water: two sheets of the
// same caustic pattern (scripts/make-caustics.py), at different sizes and
// angles, drifting slowly across each other. Only here, behind the glass —
// the panes stay still. It rests while Remote Play streams and while the
// window is in the background, so it never costs anything there.
import QtQuick

Item {
    id: room
    clip: true

    readonly property bool custom: app.backgroundUrl.length > 0
    readonly property bool streaming: typeof stream !== "undefined" && stream !== null && stream.streaming
    readonly property bool moving: app.backgroundMotion && !streaming
                                   && Qt.application.state === Qt.ApplicationActive

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    Image {
        anchors.fill: parent
        source: room.custom ? app.backgroundUrl
                            : Theme.light ? "qrc:/icons/room-light.jpg" : "qrc:/icons/room-dark.jpg"
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        smooth: true
        cache: true
    }

    // A veil in the appearance's own colour: keeps the room from competing
    // with what is on the glass. A picture of one's own gets a little more,
    // since nobody chose it for its contrast.
    Rectangle {
        anchors.fill: parent
        color: Theme.background
        opacity: (Theme.light ? 0.15 : Theme.glass ? 0.04 : 0.20) + (room.custom ? 0.10 : 0)
    }

    // ── The water light
    Item {
        id: water
        anchors.fill: parent
        visible: app.backgroundMotion
        // White light reads less on a light room: a little more of it there.
        readonly property real gain: Theme.light ? 1.8 : Theme.glass ? 1.2 : 1.0
        property real swell: 0.8
        // A slow swell in the brightness of the nearer sheet.
        SequentialAnimation on swell {
            running: room.moving
            loops: Animation.Infinite
            NumberAnimation { to: 1.0; duration: 5200; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.55; duration: 6100; easing.type: Easing.InOutSine }
        }

        // The larger, slower sheet, turned a little.
        Image {
            id: wide
            readonly property int tile: 860
            readonly property real span: Math.ceil(Math.hypot(room.width, room.height) / tile + 2) * tile
            width: span
            height: span
            x: (room.width - span) / 2 + driftX
            y: (room.height - span) / 2 + driftY
            rotation: 24
            source: "qrc:/icons/caustics.png"
            sourceSize: Qt.size(tile, tile)
            fillMode: Image.Tile
            smooth: true
            opacity: 0.06 * water.gain
            property real driftX: 0
            property real driftY: 0
            NumberAnimation on driftX {
                running: room.moving
                from: 0; to: -wide.tile
                duration: 140000
                loops: Animation.Infinite
            }
            NumberAnimation on driftY {
                running: room.moving
                from: 0; to: -wide.tile
                duration: 190000
                loops: Animation.Infinite
            }
        }

        // The smaller sheet, drifting the other way: where the two cross,
        // the light gathers and parts as it does under moving water.
        Image {
            id: near
            readonly property int tile: 560
            width: room.width + tile * 2
            height: room.height + tile * 2
            x: -tile + driftX
            y: -tile + driftY
            source: "qrc:/icons/caustics.png"
            sourceSize: Qt.size(tile, tile)
            fillMode: Image.Tile
            smooth: true
            opacity: 0.09 * water.gain * water.swell
            property real driftX: 0
            property real driftY: 0
            NumberAnimation on driftX {
                running: room.moving
                from: 0; to: near.tile
                duration: 100000
                loops: Animation.Infinite
            }
            NumberAnimation on driftY {
                running: room.moving
                from: 0; to: near.tile
                duration: 72000
                loops: Animation.Infinite
            }
        }
    }
}
