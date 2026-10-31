// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A plate: the one surface pages are built of — a card, a group of
// settings, a list. Soft corners, a hairline edge, nothing else.
//
// * ornate: a second brass hairline inside the edge, for what is the point
//   of the page (the console in use, the makers in About). Rarely.
// * sunken: one step below the page instead of above it (the inside of a
//   plate: a list, a log).
import QtQuick

Rectangle {
    id: plate

    property bool ornate: false
    property bool sunken: false
    property bool hoverable: false
    readonly property bool hovered: hoverable && hover.hovered

    radius: Theme.radius
    color: sunken ? (Theme.light ? Theme.alpha(Theme.ink, 0.04) : Qt.rgba(0, 0, 0, 0.16))
                  : Theme.panelFill
    border.width: 1
    border.color: plate.ornate ? Theme.alpha(Theme.brass, 0.6)
                : plate.hovered ? Theme.alpha(Theme.accent, 0.55) : Theme.glassEdge
    Behavior on border.color { ColorAnimation { duration: Theme.fast } }

    Rectangle {
        visible: plate.ornate
        anchors.fill: parent
        anchors.margins: 5
        radius: Math.max(0, plate.radius - 5)
        color: "transparent"
        border.width: 1
        border.color: Theme.alpha(Theme.brass, 0.24)
    }

    HoverHandler { id: hover; enabled: plate.hoverable }
}
