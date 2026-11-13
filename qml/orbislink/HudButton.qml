// SPDX-License-Identifier: AGPL-3.0-or-later
//
// An icon button of the glass pill floating over the picture: a small disc of
// clear glass that brightens under the mouse. The picture is always dark, so
// it is light whatever the theme.
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: button

    property string iconName: ""
    // A second icon next to the first, for a button that stands for two
    // things at once (the controller and the keyboard).
    property string secondIconName: ""
    property color secondTone: Theme.onStage
    property string tip: ""
    property color tone: Theme.onStage
    // Lit in red while something is live (the microphone capturing).
    property bool lit: false

    signal clicked()
    signal rightClicked()

    implicitWidth: secondIconName.length > 0 ? 62 : 36
    implicitHeight: 36
    radius: height / 2
    color: lit ? Theme.alpha(Theme.error, 0.85)
         : area.pressed ? Qt.rgba(1, 1, 1, 0.18)
         : area.containsMouse ? Qt.rgba(1, 1, 1, 0.12)
         : Qt.rgba(1, 1, 1, 0.06)
    border.width: 1
    border.color: area.containsMouse ? Qt.rgba(1, 1, 1, 0.34) : Theme.hudEdge
    Behavior on color { ColorAnimation { duration: Theme.fast } }
    scale: area.pressed ? Theme.pressScale : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }

    // The light on the disc, from the top.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: height / 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, button.lit ? 0.18 : 0.10) }
            GradientStop { position: 0.6; color: Qt.rgba(1, 1, 1, 0) }
        }
    }

    Row {
        anchors.centerIn: parent
        spacing: 8
        Icon {
            name: button.iconName
            size: 18
            color: button.lit ? "#FFFFFF" : button.tone
        }
        Icon {
            visible: button.secondIconName.length > 0
            name: button.secondIconName
            size: 18
            color: button.lit ? "#FFFFFF" : button.secondTone
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton)
                button.rightClicked()
            else
                button.clicked()
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: tip
    ToolTip.visible: area.containsMouse && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 300
}
