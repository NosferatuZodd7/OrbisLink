// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The PSN Account ID field, with the conversion in view.
//
// The value is accepted in whatever form it comes (hexadecimal, decimal or
// base64), and the other two are shown below: whoever has the ID in front
// of them confirms at a glance that it is the same number, instead of
// having to take it on trust.
//
// The byte order has a switch, and it does NOT touch what is typed: it
// reads the same text another way, and turning it off puts everything back
// as it was. A control that rewrote the field would leave whoever touched
// it to see what it did with another Account ID and without their own.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root

    // What the console needs. Empty while what is typed does not give a
    // valid Account ID.
    readonly property string base64: forms.valid ? forms.base64 : ""
    readonly property bool ok: forms.valid === true
    property alias text: field.text
    property var forms: ({ valid: false })

    spacing: 8

    function reload() {
        if (typeof stream === "undefined" || !stream) {
            forms = ({ valid: false })
            return
        }
        forms = inverse.checked ? stream.accountIdReversed(field.text)
                                 : stream.accountIdForms(field.text)
    }

    StyledField {
        id: field
        Layout.fillWidth: true
        placeholderText: qsTr("hexadecimal, decimal or base64")
        onTextChanged: {
            // A new ID always starts with the normal reading: keeping the
            // switch on from a previous attempt would be a silent
            // trap.
            inverse.checked = false
            root.reload()
        }
    }

    // The conversion box. It only appears once something is typed: a help
    // panel that is permanently empty is noise.
    Rectangle {
        Layout.fillWidth: true
        visible: field.text.trim().length > 0
        color: Theme.panelAltFill
        border.color: root.ok ? Theme.glassEdge
                              : Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.5)
        border.width: 1
        radius: Theme.radiusSmall
        implicitHeight: content.implicitHeight + 24

        Behavior on border.color { ColorAnimation { duration: Theme.fast } }

        ColumnLayout {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 7

            // When it does not work, say why instead of leaving the box empty.
            Text {
                Layout.fillWidth: true
                visible: !root.ok
                wrapMode: Text.WordWrap
                text: root.forms.error ? root.forms.error : ""
                color: Theme.error
                font.pixelSize: 11
            }

            Text {
                Layout.fillWidth: true
                visible: root.ok
                text: {
                    if (root.forms.format === "base64") return qsTr("Read as base64")
                    if (root.forms.format === "decimal") return qsTr("Read as decimal")
                    return qsTr("Read as hexadecimal")
                }
                color: Theme.textSecondary
                font.pixelSize: 11
            }

            Repeater {
                model: root.ok ? [
                    { tag: qsTr("Base64 — what the console receives"), value: root.forms.base64, highlight: true },
                    { tag: qsTr("Hexadecimal"), value: root.forms.hex, highlight: false },
                    { tag: qsTr("Decimal"), value: root.forms.decimal, highlight: false }
                ] : []

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.preferredWidth: 180
                        text: modelData.tag
                        color: Theme.textSecondary
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    // Selectable: the converted value is only useful if it
                    // can be copied elsewhere.
                    TextInput {
                        Layout.fillWidth: true
                        text: modelData.value
                        readOnly: true
                        selectByMouse: true
                        color: modelData.highlight ? Theme.accent : Theme.text
                        font.pixelSize: 12
                        font.family: "monospace"
                        font.bold: modelData.highlight
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 3
                visible: root.ok
                height: 1
                color: Theme.glassEdge
            }

            // The emergency hatch, and only that: some tools show the eight
            // raw bytes instead of the number, and then the hexadecimal
            // appears in reverse order.
            StyledCheck {
                id: inverse
                visible: root.ok
                text: qsTr("The bytes are in the opposite order")
                labelColor: Theme.textSecondary
                font.pixelSize: 11
                onCheckedChanged: root.reload()
            }

            Text {
                Layout.fillWidth: true
                visible: root.ok
                wrapMode: Text.WordWrap
                text: inverse.checked
                    ? qsTr("Reading the ID backwards. Turn this off if the lines above no longer "
                           + "match what the console shows.")
                    : qsTr("Only turn this on if the lines above do not match what you saw on "
                           + "the console. What you typed is not changed.")
                color: inverse.checked ? Theme.warn : Theme.textSecondary
                font.pixelSize: 10
            }
        }
    }
}
