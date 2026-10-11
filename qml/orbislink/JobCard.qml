// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A card in the installs panel for a job that is not a queued package: a
// homebrew store install, a console library move or payload run. Its icon,
// name and console, a state chip, a bar (a sweep while there is no
// percentage), and what came of it.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: card

    // { name, icon, console, version, state ("working"/"done"/"error"/
    //   "cancelled"), stageText, percent, speedText, amountText, etaText,
    //   message }
    property var job: ({})
    property string fallbackIcon: "package"
    property string stateText: ""
    property bool showBar: true
    // Whether a working job can be stopped from here.
    property bool cancellable: false
    signal cancelClicked()
    signal removeClicked()

    readonly property bool working: job.state === "working"
    readonly property bool measured: job.percent !== undefined && job.percent !== null
    readonly property color tone: job.state === "error" ? Theme.error
                                : job.state === "done" ? Theme.ok
                                : job.state === "cancelled" ? Theme.textSecondary : Theme.accent

    radius: 16
    color: Theme.panelAltFill
    border.width: 1
    border.color: working ? Theme.alpha(Theme.accent, 0.6) : Theme.border
    implicitHeight: column.implicitHeight + 24

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                width: 44; height: 44; radius: 10
                color: Theme.controlFill
                border.color: Theme.border
                clip: true
                Image {
                    id: picture
                    anchors.fill: parent
                    anchors.margins: 1
                    source: card.job.icon ? card.job.icon : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: status === Image.Ready
                }
                Icon {
                    anchors.centerIn: parent
                    visible: !picture.visible
                    name: card.fallbackIcon
                    size: 22
                    color: Theme.textSecondary
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: card.job.name || ""
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: [card.job.console, card.job.version,
                           card.working ? card.job.stageText : ""].filter(function (part) {
                        return part && part.length > 0
                    }).join("  ·  ")
                    color: Theme.textMuted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
            Rectangle {
                radius: 11
                color: Theme.alpha(card.tone, 0.12)
                border.color: Theme.alpha(card.tone, 0.35)
                implicitWidth: stateLabel.implicitWidth + 18
                implicitHeight: 22
                Text {
                    id: stateLabel
                    anchors.centerIn: parent
                    text: card.stateText
                    color: card.tone
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }
            }
        }

        Rectangle {
            id: bar
            visible: card.showBar
            Layout.fillWidth: true
            height: 8
            radius: 4
            color: Theme.controlFill
            clip: true
            Rectangle {
                visible: card.measured || !card.working
                width: parent.width * (card.measured ? Math.max(0, Math.min(1, card.job.percent / 100)) : 1)
                height: parent.height
                radius: 4
                color: card.tone
                Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            }
            // Nothing to count: a sweep while it lasts.
            Rectangle {
                id: sweep
                visible: !card.measured && card.working
                width: parent.width * 0.3
                height: parent.height
                radius: 4
                color: card.tone
                NumberAnimation on x {
                    running: sweep.visible
                    loops: Animation.Infinite
                    from: -sweep.width
                    to: bar.width
                    duration: 1200
                    easing.type: Easing.InOutQuad
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: card.measured || !card.working || card.cancellable
            spacing: 10
            Text {
                Layout.alignment: Qt.AlignTop
                visible: card.showBar && card.measured
                text: (card.job.percent || 0).toFixed(1) + "%"
                color: Theme.textMuted
                font.pixelSize: 11
            }
            Text {
                text: "↓ " + card.job.speedText
                color: Theme.textMuted
                font.pixelSize: 11
                visible: card.working && (card.job.speedText || "").length > 0
            }
            Text {
                Layout.fillWidth: true
                text: (card.job.amountText || "")
                      + ((card.job.etaText || "").length > 0 ? "   ⏱ " + card.job.etaText : "")
                color: Theme.textMuted
                font.pixelSize: 11
                elide: Text.ElideRight
                visible: card.working
            }
            // What came of it, beside the button that clears the card.
            Text {
                Layout.fillWidth: true
                visible: !card.working
                text: card.job.message || ""
                color: card.job.state === "error" ? Theme.error : Theme.textMuted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            StyledToolButton {
                Layout.alignment: Qt.AlignTop
                visible: !card.working || card.cancellable
                iconName: "close"
                iconSize: 14
                implicitWidth: 28; implicitHeight: 28
                danger: card.working
                ToolTip.visible: hovered
                ToolTip.text: card.working ? qsTr("Cancel") : qsTr("Remove")
                onClicked: card.working ? card.cancelClicked() : card.removeClicked()
            }
        }
    }
}
