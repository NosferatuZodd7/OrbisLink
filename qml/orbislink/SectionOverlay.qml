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
        anchors.topMargin: 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: Math.min(parent.width - 32, Math.max(Math.min(720, parent.width - 32), parent.width * 0.74))
        clip: true

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusPane
            // Solid: how much of the picture shows through is the slider's alone.
            color: Qt.rgba(Theme.panel.r, Theme.panel.g, Theme.panel.b, 1)
            border.width: 1
            border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Theme.glassEdge
            opacity: overlay.shownOpacity
            // The light on the glass, as on every pane.
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: Math.max(0, parent.radius - 1)
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.38 : 0.05) }
                    GradientStop { position: 0.3; color: Qt.rgba(1, 1, 1, 0) }
                }
            }
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
            // The page draws no panel of its own over the picture (margins 0
            // there): the room around it on this plate is given here.
            anchors.margins: 20
            anchors.topMargin: 16
            opacity: overlay.contentOpacity
            // As one picture, so what overlaps inside does not show through itself.
            layer.enabled: opacity < 1
        }
    }

    // The bar: what is open, how see-through, and the way out — the page's
    // head, as PageHeader draws it elsewhere: a glass disc with the page's
    // icon, the champagne eyebrow ("Over Remote Play") and the title in bold,
    // on a pill of floating glass.
    Plate {
        id: bar
        level: "secondary"
        anchors.top: parent.top
        anchors.topMargin: 14
        anchors.horizontalCenter: panel.horizontalCenter
        width: panel.width
        height: 60
        radius: height / 2
        color: Theme.menuFill
        border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.06)
        opacity: overlay.barOpacity

        MouseArea {
            anchors.fill: parent
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 14
            spacing: 12

            // The page's icon on a glass disc.
            Rectangle {
                visible: overlay.iconName.length > 0
                implicitWidth: 38
                implicitHeight: 38
                radius: 19
                color: Theme.controlFill
                border.width: 1
                border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.06)
                Icon {
                    anchors.centerIn: parent
                    name: overlay.iconName
                    size: 17
                    color: Theme.ink
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Eyebrow {
                    Layout.fillWidth: true
                    text: qsTr("Over Remote Play")
                }
                Text {
                    Layout.fillWidth: true
                    text: overlay.title
                    color: Theme.ink
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.Bold
                    font.letterSpacing: -0.2
                    elide: Text.ElideRight
                }
            }
            Rectangle {
                implicitWidth: 1
                implicitHeight: 28
                color: Theme.rule
                visible: bar.width > 520
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
                Accessible.name: ToolTip.text
                ToolTip.visible: hovered || pressed
                ToolTip.text: qsTr("How opaque this page is over the picture")

                background: Rectangle {
                    x: opacitySlider.leftPadding
                    y: opacitySlider.topPadding + opacitySlider.availableHeight / 2 - height / 2
                    width: opacitySlider.availableWidth
                    height: 4
                    radius: 2
                    color: Theme.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.14)
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
                    width: 20
                    height: 20
                    radius: 10
                    // A glass bead, white and lit, as the switches' knobs are.
                    color: "#FBFAF7"
                    border.width: 1
                    border.color: Theme.light ? Qt.rgba(0, 0, 0, 0.12) : Qt.rgba(1, 1, 1, 0.6)
                    scale: opacitySlider.pressed ? 1.08 : 1
                    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
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
                Accessible.name: ToolTip.text
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
