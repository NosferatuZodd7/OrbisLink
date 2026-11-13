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

    // A sheet of glass floating over the window, lit along its upper edge;
    // it fades in and settles, without bouncing.
    background: Plate {
        level: "secondary"
        radius: Theme.radiusDialog
        color: Theme.dialogFill
        border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Theme.glassEdge
    }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
        NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast; easing.type: Theme.easeOut }
    }

    header: DialogHeader {
        eyebrow: qsTr("Upload over FTP")
        title: qsTr("The file already exists")
        dialog: dialog
    }

    contentItem: ColumnLayout {
        spacing: 16

        // The file, and the two copies side by side: what is on the console
        // and what would replace it, in a well.
        Plate {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: 4
            sunken: true
            radius: 14
            implicitHeight: fileColumn.implicitHeight + 28

            ColumnLayout {
                id: fileColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: 40
                        implicitHeight: 40
                        radius: 20
                        color: Theme.alpha(Theme.warn, 0.14)
                        border.color: Theme.alpha(Theme.warn, 0.4)
                        Icon {
                            anchors.centerIn: parent
                            name: "file"
                            size: 18
                            color: Theme.light ? Qt.darker(Theme.warn, 1.3) : Theme.warn
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: dialog.item ? dialog.item.name : ""
                            color: Theme.ink
                            font.family: Theme.fontMono
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            wrapMode: Text.WrapAnywhere
                        }
                        Text {
                            visible: dialog.conflicts.length > 1
                            text: qsTr("File %1 of %2").arg(dialog.current + 1).arg(dialog.conflicts.length)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: Theme.glassEdge
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    Column {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 3
                        Eyebrow { text: qsTr("On the console"); color: Theme.textSecondary }
                        Text {
                            text: dialog.item ? dialog.item.remoteSize : ""
                            color: Theme.ink
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSmall + 1
                        }
                    }
                    Rectangle {
                        implicitWidth: 1
                        implicitHeight: 30
                        color: Theme.glassEdge
                    }
                    Column {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 3
                        Eyebrow { text: qsTr("This PC"); color: Theme.textSecondary }
                        Text {
                            text: dialog.item ? dialog.item.localSize : ""
                            color: Theme.ink
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSmall + 1
                        }
                    }
                }
            }
        }

        // The name to use when sending it as a new file.
        ColumnLayout {
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            spacing: 6
            Eyebrow {
                text: qsTr("New name, to keep both")
            }
            StyledField {
                id: nameInput
                Layout.fillWidth: true
                font.family: Theme.fontMono
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
