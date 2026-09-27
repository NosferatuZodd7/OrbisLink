// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Drag-and-drop overlay: two zones, one per mode, on a glass card over the
// dimmed window.
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
        color: Qt.rgba(4 / 255, 8 / 255, 14 / 255, 0.78)
    }

    Rectangle {
        id: sheet
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 760)
        height: Math.min(parent.height - 80, 440)
        radius: 26
        color: Qt.rgba(22 / 255, 34 / 255, 54 / 255, 0.92)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.14)
        scale: root.active ? 1.0 : 0.96
        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.07) }
                GradientStop { position: 0.5; color: "transparent" }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 28
            spacing: 16

            Icon {
                Layout.alignment: Qt.AlignHCenter
                name: "package"
                size: 40
                strokeWidth: 1.5
                color: Theme.onStage
            }

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Drop to…")
                color: Theme.onStage
                font.pixelSize: 22
                font.weight: Font.DemiBold
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
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Takes several files and folders (it looks for .pkg inside)")
                color: Theme.onStageMuted
                font.pixelSize: 12
            }
        }
    }
}
