// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The packages of a folder of this PC put in order, as PS4 PKG Tool does:
// each renamed from what it says about itself, numbered in the order they
// install, a folder per game. What would change is shown first; nothing is
// renamed until "Organise".
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: dialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(720, parent.width - 48)
    height: Math.min(640, parent.height - 48)
    modal: true
    padding: 0
    closePolicy: shelf.organizing ? Popup.NoAutoClose : Popup.CloseOnEscape

    property string folderName: ""
    property var steps: []
    property int changes: 0
    property int duplicates: 0
    property bool planned: false
    property int done: 0
    property int total: 0
    readonly property var presets: shelf.organizePatterns()
    // The pattern: one of the presets, or one typed.
    property string pattern: presets.length > 0 ? presets[0] : ""

    function begin(name) {
        folderName = name
        steps = []
        planned = false
        done = 0
        total = 0
        open()
        plan()
    }
    function plan() {
        planned = false
        shelf.planOrganize(pattern, orderCheck.checked, folderCheck.checked)
    }

    // Typing: wait for a pause before reading the packages again.
    Timer {
        id: replan
        interval: 400
        onTriggered: dialog.plan()
    }

    Connections {
        target: shelf
        function onOrganizePlanned(steps, changes, duplicates) {
            if (!dialog.visible)
                return
            dialog.steps = steps
            dialog.changes = changes
            dialog.duplicates = duplicates
            dialog.planned = true
        }
        function onOrganizeProgress(done, total) {
            dialog.done = done
            dialog.total = total
        }
        function onOrganizeFinished(moved, failed) {
            if (dialog.visible)
                dialog.close()
        }
    }

    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radiusDialog
    }

    header: DialogHeader {
        title: qsTr("Organise packages")
        dialog: dialog
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            text: qsTr("The .pkg files in %1 and its folders, renamed from what each package says about itself. "
                       + "Nothing changes until you press Organise.").arg(dialog.folderName)
            color: Theme.textSecondary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        // The name: a preset or one's own, with the placeholders below.
        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            columns: 2
            columnSpacing: 10
            rowSpacing: 8
            Text {
                text: qsTr("Name")
                color: Theme.text
                font.pixelSize: 13
            }
            StyledCombo {
                id: presetCombo
                Layout.fillWidth: true
                model: dialog.presets.concat([qsTr("My own…")])
                onActivated: function (index) {
                    if (index < dialog.presets.length) {
                        dialog.pattern = dialog.presets[index]
                        patternField.text = dialog.pattern
                        dialog.plan()
                    } else {
                        patternField.forceActiveFocus()
                    }
                }
            }
            Item { implicitWidth: 1 }
            StyledField {
                id: patternField
                Layout.fillWidth: true
                text: dialog.pattern
                onTextEdited: {
                    dialog.pattern = text
                    var preset = dialog.presets.indexOf(text)
                    presetCombo.currentIndex = preset >= 0 ? preset : dialog.presets.length
                    replan.restart()
                }
            }
            Item { implicitWidth: 1 }
            Text {
                Layout.fillWidth: true
                text: "{TITLE}  {TITLE_ID}  {VERSION}  {CATEGORY}  {REGION}  {CONTENT_ID}  {FIRMWARE}  {TYPE}"
                color: Theme.textMuted
                font.pixelSize: 11
                font.family: "monospace"
                wrapMode: Text.WrapAnywhere
            }
        }

        ColumnLayout {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 2
            StyledCheck {
                id: orderCheck
                text: qsTr("Number them in install order: 00 the game, 01 its update, 02 its add-ons")
                onToggled: dialog.plan()
            }
            StyledCheck {
                id: folderCheck
                text: qsTr("A folder per game: “Title (TITLE ID)”")
                onToggled: dialog.plan()
            }
        }

        // What happens to each package.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            radius: 12
            color: Theme.panelAltFill
            border.width: 1
            border.color: Theme.glassEdge
            clip: true

            ListView {
                id: stepList
                anchors.fill: parent
                anchors.margins: 6
                model: dialog.steps
                spacing: 2
                ScrollBar.vertical: ScrollBar {}
                delegate: Item {
                    id: stepRow
                    required property var modelData
                    width: stepList.width - 8
                    height: stepColumn.implicitHeight + 10
                    ColumnLayout {
                        id: stepColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1
                        Text {
                            Layout.fillWidth: true
                            text: stepRow.modelData.from
                            color: stepRow.modelData.changes ? Theme.textSecondary : Theme.textMuted
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                        Text {
                            Layout.fillWidth: true
                            text: stepRow.modelData.duplicate
                                  ? qsTr("The same package as %1 — left as it is").arg(stepRow.modelData.sameAs)
                                  : stepRow.modelData.changes ? "→ " + stepRow.modelData.to
                                  : qsTr("Already named this way")
                            color: stepRow.modelData.duplicate ? Theme.warn
                                   : stepRow.modelData.changes ? Theme.text : Theme.textMuted
                            font.pixelSize: 12
                            font.weight: stepRow.modelData.changes ? Font.Medium : Font.Normal
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: dialog.planned && dialog.steps.length === 0
                text: qsTr("No packages in this folder.")
                color: Theme.textSecondary
                font.pixelSize: 13
            }
            BusyIndicator {
                anchors.centerIn: parent
                visible: !dialog.planned
                running: visible
            }
        }
    }

    footer: Rectangle {
        implicitHeight: Theme.dialogFooter
        color: "transparent"
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogInner
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: shelf.organizing && dialog.total > 0
                      ? qsTr("Renaming %1 of %2…").arg(dialog.done).arg(dialog.total)
                      : !dialog.planned ? qsTr("Reading the packages…")
                      : (qsTr("%n to rename", "", dialog.changes)
                         + (dialog.duplicates > 0 ? "  ·  " + qsTr("%n copies", "", dialog.duplicates) : ""))
                color: Theme.textSecondary
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            StyledButton {
                text: qsTr("Cancel")
                minimumWidth: 90
                enabled: !shelf.organizing
                onClicked: dialog.close()
            }
            StyledButton {
                text: qsTr("Organise")
                iconName: "list-ordered"
                primary: true
                enabled: dialog.planned && dialog.changes > 0 && !shelf.organizing
                onClicked: shelf.applyOrganize()
            }
        }
    }
}
