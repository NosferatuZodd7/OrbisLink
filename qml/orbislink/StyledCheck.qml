// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

CheckBox {
    id: control
    property color labelColor: Theme.text

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: 4
        color: control.checked ? Theme.accent : Theme.panelAlt
        border.color: control.checked ? Theme.accent : Theme.border

        Text {
            anchors.centerIn: parent
            visible: control.checked
            text: "✓"
            color: "#0f1117"
            font.pixelSize: 12
            font.bold: true
        }
    }

    contentItem: Text {
        text: control.text
        color: control.labelColor
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        leftPadding: control.indicator.width + 8
        verticalAlignment: Text.AlignVCenter
    }
}
