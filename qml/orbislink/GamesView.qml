// SPDX-License-Identifier: AGPL-3.0-or-later
//
// PS1/PS2 Games: the discs found in a folder of the PC, as cards. A click
// opens one; the box in a card's corner selects it, and with several
// selected a bar at the bottom does the same to all of them.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    signal back()

    // The "path" of each selected game.
    property var selected: []

    function isSelected(path) { return selected.indexOf(path) >= 0 }
    function toggle(path) {
        var copy = selected.slice()
        var at = copy.indexOf(path)
        if (at >= 0)
            copy.splice(at, 1)
        else
            copy.push(path)
        selected = copy
    }
    function selectedGames() {
        var out = []
        for (var i = 0; i < games.games.length; ++i)
            if (isSelected(games.games[i].path))
                out.push(games.games[i])
        return out
    }

    // A new scan forgets selections of discs that are gone.
    Connections {
        target: games
        function onScanChanged() {
            var keep = []
            for (var i = 0; i < games.games.length; ++i)
                if (root.isSelected(games.games[i].path))
                    keep.push(games.games[i].path)
            root.selected = keep
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: Theme.gutter
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        radius: Theme.radius
        color: Theme.panelFill
        border.width: 1
        border.color: Theme.glassEdge
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            // ── header
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                StyledToolButton {
                    iconName: "chevron-left"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Back to the consoles")
                    onClicked: root.back()
                }
                ColumnLayout {
                    spacing: 2
                    Text {
                        text: qsTr("PS1/PS2 Games")
                        color: Theme.text
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: games.gamesFolder.length === 0 ? qsTr("Choose the folder where your disc images are.")
                              : games.scanning ? games.scanStatus
                              : games.scanStatus
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                }
                Item { Layout.fillWidth: true }
                // The games folder, as a chip that changes it.
                StyledButton {
                    visible: games.gamesFolder.length > 0
                    iconName: "folder-open"
                    chip: true
                    text: games.gamesFolder
                    Layout.maximumWidth: 320
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Change the games folder")
                    onClicked: gamesFolderDialog.open()
                }
                StyledToolButton {
                    visible: games.gamesFolder.length > 0
                    iconName: "refresh"
                    enabled: !games.scanning
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Look again")
                    onClicked: games.rescan()
                }
                StyledToolButton {
                    iconName: "cpu"
                    active: games.ps1Emulator || games.ps2Emulator
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("PS1/PS2 Classics files: %1").arg(emulatorSummary.text)
                    onClicked: emulatorFolderDialog.open()
                }
            }

            // ── the emulators: what was found, or why they are needed
            Rectangle {
                Layout.fillWidth: true
                visible: games.gamesFolder.length > 0 && !(games.ps1Emulator && games.ps2Emulator)
                radius: 14
                color: Theme.alpha(Theme.warn, 0.08)
                border.width: 1
                border.color: Theme.alpha(Theme.warn, 0.30)
                implicitHeight: emuRow.implicitHeight + 24
                RowLayout {
                    id: emuRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 16
                    anchors.rightMargin: 14
                    spacing: 12
                    Icon { name: "info"; size: 18; color: Theme.warn; Layout.alignment: Qt.AlignTop }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            id: emulatorSummary
                            Layout.fillWidth: true
                            text: (games.ps2Emulator ? qsTr("PS2 Classics files: %1").arg(games.ps2EmulatorName) : qsTr("PS2 Classics files: not found"))
                                  + "  ·  " + (games.ps1Emulator ? qsTr("PS1 Classics files: found") : qsTr("PS1 Classics files: not found"))
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.textSecondary
                            font.pixelSize: 11
                            text: qsTr("OrbisLink converts the disc into a PS4 package and sends it over FTP; "
                                       + "it is not an emulator. But the PS4 only runs a PS1/PS2 game packed "
                                       + "with Sony's Classics files (as the store's versions are), and those "
                                       + "cannot be shipped: choose the folder with your copy (an \"emus\" "
                                       + "folder like PS Classics fPKG Builder's works). Without them, the "
                                       + "disc file can still be sent as it is.")
                        }
                    }
                    StyledButton {
                        text: qsTr("Classics files…")
                        chip: true
                        onClicked: emulatorFolderDialog.open()
                    }
                }
            }

            // ── nothing chosen yet
            ColumnLayout {
                visible: games.gamesFolder.length === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 14
                Item { Layout.fillHeight: true }
                Icon {
                    Layout.alignment: Qt.AlignHCenter
                    name: "disc"
                    size: 56
                    color: Theme.alpha(Theme.accent, 0.8)
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Your PS1 and PS2 games, ready for the console")
                    color: Theme.text
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: 520
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("Choose the folder with your disc images (.iso, .bin/.cue, .img). The app "
                               + "finds which are PS1 and PS2 games, converts them into PS4 packages and "
                               + "sends them to the console over FTP.")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
                StyledButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 6
                    text: qsTr("Choose games folder…")
                    iconName: "folder-open"
                    primary: true
                    onClicked: gamesFolderDialog.open()
                }
                Item { Layout.fillHeight: true }
            }

            // ── the games
            ScrollView {
                id: gamesScroll
                visible: games.gamesFolder.length > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                Flow {
                    width: gamesScroll.availableWidth
                    topPadding: 8
                    leftPadding: 6
                    spacing: 18
                    bottomPadding: root.selected.length > 0 ? 90 : 16

                    Repeater {
                        model: games.games
                        GameCard {
                            required property var modelData
                            game: modelData
                            selected: root.isSelected(modelData.path)
                            onToggle: root.toggle(modelData.path)
                            onOpen: gameDialog.begin([modelData])
                        }
                    }
                }

                // Looking, or found nothing.
                Column {
                    anchors.centerIn: parent
                    visible: games.scanning || (games.games.length === 0 && games.gamesFolder.length > 0)
                    spacing: 12
                    BusyIndicator {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: games.scanning
                        running: games.scanning
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: games.scanning ? qsTr("Looking for games…")
                                             : qsTr("No PS1 or PS2 discs in this folder.")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                }
            }
        }

        // ── with several selected, what to do with them
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.selected.length > 0 ? 20 : -height
            Behavior on anchors.bottomMargin { NumberAnimation { duration: Theme.normal; easing.type: Theme.easeOut } }
            width: Math.min(parent.width - 48, bar.implicitWidth + 32)
            height: 60
            radius: 18
            color: Theme.menuFill
            border.width: 1
            border.color: Theme.glassEdge
            RowLayout {
                id: bar
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 12
                spacing: 10
                Text {
                    text: qsTr("%n selected", "", root.selected.length)
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                StyledToolButton {
                    iconName: "close"
                    iconSize: 14
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Clear the selection")
                    onClicked: root.selected = []
                }
                Item { Layout.fillWidth: true; implicitWidth: 12 }
                StyledButton {
                    text: qsTr("Continue…")
                    iconName: "package-plus"
                    primary: true
                    onClicked: gameDialog.begin(root.selectedGames())
                }
            }
        }
    }

    GameDialog {
        id: gameDialog
        onChooseEmulator: emulatorFolderDialog.open()
        onStarted: root.selected = []
    }

    FolderDialog {
        id: gamesFolderDialog
        title: qsTr("Folder with your PS1/PS2 disc images")
        currentFolder: games.folderUrl(games.gamesFolder)
        onAccepted: games.setGamesFolder(selectedFolder)
    }
    FolderDialog {
        id: emulatorFolderDialog
        title: qsTr("Folder with the PS1/PS2 Classics files")
        currentFolder: games.folderUrl(games.emulatorFolder)
        onAccepted: games.setEmulatorFolder(selectedFolder)
    }
}
