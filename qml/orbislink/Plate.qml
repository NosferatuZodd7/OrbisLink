// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A pane of glass: the one surface the interface is built of.
//
// Four weights (level):
// * "window"    — the one large pane the whole application sits in, floating
//                 over the room, with a soft shadow.
// * "primary"   — a pane inside it (navigation, toolbar, the page, the
//                 transfers): clearer glass with a luminous white edge.
// * "secondary" — toolbars, menus, popovers and floating actions over a pane.
// * "subtle"    — what sits inside a pane (a card, a group of settings, a
//                 list): barely there, so glass is never stacked on glass.
//
// Every weight has the same parts: a translucent fill a little lighter at
// the top, a fine edge lit along its upper side, and — for the two that
// float — a shadow underneath. There is no live blur: the room behind is
// painted already out of focus (GlassBackground), so the panes read as
// frosted without costing the GPU anything.
//
// * ornate: a champagne edge, for the single thing a screen is about (the
//   console in use, the makers in About). Rarely.
// * sunken: a well one step below its pane (a log, a file list).
// * hoverable: the edge brightens under the mouse.
import QtQuick

Rectangle {
    id: plate

    property string level: "subtle"
    property bool ornate: false
    property bool sunken: false
    property bool hoverable: false
    readonly property bool hovered: hoverable && hover.hovered

    readonly property bool floating: (level === "window" || level === "secondary") && !sunken
    readonly property real fillAlpha: sunken ? (Theme.light ? 0.30 : 0.22)
                                     : level === "window" ? Theme.glassWindow
                                     : level === "primary" ? Theme.glassPrimary
                                     : level === "secondary" ? Theme.glassSecondary
                                     : Theme.light ? Theme.glassSubtle : Theme.glassSubtle + (hovered ? 0.04 : 0)

    radius: level === "window" ? Theme.radiusWindow : level === "primary" ? Theme.radiusPane : Theme.radius
    color: sunken ? (Theme.light ? Qt.rgba(1, 1, 1, fillAlpha) : Qt.rgba(0, 0, 0, fillAlpha))
         : (level === "subtle" || level === "primary") && !Theme.light ? Qt.rgba(1, 1, 1, fillAlpha)
         : level === "primary" ? Qt.rgba(1, 1, 1, fillAlpha)
         : Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, fillAlpha)
    border.width: 1
    border.color: plate.ornate ? Theme.alpha(Theme.brass, 0.70)
                : plate.hovered ? (Theme.light ? Theme.lightHairlineStrong : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.16))
                : plate.sunken ? Theme.glassEdge
                : level === "primary" || level === "window" ? (Theme.light ? Qt.rgba(1, 1, 1, 0.85) : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.12))
                // On Daylight a white edge would vanish on a white dialog.
                : Theme.light ? Theme.lightHairline : Theme.glassEdge
    Behavior on border.color { ColorAnimation { duration: Theme.fast } }
    Behavior on color { ColorAnimation { duration: Theme.fast } }

    // The shadow, under the pane (below the parent's own fill).
    Rectangle {
        z: -1
        visible: plate.floating
        anchors.fill: parent
        anchors.topMargin: 10
        anchors.bottomMargin: -14
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        radius: plate.radius + 6
        color: Qt.rgba(0, 0, 0, Theme.light ? 0.06 : 0.20)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            radius: parent.radius + 6
            color: Qt.rgba(0, 0, 0, Theme.light ? 0.03 : 0.10)
        }
    }

    // The light on the glass: a sheen from the top, fading by mid-height.
    Rectangle {
        visible: !plate.sunken
        anchors.fill: parent
        anchors.margins: 1
        radius: Math.max(0, plate.radius - 1)
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.38 : Theme.glassHighlight + (plate.floating ? 0.03 : 0)) }
            GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0) }
        }
    }

    // The lit upper edge: a hairline that is brightest in the middle.
    Rectangle {
        visible: !plate.sunken
        anchors.top: parent.top
        anchors.topMargin: 1
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - plate.radius * 1.6
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0) }
            GradientStop { position: 0.5; color: plate.ornate ? Theme.alpha(Theme.brass, 0.9) : Theme.glassStrokeTop }
            GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0) }
        }
    }

    // Ornate: a second, fainter champagne edge inside the first.
    Rectangle {
        visible: plate.ornate
        anchors.fill: parent
        anchors.margins: 4
        radius: Math.max(0, plate.radius - 4)
        color: "transparent"
        border.width: 1
        border.color: Theme.alpha(Theme.brass, 0.22)
    }

    HoverHandler { id: hover; enabled: plate.hoverable }
}
