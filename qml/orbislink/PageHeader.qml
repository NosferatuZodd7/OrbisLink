// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The head of every page: an eyebrow in brass, the title in the serif, a line
// of what it is for, and the page's own actions on the right — then an
// engraved rule. Children go on the right, in the order given.
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

        // The seal: the page's icon inside a ring, like a plate's stamp.
        Rectangle {
            visible: header.iconName.length > 0
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: 52
            implicitHeight: 52
            radius: 26
            color: Theme.alpha(Theme.ink, Theme.light ? 0.06 : 0.07)
            border.width: 1
            border.color: Theme.alpha(Theme.brass, 0.55)
            Rectangle {
                anchors.fill: parent
                anchors.margins: 4
                radius: width / 2
                color: "transparent"
                border.width: 1
                border.color: Theme.alpha(Theme.brass, 0.22)
            }
            Icon {
                anchors.centerIn: parent
                name: header.iconName
                size: 22
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
                font.weight: Font.DemiBold
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
