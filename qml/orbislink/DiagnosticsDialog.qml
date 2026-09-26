// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The log window. It serves two purposes: watching live what the
// application is doing (above all Remote Play, which has many steps and
// can fail at any of them), and producing a file that can be attached
// to a message without having to explain anything.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: dialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 60 : 900, 900)
    height: Math.min(parent ? parent.height - 60 : 620, 620)
    modal: true
    padding: 0

    property bool followTail: true
    property string filter: ""

    // A nearly opaque modal: with the panels' transparency, what is behind
    // would show through the box, and a box asking for a decision must not
    // be a window.
    Overlay.modal: Rectangle { color: Theme.scrim }

    background: Rectangle {
        color: Theme.dialogFill
        border.color: Theme.border
        radius: Theme.radius
    }

    function levelColor(level) {
        if (level === "ERRO" || level === "ERROR") return Theme.error
        if (level === "AVISO" || level === "WARNING") return Theme.warn
        if (level === "DEBUG") return Theme.textMuted
        return Theme.text
    }

    function load() {
        lines.clear()
        var recent = app.recentLog(1000)
        for (var i = 0; i < recent.length; ++i)
            append(recent[i])
        if (followTail)
            items.positionViewAtEnd()
    }

    // Lines coming from the log already carry "date [level] text".
    function append(line) {
        var level = ""
        var openAt = line.indexOf("[")
        var closeAt = line.indexOf("]")
        if (openAt > 0 && closeAt > openAt)
            level = line.substring(openAt + 1, closeAt)
        lines.append({ "message": line, "level": level })
        while (lines.count > 2000)
            lines.remove(0)
    }

    onOpened: load()

    ListModel { id: lines }

    Connections {
        target: app
        function onLogLine(level, text) {
            if (!dialog.visible)
                return
            // Rebuilds the line as it appears in the file.
            dialog.append(Qt.formatDateTime(new Date(), "yyyy-MM-dd hh:mm:ss.zzz")
                               + " [" + level + "] " + text)
            if (dialog.followTail)
                items.positionViewAtEnd()
        }
    }

    header: Rectangle {
        implicitHeight: Theme.dialogHeader
        color: "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogInner
            spacing: 12
            Text {
                text: qsTr("Log and diagnostics")
                color: Theme.text
                font.pixelSize: 15
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            StyledCheck {
                text: qsTr("Remote Play detail")
                checked: app.streamVerbose()
                onCheckedChanged: app.setStreamVerbose(checked)
            }
            StyledCheck {
                text: qsTr("Follow the tail")
                checked: dialog.followTail
                onCheckedChanged: dialog.followTail = checked
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
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
                text: qsTr("File: %1").arg(app.logFilePath())
                color: Theme.textMuted
                font.pixelSize: 10
                elide: Text.ElideMiddle
            }
            StyledButton {
                text: qsTr("Copy everything")
                implicitHeight: 30
                onClicked: app.copyDiagnosticsToClipboard()
            }
            StyledButton {
                text: qsTr("Save to…")
                implicitHeight: 30
                onClicked: diagnosticsDestination.open()
            }
            StyledButton {
                text: qsTr("Save to the desktop")
                implicitHeight: 30
                primary: true
                onClicked: {
                    var path = app.exportDiagnostics("")
                    if (path.length > 0)
                        app.openLocalFolder(path)
                }
            }
            StyledButton {
                text: qsTr("Close")
                implicitHeight: 30
                onClicked: dialog.close()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: Theme.dialogInner
            spacing: 8
            StyledField {
                Layout.fillWidth: true
                placeholderText: qsTr("filter (e.g. Remote Play, FAILED, FTP)")
                onTextChanged: dialog.filter = text
            }
            StyledButton {
                text: qsTr("Errors only")
                implicitHeight: 30
                onClicked: dialog.filter = "ERROR"
            }
            StyledButton {
                text: qsTr("Clear filter")
                implicitHeight: 30
                onClicked: dialog.filter = ""
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.bottomMargin: Theme.dialogInner
            color: "#0b0d12"
            border.color: Theme.border
            radius: 6

            ListView {
                id: items
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: lines
                spacing: 1
                ScrollBar.vertical: ScrollBar { }

                delegate: Text {
                    width: items.width
                    visible: dialog.filter.length === 0
                             || model.message.toLowerCase().indexOf(dialog.filter.toLowerCase()) >= 0
                    height: visible ? implicitHeight : 0
                    text: model.message
                    color: dialog.levelColor(model.level)
                    font.family: "monospace"
                    font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
                }
            }

            Text {
                anchors.centerIn: parent
                visible: lines.count === 0
                text: qsTr("Nothing logged yet.")
                color: Theme.textMuted
                font.pixelSize: 12
            }
        }
    }

    FolderDialog {
        id: diagnosticsDestination
        title: qsTr("Where to save the diagnostics")
        onAccepted: {
            var path = app.exportDiagnostics(selectedFolder)
            if (path.length > 0)
                app.openLocalFolder(path)
        }
    }
}
