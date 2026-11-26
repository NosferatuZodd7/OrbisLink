// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The console's captures: its screenshots and video clips, read over FTP
// from where the console keeps them, newest first. A photo shows a small
// picture made on this PC; a clip shows its icon. Clicking one shows it
// whole, with a button to copy it; its corner's circle picks it (and, with
// some picked, a click picks too). The picked ones (or all, with none
// picked) are copied to this PC, each
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

    // The one shown whole, by its place in the list.
    function show(index) {
        if (index < 0 || index >= captures.captures.length)
            return
        viewer.index = index
        viewer.full = ""
        viewer.failed = false
        if (!viewer.opened)
            viewer.open()
        if (captures.captures[index].kind === "photo")
            captures.preview(captures.captures[index].path)
    }

    // A first look the first time it shows (and the console answers).
    onVisibleChanged: if (visible && !captures.scanned && !captures.busy && app.canUseFtp) captures.refresh()
    Connections {
        target: captures
        function onFinished(message, error) { toast.show(message, error) }
        function onPreviewReady(path, url) {
            if (viewer.opened && viewer.capture.path === path) {
                viewer.full = url
                viewer.failed = url.length === 0
            }
        }
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
                    required property int index
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
                            // Shown whole; with some picked (or Ctrl held), picked.
                            onClicked: (mouse) => {
                                if (root.pickedPaths.length > 0 || (mouse.modifiers & Qt.ControlModifier))
                                    root.toggle(tile.modelData.path)
                                else
                                    root.show(tile.index)
                            }
                        }
                        // The circle that picks it, on hover (filled once picked).
                        Rectangle {
                            visible: !tile.isPicked && (tileArea.containsMouse || pickArea.containsMouse
                                                        || root.pickedPaths.length > 0)
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 6
                            width: 22
                            height: 22
                            radius: 11
                            color: pickArea.containsMouse ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(0, 0, 0, 0.35)
                            border.width: 2
                            border.color: "#FFFFFF"
                        }
                        MouseArea {
                            id: pickArea
                            anchors.right: parent.right
                            anchors.top: parent.top
                            width: 40
                            height: 40
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

    // One capture shown whole: its small picture at once, the whole one once
    // fetched; ← and → go to the one before and after.
    Dialog {
        id: viewer
        property int index: -1
        property string full: ""
        property bool failed: false
        readonly property var capture: index >= 0 && index < captures.captures.length ? captures.captures[index] : ({})
        readonly property string thumb: capture.path ? (captures.thumbs[capture.path] || "") : ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 1180)
        height: Math.min(parent.height - 48, width * 0.56 + 140)
        modal: true
        padding: 0
        focus: true
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: ShelfTile.DialogGlass {}
        onClosed: { full = ""; index = -1 }
        header: DialogHeader {
            eyebrow: viewer.capture.game || qsTr("Capture")
            title: [viewer.capture.name || "", viewer.capture.dateText || ""].filter(function (p) {
                return p.length > 0 }).join(" · ")
            dialog: viewer
        }
        contentItem: Item {
            focus: true
            Keys.onLeftPressed: root.show(viewer.index - 1)
            Keys.onRightPressed: root.show(viewer.index + 1)
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                radius: Theme.radiusSmall
                color: Qt.rgba(0, 0, 0, 0.35)
                clip: true
                Image {
                    id: small
                    anchors.fill: parent
                    visible: !whole.visible && status === Image.Ready
                    source: viewer.thumb
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }
                Image {
                    id: whole
                    anchors.fill: parent
                    visible: status === Image.Ready
                    source: viewer.full
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    mipmap: true
                }
                Icon {
                    anchors.centerIn: parent
                    visible: !whole.visible && !small.visible
                    name: viewer.capture.kind === "video" ? "play" : "image"
                    size: 40
                    color: Theme.textSecondary
                }
                Spinner {
                    anchors.centerIn: parent
                    visible: viewer.capture.kind === "photo" && !whole.visible && !viewer.failed
                }
                // A clip is copied, not played here.
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 14
                    visible: viewer.capture.kind === "video" || viewer.failed
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    text: viewer.failed ? qsTr("The whole picture could not be fetched now: this is its small one.")
                                        : qsTr("A clip: copy it to this PC to play it.")
                }
                // The one before and after.
                StyledToolButton {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    visible: viewer.index > 0
                    iconName: "chevron-left"
                    Accessible.name: qsTr("Previous")
                    onClicked: root.show(viewer.index - 1)
                }
                StyledToolButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    visible: viewer.index < captures.captures.length - 1
                    iconName: "chevron-right"
                    Accessible.name: qsTr("Next")
                    onClicked: root.show(viewer.index + 1)
                }
            }
        }
        footer: Item {
            implicitHeight: Theme.dialogFooter
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    text: viewer.capture.sizeText || ""
                }
                StyledButton {
                    text: root.picked[viewer.capture.path] === true ? qsTr("Picked") : qsTr("Pick")
                    iconName: root.picked[viewer.capture.path] === true ? "check" : ""
                    onClicked: root.toggle(viewer.capture.path)
                }
                StyledButton {
                    text: qsTr("Copy to this PC")
                    iconName: "download"
                    solid: true
                    enabled: !captures.busy || captures.thumbnailing
                    onClicked: captures.download([viewer.capture.path])
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
