// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One disc (or several selected ones): what it is, and the three things
// that can be done with it.
//
// * Convert and install: the disc becomes a PS4 package that goes into the
//   install queue. Needs a console with a jailbreak and Remote Package
//   Installer open on it.
// * Convert only: the package stays in the output folder (for consoles
//   without a jailbreak, or to install later).
// * Send to console: the disc file itself, over FTP.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Dialog {
    id: dialog

    property var items: []
    readonly property bool single: items.length === 1
    readonly property var game: single ? items[0] : null
    readonly property bool allConvertible: {
        for (var i = 0; i < items.length; ++i)
            if (!items[i].convertible)
                return false
        return items.length > 0
    }
    readonly property bool needsPs2: {
        for (var i = 0; i < items.length; ++i)
            if (items[i].platform === "ps2" && !items[i].hasEmulator)
                return true
        return false
    }
    readonly property bool needsPs1: {
        for (var i = 0; i < items.length; ++i)
            if (items[i].platform === "ps1" && !items[i].hasEmulator)
                return true
        return false
    }
    readonly property bool installerUp: app.installerState === "available"
    readonly property bool hasOutput: games.outputFolder.length > 0

    signal chooseEmulator()
    // Something was started with the games on screen.
    signal started()

    function begin(list) {
        items = list
        titleInput.text = list.length === 1 ? list[0].title : ""
        open()
    }

    function paths() {
        var out = []
        for (var i = 0; i < items.length; ++i)
            out.push(items[i].path)
        return out
    }

    function run(install) {
        if (!hasOutput) {
            pendingAction = install ? "install" : "convert"
            outputDialog.open()
            return
        }
        games.convert(paths(), install, single ? [titleInput.text] : [])
        started()
        close()
    }
    // What to do once an output folder is chosen ("install", "convert" or
    // nothing, when the folder was only being changed).
    property string pendingAction: ""

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 600
    modal: true
    padding: 0

    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radiusDialog
    }

    header: DialogHeader {
        title: dialog.single ? dialog.game.title : qsTr("%n game(s) selected", "", dialog.items.length)
        dialog: dialog
    }

    contentItem: ColumnLayout {
        spacing: 14

        // ── what it is
        Rectangle {
            visible: dialog.single
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: 4
            radius: 16
            color: Theme.panelAltFill
            border.width: 1
            border.color: Theme.glassEdge
            implicitHeight: details.implicitHeight + 28

            GridLayout {
                id: details
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                columns: 2
                columnSpacing: 16
                rowSpacing: 6

                Repeater {
                    model: dialog.single ? [
                        [qsTr("Platform"), dialog.game.platform === "ps2" ? "PlayStation 2" : "PlayStation"],
                        [qsTr("Serial"), dialog.game.serial.length > 0 ? dialog.game.serial : qsTr("not found")],
                        [qsTr("Region"), dialog.game.region.length > 0 ? dialog.game.region : "—"],
                        [qsTr("File"), dialog.game.fileName],
                        [qsTr("Size"), dialog.game.sizeText + "  ·  ." + dialog.game.format],
                        [qsTr("Folder"), dialog.game.folder]
                    ] : []
                    delegate: Item {
                        required property var modelData
                        required property int index
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        implicitHeight: Math.max(k.implicitHeight, v.implicitHeight)
                        Text {
                            id: k
                            width: 90
                            text: modelData[0]
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                        Text {
                            id: v
                            x: 106
                            width: parent.width - 106
                            text: modelData[1]
                            color: Theme.text
                            font.pixelSize: 12
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }
        }

        // Several: their names.
        Text {
            visible: !dialog.single
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: 4
            wrapMode: Text.WordWrap
            maximumLineCount: 6
            elide: Text.ElideRight
            color: Theme.text
            font.pixelSize: 13
            text: dialog.items.map(function (g) { return g.title }).join(", ")
        }

        // ── the name it will have on the console
        ColumnLayout {
            visible: dialog.single
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 6
            Text {
                text: qsTr("Name on the console")
                color: Theme.textSecondary
                font.pixelSize: 12
            }
            StyledField {
                id: titleInput
                Layout.fillWidth: true
                maximumLength: 120
            }
        }

        // ── where the packages go
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 10
            Icon {
                name: "folder"
                size: 16
                color: Theme.textSecondary
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    text: qsTr("Packages are saved in")
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
                Text {
                    Layout.fillWidth: true
                    text: dialog.hasOutput ? games.outputFolder : qsTr("no folder chosen yet")
                    color: dialog.hasOutput ? Theme.text : Theme.warn
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                }
            }
            StyledButton {
                text: qsTr("Change…")
                chip: true
                onClicked: { dialog.pendingAction = ""; outputDialog.open() }
            }
        }

        // ── what is missing, or worth knowing
        Rectangle {
            visible: !dialog.allConvertible
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            radius: 14
            color: Theme.alpha(Theme.warn, 0.10)
            border.width: 1
            border.color: Theme.alpha(Theme.warn, 0.35)
            implicitHeight: missing.implicitHeight + 24
            RowLayout {
                id: missing
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 12
                Icon { name: "warning"; size: 18; color: Theme.warn; Layout.alignment: Qt.AlignTop }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: 12
                    text: (dialog.needsPs2 && dialog.needsPs1
                           ? qsTr("No PS1 or PS2 emulator found yet.")
                           : dialog.needsPs2 ? qsTr("No PS2 emulator found yet.")
                           : dialog.needsPs1 ? qsTr("No PS1 emulator found yet.")
                           : qsTr("The serial of this disc could not be read."))
                          + " " + qsTr("The emulator is Sony's and does not come with OrbisLink: point "
                                       + "the app at the folder with your own copy. Sending the disc "
                                       + "over FTP still works.")
                }
                StyledButton {
                    visible: dialog.needsPs1 || dialog.needsPs2
                    text: qsTr("Choose…")
                    chip: true
                    onClicked: { dialog.close(); dialog.chooseEmulator() }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.bottomMargin: 4
            spacing: 10
            Icon {
                name: dialog.installerUp ? "info" : "warning"
                size: 16
                color: dialog.installerUp ? Theme.textSecondary : Theme.warn
                Layout.alignment: Qt.AlignTop
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 11
                text: dialog.installerUp
                    ? qsTr("Installing needs Remote Package Installer open on the console. A console "
                           + "without a jailbreak cannot install packages: convert only, and the "
                           + "package stays in the folder above.")
                    : qsTr("The remote installer is not answering now. To install, open Remote Package "
                           + "Installer on the console (it needs a jailbreak); otherwise, convert only "
                           + "and keep the package.")
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
                text: qsTr("Send to console")
                iconName: "upload"
                enabled: app.canUseFtp
                ToolTip.visible: hovered
                ToolTip.text: app.canUseFtp ? qsTr("The disc file as it is, over FTP, to the folder the file list is in")
                                            : qsTr("The console has no FTP right now")
                onClicked: { games.sendToConsole(dialog.paths()); dialog.started(); dialog.close() }
            }
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Convert only")
                enabled: dialog.allConvertible
                onClicked: dialog.run(false)
            }
            StyledButton {
                text: qsTr("Convert and install")
                iconName: "package-plus"
                primary: true
                enabled: dialog.allConvertible
                onClicked: dialog.run(true)
            }
        }
    }

    FolderDialog {
        id: outputDialog
        title: qsTr("Where to save the packages")
        currentFolder: games.folderUrl(games.outputFolder)
        onAccepted: {
            games.setOutputFolder(selectedFolder)
            if (dialog.pendingAction.length > 0)
                dialog.run(dialog.pendingAction === "install")
        }
    }
}
