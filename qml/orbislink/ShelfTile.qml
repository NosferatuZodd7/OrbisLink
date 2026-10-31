// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What every card on the games page's shelf has in common: a square picture
// on top (what goes in it is the card's) with soft corners and a hairline
// edge, the name under it and a line of technical facts in mono. Picked out,
// it lifts a little and an accent ring with a soft halo goes round the
// picture, as on the console's own home screen; a card can also keep a quiet
// halo of its own (green: installed on the console).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: tile

    property bool selected: false
    property bool dimmed: false
    // A lasting glow of the tile's own (green: installed on the console),
    // the same soft frame as the one picked; picking it shows the accent.
    property color glow: "transparent"
    readonly property bool glowing: glow.a > 0
    property int artSize: 148
    property string title: ""
    property string subtitle: ""
    // The line under the name is technical (a title ID, a version, a size):
    // set in mono. Off for words ("3 items", "Not available").
    property bool subtitleMono: true
    // The corner radius of the picture, which the cards' own art follows.
    readonly property int artRadius: 10
    // The square on top.
    default property alias content: holder.data

    // With the keys held (Shift picks a run of games while picking several).
    signal activated(int modifiers)

    readonly property bool hover: area.containsMouse
    // Where the pointer is over the tile, and where the last click was:
    // a card with a button of its own on the picture tells them apart.
    readonly property point pointer: Qt.point(area.mouseX, area.mouseY)
    property point clickPoint: Qt.point(-1, -1)

    width: artSize
    height: artSize + 10 + names.implicitHeight
    z: selected ? 2 : hover ? 1 : 0
    opacity: dimmed ? 0.55 : 1.0
    scale: area.pressed ? Theme.pressScale : selected ? 1.05 : hover ? 1.025 : 1.0
    transformOrigin: Item.Top
    Behavior on scale { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }

    Accessible.role: Accessible.Button
    Accessible.name: tile.title
    Accessible.description: tile.subtitle

    // A soft shadow under the picture, so the shelf reads as plates laid on
    // the page rather than holes cut in it.
    Rectangle {
        x: holder.x
        y: holder.y + (tile.hover || tile.selected ? 6 : 3)
        width: holder.width
        height: holder.height
        radius: tile.artRadius
        color: Qt.rgba(0, 0, 0, Theme.light ? (tile.hover || tile.selected ? 0.10 : 0.06)
                                            : (tile.hover || tile.selected ? 0.34 : 0.22))
        Behavior on y { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
    }

    // The soft halo around the one picked (or one with a glow of its own).
    Repeater {
        model: 3
        Rectangle {
            required property int index
            anchors.fill: holder
            anchors.margins: -(index + 2) * 2
            radius: tile.artRadius + (index + 2) * 2
            color: "transparent"
            border.width: 2
            border.color: tile.selected ? Theme.alpha(Theme.accent, (Theme.light ? 0.14 : 0.22) - index * 0.06)
                        : tile.glowing ? Theme.alpha(tile.glow, (Theme.light ? 0.16 : 0.24) - index * 0.07)
                        : "transparent"
            Behavior on border.color { ColorAnimation { duration: Theme.cardEase } }
        }
    }

    Item {
        id: holder
        width: tile.artSize
        height: tile.artSize
    }

    // The ring: the accent when picked, a wash of it on hover, the glow's
    // own colour when it has one.
    Rectangle {
        anchors.fill: holder
        anchors.margins: -3
        radius: tile.artRadius + 3
        color: "transparent"
        border.width: tile.selected ? 2 : 1.5
        border.color: tile.selected ? Theme.accent : tile.hover ? Theme.alpha(Theme.accent, 0.5)
                    : tile.glowing ? Theme.alpha(tile.glow, 0.7) : "transparent"
        Behavior on border.color { ColorAnimation { duration: Theme.cardEase } }
    }

    Column {
        id: names
        anchors.top: holder.bottom
        anchors.topMargin: 10
        width: tile.artSize
        spacing: 3
        Text {
            width: parent.width
            text: tile.title
            color: tile.selected ? Theme.ink : Theme.alpha(Theme.ink, 0.92)
            font.family: Theme.fontSans
            font.pixelSize: 13
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            lineHeight: 1.04
        }
        Text {
            visible: tile.subtitle.length > 0
            width: parent.width
            text: tile.subtitle
            color: Theme.textSecondary
            font.family: tile.subtitleMono ? Theme.fontMono : Theme.fontSans
            font.pixelSize: tile.subtitleMono ? 10 : 11
            elide: Text.ElideMiddle
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: (mouse) => {
            tile.clickPoint = Qt.point(mouse.x, mouse.y)
            tile.activated(mouse.modifiers)
        }
    }

    // ── shared pieces of the games page, kept here because every file of
    // the page already depends on this one (and it on none of them):
    // ShelfTile.EmptyState, ShelfTile.DialogHead and ShelfTile.FactRow.

    // An empty state, as the atlas leaves a plate blank: the faint planet, a
    // heading in the serif, one line of why, and the way on (children).
    component EmptyState: ColumnLayout {
        id: empty
        property string heading: ""
        property string line: ""
        default property alias actions: emptyActions.data
        spacing: 10
        Image {
            Layout.alignment: Qt.AlignHCenter
            source: "qrc:/icons/planet.png"
            sourceSize.width: 104
            sourceSize.height: 104
            fillMode: Image.PreserveAspectFit
            opacity: Theme.light ? 0.35 : 0.45
        }
        Text {
            Layout.fillWidth: true
            visible: empty.heading.length > 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: empty.heading
            color: Theme.ink
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontHeading
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            visible: empty.line.length > 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: empty.line
            color: Theme.textSecondary
            font.pixelSize: 13
        }
        RowLayout {
            id: emptyActions
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
            spacing: 8
        }
    }

    // The head of the games page's dialogs: a brass eyebrow (what kind of
    // thing it is), the title in the serif, an optional ‹ back and the ✕.
    component DialogHead: Item {
        id: head
        property string eyebrow: ""
        property string title: ""
        property var dialog: null
        property bool backable: false
        property string backText: qsTr("Back")
        signal back()
        implicitHeight: headColumn.implicitHeight + 26 + 12

        StyledToolButton {
            id: headBack
            visible: head.backable
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: headColumn.verticalCenter
            iconName: "chevron-left"
            Accessible.name: head.backText
            ToolTip.visible: hovered
            ToolTip.text: head.backText
            onClicked: head.back()
        }
        ColumnLayout {
            id: headColumn
            anchors.left: headBack.visible ? headBack.right : parent.left
            anchors.leftMargin: headBack.visible ? 8 : Theme.dialogMargin
            anchors.right: headClose.left
            anchors.rightMargin: 12
            anchors.top: parent.top
            anchors.topMargin: 26
            spacing: 4
            Eyebrow {
                Layout.fillWidth: true
                visible: head.eyebrow.length > 0
                text: head.eyebrow
            }
            Text {
                Layout.fillWidth: true
                text: head.title
                color: Theme.ink
                font.family: Theme.fontDisplay
                font.pixelSize: 24
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }
        StyledToolButton {
            id: headClose
            visible: head.dialog !== null
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.top: parent.top
            anchors.topMargin: 18
            iconName: "close"
            Accessible.name: qsTr("Close")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Close")
            onClicked: head.dialog.close()
        }
    }

    // A fact about something, as a row of a ledger: the name on the left in
    // the secondary ink, the value on the right (mono when it is technical),
    // a hairline under it unless it is the last.
    component FactRow: Item {
        id: fact
        property string label: ""
        property string value: ""
        property bool mono: false
        property bool last: false
        implicitHeight: 32
        Text {
            id: factLabel
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: fact.label
            color: Theme.textSecondary
            font.pixelSize: 12
        }
        Text {
            anchors.right: parent.right
            anchors.left: factLabel.right
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignRight
            text: fact.value
            color: Theme.text
            font.family: fact.mono ? Theme.fontMono : Theme.fontSans
            font.pixelSize: fact.mono ? 11 : 12
            elide: Text.ElideMiddle
        }
        Rectangle {
            visible: !fact.last
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.glassEdge
        }
    }
}
