// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The head of every page: an eyebrow in champagne, the title, a line of
// what it is for, and the page's own actions on the right — then a fading
// hairline. Children go on the right, in the order given.
//
//   PageHeader {
//       eyebrow: qsTr("Library"); title: qsTr("Games")
//       subtitle: qsTr("Every game on the console")
//       StyledButton { text: qsTr("Refresh") }
//   }
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: header

    property string eyebrow: ""
    property string title: ""
    property string subtitle: ""
    // An icon from Icons.js in a round seal before the title (optional).
    property string iconName: ""
    // The engraved rule under it; off where the page draws its own.
    property bool rule: true
    default property alias actions: actionRow.data

    Layout.fillWidth: true
    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        spacing: 16

        // The page's icon in a small disc of glass.
        Rectangle {
            visible: header.iconName.length > 0
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: 48
            implicitHeight: 48
            radius: 24
            color: Theme.controlFill
            border.width: 1
            border.color: Theme.light ? Qt.rgba(1, 1, 1, 0.9) : Theme.glassEdge
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: width / 2
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.5 : 0.10) }
                    GradientStop { position: 0.6; color: Qt.rgba(1, 1, 1, 0) }
                }
            }
            Icon {
                anchors.centerIn: parent
                name: header.iconName
                size: 21
                color: Theme.ink
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Eyebrow {
                Layout.fillWidth: true
                visible: header.eyebrow.length > 0
                text: header.eyebrow
            }
            Text {
                Layout.fillWidth: true
                text: header.title
                color: Theme.ink
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
                font.letterSpacing: -0.4
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 2
                visible: header.subtitle.length > 0
                text: header.subtitle
                color: Theme.textSecondary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }

        RowLayout {
            id: actionRow
            Layout.alignment: Qt.AlignVCenter
            spacing: 8
        }
    }

    EngravedRule {
        visible: header.rule
        Layout.fillWidth: true
        Layout.topMargin: 14
    }
}
