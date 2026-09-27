// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Text field: a quiet fill with a thin border; on focus the border turns to
// the accent with a soft glow around it.
import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: field
    color: Theme.text
    font.pixelSize: 13
    selectByMouse: true
    placeholderTextColor: Theme.alpha(Theme.textSecondary, 0.75)
    implicitHeight: Theme.fieldHeight
    leftPadding: 14
    rightPadding: 14
    selectionColor: Theme.alpha(Theme.accent, 0.35)
    selectedTextColor: Theme.text

    background: Item {
        // The focus glow, underneath.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: Theme.radiusField + 3
            visible: field.activeFocus
            color: "transparent"
            border.width: 3
            border.color: Theme.alpha(Theme.accent, 0.20)
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusField
            color: Theme.light ? "#FFFFFF" : Theme.controlFill
            border.width: 1
            border.color: field.activeFocus ? Theme.accent : Theme.glassEdge
            Behavior on border.color { ColorAnimation { duration: Theme.fast } }
        }
    }
}
