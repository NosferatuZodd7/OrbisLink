// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One disc (or several selected ones): what it is, and the three things
// that can be done with it.
//
// * Convert and install: the disc becomes a PS4 package, goes to the
//   console over FTP and is installed there.
// * Convert only: the package stays in the output folder (for consoles
//   without a jailbreak, or to install later).
// * Send disc file: the image as it is, over FTP.
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
    readonly property bool installerUp: app.installerState === "available"
    readonly property bool ftpUp: app.canUseFtp
    readonly property bool hasOutput: games.outputFolder.length > 0

    // Something was started with the games on screen.
    signal started()

    function begin(list) {
        items = list
        reuse = true
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
        games.convert(paths(), install, single ? [titleInput.text] : [], reusing)
        started()
        close()
    }
    // Packages made before for these discs, in the output folder: they can
    // be sent as they are instead of being made again.
    readonly property var existingPackages: {
        var out = []
        if (!hasOutput)
            return out
        for (var i = 0; i < items.length; ++i) {
            var file = games.existingPackage(items[i].path, single ? titleInput.text : items[i].title)
            if (file.length > 0)
                out.push(file)
        }
        return out
    }
    property bool reuse: true
    readonly property bool reusing: reuse && existingPackages.length > 0

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

        // ── made before: send it as it is, or make it again
        Rectangle {
            visible: dialog.existingPackages.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            radius: 14
            color: Theme.alpha(Theme.ok, 0.08)
            border.width: 1
            border.color: Theme.alpha(Theme.ok, 0.35)
            implicitHeight: existingColumn.implicitHeight + 24
            ColumnLayout {
                id: existingColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Icon { name: "package"; size: 18; color: Theme.ok; Layout.alignment: Qt.AlignTop }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        font.pixelSize: 12
                        text: dialog.single
                            ? qsTr("This game was converted before: %1 is in the folder.")
                                .arg(dialog.existingPackages[0].split(/[\\/]/).pop())
                            : qsTr("%n of these games were converted before; their packages are in the folder.",
                                   "", dialog.existingPackages.length)
                    }
                }
                RowLayout {
                    spacing: 8
                    StyledButton {
                        chip: true
                        iconName: dialog.reuse ? "square-check" : "square"
                        text: qsTr("Use the existing package (send and install only)")
                        onClicked: dialog.reuse = true
                    }
                    StyledButton {
                        chip: true
                        iconName: dialog.reuse ? "square" : "square-check"
                        text: qsTr("Convert again")
                        onClicked: dialog.reuse = false
                    }
                }
            }
        }

        // ── a disc whose serial could not be read
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
                    text: qsTr("The serial of this disc could not be read, so it cannot be converted. "
                               + "The disc file can still be sent as it is.")
                }
            }
        }

        // ── where it goes: the console in use, or why nothing can be sent
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            readonly property color tone: dialog.ftpUp ? Theme.hen : Theme.warn
            radius: 14
            color: Theme.alpha(tone, 0.08)
            border.width: 1
            border.color: Theme.alpha(tone, 0.35)
            implicitHeight: targetRow.implicitHeight + 22
            RowLayout {
                id: targetRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 14
                anchors.rightMargin: 12
                spacing: 12
                Icon {
                    name: dialog.ftpUp ? "unlock" : "warning"
                    size: 18
                    color: Theme.light ? Qt.darker(parent.parent.tone, 1.2) : parent.parent.tone
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: 12
                    text: dialog.ftpUp
                        ? qsTr("Sends to %1 (%2)%3.").arg(app.consoleName).arg(app.consoleAddress)
                              .arg(app.jailbreaks[app.consoleAddress] ? " · " + app.jailbreaks[app.consoleAddress] : "")
                        : app.consoleAddress.length > 0
                        ? qsTr("Not connected to %1: sending and installing need its FTP (a jailbreak such as "
                               + "GoldHEN). Converting works without a console.").arg(app.consoleName)
                        : qsTr("No console yet: sending and installing need one with FTP (a jailbreak such as "
                               + "GoldHEN). Converting works without a console.")
                }
                StyledButton {
                    visible: !dialog.ftpUp && app.consoleAddress.length > 0
                    text: qsTr("Look again")
                    chip: true
                    onClicked: app.checkServicesNow()
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
            visible: dialog.ftpUp
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
                    text: (games.assetsState === "ready" || dialog.reusing ? ""
                           : qsTr("The first conversion also downloads the emulator files (about 109 MB, "
                                  + "only once). ")) + (!dialog.ftpUp
                        ? qsTr("The console has no FTP right now (it needs a jailbreak: GoldHEN or "
                               + "etaHEN). Convert only, and install the package later.")
                        : dialog.reusing
                        ? qsTr("Send and install: the package already in the folder goes to /data/OrbisLinkFPKG "
                               + "on the console and is installed, without converting the disc again; the "
                               + "copy there is deleted once installed.")
                        : dialog.installerUp
                        ? qsTr("Convert and install: the package is made here, sent to /data/OrbisLinkFPKG "
                               + "on the console and installed; the copy there is deleted once installed. "
                               + "Your package stays in the folder above.")
                        : qsTr("Convert and install: the package is made here, sent to /data/OrbisLinkFPKG "
                               + "and then installed (the copy there is deleted afterwards) — open Remote Package Installer on the console for that last "
                               + "step (it waits in the queue until then)."))
                }
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
                visible: !dialog.reusing
                text: qsTr("Convert only")
                // Without a console, it is the thing to do.
                primary: !dialog.ftpUp
                enabled: dialog.allConvertible
                onClicked: dialog.run(false)
            }
            StyledButton {
                text: dialog.reusing ? qsTr("Send and install") : qsTr("Convert and install")
                iconName: "package-plus"
                primary: dialog.ftpUp
                enabled: (dialog.allConvertible || dialog.reusing) && dialog.ftpUp
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
                dialog.run(dialog.pendingAction === "install")
        }
    }
}
