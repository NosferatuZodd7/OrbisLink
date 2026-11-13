// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Drag-and-drop overlay: two zones, one per mode, on an ornate plate over
// the dimmed window.
// The stream keeps running underneath — nothing is paused.
//
// There is no DropArea here. One DropArea per zone does not work: the
// window's one keeps hold of the drag and never lets go, and the zones never
// learn the cursor is over them. There is a single DropArea, on the window,
// which says where the cursor is; the zone pointed at is worked out here.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    property bool active: false

    // Cursor position inside the window, in window coordinates.
    property real pointerX: -1
    property real pointerY: -1

    // 0 = direct install, 1 = FTP upload, -1 = outside both.
    readonly property int zoneUnderPointer: {
        if (!active || pointerX < 0)
            return -1
        if (containsPoint(installZone))
            return 0
        if (containsPoint(ftpZone))
            return 1
        return -1
    }

    function containsPoint(item) {
        var p = item.mapFromItem(null, pointerX, pointerY)
        return p.x >= 0 && p.y >= 0 && p.x <= item.width && p.y <= item.height
    }

    function show(x, y) {
        pointerX = x === undefined ? pointerX : x
        pointerY = y === undefined ? pointerY : y
        active = true
    }

    function movePointer(x, y) {
        pointerX = x
        pointerY = y
    }

    function hide() {
        active = false
        pointerX = -1
        pointerY = -1
    }

    // Called by the window's DropArea: returns true if the file was accepted.
    function dropAt(x, y, urls) {
        movePointer(x, y)
        var zoneName = zoneUnderPointer
        hide()
        if (zoneName === 0 && app.canInstallDirectly) {
            app.dropUrls(urls, 0)
            return true
        }
        if (zoneName === 1 && app.canUseFtp) {
            app.dropUrls(urls, 1)
            return true
        }
        return false
    }

    anchors.fill: parent
    visible: opacity > 0
    opacity: active ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 140 } }

    // The whole window dims a little; the picture keeps going underneath.
    Rectangle {
        anchors.fill: parent
        color: Theme.light ? Qt.rgba(40 / 255, 36 / 255, 30 / 255, 0.30) : Qt.rgba(0, 0, 0, 0.55)
    }

    // The sheet: a card of floating glass that rises into place, holding
    // the two big glass targets.
    Plate {
        id: sheet
        level: "secondary"
        anchors.centerIn: parent
        anchors.verticalCenterOffset: root.active ? 0 : 8
        Behavior on anchors.verticalCenterOffset { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
        width: Math.min(parent.width - 80, 760)
        height: Math.min(parent.height - 80, 440)
        radius: Theme.radiusPane + 6
        // Nearly opaque: what is under it must not compete with the targets.
        color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, Theme.light ? 0.95 : 0.93)
        border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.08)

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            anchors.topMargin: 26
            spacing: 4

            Eyebrow {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Files to the console")
            }

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Drop to…")
                color: Theme.ink
                font.pixelSize: Theme.fontTitle
                font.family: Theme.fontDisplay
                font.weight: Font.Bold
                font.letterSpacing: -0.4
            }

            Item { Layout.preferredHeight: 16 }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 16

                DropZone {
                    id: ftpZone
                    objectName: "ftpZone"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: qsTr("Send over FTP")
                    subtitle: qsTr("Copies to %1").arg(app.ftpPath)
                    glyph: "cloud-upload"
                    enabledZone: app.canUseFtp
                    disabledReason: app.ftpHint
                    highlighted: root.zoneUnderPointer === 1
                }

                DropZone {
                    id: installZone
                    objectName: "installZone"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: qsTr("Install directly")
                    subtitle: qsTr("The console downloads from this PC and installs")
                    glyph: "download"
                    enabledZone: app.canInstallDirectly
                    disabledReason: app.installerHint
                    highlighted: root.zoneUnderPointer === 0
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: 10
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Takes several files and folders (it looks for .pkg inside)")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
            }
        }
    }
}
