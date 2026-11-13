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

    // A large sheet of glass floating over the window: nearly opaque, lit
    // along its upper edge, with a sheen from the top and a soft shadow.
    background: Plate {
        level: "secondary"
        radius: Theme.radiusDialog
        color: Theme.dialogFill
        border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Theme.glassEdge
    }

    // It fades in and settles, without bouncing.
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
        NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Theme.normal; easing.type: Theme.easeOut }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast; easing.type: Theme.easeOut }
    }

    // On paper the state colours are too light to read as text: a shade darker.
    function levelColor(level) {
        if (level === "ERRO" || level === "ERROR")
            return Theme.light ? Qt.darker(Theme.error, 1.3) : Theme.error
        if (level === "AVISO" || level === "WARNING")
            return Theme.light ? Qt.darker(Theme.warn, 1.35) : Theme.warn
        if (level === "DEBUG") return Theme.textSecondary
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

    header: DialogHeader {
        eyebrow: qsTr("Diagnostics")
        title: qsTr("Log and diagnostics")
        dialog: dialog
    }

    footer: Item {
        implicitHeight: Theme.dialogFooter
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.dialogInner
            anchors.leftMargin: Theme.dialogMargin
            anchors.rightMargin: Theme.dialogMargin
            spacing: 8
            Icon {
                name: "file"
                size: 14
                color: Theme.textSecondary
            }
            Text {
                Layout.fillWidth: true
                text: app.logFilePath()
                color: Theme.textSecondary
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideMiddle
                ToolTip.visible: pathHover.hovered && truncated
                ToolTip.text: text
                HoverHandler { id: pathHover }
            }
            StyledButton {
                text: qsTr("Copy everything")
                iconName: "copy"
                onClicked: app.copyDiagnosticsToClipboard()
            }
            StyledButton {
                text: qsTr("Save to…")
                iconName: "folder"
                onClicked: diagnosticsDestination.open()
            }
            StyledButton {
                text: qsTr("Save to the desktop")
                iconName: "save"
                primary: true
                onClicked: {
                    var path = app.exportDiagnostics("")
                    if (path.length > 0)
                        app.openLocalFolder(path)
                }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        // The tools over the log: what to show on the left, how the log runs
        // on the right.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.topMargin: 4
            spacing: 8
            StyledField {
                id: filterField
                Layout.fillWidth: true
                Layout.maximumWidth: 360
                leftPadding: 38
                placeholderText: qsTr("filter (e.g. Remote Play, FAILED, FTP)")
                text: dialog.filter
                onTextChanged: dialog.filter = text
                Icon {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"
                    size: 15
                    color: Theme.textSecondary
                }
            }
            StyledButton {
                chip: true
                iconName: "alert"
                text: qsTr("Errors only")
                primary: dialog.filter === "ERROR"
                onClicked: dialog.filter = dialog.filter === "ERROR" ? "" : "ERROR"
            }
            StyledButton {
                chip: true
                text: qsTr("Clear filter")
                enabled: dialog.filter.length > 0
                onClicked: dialog.filter = ""
            }
            Item { Layout.fillWidth: true }
            StyledCheck {
                Layout.fillWidth: false
                text: qsTr("Remote Play detail")
                checked: app.streamVerbose()
                onCheckedChanged: app.setStreamVerbose(checked)
            }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 20; color: Theme.glassEdge }
            StyledCheck {
                Layout.fillWidth: false
                text: qsTr("Follow the tail")
                checked: dialog.followTail
                onCheckedChanged: dialog.followTail = checked
            }
        }

        // The log itself, in a well one step below the sheet, in the mono.
        Plate {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.dialogMargin
            Layout.rightMargin: Theme.dialogMargin
            Layout.bottomMargin: 4
            sunken: true
            radius: Theme.radius

            ListView {
                id: items
                anchors.fill: parent
                anchors.margins: 16
                anchors.rightMargin: 6
                clip: true
                model: lines
                spacing: 2
                ScrollBar.vertical: ScrollBar { }

                delegate: Text {
                    width: items.width - 10
                    visible: dialog.filter.length === 0
                             || model.message.toLowerCase().indexOf(dialog.filter.toLowerCase()) >= 0
                    height: visible ? implicitHeight : 0
                    text: model.message
                    color: dialog.levelColor(model.level)
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontCaption
                    lineHeight: 1.1
                    wrapMode: Text.WrapAnywhere
                }
            }

            // Empty: a faint icon and one line.
            Column {
                anchors.centerIn: parent
                visible: lines.count === 0
                spacing: 10
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "file"
                    size: 22
                    color: Theme.alpha(Theme.textSecondary, 0.6)
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Nothing logged yet.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                }
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
