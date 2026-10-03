// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Files dropped for FTP whose name is already taken in the upload folder
// (on the console, or by an upload still in the queue). One at a time, as
// FileZilla does: overwrite it, send it under another name, or skip it —
// and, when there are several, do the same for the rest.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: dialog

    // What came from app.uploadConflicts, and the answers so far.
    property var conflicts: []
    property var decisions: []
    property int current: 0
    readonly property var item: conflicts.length > current ? conflicts[current] : null
    readonly property int remaining: conflicts.length - current - 1

    function begin(list) {
        conflicts = list
        decisions = []
        current = 0
        applyAll.checked = false
        loadName()
        open()
    }

    function loadName() {
        if (item)
            nameInput.text = item.suggestion
    }

    // Records the answer for the file on screen (and, with "the same for the
    // rest", for all the others); when there is nothing left, sends them.
    function answer(action) {
        var chosen = decisions.slice()
        var last = applyAll.checked ? conflicts.length : current + 1
        for (var i = current; i < last; ++i) {
            var entry = conflicts[i]
            chosen.push({
                index: entry.index,
                action: action,
                // Each file keeps its own free name when renaming them all.
                name: i === current ? nameInput.text.trim() : entry.suggestion
            })
        }
        decisions = chosen
        current = last
        if (current >= conflicts.length) {
            app.resolveUploadConflicts(decisions)
            close()
        } else {
            loadName()
        }
    }

    // Closing with the ✕ or Esc before answering: the files still without
    // an answer are skipped (the ones already answered go as decided).
    onClosed: {
        if (current < conflicts.length) {
            var chosen = decisions.slice()
            for (var i = current; i < conflicts.length; ++i)
                chosen.push({ index: conflicts[i].index, action: "skip", name: "" })
            app.resolveUploadConflicts(chosen)
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radiusDialog
    }

    header: DialogHeader {
        title: qsTr("The file already exists")
        dialog: dialog
    }

    contentItem: ColumnLayout {
        spacing: 14

        RowLayout {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: 6
            spacing: 14

            Rectangle {
                Layout.alignment: Qt.AlignTop
                implicitWidth: 44
                implicitHeight: 44
                radius: 22
                color: Theme.alpha(Theme.warn, 0.14)
                Icon {
                    anchors.centerIn: parent
                    name: "file"
                    size: 20
                    color: Theme.warn
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    text: dialog.item ? dialog.item.name : ""
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    wrapMode: Text.WrapAnywhere
                }
                Text {
                    Layout.fillWidth: true
                    text: dialog.item
                          ? qsTr("On the console: %1  ·  This PC: %2")
                                .arg(dialog.item.remoteSize).arg(dialog.item.localSize)
                          : ""
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: dialog.conflicts.length > 1
                    text: qsTr("File %1 of %2").arg(dialog.current + 1).arg(dialog.conflicts.length)
                    color: Theme.textSecondary
                    font.pixelSize: 11
                }
            }
        }

        // The name to use when sending it as a new file.
        ColumnLayout {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 6
            Text {
                text: qsTr("New name, to keep both")
                color: Theme.textSecondary
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            StyledField {
                id: nameInput
                Layout.fillWidth: true
                onAccepted: if (text.trim().length > 0) dialog.answer("rename")
            }
        }

        StyledCheck {
            id: applyAll
            visible: dialog.remaining > 0
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            text: qsTr("Do the same for the other %n file(s)", "", dialog.remaining)
        }
    }

    footer: Item {
        implicitHeight: Theme.dialogFooter
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogInner
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 10
            StyledButton {
                text: qsTr("Skip")
                minimumWidth: 90
                onClicked: dialog.answer("skip")
            }
            Item { Layout.fillWidth: true }
            StyledButton {
                text: qsTr("Keep both")
                iconName: "copy"
                minimumWidth: 110
                enabled: nameInput.text.trim().length > 0
                onClicked: dialog.answer("rename")
            }
            StyledButton {
                text: qsTr("Replace")
                primary: true
                minimumWidth: 110
                onClicked: dialog.answer("overwrite")
            }
        }
    }
}
