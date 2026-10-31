// SPDX-License-Identifier: AGPL-3.0-or-later
//
// An engraved divider, as in the atlas: two thin lines meeting a small
// four-pointed star in the middle. For the structure of a page, not for every
// gap — a plain 1px border does that.
import QtQuick

Item {
    id: rule

    property color tone: Theme.rule
    // The star at the centre; without it, a double hairline.
    property bool star: true

    implicitHeight: 14
    implicitWidth: 200

    readonly property real gap: star ? 16 : 0

    // The lines: a firm one and, under it, a fainter one, like a burin's pass.
    Repeater {
        model: 2
        Item {
            required property int index
            x: index === 0 ? 0 : (rule.width + rule.gap) / 2
            width: (rule.width - rule.gap) / 2
            height: rule.height
            Rectangle {
                width: parent.width
                height: 1
                y: parent.height / 2 - 1
                color: rule.tone
            }
            Rectangle {
                width: parent.width * 0.86
                x: index === 0 ? parent.width * 0.14 : 0
                height: 1
                y: parent.height / 2 + 2
                color: Theme.alpha(rule.tone, rule.tone.a * 0.5)
            }
        }
    }

    Sparkle {
        visible: rule.star
        anchors.centerIn: parent
        size: 10
        color: rule.tone
    }
}
