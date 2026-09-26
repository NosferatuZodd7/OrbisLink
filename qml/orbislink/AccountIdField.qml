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
    readonly property string base64: formas.valid ? formas.base64 : ""
    readonly property bool ok: formas.valid === true
    property alias text: campo.text
    property var formas: ({ valid: false })

    spacing: 8

    function reler() {
        if (typeof stream === "undefined" || !stream) {
            formas = ({ valid: false })
            return
        }
        formas = inverso.checked ? stream.accountIdReversed(campo.text)
                                 : stream.accountIdForms(campo.text)
    }

    StyledField {
        id: campo
        Layout.fillWidth: true
        placeholderText: qsTr("hexadecimal, decimal or base64")
        onTextChanged: {
            // A new ID always starts with the normal reading: keeping the
            // switch on from a previous attempt would be a silent
            // trap.
            inverso.checked = false
            root.reler()
        }
    }

    // The conversion box. It only appears once something is typed: a help
    // panel that is permanently empty is noise.
    Rectangle {
        Layout.fillWidth: true
        visible: campo.text.trim().length > 0
        color: Theme.panelAltFill
        border.color: root.ok ? Theme.glassEdge
                              : Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.5)
        border.width: 1
        radius: Theme.radiusSmall
        implicitHeight: conteudo.implicitHeight + 24

        Behavior on border.color { ColorAnimation { duration: Theme.fast } }

        ColumnLayout {
            id: conteudo
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
                text: root.formas.error ? root.formas.error : ""
                color: Theme.error
                font.pixelSize: 11
            }

            Text {
                Layout.fillWidth: true
                visible: root.ok
                text: {
                    if (root.formas.format === "base64") return qsTr("Read as base64")
                    if (root.formas.format === "decimal") return qsTr("Read as decimal")
                    return qsTr("Read as hexadecimal")
                }
                color: Theme.textSecondary
                font.pixelSize: 11
            }

            Repeater {
                model: root.ok ? [
                    { etiqueta: qsTr("Base64 — what the console receives"), valor: root.formas.base64, destaque: true },
                    { etiqueta: qsTr("Hexadecimal"), valor: root.formas.hex, destaque: false },
                    { etiqueta: qsTr("Decimal"), valor: root.formas.decimal, destaque: false }
                ] : []

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.preferredWidth: 180
                        text: modelData.etiqueta
                        color: Theme.textSecondary
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    // Selectable: the converted value is only useful if it
                    // can be copied elsewhere.
                    TextInput {
                        Layout.fillWidth: true
                        text: modelData.valor
                        readOnly: true
                        selectByMouse: true
                        color: modelData.destaque ? Theme.accent : Theme.text
                        font.pixelSize: 12
                        font.family: "monospace"
                        font.bold: modelData.destaque
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
                id: inverso
                visible: root.ok
                text: qsTr("The bytes are in the opposite order")
                labelColor: Theme.textSecondary
                font.pixelSize: 11
                onCheckedChanged: root.reler()
            }

            Text {
                Layout.fillWidth: true
                visible: root.ok
                wrapMode: Text.WordWrap
                text: inverso.checked
                    ? qsTr("Reading the ID backwards. Turn this off if the lines above no longer "
                           + "match what the console shows.")
                    : qsTr("Only turn this on if the lines above do not match what you saw on "
                           + "the console. What you typed is not changed.")
                color: inverso.checked ? Theme.warn : Theme.textSecondary
                font.pixelSize: 10
            }
        }
    }
}
