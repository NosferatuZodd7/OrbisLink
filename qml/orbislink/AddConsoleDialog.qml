// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Adding a console to the list: those answering on the network show up by
// themselves, and one that does not (another subnet, blocked discovery) is typed by hand.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: dialog

    readonly property bool hasStream: typeof stream !== "undefined" && stream !== null
    readonly property bool searching: hasStream && stream.scanning
    readonly property var found: hasStream ? stream.scanResults : []

    function alreadyListed(address) {
        var items = app.consoles
        for (var i = 0; i < items.length; ++i)
            if (items[i].address === address)
                return true
        return false
    }

    // False when opened from the settings: adding a console there does not
    // switch to it.
    property bool selectAfterAdd: true

    function add(name, address, kind) {
        app.addConsole(name, address, kind || "", selectAfterAdd,
                       startBox.currentIndex === 1 ? "ftp" : "remoteplay")
        dialog.close()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 560
    modal: true
    padding: 0

    // A nearly opaque modal: with the panels' transparency, what is behind
    // would show through the box, and a box asking for a decision must not
    // be a window.
    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radiusDialog
    }

    onOpened: {
        consoleNameField.text = ""
        addressInput.text = ""
        startBox.currentIndex = 0
        if (hasStream)
            stream.scanNetwork()
    }

    header: DialogHeader {
        title: qsTr("Add console")
        dialog: dialog
    }

    // No button row at the bottom: the ✕ in the header closes it, and each
    // way of adding has its own button.
    contentItem: ColumnLayout {
        spacing: 14

        Item { Layout.preferredHeight: Theme.dialogInner - 14 }

        // What a click on the new console's card will start, whichever way it
        // is added below.
        RowLayout {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 12
            Text {
                text: qsTr("Start with")
                color: Theme.textSecondary
                font.weight: Font.DemiBold
                font.pixelSize: 12
            }
            StyledCombo {
                id: startBox
                Layout.fillWidth: true
                model: [qsTr("Remote Play (with FTP and the installer)"), qsTr("FTP only")]
            }
        }
        Text {
            visible: startBox.currentIndex === 1
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("FTP only works on a console with an FTP server running (a jailbreak: "
                       + "GoldHEN on the PS4, etaHEN on the PS5). Without it, the console starts "
                       + "with Remote Play.")
            color: Theme.textSecondary
            font.pixelSize: 11
        }

        Rectangle {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        // ── On the network
        RowLayout {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: qsTr("On the network")
                color: Theme.textSecondary
                font.weight: Font.DemiBold
                font.pixelSize: 12
            }
            StyledButton {
                visible: dialog.hasStream
                enabled: !dialog.searching
                chip: true
                iconName: dialog.searching ? "loader" : "refresh"
                text: dialog.searching ? qsTr("Searching…") : qsTr("Search again")
                onClicked: stream.scanNetwork()
            }
        }

        // While the search runs and nothing has answered yet: the shape of the
        // rows about to appear, with a shine running across them.
        Repeater {
            model: dialog.searching && dialog.found.length === 0 ? 2 : 0
            SkeletonRow {
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.fillWidth: true
            }
        }

        Text {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.fillWidth: true
            visible: !dialog.searching && dialog.found.length === 0
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: 12
            text: !dialog.hasStream
                  ? qsTr("This build has no Remote Play, so it can't search the network. Type "
                         + "the address below.")
                  : qsTr("No console answered. Check that it's on, on the same network, with "
                         + "Remote Play enabled — or type the address below.")
        }

        Repeater {
            model: dialog.found

            Rectangle {
                id: line
                readonly property bool listed: dialog.alreadyListed(modelData.address)
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.fillWidth: true
                implicitHeight: 60
                radius: 14
                color: Theme.panelAltFill
                border.color: Theme.glassEdge

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12
                    Rectangle {
                        implicitWidth: 46
                        implicitHeight: 34
                        radius: 10
                        color: Theme.controlFill
                        border.color: Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: modelData.ps5 ? "PS5" : "PS4"
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }
                    }
                    Column {
                        Layout.fillWidth: true
                        Text {
                            text: modelData.name.length > 0 ? modelData.name : modelData.address
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: modelData.address + "  ·  "
                                  + (modelData.state === "standby" ? qsTr("in rest mode")
                                                                   : qsTr("ready"))
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                    }
                    StyledButton {
                        text: line.listed ? qsTr("Already added") : qsTr("Add")
                        iconName: line.listed ? "check" : "plus"
                        enabled: !line.listed
                        primary: !line.listed
                        implicitHeight: 36
                        minimumWidth: 100
                        onClicked: dialog.add(modelData.name, modelData.address,
                                                    modelData.ps5 ? "ps5" : "ps4")
                    }
                }
            }
        }

        Rectangle {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.fillWidth: true
            Layout.topMargin: 6
            height: 1
            color: Theme.border
        }

        // ── By hand
        Text {
            Layout.leftMargin: Theme.dialogMargin
            text: qsTr("By hand")
            color: Theme.textSecondary
            font.weight: Font.DemiBold
            font.pixelSize: 12
        }

        RowLayout {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.bottomMargin: Theme.dialogMargin
            spacing: 10
            StyledField {
                id: consoleNameField
                Layout.preferredWidth: 150
                placeholderText: qsTr("Name (optional)")
            }
            StyledField {
                id: addressInput
                Layout.fillWidth: true
                placeholderText: qsTr("IP address, e.g. 192.168.1.50")
                onAccepted: if (text.trim().length > 0) dialog.add(consoleNameField.text, text)
            }
            StyledButton {
                text: qsTr("Add")
                enabled: addressInput.text.trim().length > 0
                minimumWidth: 100
                onClicked: dialog.add(consoleNameField.text, addressInput.text)
            }
        }
    }
}
