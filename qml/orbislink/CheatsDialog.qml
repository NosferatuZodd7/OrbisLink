// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Cheats and game patches for a game the console has installed: the cheat
// files GoldHEN's and etaHEN's lists have for it, with the cheats in each,
// sent to the console or taken off it; and on a PS4 the game patches of
// illusion0001's archive, each with its switch for GoldHEN.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: dialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(760, parent.width - 48)
    height: Math.min(680, parent.height - 48)
    modal: true
    padding: 0

    property int tab: 0
    readonly property var game: cheats.game
    readonly property bool showPatches: cheats.patchesSupported && tab === 1
    // What the last action did, said in the footer (a notice would be
    // under the dialog).
    property string message: ""
    property bool messageError: false

    function begin(card) {
        tab = 0
        message = ""
        cheats.open(card)
        open()
    }

    Connections {
        target: cheats
        function onFinished(message, error) {
            if (dialog.visible) {
                dialog.message = message
                dialog.messageError = error
            } else {
                toast.show(message, error)
            }
        }
    }

    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radiusDialog
    }

    header: DialogHeader {
        title: cheats.patchesSupported ? qsTr("Cheats and patches") : qsTr("Cheats")
        dialog: dialog
    }

    contentItem: ColumnLayout {
        spacing: 12

        // The game, where its cheats go, and the lists fetched again.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: [dialog.game.title || "", dialog.game.titleId || "",
                       dialog.game.version ? qsTr("version %1").arg(dialog.game.version) : "",
                       cheats.ps5 ? qsTr("etaHEN (PS5)") : qsTr("GoldHEN (PS4)")]
                      .filter(function (part) { return part.length > 0 }).join("  ·  ")
                color: Theme.textSecondary
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            StyledButton {
                chip: true
                iconName: "refresh"
                text: qsTr("Fetch the lists again")
                enabled: !cheats.busy
                onClicked: cheats.reload()
            }
        }

        TabBar {
            visible: cheats.patchesSupported
            Layout.leftMargin: Theme.dialogMargin
            Layout.preferredWidth: 360
            currentIndex: dialog.tab
            onCurrentIndexChanged: dialog.tab = currentIndex
            padding: 4
            spacing: 4
            background: Rectangle {
                radius: 12
                color: Theme.controlFill
                border.width: 1
                border.color: Theme.glassEdge
            }
            StyledTab {
                text: cheats.cheats.length > 0 ? qsTr("Cheats (%1)").arg(cheats.cheats.length) : qsTr("Cheats")
            }
            StyledTab {
                text: cheats.patches.length > 0 ? qsTr("Patches (%1)").arg(cheats.patches.length) : qsTr("Patches")
            }
        }

        // What could not be read, said once.
        Repeater {
            model: [
                cheats.offline ? (cheats.loaded
                                  ? qsTr("The lists did not answer (%1): what is shown is the copy kept on this PC.").arg(cheats.error)
                                  : qsTr("The lists could not be fetched: %1").arg(cheats.error)) : "",
                cheats.loaded && !cheats.consoleRead && !cheats.busy
                    ? qsTr("The console's FTP is not answering: what it already has could not be read.") : "",
                dialog.showPatches && cheats.consoleRead && !cheats.patchPlugin && cheats.patches.length > 0
                    ? qsTr("GoldHEN's game_patch plugin is not in plugins.ini: the patches do nothing until it is "
                           + "(Payloads page, GoldHEN plugins).") : ""
            ].filter(function (line) { return line.length > 0 })
            delegate: RowLayout {
                id: noticeRow
                required property string modelData
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                spacing: 6
                Icon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 1
                    name: "warning"
                    size: 14
                    color: Theme.warn
                }
                Text {
                    Layout.fillWidth: true
                    text: noticeRow.modelData
                    color: Theme.warn
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }

        // ── the cheat files
        Rectangle {
            visible: !dialog.showPatches
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            radius: 12
            color: Theme.panelAltFill
            border.width: 1
            border.color: Theme.glassEdge
            clip: true

            ListView {
                id: cheatList
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6
                model: cheats.cheats
                ScrollBar.vertical: ScrollBar {}
                footer: Column {
                    width: cheatList.width - 8
                    topPadding: cheats.otherFiles.length > 0 ? 8 : 0
                    spacing: 4
                    Text {
                        visible: cheats.otherFiles.length > 0
                        leftPadding: 8
                        text: qsTr("Also on the console for this game")
                        color: Theme.textSecondary
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: cheats.otherFiles
                        delegate: RowLayout {
                            id: otherRow
                            required property var modelData
                            width: parent.width
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                leftPadding: 8
                                text: otherRow.modelData.name
                                color: Theme.text
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                            }
                            StyledButton {
                                chip: true
                                danger: true
                                iconName: "trash"
                                text: qsTr("Delete")
                                enabled: !cheats.busy
                                onClicked: cheats.removeCheat(otherRow.modelData.name)
                            }
                        }
                    }
                }
                delegate: Rectangle {
                    id: fileCard
                    required property var modelData
                    width: cheatList.width - 8
                    height: fileColumn.implicitHeight + 20
                    radius: 10
                    color: fileCard.modelData.installed ? Theme.alpha(Theme.ok, 0.08) : Theme.controlFill
                    border.width: 1
                    border.color: fileCard.modelData.installed ? Theme.alpha(Theme.ok, 0.35) : Theme.glassEdge

                    ColumnLayout {
                        id: fileColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 10
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle {
                                implicitWidth: formatLabel.implicitWidth + 12
                                implicitHeight: 18
                                radius: 5
                                color: Theme.alpha(Theme.accent, 0.14)
                                Text {
                                    id: formatLabel
                                    anchors.centerIn: parent
                                    text: fileCard.modelData.format
                                    color: Theme.accent
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                }
                            }
                            Text {
                                text: qsTr("Version %1").arg(fileCard.modelData.version)
                                      + (fileCard.modelData.variant ? "  ·  " + fileCard.modelData.variant : "")
                                color: Theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: fileCard.modelData.exact ? qsTr("the version installed")
                                                               : qsTr("another version")
                                color: fileCard.modelData.exact ? Theme.ok : Theme.warn
                                font.pixelSize: 11
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                visible: fileCard.modelData.installed
                                text: qsTr("On the console")
                                color: Theme.ok
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                            StyledButton {
                                chip: true
                                visible: fileCard.modelData.installed
                                danger: true
                                iconName: "trash"
                                text: qsTr("Delete")
                                enabled: !cheats.busy && cheats.consoleRead
                                onClicked: cheats.removeCheat(fileCard.modelData.consoleName)
                            }
                            StyledButton {
                                chip: true
                                visible: !fileCard.modelData.installed
                                primary: fileCard.modelData.exact
                                iconName: "upload"
                                text: qsTr("Send to the console")
                                enabled: !cheats.busy && cheats.consoleRead
                                onClicked: cheats.installCheat(fileCard.modelData.key)
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: fileCard.modelData.readable
                                  ? fileCard.modelData.cheats.map(function (c) { return c.name }).join("  ·  ")
                                  : fileCard.modelData.format === "MC4"
                                    ? qsTr("An MC4 file is encrypted: its cheats show in the console's menu.")
                                    : qsTr("Its cheats could not be read.")
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            maximumLineCount: 4
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: fileCard.modelData.author.length > 0
                            text: qsTr("by %1").arg(fileCard.modelData.author)
                            color: Theme.textMuted
                            font.pixelSize: 11
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 48
                visible: cheats.loaded && !cheats.busy && cheats.cheats.length === 0 && cheats.otherFiles.length === 0
                text: qsTr("The cheat lists have nothing for %1.").arg(dialog.game.titleId || "")
                color: Theme.textSecondary
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
            BusyIndicator {
                anchors.centerIn: parent
                visible: cheats.busy && cheats.cheats.length === 0
                running: visible
            }
        }

        // ── the game patches (PS4)
        Rectangle {
            visible: dialog.showPatches
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            radius: 12
            color: Theme.panelAltFill
            border.width: 1
            border.color: Theme.glassEdge
            clip: true

            ListView {
                id: patchList
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2
                model: cheats.patches
                ScrollBar.vertical: ScrollBar {}
                delegate: Item {
                    id: patchRow
                    required property var modelData
                    required property int index
                    width: patchList.width - 8
                    height: patchColumn.implicitHeight + 16
                    ColumnLayout {
                        id: patchColumn
                        anchors.left: parent.left
                        anchors.right: patchSwitch.left
                        anchors.leftMargin: 10
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: patchRow.modelData.name
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: patchRow.modelData.note.length > 0
                            text: patchRow.modelData.note
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: [patchRow.modelData.author ? qsTr("by %1").arg(patchRow.modelData.author) : "",
                                   patchRow.modelData.appVersion.indexOf(".") > 0 ? "" : qsTr("every version")]
                                  .filter(function (part) { return part.length > 0 }).join("  ·  ")
                            visible: text.length > 0
                            color: Theme.textMuted
                            font.pixelSize: 11
                        }
                    }
                    StyledCheck {
                        id: patchSwitch
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 44
                        Layout.fillWidth: false
                        checked: patchRow.modelData.on
                        enabled: !cheats.busy && cheats.consoleRead
                        onToggled: cheats.setPatch(patchRow.modelData.key, checked)
                    }
                    Rectangle {
                        visible: patchRow.index < patchList.count - 1
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 10
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.glassEdge
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 48
                visible: cheats.loaded && !cheats.busy && cheats.patches.length === 0
                text: (dialog.game.version ? qsTr("The patch archive has nothing for version %1 of this game.").arg(dialog.game.version)
                                           : qsTr("The patch archive has nothing for this game."))
                      + (cheats.patchVersions.length > 0
                         ? "\n" + qsTr("It has patches for version %1.").arg(cheats.patchVersions.join(", ")) : "")
                color: Theme.textSecondary
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
            BusyIndicator {
                anchors.centerIn: parent
                visible: cheats.busy && cheats.patches.length === 0
                running: visible
            }
        }

        // How the console uses them.
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            text: dialog.showPatches
                  ? qsTr("A patch applies when the game starts: turn it on here, then start the game. "
                         + "GoldHEN's own menu turns the same switches on and off.")
                  : cheats.ps5
                    ? qsTr("etaHEN reads new cheat files when it starts, or after “Reload cheats” in its toolbox; "
                           + "they only work for the version of the game they say.")
                    : qsTr("In the game, hold Share for GoldHEN's cheat menu. A cheat file only works for the version "
                           + "of the game it says, and GoldHEN loads one format per version.")
            color: Theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.bottomMargin: 8
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
            BusyIndicator {
                visible: cheats.busy
                running: visible
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }
            Icon {
                visible: !cheats.busy && dialog.message.length > 0
                name: dialog.messageError ? "warning" : "check-circle"
                size: 14
                color: dialog.messageError ? Theme.error : Theme.ok
            }
            Text {
                Layout.fillWidth: true
                text: cheats.busy ? cheats.status
                      : dialog.message.length > 0 ? dialog.message
                      : dialog.showPatches
                        ? qsTr("%n patch(es) on", "", cheats.patches.filter(function (p) { return p.on }).length)
                        : cheats.exactCheats > 0 ? qsTr("%n file(s) for the version installed", "", cheats.exactCheats)
                        : ""
                color: !cheats.busy && dialog.message.length > 0
                       ? (dialog.messageError ? Theme.error : Theme.text) : Theme.textSecondary
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            StyledButton {
                text: qsTr("Close")
                minimumWidth: 90
                onClicked: dialog.close()
            }
        }
    }
}
