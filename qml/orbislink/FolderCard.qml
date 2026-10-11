// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A folder on the shelf, as the console shows its folders: the covers of
// the first things in it, three to a tile with the folder's own sign in the
// fourth corner, and how many things it holds. An empty or picture-less
// folder shows the sign alone.
import QtQuick

ShelfTile {
    id: card

    property var info: ({})
    readonly property var previews: info.previews || []
    readonly property int count: info.count === undefined ? -1 : info.count

    title: info.title || info.name || ""
    subtitle: count < 0 ? "" : qsTr("%n item(s)", "", count)

    Rectangle {
        anchors.fill: parent
        radius: 6
        border.width: 1
        border.color: Theme.cardEdge
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.cardTop }
            GradientStop { position: 1.0; color: Theme.cardBottom }
        }
    }

    // Three covers and the folder's sign, two by two.
    Grid {
        visible: card.previews.length > 0
        anchors.fill: parent
        anchors.margins: 8
        columns: 2
        spacing: 6
        readonly property real cell: (width - spacing) / 2
        Repeater {
            model: 4
            Item {
                required property int index
                width: parent.cell
                height: parent.cell
                CoverArt {
                    anchors.fill: parent
                    visible: parent.index < 3 && parent.index < card.previews.length
                    picture: visible ? (card.previews[parent.index].picture || "") : ""
                    placeholder: visible ? (card.previews[parent.index].placeholder || "bd") : "bd"
                    radius: 4
                }
                Rectangle {
                    anchors.fill: parent
                    visible: parent.index < 3 && parent.index >= card.previews.length
                    radius: 4
                    color: Theme.alpha(Theme.text, 0.04)
                }
                Item {
                    anchors.fill: parent
                    visible: parent.index === 3
                    Icon {
                        anchors.centerIn: parent
                        name: "folder"
                        size: parent.width * 0.62
                        strokeWidth: 1.5
                        color: Theme.alpha(Theme.text, 0.85)
                    }
                }
            }
        }
    }

    DiscPlaceholder {
        anchors.fill: parent
        visible: card.previews.length === 0
        variant: "folder"
    }

    Rectangle {
        visible: card.count > 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        radius: 9
        color: Theme.alpha(Theme.cardBottom, 0.9)
        border.width: 1
        border.color: Theme.cardEdge
        implicitWidth: Math.max(18, countLabel.implicitWidth + 10)
        implicitHeight: 18
        Text {
            id: countLabel
            anchors.centerIn: parent
            text: card.count
            color: Theme.cardText
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }
    }
}
