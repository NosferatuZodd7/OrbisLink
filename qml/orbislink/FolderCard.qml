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
    subtitleMono: false

    Rectangle {
        anchors.fill: parent
        radius: card.artRadius
        border.width: 1
        border.color: "transparent"
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.cardTop }
            GradientStop { position: 1.0; color: Theme.cardBottom }
        }
    }

    // Three covers and the folder's sign, two by two.
    Grid {
        visible: card.previews.length > 0
        anchors.fill: parent
        anchors.margins: 10
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
                    crop: true
                    visible: parent.index < 3 && parent.index < card.previews.length
                    picture: visible ? (card.previews[parent.index].picture || "") : ""
                    placeholder: visible ? (card.previews[parent.index].placeholder || "bd") : "bd"
                    radius: 6
                }
                Rectangle {
                    anchors.fill: parent
                    visible: parent.index < 3 && parent.index >= card.previews.length
                    radius: 6
                    color: Theme.alpha(Theme.ink, 0.04)
                    border.width: 1
                    border.color: Theme.alpha(Theme.ink, 0.06)
                }
                Item {
                    anchors.fill: parent
                    visible: parent.index === 3
                    Icon {
                        anchors.centerIn: parent
                        name: "folder"
                        size: parent.width * 0.56
                        strokeWidth: 1.5
                        color: Theme.alpha(Theme.ink, 0.8)
                    }
                }
            }
        }
    }

    DiscPlaceholder {
        anchors.fill: parent
        visible: card.previews.length === 0
        variant: "folder"
        radius: card.artRadius
    }

    // How many things it holds, in the corner.
    ShelfTile.CoverTag {
        visible: card.count > 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        text: card.count
        mono: true
    }
}
