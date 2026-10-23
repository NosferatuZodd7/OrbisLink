// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The three themes and the custom look as icons in the top bar, next to
// the settings. One click switches right away and only one is lit: a plain
// theme sets the custom colours aside; the custom look brings back the
// chosen saved look (or the colours set aside).
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: selector

    // No custom look yet: the Personalisation page should open.
    signal customRequested()

    readonly property var themes: [
        { name: "dark", icon: "moon", tip: qsTr("Dark theme") },
        { name: "glass", icon: "contrast", tip: qsTr("Glass theme (dark, more translucent)") },
        { name: "light", icon: "sun", tip: qsTr("Light theme") }
    ]

    implicitWidth: line.implicitWidth + 8
    implicitHeight: 36
    radius: 11
    color: Theme.controlFill
    border.width: 1
    border.color: Theme.glassEdge

    Row {
        id: line
        anchors.centerIn: parent
        spacing: 2

        Repeater {
            model: selector.themes

            StyledToolButton {
                width: 30
                height: 28
                iconName: modelData.icon
                iconSize: 16
                active: Theme.name === modelData.name && !app.customLook
                ToolTip.visible: hovered
                ToolTip.text: modelData.tip
                Accessible.name: modelData.tip
                // Main.qml applies the theme when the settings change; here
                // it is only saved.
                onClicked: app.setTheme(modelData.name)
            }
        }

        StyledToolButton {
            width: 30
            height: 28
            iconName: "sparkles"
            iconSize: 16
            active: app.customLook
            ToolTip.visible: hovered
            ToolTip.text: app.activePreset.length > 0 ? qsTr("Custom look: %1").arg(app.activePreset)
                                                      : qsTr("Custom look (Settings → Personalisation)")
            onClicked: {
                if (!app.applyCustomLook())
                    selector.customRequested()
            }
        }
    }
}
