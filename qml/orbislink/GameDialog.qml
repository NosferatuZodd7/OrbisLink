// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One disc (or several selected ones): what it is, and the three things
// that can be done with it.
//
// * Convert and send: the disc becomes a PS4 package that goes to the
//   console over FTP (and is installed after, if that is switched on).
// * Convert only: the package stays in the output folder (for consoles
//   without a jailbreak, or to send later).
// * Send the disc file: the image as it is, over FTP.
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
    readonly property bool ftpUp: app.canUseFtp
    readonly property bool hasOutput: games.outputFolder.length > 0

    signal chooseEmulator()
    // Something was started with the games on screen.
    signal started()

    function begin(list) {
        items = list
        titleInput.text = list.length === 1 ? list[0].title : ""
        installAfter.checked = app.settingsMap().installAfterUpload === true
        open()
    }

    function paths() {
        var out = []
        for (var i = 0; i < items.length; ++i)
            out.push(items[i].path)
        return out
    }

    function run(send) {
        if (!hasOutput) {
            pendingAction = send ? "send" : "convert"
            outputDialog.open()
            return
        }
        games.convert(paths(), send, single ? [titleInput.text] : [])
        started()
        close()
    }
    // What to do once an output folder is chosen ("send", "convert" or
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
                           ? qsTr("The PS1 and PS2 Classics files are missing.")
                           : dialog.needsPs2 ? qsTr("The PS2 Classics files are missing.")
                           : dialog.needsPs1 ? qsTr("The PS1 Classics files are missing.")
                           : qsTr("The serial of this disc could not be read."))
                          + " " + qsTr("The PS4 only runs a PS1/PS2 disc packed together with Sony's "
                                       + "Classics files, as the store's versions are. They are not "
                                       + "included: choose the folder with your copy. The disc file can "
                                       + "still be sent as it is.")
                }
                StyledButton {
                    visible: dialog.needsPs1 || dialog.needsPs2
                    text: qsTr("Choose…")
                    chip: true
                    onClicked: { dialog.close(); dialog.chooseEmulator() }
                }
            }
        }

        // ── how the package gets to the console
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.bottomMargin: 4
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Icon {
                    name: dialog.ftpUp ? "info" : "warning"
                    size: 16
                    color: dialog.ftpUp ? Theme.textSecondary : Theme.warn
                    Layout.alignment: Qt.AlignTop
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    text: dialog.ftpUp
                        ? qsTr("The package goes to the console over FTP, to the folder the file list is "
                               + "in. A console without a jailbreak has no FTP: convert only, and the "
                               + "package stays in the folder above.")
                        : qsTr("The console has no FTP right now (it needs a jailbreak: GoldHEN or "
                               + "etaHEN). Convert only, and send the package later.")
                }
            }
            // Installing right after the upload: the same setting as for any
            // file sent over FTP.
            StyledCheck {
                id: installAfter
                Layout.fillWidth: true
                enabled: dialog.ftpUp
                text: dialog.installerUp
                      ? qsTr("Install it after sending")
                      : qsTr("Install it after sending (open Remote Package Installer on the console)")
                onToggled: app.applySettings({ "installAfterUpload": checked })
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
                text: qsTr("Send disc file")
                iconName: "disc"
                enabled: dialog.ftpUp
                ToolTip.visible: hovered
                ToolTip.text: dialog.ftpUp ? qsTr("The disc image as it is, over FTP, to the folder the file list is in")
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
                text: qsTr("Convert and send")
                iconName: "upload"
                primary: true
                enabled: dialog.allConvertible && dialog.ftpUp
                ToolTip.visible: hovered && !dialog.ftpUp
                ToolTip.text: qsTr("The console has no FTP right now")
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
                dialog.run(dialog.pendingAction === "send")
        }
    }
}
