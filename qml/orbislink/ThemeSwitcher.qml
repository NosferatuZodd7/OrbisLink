// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The three themes as icons in the top bar, next to the settings.
// One click switches right away, without opening the settings, and the
// active theme is shown lit.
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: selector

    readonly property var themes: [
        { name: "dark", symbol: "☾", tip: qsTr("Dark theme") },
        { name: "glass",  symbol: "◐", tip: qsTr("Glass theme (dark, more translucent)") },
        { name: "light",  symbol: "☀", tip: qsTr("Light theme") }
    ]

    implicitWidth: line.implicitWidth + 8
    implicitHeight: 38
    radius: height / 2
    color: Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b, 0.35)
    border.width: 1
    border.color: Theme.glassEdge

    Row {
        id: line
        anchors.centerIn: parent
        spacing: 2

        Repeater {
            model: selector.themes

            ToolButton {
                id: button
                readonly property bool isActive: Theme.name === modelData.name

                width: 30
                height: 30
                font.pixelSize: 14
                text: modelData.symbol
                ToolTip.visible: hovered
                ToolTip.text: modelData.tip
                Accessible.name: modelData.tip

                scale: down ? Theme.pressScale : (hovered ? Theme.hoverScale : 1.0)
                Behavior on scale {
                    NumberAnimation { duration: Theme.fast; easing.type: Theme.easeSpring; easing.overshoot: 1.1 }
                }

                // Main.qml applies the theme when the settings change; here
                // it is only saved.
                onClicked: app.setTheme(modelData.name)

                background: Rectangle {
                    radius: width / 2
                    color: button.isActive ? Theme.accentFill
                         : button.hovered ? Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g,
                                                   Theme.panelAlt.b, 0.55)
                         : "transparent"
                    border.width: button.isActive ? 1 : 0
                    border.color: Theme.accent
                    Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
                }

                contentItem: Text {
                    text: button.text
                    font: button.font
                    color: button.isActive ? Theme.accent
                         : button.hovered ? Theme.text : Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    Behavior on color { ColorAnimation { duration: Theme.fast } }
                }
            }
        }
    }
}
