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

    // The whole window dims; the picture keeps going underneath.
    Rectangle {
        anchors.fill: parent
        color: Theme.light ? Qt.rgba(32 / 255, 43 / 255, 50 / 255, 0.55) : Qt.rgba(4 / 255, 10 / 255, 18 / 255, 0.78)
    }

    // The sheet: a single ornate plate (brass double edge), since for the
    // moment of a drop it is the only thing on screen that matters.
    Plate {
        id: sheet
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 760)
        height: Math.min(parent.height - 80, 460)
        radius: Theme.radiusDialog + 6
        ornate: true
        color: Theme.dialogFill
        scale: root.active ? 1.0 : 0.96
        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 32
            anchors.topMargin: 28
            spacing: 6

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
                font.weight: Font.DemiBold
            }

            EngravedRule {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 220
                Layout.topMargin: 4
                Layout.bottomMargin: 14
                star: true
            }

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
