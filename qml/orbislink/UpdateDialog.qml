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

    readonly property bool aDescarregar: app.updateState === "downloading"
    readonly property bool pronto: app.updateState === "ready"

    // A nearly opaque modal: with the panels' transparency, what is behind
    // would show through the box, and a box asking for a decision must not
    // be a window.
    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radius
    }

    header: Rectangle {
        implicitHeight: Theme.dialogHeader
        color: "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: qsTr("Version %1 available").arg(app.updateVersion)
                color: Theme.text
                font.pixelSize: 16
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                text: qsTr("you have %1").arg(app.version)
                color: Theme.textMuted
                font.pixelSize: 11
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
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
            StyledButton {
                text: qsTr("View on GitHub")
                enabled: app.updatePageUrl.length > 0
                onClicked: app.openUpdatePage()
            }
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Later")
                enabled: !dialog.aDescarregar && !dialog.pronto
                onClicked: { app.dismissUpdate(); dialog.close() }
            }
            StyledButton {
                text: app.updateCanInstall ? qsTr("Install now") : qsTr("Open the page")
                primary: true
                larguraMinima: 140
                enabled: !dialog.aDescarregar && !dialog.pronto
                onClicked: app.installUpdate()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 10

        // What changed. The release body comes in markdown; it is shown as
        // text, which is honest and does not fake formatting that is not there.
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: Theme.dialogInner
            Layout.preferredHeight: 200
            radius: 8
            color: Theme.panelAltFill
            border.color: Theme.border

            ScrollView {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                clip: true
                TextArea {
                    text: app.updateNotes.length > 0 ? app.updateNotes
                                                     : qsTr("This release came with no notes.")
                    readOnly: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: 12
                    background: null
                    selectByMouse: true
                }
            }
        }

        // Progresso da descarga.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 4
            visible: dialog.aDescarregar || dialog.pronto || app.updateState === "error"

            Text {
                Layout.fillWidth: true
                text: app.updateMessage
                color: app.updateState === "error" ? Theme.error : Theme.textMuted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true
                visible: dialog.aDescarregar
                height: 4
                radius: 2
                color: Theme.border
                Rectangle {
                    width: parent.width * app.updateProgress
                    height: parent.height
                    radius: 2
                    color: Theme.accent
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.bottomMargin: Theme.dialogInner
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.pixelSize: 11
            visible: app.updateCanInstall
            text: qsTr("The installer opens and the app closes. Windows will ask for permission "
                       + "— the installer writes to Program Files.")
        }
    }
}
