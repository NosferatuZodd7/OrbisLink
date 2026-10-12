// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A page of the app (Games, the store, the save vault, payloads) over the
// Remote Play picture, so the game stays on screen while it is used: a panel
// sized for the room there is, its opacity chosen with a slider — never so
// faint that it cannot be read (app.overlayMinOpacity) — and over it a bar
// with the page's name, that slider and the ✕, always a step clearer than
// the panel so it can be found even when the panel is faint. The page itself
// is the very same one the window shows otherwise: Main.qml moves it in here
// (`slot`) while the overlay is open.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

FocusScope {
    id: overlay

    // What it shows ("games", "store", "saves", "payloads"; "" for nothing).
    property string section: ""
    property string title: ""
    property string iconName: ""
    // Where the page goes.
    property alias slot: holder
    // The opacity while the slider is dragged; kept (app.setOverlayOpacity)
    // when it is let go.
    property real shownOpacity: app.overlayOpacity
    readonly property real contentOpacity: Math.min(1, shownOpacity + 0.15)
    // The bar is never fainter than the page, so it can always be found.
    readonly property real barOpacity: Math.min(1, Math.max(0.92, shownOpacity + 0.25))

    signal closed()

    visible: section.length > 0

    // The panel: most of the picture's width on a narrow window, a
    // comfortable reading width on a wide one; the height the picture
    // leaves under the bar. Its fill follows the slider; what is written on
    // it stays a step clearer, so the words keep standing out from the game.
    Item {
        id: panel
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: bar.bottom
        anchors.topMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: Math.min(parent.width - 32, Math.max(Math.min(720, parent.width - 32), parent.width * 0.74))
        clip: true

        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            // Solid: how much of the picture shows through is the slider's alone.
            color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 1)
            border.width: 1
            border.color: Theme.glassEdge
            opacity: overlay.shownOpacity
        }

        // Clicks on the panel stay on it (the picture under it is not touched).
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: (wheel) => wheel.accepted = true
        }

        Item {
            id: holder
            anchors.fill: parent
            opacity: overlay.contentOpacity
            // As one picture, so what overlaps inside does not show through itself.
            layer.enabled: opacity < 1
        }
    }

    // The bar: what is open, how see-through, and the way out.
    Rectangle {
        id: bar
        anchors.top: parent.top
        anchors.topMargin: 14
        anchors.horizontalCenter: panel.horizontalCenter
        width: panel.width
        height: 46
        radius: 23
        color: Theme.menuFill
        border.width: 1
        border.color: Theme.alpha(Theme.accent, 0.45)
        opacity: overlay.barOpacity

        MouseArea {
            anchors.fill: parent
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 8
            spacing: 10
            Icon {
                visible: overlay.iconName.length > 0
                name: overlay.iconName
                size: 16
                color: Theme.accent
            }
            Text {
                Layout.fillWidth: true
                text: overlay.title
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                text: qsTr("Over Remote Play")
                color: Theme.textSecondary
                font.pixelSize: 12
                visible: bar.width > 620
            }
            Rectangle {
                width: 1
                height: 20
                color: Theme.glassEdge
                visible: bar.width > 620
            }
            Icon {
                name: "contrast"
                size: 15
                color: Theme.textSecondary
            }
            Slider {
                id: opacitySlider
                Layout.preferredWidth: Math.min(180, bar.width * 0.25)
                from: app.overlayMinOpacity
                to: 1
                stepSize: 0.01
                value: app.overlayOpacity
                onMoved: overlay.shownOpacity = value
                onPressedChanged: if (!pressed) app.setOverlayOpacity(value)
                ToolTip.visible: hovered || pressed
                ToolTip.text: qsTr("How opaque this page is over the picture")

                background: Rectangle {
                    x: opacitySlider.leftPadding
                    y: opacitySlider.topPadding + opacitySlider.availableHeight / 2 - height / 2
                    width: opacitySlider.availableWidth
                    height: 4
                    radius: 2
                    color: Theme.alpha(Theme.text, 0.18)
                    Rectangle {
                        width: opacitySlider.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: Theme.accent
                    }
                }
                handle: Rectangle {
                    x: opacitySlider.leftPadding + opacitySlider.visualPosition * (opacitySlider.availableWidth - width)
                    y: opacitySlider.topPadding + opacitySlider.availableHeight / 2 - height / 2
                    width: 16
                    height: 16
                    radius: 8
                    color: "#FFFFFF"
                    border.width: 2
                    border.color: Theme.accent
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }
            Text {
                text: Math.round(overlay.shownOpacity * 100) + "%"
                color: Theme.textSecondary
                font.pixelSize: 12
                font.family: Theme.fontMono
                Layout.preferredWidth: 38
            }
            StyledToolButton {
                iconName: "close"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Close (Esc) — back to the picture")
                onClicked: overlay.closed()
            }
        }
    }

    // A value changed elsewhere (another window, the settings) is followed.
    Connections {
        target: app
        function onSettingsChanged() {
            if (!opacitySlider.pressed)
                overlay.shownOpacity = app.overlayOpacity
        }
    }

    // Esc closes it, when nothing inside it took the key first (the
    // library's Esc goes up a folder before it gets here).
    Keys.onEscapePressed: overlay.closed()
}
