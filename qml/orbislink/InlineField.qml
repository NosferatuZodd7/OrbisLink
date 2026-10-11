// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A text that can be edited where it is: it reads as plain text, shows a
// quiet field under the pointer, and becomes a field with the accent edge
// when clicked — the way names are renamed in place in system settings.
import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: field

    property bool mono: false

    color: Theme.text
    font.pixelSize: 13
    font.family: mono ? Theme.fontMono : font.family
    selectByMouse: true
    placeholderTextColor: Theme.alpha(Theme.textSecondary, 0.85)
    implicitHeight: 28
    leftPadding: 8
    rightPadding: 8
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    selectionColor: Theme.alpha(Theme.accent, 0.35)
    selectedTextColor: Theme.text

    HoverHandler { id: hover; cursorShape: Qt.IBeamCursor }

    background: Rectangle {
        radius: 8
        color: field.activeFocus ? (Theme.light ? "#FFFFFF" : Theme.controlFill)
             : hover.hovered ? Theme.alpha(Theme.text, 0.05) : "transparent"
        border.width: field.activeFocus ? 1 : 0
        border.color: Theme.accent
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }

    Keys.onEscapePressed: field.focus = false
}
