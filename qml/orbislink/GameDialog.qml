// SPDX-License-Identifier: AGPL-3.0-or-later
//
// One disc (or several selected ones): what it is, and the three things
// that can be done with it.
//
// * Convert and install: the disc becomes a PS4 package, goes to the
//   console over FTP — its own memory, a USB drive or the extended
//   storage — and is installed there.
// * Convert only: the package stays in the output folder (for consoles
//   without a jailbreak, or to install later).
// * Send disc file: the image as it is, over FTP.
//
// Laid out as the games page's other dialogs: an eyebrow and the title in
// bold, the cover large beside a ledger of facts (mono where they are
// technical), the choices, and the actions in a footer under a hairline.
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
        if (ftpUp)
            games.checkDrives()
        open()
    }
    function driveMissing(id) {
        return id !== "internal" && games.drives.checked === true && games.drives[id] !== true
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

    background: ShelfTile.DialogGlass {}

    header: ShelfTile.DialogHead {
        eyebrow: !dialog.single ? qsTr("PS1/PS2 converter")
               : dialog.game.platform === "ps2" ? qsTr("PS2 disc") : qsTr("PS1 disc")
        title: dialog.single ? dialog.game.title : qsTr("%n game(s) selected", "", dialog.items.length)
        dialog: dialog
    }

    contentItem: ColumnLayout {
        spacing: 16

        // ── what it is: the cover, large, beside a ledger of its facts
        RowLayout {
            visible: dialog.single
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 22

            // The game's cover, from the collection by its serial, on a
            // soft shadow.
            Item {
                Layout.alignment: Qt.AlignTop
                implicitWidth: 132
                implicitHeight: 184
                Rectangle {
                    x: 2
                    y: 7
                    width: parent.width
                    height: parent.height
                    radius: 12
                    color: Qt.rgba(0, 0, 0, Theme.light ? 0.10 : 0.35)
                }
                CoverArt {
                    id: dialogCover
                    anchors.fill: parent
                    crop: true
                    radius: 12
                    readonly property var game: dialog.game || ({})
                    picture: dialog.single && shelf.coverRevision >= 0
                             ? shelf.cover(game.platform || "", game.serial || "") : ""
                    placeholder: game.platform === "ps2" ? "dvd" : "cd"
                    label: game.platform === "ps2" ? "PS2" : "PS1"
                }
            }

            Column {
                id: details
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Repeater {
                    model: dialog.single ? [
                        [qsTr("Platform"), dialog.game.platform === "ps2" ? "PlayStation 2" : "PlayStation", false],
                        [qsTr("Serial"), dialog.game.serial.length > 0 ? dialog.game.serial : qsTr("not found"),
                         dialog.game.serial.length > 0],
                        [qsTr("Region"), dialog.game.region.length > 0 ? dialog.game.region : "—", false],
                        [qsTr("File"), dialog.game.fileName, true],
                        [qsTr("Size"), dialog.game.sizeText + "  ·  ." + dialog.game.format, true],
                        [qsTr("Folder"), dialog.game.folder, true]
                    ] : []
                    delegate: ShelfTile.FactRow {
                        required property var modelData
                        required property int index
                        width: details.width
                        label: modelData[0]
                        value: modelData[1]
                        mono: modelData[2]
                        last: index === 5
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
            spacing: 8
            Eyebrow {
                text: qsTr("Name on the console")
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
                size: 18
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
                    color: !dialog.hasOutput ? (Theme.light ? Qt.darker(Theme.warn, 1.3) : Theme.warn) : Theme.text
                    font.family: dialog.hasOutput ? Theme.fontMono : Theme.fontSans
                    font.pixelSize: dialog.hasOutput ? 11 : 12
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
            radius: 12
            color: Theme.alpha(Theme.ok, Theme.light ? 0.06 : 0.08)
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
                    Icon { name: "package"; size: 18; color: Theme.light ? Qt.darker(Theme.ok, 1.3) : Theme.ok; Layout.alignment: Qt.AlignTop }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        font.pixelSize: 12
                        text: dialog.single
                            ? qsTr("This game was converted before: %1 is in the folder.")
                                .arg((dialog.existingPackages[0] || "").split(/[\\/]/).pop())
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
            radius: 12
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
                Icon { name: "warning"; size: 18; color: Theme.light ? Qt.darker(Theme.warn, 1.3) : Theme.warn; Layout.alignment: Qt.AlignTop }
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
            radius: 12
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
            // Where on the console it goes, and is installed from.
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    height: 30
                    verticalAlignment: Text.AlignVCenter
                    rightPadding: 4
                    text: qsTr("On the console:")
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
                Repeater {
                    model: [
                        { id: "internal", label: qsTr("Its memory"), icon: "hard-drive" },
                        { id: "usb", label: qsTr("USB drive"), icon: "hard-drive" },
                        { id: "ext", label: qsTr("Extended storage"), icon: "hard-drive" }
                    ]
                    StyledButton {
                        required property var modelData
                        chip: true
                        iconName: games.storage === modelData.id ? "check" : modelData.icon
                        primary: games.storage === modelData.id
                        text: modelData.label + (dialog.driveMissing(modelData.id) ? "  ·  " + qsTr("not found") : "")
                        ToolTip.visible: hovered
                        ToolTip.text: dialog.driveMissing(modelData.id)
                                      ? qsTr("Nothing shows on it: plug it in (an empty drive works too). Goes to %1")
                                        .arg(games.folderFor(modelData.id))
                                      : qsTr("Goes to %1").arg(games.folderFor(modelData.id))
                        onClicked: games.setStorage(modelData.id)
                    }
                }
            }
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
                        : (dialog.reusing
                           ? qsTr("Send and install: the package already in the folder goes to %1 on the "
                                  + "console and is installed, without converting the disc again.")
                           : qsTr("Convert and install: the package is made here, sent to %1 on the console "
                                  + "and installed. Your package stays in the folder above."))
                          .arg(games.storageFolder)
                          + " " + (app.installsFromConsole
                                   ? qsTr("The PS5 installs it from there; the copy stays until you delete it.")
                                   : qsTr("The copy there is deleted once installed."))
                          + (dialog.installerUp ? ""
                             : " " + (app.installsFromConsole
                                      ? qsTr("The installer is not answering: turn on DPI v2 in etaHEN (DPI_v2=1); "
                                             + "the install waits in the queue until then.")
                                      : qsTr("Open Remote Package Installer on the console for that last step (it "
                                             + "waits in the queue until then)."))))
                }
            }
        }
    }

    footer: Item {
        implicitHeight: Theme.dialogFooter
        // A hairline between what it is and what to do with it.
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
