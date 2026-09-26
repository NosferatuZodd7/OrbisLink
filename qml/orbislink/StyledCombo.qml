// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic

ComboBox {
    id: control
    implicitHeight: 32

    background: Rectangle {
        color: Theme.panelAltFill
        border.color: control.activeFocus ? Theme.accent : Theme.border
        radius: 6
    }
    contentItem: Text {
        leftPadding: 8
        rightPadding: 24
        text: control.displayText
        color: Theme.text
        font.pixelSize: 12
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    indicator: Text {
        x: control.width - width - 10
        y: control.height / 2 - height / 2
        text: "⌄"
        color: Theme.textMuted
        font.pixelSize: 14
    }
    delegate: ItemDelegate {
        width: control.width
        contentItem: Text {
            leftPadding: 8
            text: modelData
            color: Theme.text
            font.pixelSize: 12
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: highlighted ? Theme.accentSoft : Theme.panelAlt
        }
        highlighted: control.highlightedIndex === index
    }
    popup: Popup {
        y: control.height
        width: control.width
        implicitHeight: contentItem.implicitHeight
        padding: 1
        background: Rectangle {
            color: Theme.panelAltFill
            border.color: Theme.border
            radius: 6
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
