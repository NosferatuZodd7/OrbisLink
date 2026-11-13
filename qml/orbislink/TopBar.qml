// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The toolbar: a pane of glass across the top of the page. On the left, the
// console in use (a click switches to another); next to it its three
// services; on the right the actions that are always at hand — look for the
// services again, the transfers, the log.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Plate {
    id: bar
    level: "primary"

    property int transfers: 0
    property bool transfersOpen: false
    property bool transfersAllowed: true
    signal transfersToggled()
    signal logRequested()

    readonly property bool narrow: width < 620

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 10
        spacing: 10

        ConsoleChip {
            compact: bar.narrow
            onShowConsoles: window.showHome()
        }

        // The three services, an icon each with its state at the corner.
        Row {
            visible: app.consoles.length > 0
            spacing: 2
            ServiceIndicator {
                label: qsTr("Remote Play")
                iconName: "gamepad"
                state_: app.remotePlayState
                hint: app.remotePlayHint
            }
            ServiceIndicator {
                label: qsTr("FTP")
                state_: app.ftpState
                hint: app.ftpHint
            }
            ServiceIndicator {
                label: qsTr("Installer")
                iconName: "package"
                state_: app.installerState
                hint: app.installerHint
            }
        }

        Item { Layout.fillWidth: true }

        StyledToolButton {
            iconName: "refresh"
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Check the services now")
            onClicked: app.checkServicesNow()
        }
        StyledToolButton {
            iconName: "log"
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Log and diagnostics (Ctrl+L)")
            onClicked: bar.logRequested()
        }
        StyledToolButton {
            iconName: "panel"
            active: bar.transfersOpen
            opacity: bar.transfersAllowed ? 1.0 : 0.45
            ToolTip.visible: hovered
            ToolTip.text: bar.transfersAllowed ? qsTr("The queue and the console's files (F9)") : window.panelLockReason
            onClicked: bar.transfersToggled()
            Rectangle {
                visible: bar.transfers > 0
                x: parent.width - width + 2
                y: -2
                width: Math.max(16, n.implicitWidth + 8)
                height: 16
                radius: 8
                color: Theme.accent
                Text {
                    id: n
                    anchors.centerIn: parent
                    text: bar.transfers
                    color: Theme.textOnAccent
                    font.pixelSize: 10
                    font.weight: Font.Bold
                }
            }
        }
    }
}
