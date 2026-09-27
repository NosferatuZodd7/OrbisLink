// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Drop-down list, with the same body as the text fields.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic

ComboBox {
    id: control
    implicitHeight: Theme.fieldHeight

    background: Rectangle {
        radius: Theme.radiusField
        color: Theme.light ? "#FFFFFF" : (control.hovered ? Theme.controlHover : Theme.controlFill)
        border.width: 1
        border.color: control.activeFocus || control.popup.visible ? Theme.accent : Theme.glassEdge
    }
    contentItem: Text {
        leftPadding: 14
        rightPadding: 36
        text: control.displayText
        color: Theme.text
        font.pixelSize: 13
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    indicator: Icon {
        x: control.width - width - 12
        y: (control.height - height) / 2
        name: "chevron-down"
        size: 16
        color: Theme.textSecondary
    }
    delegate: ItemDelegate {
        id: row
        width: control.width - 8
        x: 4
        implicitHeight: 36
        contentItem: Text {
            leftPadding: 10
            text: modelData
            color: Theme.text
            font.pixelSize: 13
            font.weight: control.currentIndex === index ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 8
            color: row.highlighted ? Theme.accentFill : "transparent"
        }
        highlighted: control.highlightedIndex === index
    }
    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        padding: 4
        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.glassEdge
            radius: Theme.radiusField
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }
    }
}
