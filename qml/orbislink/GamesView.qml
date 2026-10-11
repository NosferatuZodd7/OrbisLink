// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Games. All games: the library (ShelfView.qml), every place games can be —
// the console's folders and drives, this PC's — shown as their covers.
// Apart from it, the PS1/PS2 converter, for whoever wants it: the discs
// found in a folder on this PC, as cards; a click opens one, the box in a
// card's corner selects it, and with several selected a bar at the bottom
// does the same to all of them.
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
    // 0: all games, the console's library; 1: the PS1/PS2 converter (where
    // the demo discs are).
    property int tab: typeof demoGames !== "undefined" && demoGames ? 1 : 0

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
        anchors.rightMargin: window.panelShown ? 8 : Theme.gutter
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
                        text: qsTr("Games")
                        color: Theme.text
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: root.tab === 0
                              ? (shelf.level === "home" ? qsTr("%n place(s)", "", shelf.count)
                                 : shelf.loading ? qsTr("Reading…") : qsTr("%n item(s)", "", shelf.count))
                              : games.gamesFolder.length === 0 ? qsTr("Choose the folder where your disc images are.")
                              : games.scanStatus
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                }
                Item { Layout.fillWidth: true }
                // The games folder, as a chip that changes it.
                StyledButton {
                    visible: root.tab === 1 && games.gamesFolder.length > 0
                    iconName: "folder-open"
                    chip: true
                    text: games.gamesFolder
                    Layout.maximumWidth: 320
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Change the games folder")
                    onClicked: gamesFolderDialog.open()
                }
                StyledToolButton {
                    visible: root.tab === 1 && games.gamesFolder.length > 0
                    iconName: "refresh"
                    enabled: !games.scanning
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Look again")
                    onClicked: games.rescan()
                }
            }

            // ── all games | the PS1/PS2 converter, apart: converting is a
            // choice, not a step
            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                TabBar {
                    id: placeTabs
                    Layout.preferredWidth: games.available ? 380 : 190
                    currentIndex: root.tab
                    onCurrentIndexChanged: root.tab = currentIndex
                    padding: 4
                    spacing: 4
                    background: Rectangle {
                        radius: 12
                        color: Theme.controlFill
                        border.width: 1
                        border.color: Theme.glassEdge
                    }
                    // Halves, or all of it where this build has no converter.
                    readonly property real half: (availableWidth - spacing) / 2
                    StyledTab {
                        width: games.available ? placeTabs.half : placeTabs.availableWidth
                        text: qsTr("All games")
                    }
                    StyledTab {
                        visible: games.available
                        width: games.available ? placeTabs.half : 0
                        text: qsTr("PS1/PS2 converter")
                    }
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: Theme.textMuted
                    font.pixelSize: 12
                    text: root.tab === 0
                          ? qsTr("Your games on the console and on this PC, place by place")
                          : qsTr("Optional: PS1/PS2 discs on this PC made into PS4 packages")
                }
            }

            // ── the emulator files: downloaded once, the first time they are
            // needed (as easy-ps2-fpkg does), and nothing else to provide
            Rectangle {
                id: assetsStrip
                readonly property string state_: games.assetsState
                readonly property bool busy: state_ === "downloading" || state_ === "unpacking"
                readonly property color tone: state_ === "error" ? Theme.error : Theme.accent
                Layout.fillWidth: true
                visible: root.tab === 1 && games.gamesFolder.length > 0 && state_ !== "ready"
                radius: 14
                color: Theme.alpha(tone, 0.08)
                border.width: 1
                border.color: Theme.alpha(tone, 0.30)
                implicitHeight: assetsColumn.implicitHeight + 24
                ColumnLayout {
                    id: assetsColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 16
                    anchors.rightMargin: 14
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Icon {
                            name: assetsStrip.state_ === "error" ? "warning" : assetsStrip.busy ? "loader" : "download"
                            spinning: assetsStrip.busy
                            size: 18
                            color: assetsStrip.tone
                            Layout.alignment: Qt.AlignTop
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            font.pixelSize: 12
                            text: assetsStrip.state_ === "downloading"
                                  ? qsTr("Downloading the PS1/PS2 emulator files… %1%").arg(Math.floor(games.assetsPercent))
                                  : assetsStrip.state_ === "unpacking"
                                  ? qsTr("Unpacking the emulator files… %1%").arg(Math.floor(games.assetsPercent))
                                  : assetsStrip.state_ === "error"
                                  ? qsTr("The emulator files could not be downloaded: %1").arg(games.assetsMessage)
                                  : qsTr("Nothing else is needed: the first conversion downloads the PS1/PS2 "
                                         + "emulator files once (about 109 MB), like easy-ps2-fpkg.")
                        }
                        StyledButton {
                            visible: !assetsStrip.busy
                            text: assetsStrip.state_ === "error" ? qsTr("Try again") : qsTr("Download now")
                            chip: true
                            onClicked: games.downloadAssets()
                        }
                    }
                    Rectangle {
                        visible: assetsStrip.busy
                        Layout.fillWidth: true
                        height: 4
                        radius: 2
                        color: Theme.alpha(assetsStrip.tone, 0.18)
                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(1, games.assetsPercent / 100))
                            height: parent.height
                            radius: parent.radius
                            color: assetsStrip.tone
                            Behavior on width { NumberAnimation { duration: 250 } }
                        }
                    }
                }
            }

            // ── nothing chosen yet: centred in the page, both ways
            Item {
                visible: root.tab === 1 && games.gamesFolder.length === 0
                Layout.fillWidth: true
                Layout.fillHeight: true

                Column {
                    anchors.centerIn: parent
                    width: Math.min(520, parent.width - 32)
                    spacing: 14
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: "disc"
                        size: 56
                        color: Theme.alpha(Theme.accent, 0.8)
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Your PS1 and PS2 games, ready for the console")
                        color: Theme.text
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Choose the folder with your disc images (.iso, .bin/.cue, .img). The app "
                                   + "finds which are PS1 and PS2 games, converts them into PS4 packages, "
                                   + "sends them to the console over FTP and installs them.")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                    Item { width: 1; height: 6 }
                    StyledButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Choose games folder…")
                        iconName: "folder-open"
                        primary: true
                        onClicked: gamesFolderDialog.open()
                    }
                }
            }

            // ── the console's library
            ShelfView {
                id: shelfView
                visible: root.tab === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                onVisibleChanged: if (visible) forceActiveFocus()
            }
            Binding { target: shelf; property: "active"; value: root.visible && root.tab === 0 }

            // ── the games: centred, as many to a row as fit
            Item {
                visible: root.tab === 1 && games.gamesFolder.length > 0
                Layout.fillWidth: true
                Layout.fillHeight: true

                Flickable {
                    id: gamesScroll
                    anchors.fill: parent
                    // Room for the cards' glow and lift on hover.
                    anchors.leftMargin: -14
                    anchors.rightMargin: -14
                    clip: true
                    contentWidth: width
                    contentHeight: gamesFlow.height + gamesFlow.y + (root.selected.length > 0 ? 90 : 16)
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    Flow {
                        id: gamesFlow
                        readonly property int cell: 236 + spacing
                        readonly property int fits: Math.max(1, Math.floor((gamesScroll.width - 28 + spacing) / cell))
                        x: Math.round((gamesScroll.width - width) / 2)
                        y: 14
                        width: Math.max(0, Math.min(fits, games.games.length) * cell - spacing)
                        spacing: 18

                        Repeater {
                            model: games.games
                            GameCard {
                                required property var modelData
                                game: modelData
                                selected: root.isSelected(modelData.path)
                                progress: games.progress[modelData.path] || null
                                onToggle: root.toggle(modelData.path)
                                onOpen: gameDialog.begin([modelData])
                            }
                        }
                    }
                }

                // Looking, or found nothing.
                Column {
                    anchors.centerIn: parent
                    visible: games.scanning || games.games.length === 0
                    spacing: 12
                    BusyIndicator {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: games.scanning
                        running: games.scanning
                    }
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: !games.scanning
                        name: "disc"
                        size: 44
                        color: Theme.alpha(Theme.textSecondary, 0.7)
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
            anchors.bottomMargin: root.selected.length > 0 && root.tab === 1 ? 20 : -height
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
        onStarted: root.selected = []
    }

    FolderDialog {
        id: gamesFolderDialog
        title: qsTr("Folder with your PS1/PS2 disc images")
        currentFolder: games.folderUrl(games.gamesFolder)
        onAccepted: games.setGamesFolder(selectedFolder)
    }
}
