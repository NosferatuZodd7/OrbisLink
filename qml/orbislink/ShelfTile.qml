// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What every card on the games page's shelf has in common: a square picture
// on top (what goes in it is the card's), the name under it, and the way it
// is picked out — the accent frame and soft glow of the console cards, and
// a slight zoom, as on the console's own home screen.
import QtQuick

Item {
    id: tile

    property bool selected: false
    property bool dimmed: false
    property int artSize: 148
    property string title: ""
    property string subtitle: ""
    // The square on top.
    default property alias content: holder.data

    signal activated()

    readonly property bool hover: area.containsMouse

    width: artSize
    height: artSize + 10 + names.implicitHeight
    z: selected ? 2 : hover ? 1 : 0
    opacity: dimmed ? 0.55 : 1.0
    scale: area.pressed ? Theme.pressScale : selected ? 1.07 : hover ? 1.03 : 1.0
    transformOrigin: Item.Top
    Behavior on scale { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }

    // The soft frame around the one picked.
    Repeater {
        model: 3
        Rectangle {
            required property int index
            anchors.fill: holder
            anchors.margins: -(index + 2) * 2
            radius: 8 + (index + 1) * 2
            color: "transparent"
            border.width: 2
            border.color: tile.selected ? Theme.alpha(Theme.accent, (Theme.light ? 0.14 : 0.22) - index * 0.06)
                                        : "transparent"
            Behavior on border.color { ColorAnimation { duration: Theme.cardEase } }
        }
    }

    Item {
        id: holder
        width: tile.artSize
        height: tile.artSize
    }

    Rectangle {
        anchors.fill: holder
        anchors.margins: -3
        radius: 9
        color: "transparent"
        border.width: 2
        border.color: tile.selected ? Theme.accent : tile.hover ? Theme.alpha(Theme.accent, 0.45) : "transparent"
        Behavior on border.color { ColorAnimation { duration: Theme.cardEase } }
    }

    Column {
        id: names
        anchors.top: holder.bottom
        anchors.topMargin: 10
        width: tile.artSize
        spacing: 2
        Text {
            width: parent.width
            text: tile.title
            color: tile.selected ? Theme.text : Theme.alpha(Theme.text, 0.9)
            font.pixelSize: 12
            font.weight: tile.selected ? Font.DemiBold : Font.Medium
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            lineHeight: 1.05
        }
        Text {
            visible: tile.subtitle.length > 0
            width: parent.width
            text: tile.subtitle
            color: Theme.textSecondary
            font.pixelSize: 11
            elide: Text.ElideMiddle
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: tile.activated()
    }
}
