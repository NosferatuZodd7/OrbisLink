// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The three appearances (Graphite, Ambient glass, Daylight) and the custom
// look as icons on a glass track at the foot of the navigation: in a row, or
// a column when it is folded. One click switches right away and only one is
// lit, as a glass pill: a plain
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

    // A column, for the folded navigation.
    property bool vertical: false
    // Each button's width when laid in a row (the row fills the given width).
    readonly property real cell: vertical ? 30 : Math.max(30, (width - 8 - 3 * 2) / 4)

    implicitWidth: vertical ? 38 : line.implicitWidth + 8
    implicitHeight: vertical ? line.implicitHeight + 8 : 36
    // A recessed glass track; the chosen look is the glass pill on it.
    radius: vertical ? 19 : height / 2
    color: Theme.light ? Qt.rgba(1, 1, 1, 0.45) : Qt.rgba(0, 0, 0, 0.20)
    border.width: 1
    border.color: Theme.light ? Qt.rgba(1, 1, 1, 0.9) : Theme.glassEdge

    Grid {
        id: line
        anchors.centerIn: parent
        columns: selector.vertical ? 1 : 4
        spacing: 2

        Repeater {
            model: selector.themes

            StyledToolButton {
                width: selector.cell
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
            width: selector.cell
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
