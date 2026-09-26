// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root
    property string label: ""
    property string state_: "unknown"
    property string hint: ""

    implicitWidth: row.implicitWidth + 26
    implicitHeight: 34

    scale: hoverHandler.hovered ? 1.03 : 1.0
    Behavior on scale {
        NumberAnimation { duration: Theme.fast; easing.type: Theme.easeSpring; easing.overshoot: 1.1 }
    }

    // Cápsula de vidro, com um halo da cor do estado quando há problema.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -4
        radius: (parent.height + 8) / 2
        visible: root.state_ === "unavailable"
        color: "transparent"
        border.width: 4
        border.color: Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.16)
    }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b,
                       hoverHandler.hovered ? Theme.panelOpacity + 0.15 : Theme.panelOpacity)
        border.color: Theme.glassEdge
        border.width: 1
        Behavior on color { ColorAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.glassHighlight) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.05) }
            }
        }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8

        Rectangle {
            width: 8; height: 8; radius: 4
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.stateColor(root.state_)
            // Um ponto aceso tem halo; é o que o distingue de um pixel.
            Rectangle {
                anchors.centerIn: parent
                width: 16; height: 16; radius: 8
                color: "transparent"
                border.width: 4
                border.color: Qt.rgba(parent.color.r, parent.color.g, parent.color.b, 0.22)
            }

            SequentialAnimation on opacity {
                running: root.state_ === "checking"
                loops: Animation.Infinite
                NumberAnimation { to: 0.3; duration: 600 }
                NumberAnimation { to: 1.0; duration: 600 }
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: Theme.text
            font.pixelSize: 12
            font.weight: Font.Medium
            font.letterSpacing: -0.2
        }
    }

    ToolTip.visible: hoverHandler.hovered && root.hint.length > 0
    ToolTip.text: root.hint
    ToolTip.delay: 200
    HoverHandler { id: hoverHandler }
}
