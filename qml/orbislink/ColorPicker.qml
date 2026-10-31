// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A small colour picker: a saturation/brightness square, a hue strip,
// the hex code and a row of suggested colours. Every change is reported
// at once (picked), so what it colours changes while choosing.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: picker

    property color color: "#6C9DA0"
    signal picked(color value)

    // Kept apart from `color`: a grey has no hue of its own, and dragging
    // through it must not throw the hue strip back to red.
    property real hue: 0
    property real saturation: 0
    property real brightness: 1

    function load(value) {
        color = value
        var c = Qt.color(value)
        if (c.hsvHue >= 0)
            hue = c.hsvHue
        saturation = c.hsvSaturation
        brightness = c.hsvValue
        hexField.text = c.toString().toUpperCase()
    }
    function update() {
        color = Qt.hsva(hue, saturation, brightness, 1)
        hexField.text = color.toString().toUpperCase()
        picked(color)
    }

    readonly property var suggestions: ["#173B5C", "#426F89", "#6C9DA0", "#B28A4A", "#C18B43", "#A84D46",
                                        "#65866A", "#10283D", "#202B32", "#E5D3AE", "#F3E8CC", "#7C5CFF"]

    width: 272
    padding: 16
    // Never past the window's edges (nor under the taskbar).
    margins: 10
    modal: false
    // With the focus, Esc closes only the picker and not the window under it.
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Theme.menuFill
        radius: Theme.radius
        border.width: 1
        border.color: Theme.glassEdge
    }

    contentItem: ColumnLayout {
        spacing: 12

        Eyebrow { text: qsTr("Pick a colour") }

        // Saturation left to right, brightness bottom to top.
        Rectangle {
            id: field
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            radius: Theme.radiusSmall
            color: Qt.hsva(picker.hue, 1, 1, 1)
            clip: true
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#FFFFFFFF" }
                    GradientStop { position: 1.0; color: "#00FFFFFF" }
                }
            }
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#00000000" }
                    GradientStop { position: 1.0; color: "#FF000000" }
                }
            }
            Rectangle {
                x: picker.saturation * field.width - width / 2
                y: (1 - picker.brightness) * field.height - height / 2
                width: 16
                height: 16
                radius: 8
                color: picker.color
                border.width: 2
                border.color: "#FFFFFF"
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.CrossCursor
                function set(mouse) {
                    picker.saturation = Math.max(0, Math.min(1, mouse.x / width))
                    picker.brightness = Math.max(0, Math.min(1, 1 - mouse.y / height))
                    picker.update()
                }
                onPressed: (mouse) => set(mouse)
                onPositionChanged: (mouse) => { if (pressed) set(mouse) }
            }
        }

        // The hue strip.
        Rectangle {
            id: hueStrip
            Layout.fillWidth: true
            Layout.preferredHeight: 14
            radius: 7
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#FF0000" }
                GradientStop { position: 0.17; color: "#FFFF00" }
                GradientStop { position: 0.33; color: "#00FF00" }
                GradientStop { position: 0.5; color: "#00FFFF" }
                GradientStop { position: 0.67; color: "#0000FF" }
                GradientStop { position: 0.83; color: "#FF00FF" }
                GradientStop { position: 1.0; color: "#FF0000" }
            }
            Rectangle {
                x: picker.hue * hueStrip.width - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                radius: 9
                color: Qt.hsva(picker.hue, 1, 1, 1)
                border.width: 2
                border.color: "#FFFFFF"
            }
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: Qt.PointingHandCursor
                function set(mouse) {
                    picker.hue = Math.max(0, Math.min(0.999, (mouse.x - 6) / hueStrip.width))
                    picker.update()
                }
                onPressed: (mouse) => set(mouse)
                onPositionChanged: (mouse) => { if (pressed) set(mouse) }
            }
        }

        // The code, for whoever knows the exact colour.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                radius: 18
                color: picker.color
                border.width: 1
                border.color: Theme.glassEdge
            }
            StyledField {
                id: hexField
                Layout.fillWidth: true
                font.family: Theme.fontMono
                maximumLength: 7
                validator: RegularExpressionValidator { regularExpression: /#?[0-9A-Fa-f]{0,6}/ }
                onTextEdited: {
                    var code = text.charAt(0) === "#" ? text : "#" + text
                    if (/^#[0-9A-Fa-f]{6}$/.test(code)) {
                        var c = Qt.color(code)
                        if (c.hsvHue >= 0)
                            picker.hue = c.hsvHue
                        picker.saturation = c.hsvSaturation
                        picker.brightness = c.hsvValue
                        picker.color = c
                        picker.picked(c)
                    }
                }
            }
        }

        // Suggestions: the atlas's own inks first, then a few to go further.
        Eyebrow {
            Layout.topMargin: 2
            text: qsTr("Suggestions")
            color: Theme.textSecondary
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 6
            rowSpacing: 8
            columnSpacing: 8
            Repeater {
                model: picker.suggestions
                Rectangle {
                    required property string modelData
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 14
                    color: modelData
                    border.width: swatchArea.containsMouse ? 2 : 1
                    border.color: swatchArea.containsMouse ? Theme.text : Theme.glassEdge
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData
                    MouseArea {
                        id: swatchArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            picker.load(parent.modelData)
                            picker.picked(picker.color)
                        }
                    }
                }
            }
        }
    }
}
