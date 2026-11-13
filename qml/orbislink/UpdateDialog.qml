// SPDX-License-Identifier: AGPL-3.0-or-later
//
// "There is a new version." Shows which, what changed, and gives a button
// to install. Nothing happens by itself: the download only starts on
// request, and the installer only opens after the SHA-256 matches.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: dialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 630
    modal: true
    padding: 0

    readonly property bool downloading: app.updateState === "downloading"
    readonly property bool isReady: app.updateState === "ready"

    // A nearly opaque modal: with the panels' transparency, what is behind
    // would show through the box, and a box asking for a decision must not
    // be a window.
    Overlay.modal: Rectangle { color: Theme.scrim }

    // A large sheet of glass floating over the window: nearly opaque, lit
    // along its upper edge, with a sheen from the top and a soft shadow.
    background: Plate {
        level: "secondary"
        radius: Theme.radiusDialog
        color: Theme.dialogFill
        border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Theme.glassEdge
    }

    // It fades in and settles, without bouncing.
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
        NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast; easing.type: Theme.easeOut }
    }

    header: DialogHeader {
        eyebrow: qsTr("Update")
        title: qsTr("Version %1 available").arg(app.updateVersion)
        dialog: dialog
    }

    footer: Item {
        implicitHeight: Theme.dialogFooter
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogInner
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 8
            StyledButton {
                text: qsTr("View on GitHub")
                iconName: "external-link"
                enabled: app.updatePageUrl.length > 0
                onClicked: app.openUpdatePage()
            }
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Later")
                enabled: !dialog.downloading && !dialog.isReady
                onClicked: { app.dismissUpdate(); dialog.close() }
            }
            StyledButton {
                text: app.updateCanInstall ? qsTr("Install now") : qsTr("Open the page")
                iconName: app.updateCanInstall ? "download" : "external-link"
                primary: true
                minimumWidth: 140
                enabled: !dialog.downloading && !dialog.isReady
                onClicked: app.installUpdate()
            }
        }
    }

    // One version on a quiet pane of glass: what it is in small capitals,
    // the number in the mono. The new one is lit.
    component VersionTile: Plate {
        id: tile
        property string label: ""
        property string version: ""
        property bool fresh: false
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.preferredHeight: 62
        color: fresh ? (Theme.light ? Qt.rgba(1, 1, 1, 0.92) : Qt.rgba(1, 1, 1, 0.12))
                     : Theme.light ? Qt.rgba(1, 1, 1, 0.40) : Qt.rgba(1, 1, 1, 0.04)
        Column {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Eyebrow {
                text: tile.label
                color: tile.fresh ? Theme.brass : Theme.textSecondary
            }
            Text {
                text: "v" + tile.version
                color: tile.fresh ? (Theme.light ? Qt.darker(Theme.accent, 1.2) : Theme.accent) : Theme.text
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCardTitle
                font.weight: Font.DemiBold
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        // From which version to which: the orb, then the one in use and the
        // new one on two quiet panes of glass, the new one lit.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 12
            Image {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                Layout.rightMargin: 6
                source: "qrc:/icons/mark.png"
                sourceSize.width: 112
                sourceSize.height: 112
                smooth: true
                mipmap: true
            }
            VersionTile { label: qsTr("In use"); version: app.version }
            Icon {
                name: "chevron-right"
                size: 16
                color: Theme.textSecondary
            }
            VersionTile { label: qsTr("New"); version: app.updateVersion; fresh: true }
        }

        // What changed. The release body comes in markdown; it is shown as
        // text, which is honest and does not fake formatting that is not there.
        Eyebrow {
            Layout.leftMargin: Theme.dialogMargin + 4
            Layout.topMargin: 4
            text: qsTr("What changed")
        }
        Plate {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.preferredHeight: 220
            sunken: true
            radius: Theme.radius

            ScrollView {
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                TextArea {
                    text: app.updateNotes.length > 0 ? app.updateNotes
                                                     : qsTr("This release came with no notes.")
                    readOnly: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: 13
                    background: null
                    selectByMouse: true
                    selectionColor: Theme.alpha(Theme.accent, 0.35)
                }
            }
        }

        // The download's progress.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 6
            visible: dialog.downloading || dialog.isReady || app.updateState === "error"

            Text {
                Layout.fillWidth: true
                text: app.updateMessage
                color: app.updateState === "error" ? (Theme.light ? Qt.darker(Theme.error, 1.3) : Theme.error)
                                                   : Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
            // A slim glass track, the progress in the accent sliding along it.
            Rectangle {
                Layout.fillWidth: true
                visible: dialog.downloading
                height: 6
                radius: 3
                color: Theme.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.08)
                Rectangle {
                    width: parent.width * app.updateProgress
                    height: parent.height
                    radius: 3
                    color: Theme.accent
                    Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.bottomMargin: 4
            visible: app.updateCanInstall
            spacing: 8
            Icon {
                Layout.alignment: Qt.AlignTop
                name: "shield-check"
                size: 15
                color: Theme.light ? Qt.darker(Theme.ok, 1.3) : Theme.ok
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                text: qsTr("The installer opens and the app closes. Windows will ask for permission "
                           + "— the installer writes to Program Files.")
            }
        }
    }
}
