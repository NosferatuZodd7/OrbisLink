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

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radiusDialog
    }

    header: Rectangle {
        implicitHeight: Theme.dialogHeader
        color: "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 10
            Icon {
                name: dialog.kind === "ftp" ? "cloud-upload" : "package"
                size: 18
                color: Theme.warn
            }
            Text {
                Layout.fillWidth: true
                text: dialog.heading
                color: Theme.text
                font.pixelSize: 16
                font.bold: true
                elide: Text.ElideRight
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
    }

    contentItem: Item {
        implicitHeight: bodyText.implicitHeight + 2 * Theme.dialogMargin
        Text {
            id: bodyText
            anchors.fill: parent
            anchors.margins: Theme.dialogMargin
            text: dialog.body
            color: Theme.textSecondary
            font.pixelSize: 13
            lineHeight: 1.25
            wrapMode: Text.WordWrap
        }
    }

    footer: Rectangle {
        implicitHeight: Theme.dialogFooter
        color: "transparent"
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
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
