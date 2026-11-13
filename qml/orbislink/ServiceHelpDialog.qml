// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A feature needs the console's installer or FTP, and it is not answering:
// what to do on that PS4 or PS5, step by step. Nothing is done for the user;
// for a PS5's installer, a button opens the Payloads page, where ▶ runs
// Prospero Manager.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: dialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent.width - 48)
    modal: true
    padding: 0

    // "installer" (a PS5's), "rpi" (a PS4's Remote Package Installer) or "ftp".
    property string kind: ""
    property string heading: ""
    property string body: ""

    // The Payloads page, opened by the window.
    signal openPayloads()

    function begin(what, title, text) {
        kind = what
        heading = title
        body = text
        open()
    }

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
        // What is not answering: the console's installer, or its FTP.
        eyebrow: dialog.kind === "ftp" ? qsTr("FTP is not answering")
                                       : qsTr("The installer is not answering")
        title: dialog.heading
        dialog: dialog
    }

    contentItem: Item {
        implicitHeight: bodyRow.implicitHeight + Theme.dialogInner + Theme.dialogMargin
        RowLayout {
            id: bodyRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 6
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 16

            // The service, in a round seal tinted with the warning.
            Rectangle {
                Layout.alignment: Qt.AlignTop
                implicitWidth: 44
                implicitHeight: 44
                radius: 22
                color: Theme.alpha(Theme.warn, 0.12)
                border.width: 1
                border.color: Theme.alpha(Theme.warn, 0.45)
                Icon {
                    anchors.centerIn: parent
                    name: dialog.kind === "ftp" ? "cloud-upload" : "package"
                    size: 19
                    color: Theme.light ? Qt.darker(Theme.warn, 1.35) : Theme.warn
                }
            }
            Text {
                id: bodyText
                Layout.fillWidth: true
                text: dialog.body
                color: Theme.text
                font.pixelSize: Theme.fontBody
                lineHeight: 1.3
                wrapMode: Text.WordWrap
            }
        }
    }

    footer: Item {
        implicitHeight: Theme.dialogFooter
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            height: 1
            color: Theme.glassEdge
        }
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogInner
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 8
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("OK")
                minimumWidth: 90
                primary: dialog.kind !== "installer"
                onClicked: dialog.close()
            }
            StyledButton {
                visible: dialog.kind === "installer"
                text: qsTr("Open Payloads")
                iconName: "zap"
                primary: true
                onClicked: {
                    dialog.close()
                    dialog.openPayloads()
                }
            }
        }
    }
}
