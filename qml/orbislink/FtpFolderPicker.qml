// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A folder of the console, picked like in a file explorer: quick access on
// the left, the folder tree on the right (each folder is listed when it is
// opened), the chosen path below, and a new folder made on the spot.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: picker

    // What is being moved: it and what is inside it cannot be the destination.
    property string movingPath: ""
    property string movingName: ""
    property string selected: "/"
    // A path to open the tree down to, as its folders get listed.
    property string revealing: ""

    signal chosen(string path)

    function norm(p) {
        if (!p)
            return "/"
        p = p.replace(/\/+$/, "")
        return p.length > 0 ? p : "/"
    }
    function childPath(parent, name) { return parent === "/" ? "/" + name : parent + "/" + name }
    function parentOf(p) {
        p = norm(p)
        var cut = p.lastIndexOf("/")
        return cut <= 0 ? "/" : p.substring(0, cut)
    }
    function excluded(p) {
        var m = norm(movingPath)
        return movingPath.length > 0 && (p === m || p.indexOf(m + "/") === 0)
    }
    readonly property bool canMove: !excluded(norm(selected)) && norm(selected) !== parentOf(movingPath)

    function indexOf(path) {
        for (var i = 0; i < rows.count; ++i)
            if (rows.get(i).path === path)
                return i
        return -1
    }
    function expand(i) {
        var r = rows.get(i)
        if (r.expanded)
            return
        rows.setProperty(i, "expanded", true)
        rows.setProperty(i, "loading", true)
        app.ftpListFolders(r.path)
    }
    function removeChildren(i) {
        var depth = rows.get(i).depth
        while (i + 1 < rows.count && rows.get(i + 1).depth > depth)
            rows.remove(i + 1)
    }
    function toggle(i) {
        if (rows.get(i).expanded) {
            removeChildren(i)
            rows.setProperty(i, "expanded", false)
        } else
            expand(i)
    }
    // Opens the tree folder by folder down to `revealing`.
    function continueReveal() {
        if (revealing.length === 0)
            return
        var parts = revealing.split("/").filter(function (s) { return s.length > 0 })
        var current = "/"
        for (var k = 0; k < parts.length; ++k) {
            var next = childPath(current, parts[k])
            if (indexOf(next) < 0) {
                var at = indexOf(current)
                if (at < 0)
                    return
                if (!rows.get(at).expanded)
                    expand(at)
                else if (!rows.get(at).loading)
                    revealing = "" // not there: stop at the closest folder
                return
            }
            current = next
        }
        revealing = ""
        var target = indexOf(current)
        if (target >= 0)
            tree.positionViewAtIndex(target, ListView.Contain)
    }
    function reveal(path) {
        selected = norm(path)
        revealing = selected
        continueReveal()
    }

    function openFor(path, name, startFolder) {
        movingPath = path
        movingName = name
        rows.clear()
        rows.append({ path: "/", name: qsTr("Console"), depth: 0, expanded: false, loading: false,
                      empty: false, error: "" })
        open()
        expand(0)
        reveal(startFolder)
    }

    ListModel { id: rows }

    Connections {
        target: app
        function onFtpFoldersListed(path, folders, error) {
            var i = picker.indexOf(picker.norm(path))
            if (i < 0 || !rows.get(i).expanded)
                return
            picker.removeChildren(i)
            rows.setProperty(i, "loading", false)
            rows.setProperty(i, "error", error)
            rows.setProperty(i, "empty", error.length === 0 && folders.length === 0)
            var sorted = folders.slice().sort(function (a, b) {
                return a.toLowerCase() < b.toLowerCase() ? -1 : a.toLowerCase() > b.toLowerCase() ? 1 : 0
            })
            var parent = rows.get(i)
            for (var k = 0; k < sorted.length; ++k)
                rows.insert(i + 1 + k, { path: picker.childPath(parent.path, sorted[k]), name: sorted[k],
                                         depth: parent.depth + 1, expanded: false, loading: false,
                                         empty: false, error: "" })
            picker.continueReveal()
        }
        function onFtpFolderCreated(path, error) {
            if (!picker.visible)
                return
            if (error.length > 0) {
                newFolderError.text = error
                return
            }
            newFolderPopup.close()
            // List the parent again and land on the new folder.
            var i = picker.indexOf(picker.parentOf(path))
            if (i >= 0) {
                picker.removeChildren(i)
                rows.setProperty(i, "expanded", false)
            }
            picker.reveal(path)
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 48 : 720, 720)
    height: Math.min(parent ? parent.height - 48 : 560, 560)
    modal: true
    padding: 0

    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radiusDialog
    }

    header: DialogHeader {
        title: qsTr("Move to…")
        dialog: picker
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            text: qsTr("Where should %1 go?").arg(picker.movingName)
            color: Theme.textSecondary
            font.pixelSize: 12
            elide: Text.ElideMiddle
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 12

            // ── quick access
            ColumnLayout {
                Layout.fillWidth: false
                Layout.preferredWidth: 170
                Layout.maximumWidth: 170
                Layout.fillHeight: true
                spacing: 2
                Text {
                    text: qsTr("Quick access")
                    color: Theme.textMuted
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    Layout.bottomMargin: 4
                }
                Repeater {
                    model: app.ftpShortcuts
                    ItemDelegate {
                        required property string modelData
                        readonly property string target: picker.norm(modelData)
                        Layout.fillWidth: true
                        implicitHeight: 32
                        padding: 0
                        background: Rectangle {
                            radius: 8
                            color: picker.norm(picker.selected) === parent.target ? Theme.accentFill
                                 : parent.hovered ? Theme.controlFill : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            Item { implicitWidth: 2 }
                            Icon {
                                name: modelData.indexOf("/mnt/") === 0 ? "hard-drive" : "folder"
                                size: 15
                                color: Theme.accent
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData
                                color: Theme.text
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                        onClicked: picker.reveal(target)
                    }
                }
                Item { Layout.fillHeight: true }
            }

            Rectangle {
                Layout.fillHeight: true
                implicitWidth: 1
                color: Theme.glassEdge
            }

            // ── the tree
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 12
                color: Theme.controlFill
                clip: true

                ListView {
                    id: tree
                    anchors.fill: parent
                    anchors.margins: 6
                    model: rows
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { }

                    delegate: ItemDelegate {
                        id: node
                        required property int index
                        required property string path
                        required property string name
                        required property int depth
                        required property bool expanded
                        required property bool loading
                        required property bool empty
                        required property string error
                        readonly property bool blocked: picker.excluded(path)
                        readonly property bool current: picker.norm(picker.selected) === path
                        width: tree.width
                        implicitHeight: 30
                        padding: 0
                        enabled: !blocked
                        opacity: blocked ? 0.4 : 1.0
                        background: Rectangle {
                            radius: 8
                            color: node.current ? Theme.accentFill
                                 : node.hovered ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                                 : "transparent"
                            border.width: node.current ? 1 : 0
                            border.color: Theme.alpha(Theme.accent, 0.6)
                        }
                        contentItem: RowLayout {
                            spacing: 6
                            Item { implicitWidth: 4 + node.depth * 18 }
                            // Opens / closes; turns while the folder is listed.
                            Item {
                                implicitWidth: 18
                                implicitHeight: 18
                                // The chevron turns here; the icon's own
                                // rotation is the spinner's.
                                Item {
                                    anchors.fill: parent
                                    rotation: node.expanded && !node.loading ? 90 : 0
                                    Behavior on rotation { NumberAnimation { duration: Theme.fast } }
                                    Icon {
                                        anchors.centerIn: parent
                                        visible: !node.empty
                                        name: node.loading ? "loader" : "chevron-right"
                                        spinning: node.loading
                                        size: 14
                                        color: Theme.textSecondary
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    enabled: !node.empty
                                    onClicked: picker.toggle(node.index)
                                }
                            }
                            Icon {
                                name: node.depth === 0 ? "hard-drive" : node.expanded ? "folder-open" : "folder"
                                size: 16
                                color: node.current ? Theme.accent : Theme.alpha(Theme.accent, 0.8)
                            }
                            Text {
                                Layout.fillWidth: true
                                text: node.error.length > 0 ? node.name + "  —  " + node.error : node.name
                                color: node.error.length > 0 ? Theme.error : Theme.text
                                font.pixelSize: 12
                                font.weight: node.current ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                            }
                        }
                        onClicked: picker.selected = path
                        onDoubleClicked: picker.toggle(index)
                    }
                }
            }
        }

        // ── where it goes
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 10
            Icon { name: "folder-open"; size: 16; color: Theme.textSecondary }
            Text {
                Layout.fillWidth: true
                text: picker.canMove ? picker.selected
                    : picker.excluded(picker.norm(picker.selected))
                      ? qsTr("%1 — a folder cannot go inside itself").arg(picker.selected)
                      : qsTr("%1 — it is already here").arg(picker.selected)
                color: picker.canMove ? Theme.text : Theme.warn
                font.pixelSize: 12
                elide: Text.ElideMiddle
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
            spacing: 10
            StyledButton {
                id: newFolderButton
                text: qsTr("New folder")
                iconName: "plus"
                enabled: !picker.excluded(picker.norm(picker.selected))
                onClicked: {
                    newFolderName.text = ""
                    newFolderError.text = ""
                    newFolderPopup.open()
                    newFolderName.forceActiveFocus()
                }
            }
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Cancel")
                minimumWidth: 100
                onClicked: picker.close()
            }
            StyledButton {
                text: qsTr("Move here")
                primary: true
                minimumWidth: 120
                enabled: picker.canMove
                onClicked: {
                    picker.chosen(picker.norm(picker.selected))
                    picker.close()
                }
            }
        }
    }

    // A new folder inside the selected one.
    Popup {
        id: newFolderPopup
        parent: picker.footer
        x: Theme.dialogMargin
        y: -height - 6
        width: 320
        padding: 14
        modal: false
        background: Rectangle {
            color: Theme.menuFill
            border.color: Theme.glassEdge
            radius: 14
        }
        function create() {
            var name = newFolderName.text.trim()
            if (name.length === 0 || name.indexOf("/") >= 0)
                return
            app.ftpCreateFolder(picker.childPath(picker.norm(picker.selected), name))
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                text: qsTr("New folder in %1").arg(picker.norm(picker.selected))
                color: Theme.textSecondary
                font.pixelSize: 11
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                StyledField {
                    id: newFolderName
                    Layout.fillWidth: true
                    placeholderText: qsTr("Folder name")
                    onAccepted: newFolderPopup.create()
                }
                StyledButton {
                    text: qsTr("Create")
                    primary: true
                    enabled: newFolderName.text.trim().length > 0 && newFolderName.text.indexOf("/") < 0
                    onClicked: newFolderPopup.create()
                }
            }
            Text {
                id: newFolderError
                visible: text.length > 0
                Layout.fillWidth: true
                color: Theme.error
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }
}
