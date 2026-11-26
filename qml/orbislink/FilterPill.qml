// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A filter of a list as a pill (the store's and the payloads' filters): its
// name, an optional icon and count, lit when chosen; a tooltip with `tip`.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic

Rectangle {
    id: pill
    property string text: ""
    property string iconName: ""
    property string count: ""
    property bool chosen: false
    property string tip: ""
    signal clicked()

    implicitWidth: pillRow.implicitWidth + 24
    implicitHeight: 32
    radius: height / 2
    color: chosen ? (Theme.light ? Qt.rgba(1, 1, 1, 0.92) : Qt.rgba(1, 1, 1, 0.14))
         : pillHover.hovered ? Theme.controlHover : Theme.controlFill
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus ? Theme.alpha(Theme.accent, 0.8)
                : chosen ? (Theme.light ? Qt.rgba(1, 1, 1, 1) : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.14))
                : Theme.light ? Qt.rgba(1, 1, 1, 0.75) : Theme.glassEdge
    Behavior on color { ColorAnimation { duration: Theme.fast } }

    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: text
    Keys.onSpacePressed: clicked()
    Keys.onReturnPressed: clicked()
    ToolTip.visible: pillHover.hovered && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 400

    HoverHandler { id: pillHover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: pill.clicked() }

    Row {
        id: pillRow
        anchors.centerIn: parent
        spacing: 6
        Icon {
            visible: pill.iconName.length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: pill.iconName
            size: 13
            color: pill.chosen ? Theme.accent : Theme.textSecondary
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: pill.text
            color: pill.chosen ? Theme.ink : Theme.text
            font.pixelSize: 12
            font.weight: pill.chosen ? Font.DemiBold : Font.Medium
        }
        Text {
            visible: pill.count.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: pill.count
            color: Theme.textSecondary
            font.family: Theme.fontMono
            font.pixelSize: 11
        }
    }
}
