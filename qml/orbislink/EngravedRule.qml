// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A divider for the structure of a page: one hairline that fades out at both
// ends, as light does along an edge of glass. With `star`, a small point of
// light sits at its middle. For a page's structure, not for every gap — a
// plain 1px edge does that.
import QtQuick

Item {
    id: rule

    property color tone: Theme.rule
    property bool star: false

    implicitHeight: 14
    implicitWidth: 200

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.alpha(rule.tone, 0) }
            GradientStop { position: 0.15; color: rule.tone }
            GradientStop { position: 0.85; color: rule.tone }
            GradientStop { position: 1.0; color: Theme.alpha(rule.tone, 0) }
        }
    }

    Sparkle {
        visible: rule.star
        anchors.centerIn: parent
        size: 6
    }
}
