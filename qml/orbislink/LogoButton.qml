// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The emblem, as the way home: a click opens the consoles page (or, during
// a session, closes whatever page is over the picture).
//
// It is alive but quiet: a soft halo breathes behind it, it rises and its
// halo brightens under the mouse, and it gives a little when pressed. The
// sheen across the orb turns slowly while the mouse is over it, like light
// on glass that is being looked at.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: logo

    property real size: 40
    signal clicked()

    implicitWidth: size
    implicitHeight: size
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Home")

    // The halo: breathes slowly; brighter under the mouse.
    Rectangle {
        id: halo
        anchors.centerIn: orb
        width: logo.size * 1.5
        height: width
        radius: width / 2
        color: Qt.rgba(1, 0.25, 0.65, mouse.containsMouse ? 0.22 : 0.10)
        Behavior on color { ColorAnimation { duration: Theme.normal } }
        SequentialAnimation on scale {
            loops: Animation.Infinite
            running: logo.visible
            NumberAnimation { from: 0.86; to: 1.0; duration: 2400; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1.0; to: 0.86; duration: 2400; easing.type: Easing.InOutSine }
        }
    }

    Image {
        id: orb
        anchors.centerIn: parent
        width: logo.size
        height: logo.size
        source: "qrc:/icons/mark.png"
        sourceSize.width: logo.size * 2
        sourceSize.height: logo.size * 2
        smooth: true
        mipmap: true
        scale: mouse.pressed ? 0.92 : mouse.containsMouse ? 1.08 : 1.0
        y: mouse.containsMouse ? -1 : 0
        Behavior on scale { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }

        // A highlight that turns around the orb while it is looked at.
        Item {
            anchors.fill: parent
            opacity: mouse.containsMouse ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.normal } }
            RotationAnimation on rotation {
                from: 0; to: 360; duration: 6000
                loops: Animation.Infinite
                running: mouse.containsMouse
            }
            Rectangle {
                x: parent.width * 0.18
                y: parent.height * 0.10
                width: parent.width * 0.30
                height: parent.height * 0.16
                radius: height / 2
                rotation: -30
                color: Qt.rgba(1, 1, 1, 0.35)
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: logo.clicked()
    }
    ToolTip.visible: mouse.containsMouse
    ToolTip.delay: 500
    ToolTip.text: qsTr("Home")
}
