// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The console's captures: its screenshots and video clips, read over FTP
// from where the console keeps them, newest first. A photo shows a small
// picture made on this PC; a clip shows its icon. Clicking one picks it;
// the picked ones (or all, with none picked) are copied to this PC, each
// into its game's folder, or deleted from the console after asking. On a PS5
// what can be read are the console's own previews (it keeps the originals
// encrypted), so the page says so and offers no delete.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    // The path of each capture picked.
    property var picked: ({})
    readonly property var pickedPaths: Object.keys(picked)
    readonly property int photos: captures.captures.filter(function (c) { return c.kind === "photo" }).length

    function toggle(path) {
        var next = Object.assign({}, picked)
        if (next[path])
            delete next[path]
        else
            next[path] = true
        picked = next
    }

    // A first look the first time it shows (and the console answers).
    onVisibleChanged: if (visible && !captures.scanned && !captures.busy && app.canUseFtp) captures.refresh()
    Connections {
        target: captures
        function onFinished(message, error) { toast.show(message, error) }
        function onCapturesChanged() {
            // What was picked and is gone stops being picked.
            var still = {}
            for (var i = 0; i < captures.captures.length; ++i)
                if (root.picked[captures.captures[i].path])
                    still[captures.captures[i].path] = true
            root.picked = still
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // What there is, and what can be done with it.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Theme.textSecondary
                font.pixelSize: 12
                text: captures.busy ? captures.status
                    : !captures.scanned ? qsTr("Screenshots and video clips, from %1 on the console").arg(captures.where)
                    : root.pickedPaths.length > 0 ? qsTr("%n picked", "", root.pickedPaths.length)
                    : qsTr("%n photo(s)", "", root.photos) + " · "
                      + qsTr("%n clip(s)", "", captures.captures.length - root.photos) + " · " + qsTr("newest first")
            }
            StyledButton {
                visible: root.pickedPaths.length > 0
                text: qsTr("Pick none")
                chip: true
                onClicked: root.picked = ({})
            }
            // Not on a PS5: deleting its preview would leave the original.
            StyledButton {
                visible: root.pickedPaths.length > 0 && !captures.ps5
                text: qsTr("Delete from the console…")
                iconName: "trash"
                danger: true
                enabled: (!captures.busy || captures.thumbnailing) && app.canUseFtp
                onClicked: confirmDelete.open()
            }
            StyledButton {
                visible: captures.captures.length > 0
                primary: true
                iconName: "laptop"
                enabled: (!captures.busy || captures.thumbnailing) && app.canUseFtp
                text: root.pickedPaths.length > 0 ? qsTr("Copy %n to this PC", "", root.pickedPaths.length)
                                                  : qsTr("Copy all to this PC")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Into %1, a folder per game; one already there is skipped").arg(captures.folder)
                              + (captures.ps5 ? "\n" + qsTr("From a PS5 these are its previews, smaller than the "
                                                           + "originals.") : "")
                onClicked: captures.download(root.pickedPaths)
            }
            StyledToolButton {
                iconName: "folder-open"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Open the folder on this PC")
                Accessible.name: ToolTip.text
                onClicked: captures.openFolder()
            }
            StyledToolButton {
                iconName: "refresh"
                enabled: (!captures.busy || captures.thumbnailing) && app.canUseFtp
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Read the console again")
                Accessible.name: ToolTip.text
                onClicked: captures.refresh()
            }
        }
        // A PS5's are previews: where the full-size ones and deleting are.
        Text {
            Layout.fillWidth: true
            visible: captures.ps5 && captures.captures.length > 0
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: 12
            text: qsTr("A PS5 keeps its originals encrypted: these are its own previews, smaller pictures. For "
                       + "full-size copies, or to delete, use its Media Gallery (it copies to a USB drive).")
        }
        // How far an action is.
        Rectangle {
            Layout.fillWidth: true
            visible: captures.busy
            height: 3
            radius: 1.5
            color: Theme.alpha(Theme.accent, 0.18)
            Rectangle {
                width: parent.width * Math.max(0.04, Math.min(1, captures.progress))
                height: parent.height
                radius: parent.radius
                color: Theme.accent
                Behavior on width { NumberAnimation { duration: 200 } }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Nothing to show: why.
            ShelfTile.EmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 460)
                visible: !captures.busy && captures.captures.length === 0
                heading: !app.canUseFtp ? qsTr("The console's FTP is not answering")
                       : captures.scanned ? qsTr("No captures on the console") : qsTr("The console's captures")
                line: !app.canUseFtp ? qsTr("Its captures are read over FTP: once it answers, they show here.")
                    : captures.scanned && captures.ps5
                    ? qsTr("Looked in %1 for the previews a PS5 makes of its captures (it keeps the originals "
                           + "encrypted). Full-size copies come from its Media Gallery: copy them to a USB drive.")
                      .arg(captures.where)
                    : captures.scanned
                    ? qsTr("Looked in %1, where a PS4 keeps its screenshots and clips (a folder per game). On a PS5 "
                           + "they may be elsewhere: the Files tab shows the console's folders.").arg(captures.where)
                    : qsTr("Its screenshots and video clips, to look at and copy to this PC.")
                StyledButton {
                    visible: app.canUseFtp && !captures.scanned
                    primary: true
                    iconName: "image"
                    text: qsTr("Read the console")
                    onClicked: captures.refresh()
                }
            }

            GridView {
                id: grid
                anchors.fill: parent
                visible: captures.captures.length > 0
                clip: true
                readonly property int fits: Math.max(1, Math.floor(width / 216))
                cellWidth: Math.floor(width / fits)
                cellHeight: Math.round(cellWidth * 0.56) + 44
                model: captures.captures
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                boundsBehavior: Flickable.StopAtBounds

                delegate: Item {
                    id: tile
                    required property var modelData
                    readonly property bool isPicked: root.picked[modelData.path] === true
                    readonly property string thumb: captures.thumbs[modelData.path] || ""
                    width: grid.cellWidth
                    height: grid.cellHeight

                    Rectangle {
                        id: frame
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 6
                        height: Math.round(grid.cellWidth * 0.56)
                        radius: Theme.radiusSmall
                        clip: true
                        color: Theme.controlFill
                        border.width: tile.isPicked ? 2 : 1
                        border.color: tile.isPicked ? Theme.accent : Theme.glassEdge
                        Image {
                            anchors.fill: parent
                            anchors.margins: 1
                            visible: status === Image.Ready
                            source: tile.thumb
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.width: 480
                        }
                        Icon {
                            anchors.centerIn: parent
                            visible: tile.thumb.length === 0
                            name: tile.modelData.kind === "video" ? "play" : "image"
                            size: 28
                            color: Theme.textSecondary
                        }
                        // A clip says so in its corner.
                        Rectangle {
                            visible: tile.modelData.kind === "video"
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: 6
                            radius: 6
                            color: Qt.rgba(0, 0, 0, 0.55)
                            implicitWidth: clipLabel.implicitWidth + 12
                            implicitHeight: clipLabel.implicitHeight + 4
                            Text {
                                id: clipLabel
                                anchors.centerIn: parent
                                text: qsTr("Clip")
                                color: "#FFFFFF"
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                            }
                        }
                        Rectangle {
                            visible: tile.isPicked
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 6
                            width: 22
                            height: 22
                            radius: 11
                            color: Theme.accent
                            Icon {
                                anchors.centerIn: parent
                                name: "check"
                                size: 13
                                strokeWidth: 2.5
                                color: "#FFFFFF"
                            }
                        }
                        MouseArea {
                            id: tileArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.toggle(tile.modelData.path)
                        }
                        ToolTip.visible: tileArea.containsMouse
                        ToolTip.delay: 600
                        ToolTip.text: tile.modelData.name + " — " + tile.modelData.sizeText
                    }
                    Text {
                        anchors.left: frame.left
                        anchors.right: frame.right
                        anchors.top: frame.bottom
                        anchors.topMargin: 6
                        elide: Text.ElideRight
                        color: Theme.text
                        font.pixelSize: 12
                        text: [tile.modelData.game, tile.modelData.dateText].filter(function (p) {
                            return p && p.length > 0
                        }).join(" · ") || tile.modelData.name
                    }
                }
            }
        }
    }

    // Deleting from the console asks first: the console keeps no other copy.
    Dialog {
        id: confirmDelete
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 460
        modal: true
        padding: 0
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: ShelfTile.DialogGlass {}
        header: DialogHeader {
            title: qsTr("Delete %n capture(s) from the console?", "", root.pickedPaths.length)
            dialog: confirmDelete
        }
        contentItem: Text {
            leftPadding: Theme.dialogMargin
            rightPadding: Theme.dialogMargin
            topPadding: 6
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: 13
            text: qsTr("They are deleted from the console for good. Copy them to this PC first to keep them.")
        }
        footer: Item {
            implicitHeight: Theme.dialogFooter
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 8
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancel")
                    onClicked: confirmDelete.close()
                }
                StyledButton {
                    text: qsTr("Delete from the console")
                    iconName: "trash"
                    danger: true
                    solid: true
                    enabled: !captures.busy || captures.thumbnailing
                    onClicked: { captures.remove(root.pickedPaths); root.picked = ({}); confirmDelete.close() }
                }
            }
        }
    }
}
