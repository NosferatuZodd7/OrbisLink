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

    // A large sheet of glass floating over the window: nearly opaque, lit
    // along its upper edge, with a sheen from the top and a soft shadow.
    background: Plate {
        level: "secondary"
        radius: Theme.radiusDialog
        color: Theme.dialogFill
        border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Theme.glassEdge
    }

    // It fades in and settles, without bouncing.
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
        NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast; easing.type: Theme.easeOut }
    }

    onOpened: {
        consoleNameField.text = ""
        addressInput.text = ""
        startBox.currentIndex = 0
        if (hasStream)
            stream.scanNetwork()
    }

    header: DialogHeader {
        eyebrow: qsTr("Network")
        title: qsTr("Add console")
        dialog: dialog
    }

    // No button row at the bottom: the ✕ in the header closes it, and each
    // way of adding has its own button.
    contentItem: ColumnLayout {
        spacing: 10

        // ── On the network: the consoles that answered, on one plate.
        RowLayout {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: 4
            spacing: 10
            Eyebrow {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                text: qsTr("On the network")
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

        Plate {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.fillWidth: true
            implicitHeight: foundColumn.implicitHeight + 8
            sunken: dialog.found.length === 0

            ColumnLayout {
                id: foundColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 4
                anchors.leftMargin: 16
                anchors.rightMargin: 14
                spacing: 0

                // While the search runs and nothing has answered yet: the shape of the
                // rows about to appear, with a shine running across them.
                Repeater {
                    model: dialog.searching && dialog.found.length === 0 ? 2 : 0
                    SkeletonRow {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        Layout.bottomMargin: 6
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 14
                    Layout.bottomMargin: 14
                    visible: !dialog.searching && dialog.found.length === 0
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    lineHeight: 1.2
                    text: !dialog.hasStream
                          ? qsTr("This build has no Remote Play, so it can't search the network. Type "
                                 + "the address below.")
                          : qsTr("No console answered. Check that it's on, on the same network, with "
                                 + "Remote Play enabled or its FTP server running — or type the address below.")
                }

                Repeater {
                    model: dialog.found

                    Item {
                        id: line
                        readonly property bool listed: dialog.alreadyListed(modelData.address)
                        Layout.fillWidth: true
                        implicitHeight: 64

                        Rectangle {
                            visible: line.y > 1
                            width: parent.width
                            height: 1
                            color: Theme.glassEdge
                        }

                        RowLayout {
                            anchors.fill: parent
                            spacing: 14
                            // The kind, in a round seal.
                            Rectangle {
                                implicitWidth: 42
                                implicitHeight: 42
                                radius: 21
                                color: Theme.alpha(Theme.ink, Theme.light ? 0.05 : 0.07)
                                border.width: 1
                                border.color: Theme.glassEdge
                                Text {
                                    anchors.centerIn: parent
                                    // Found by its FTP server alone, it may not say which it is.
                                    text: modelData.kind === "ps5" ? "PS5" : modelData.kind === "ps4" ? "PS4" : "FTP"
                                    color: Theme.ink
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fontCaption
                                    font.weight: Font.Bold
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.name.length > 0 ? modelData.name : modelData.address
                                    color: Theme.ink
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        text: modelData.address
                                        color: Theme.text
                                        font.family: Theme.fontMono
                                        font.pixelSize: Theme.fontSmall
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: "·  " + (modelData.state === "ftp" ? qsTr("its FTP server answered (Remote Play did not)")
                                                     : modelData.state === "standby" ? qsTr("in rest mode")
                                                     : qsTr("ready"))
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSmall
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            StyledButton {
                                text: line.listed ? qsTr("Already added") : qsTr("Add")
                                iconName: line.listed ? "check" : "plus"
                                enabled: !line.listed
                                primary: !line.listed
                                minimumWidth: 100
                                onClicked: dialog.add(modelData.name, modelData.address, modelData.kind || "")
                            }
                        }
                    }
                }
            }
        }

        // ── By hand: for a console that does not answer the search.
        Eyebrow {
            Layout.leftMargin: Theme.dialogMargin + 4
            Layout.topMargin: 10
            text: qsTr("By hand")
        }

        RowLayout {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 8
            StyledField {
                id: consoleNameField
                Layout.preferredWidth: 150
                placeholderText: qsTr("Name (optional)")
            }
            StyledField {
                id: addressInput
                Layout.fillWidth: true
                font.family: Theme.fontMono
                placeholderText: qsTr("IP address, e.g. 192.168.1.50")
                onAccepted: if (text.trim().length > 0) dialog.add(consoleNameField.text, text)
            }
            StyledButton {
                text: qsTr("Add")
                iconName: "plus"
                enabled: addressInput.text.trim().length > 0
                minimumWidth: 100
                onClicked: dialog.add(consoleNameField.text, addressInput.text)
            }
        }

        // What a click on the new console's card will start, whichever way it
        // is added above.
        Rectangle {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: 10
            Layout.fillWidth: true
            height: 1
            color: Theme.glassEdge
        }
        RowLayout {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: 4
            spacing: 16
            Text {
                text: qsTr("Start with")
                color: Theme.text
                font.weight: Font.Medium
                font.pixelSize: Theme.fontBody
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
            font.pixelSize: Theme.fontSmall
        }
        Item { Layout.preferredHeight: Theme.dialogMargin - 10 }
    }
}
