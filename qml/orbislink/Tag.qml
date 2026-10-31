// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A small round-ended tag: a version, a state, a kind. Its tone says what
// kind of fact it is: "neutral", "accent", "ok", "warn", "error" or "brass"
// (a jailbreak). Solid tags are for the one fact that matters on a card.
import QtQuick

Rectangle {
    id: tag

    property string text: ""
    property string tone: "neutral"
    property bool mono: false
    property bool solid: false
    property string iconName: ""

    readonly property color toneColor: tone === "accent" ? Theme.accent
                                     : tone === "ok" ? Theme.ok
                                     : tone === "warn" ? Theme.warn
                                     : tone === "error" ? Theme.error
                                     : tone === "brass" ? Theme.henFill
                                     : Theme.textSecondary
    // On paper the tones are too light to read as text: a shade darker.
    readonly property color inkColor: tone === "neutral" ? Theme.text
                                    : Theme.light ? Qt.darker(toneColor, 1.35) : toneColor

    implicitWidth: row.implicitWidth + 18
    implicitHeight: 22
    radius: height / 2
    color: solid ? (tone === "brass" ? Theme.henFill : toneColor)
         : tone === "neutral" ? Theme.controlFill : Theme.alpha(toneColor, Theme.light ? 0.13 : 0.16)
    border.width: solid ? 0 : 1
    border.color: tone === "neutral" ? Theme.glassEdge : Theme.alpha(toneColor, 0.45)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5
        Icon {
            visible: tag.iconName.length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: tag.iconName
            size: 12
            color: label.color
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            text: tag.text
            color: tag.solid ? (tag.tone === "brass" ? Theme.textOnHen : Theme.textOnAccent) : tag.inkColor
            font.family: tag.mono ? Theme.fontMono : Theme.fontSans
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }
}
