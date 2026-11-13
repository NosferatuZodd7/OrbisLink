// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A card in the installs panel for a job that is not a queued package: a
// homebrew store install, a console library move or payload run. Drawn as
// the queue's other rows are: its picture with the step in its corner, the
// name over a line of mono facts, a state tag, a thin bar, and the figures
// with the round buttons that act on it. Under way it is a card of brighter
// glass, lit from above; otherwise a row between hairlines that turns into a
// glass pill under the mouse.
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
    // Where it is, as the other cards say it (StageLoader): in the icon's
    // corner. None when empty.
    property string stage: ""
    property bool waiting: false
    // Under way: lit, above the others.
    property bool highlighted: false
    property string stateText: ""
    property bool showBar: true
    // Whether a working job can be stopped from here.
    property bool cancellable: false
    // A button of its own once it is done (an icon name; none when empty).
    property string doneIcon: ""
    property string doneHint: ""
    // Whether a failed or stopped job can go again from here.
    property bool retryable: false
    signal cancelClicked()
    signal removeClicked()
    signal doneClicked()
    signal retryClicked()

    readonly property bool working: job.state === "working"
    readonly property bool measured: job.percent !== undefined && job.percent !== null
    readonly property color tone: job.state === "error" ? Theme.error
                                : job.state === "done" ? Theme.ok
                                : job.state === "cancelled" ? Theme.textSecondary : Theme.accent
    readonly property string tagTone: job.state === "error" ? "error"
                                    : job.state === "done" ? "ok"
                                    : job.state === "cancelled" || card.waiting ? "neutral" : "accent"

    radius: 16
    color: card.highlighted ? (Theme.light ? Qt.rgba(1, 1, 1, 0.86) : Qt.rgba(1, 1, 1, 0.09))
         : hover.hovered ? Theme.controlFill : "transparent"
    border.width: card.highlighted ? 1 : 0
    border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.08)
    Behavior on color { ColorAnimation { duration: Theme.fast } }
    implicitHeight: column.implicitHeight + 24
    HoverHandler { id: hover }

    // The light on the card while it is under way.
    Rectangle {
        visible: card.highlighted
        anchors.fill: parent
        anchors.margins: 1
        radius: parent.radius - 1
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.5 : 0.07) }
            GradientStop { position: 0.4; color: Qt.rgba(1, 1, 1, 0) }
        }
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Rectangle {
                implicitWidth: Theme.gameIcon; implicitHeight: Theme.gameIcon; radius: 12
                color: Theme.controlFill
                border.color: Theme.light ? Qt.rgba(1, 1, 1, 0.95) : Theme.glassEdge
                CoverArt {
                    id: picture
                    anchors.fill: parent
                    visible: ready
                    crop: true
                    edge: false
                    radius: parent.radius
                    picture: card.job.icon ? card.job.icon.toString() : ""
                }
                Icon {
                    anchors.centerIn: parent
                    visible: !picture.visible
                    name: card.fallbackIcon
                    size: 20
                    color: Theme.textSecondary
                }
                Rectangle {
                    visible: badge.visible
                    anchors.fill: badge
                    anchors.margins: -2
                    radius: width / 2
                    color: Theme.menuFill
                    border.width: 1
                    border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Theme.glassEdge
                }
                StageLoader {
                    id: badge
                    visible: card.stage.length > 0
                    stage: card.stage
                    percent: card.job.percent || 0
                    waiting: card.waiting
                    width: 22
                    height: 22
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: -5
                    anchors.bottomMargin: -5
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    text: card.job.name || ""
                    color: Theme.ink
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: [card.job.console, card.job.version].filter(function (part) {
                        return part && part.length > 0
                    }).join(" · ")
                    color: Theme.textSecondary
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: card.working && text.length > 0
                    text: card.job.stageText || ""
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                }
            }
        }

        // A thin bar: a recessed glass track, the job's colour, and a small
        // lit bead where it has got to.
        Item {
            id: bar
            visible: card.showBar
            Layout.fillWidth: true
            implicitHeight: 6
            readonly property real fraction: card.measured ? Math.max(0, Math.min(1, card.job.percent / 100)) : 1
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 3
                radius: 1.5
                color: Theme.light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.09)
                clip: true
                Rectangle {
                    visible: card.measured || !card.working
                    width: parent.width * bar.fraction
                    height: parent.height
                    radius: parent.radius
                    color: card.tone
                    Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                }
                // Nothing to count: a sweep while it lasts.
                Rectangle {
                    id: sweep
                    visible: !card.measured && card.working
                    width: parent.width * 0.3
                    height: parent.height
                    radius: parent.radius
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
            Rectangle {
                visible: card.working && card.measured && bar.fraction > 0 && bar.fraction < 1
                width: 6; height: 6; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                x: Math.max(0, bar.width * bar.fraction - 3)
                color: "#FFFFFF"
                Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            // The state leads the figures, as on the queue's other rows.
            Tag {
                Layout.alignment: Qt.AlignTop
                text: card.stateText
                tone: card.tagTone
            }
            Text {
                visible: card.showBar && card.measured
                text: (card.job.percent || 0).toFixed(1) + "%"
                color: Theme.text
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
            }
            Text {
                text: card.job.speedText || ""
                color: Theme.textSecondary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                visible: card.working && (card.job.speedText || "").length > 0
            }
            Text {
                Layout.fillWidth: true
                text: [card.job.amountText || "",
                       (card.job.etaText || "").length > 0 ? qsTr("%1 left").arg(card.job.etaText) : ""]
                      .filter(function (part) { return part.length > 0 }).join(" · ")
                color: Theme.textSecondary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
                visible: card.working
            }
            // What came of it, beside the button that clears the card.
            Text {
                Layout.fillWidth: true
                visible: !card.working
                text: card.job.message || ""
                color: card.job.state === "error" ? (Theme.light ? Qt.darker(Theme.error, 1.3) : Theme.error)
                                                  : Theme.textSecondary
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.WordWrap
            }
            StyledToolButton {
                Layout.alignment: Qt.AlignTop
                visible: card.retryable && (card.job.state === "error" || card.job.state === "cancelled")
                iconName: "rotate-ccw"
                iconSize: 15
                implicitWidth: 32; implicitHeight: 32
                Accessible.name: qsTr("Try again")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Try again")
                onClicked: card.retryClicked()
            }
            StyledToolButton {
                Layout.alignment: Qt.AlignTop
                visible: card.doneIcon.length > 0 && card.job.state === "done"
                iconName: card.doneIcon
                iconSize: 15
                implicitWidth: 32; implicitHeight: 32
                Accessible.name: card.doneHint
                ToolTip.visible: hovered && card.doneHint.length > 0
                ToolTip.text: card.doneHint
                onClicked: card.doneClicked()
            }
            StyledToolButton {
                Layout.alignment: Qt.AlignTop
                visible: !card.working || card.cancellable
                iconName: "close"
                iconSize: 15
                implicitWidth: 32; implicitHeight: 32
                danger: card.working
                Accessible.name: card.working ? qsTr("Cancel") : qsTr("Remove")
                ToolTip.visible: hovered
                ToolTip.text: card.working ? qsTr("Cancel") : qsTr("Remove")
                onClicked: card.working ? card.cancelClicked() : card.removeClicked()
            }
        }
    }
}
