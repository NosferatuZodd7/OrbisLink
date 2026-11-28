// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The console a page works with (the one in use) and a menu to choose
// another of the saved ones: which answer on FTP, which is a PS4 or a PS5,
// and which one a Remote Play session is on. Choosing one makes it the
// console in use — FTP, the installer and the pages move to it — while a
// Remote Play session stays on its own console.
//
// Two looks: a chip (a page's actions) or a heading (the transfers panel).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic

Item {
    id: root

    // A bold name with a chevron instead of a chip.
    property bool heading: false
    property int headingSize: 17
    // Why another console cannot be chosen now ("" when it can), asked as
    // the menu opens.
    property string block: ""

    readonly property bool several: app.consoles.length > 1
    readonly property string sessionAddress: typeof stream !== "undefined" && stream !== null
                                             ? stream.sessionAddress : ""

    implicitWidth: heading ? headingRow.implicitWidth : chip.implicitWidth
    implicitHeight: heading ? headingRow.implicitHeight : chip.implicitHeight

    function typeLabel(c) {
        return c.type === "ps5" ? "PS5" : c.type === "ps4" ? "PS4" : ""
    }
    // What its FTP answered: true, false, or undefined while not known.
    function ftpOf(c) {
        if (c.active)
            return app.ftpState === "available" ? true : app.ftpState === "unavailable" ? false : undefined
        return app.ftpReachable[c.address]
    }
    function open() {
        if (!several)
            return
        block = app.consoleSwitchBlock()
        // Who answers now: the menu fills in as the answers come.
        var all = []
        for (var i = 0; i < app.consoles.length; ++i)
            if (!app.consoles[i].active)
                all.push(app.consoles[i].address)
        app.probeFtp(all)
        menu.popup(root, 0, root.height + 4)
    }

    readonly property string tip: several
        ? qsTr("The console these pages work with. Click for another one on the network; a Remote Play session stays on its own.")
        : qsTr("The console these pages work with.")

    StyledButton {
        id: chip
        visible: !root.heading
        chip: true
        iconName: "gamepad"
        text: app.consoleName + (root.several ? "  ▾" : "")
        width: Math.min(implicitWidth, 240)
        ToolTip.visible: hovered && !menu.visible
        ToolTip.text: root.tip
        onClicked: root.open()
    }

    Row {
        id: headingRow
        visible: root.heading
        spacing: 6
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, root.width - 22)
            text: app.consoleName
            color: Theme.ink
            font.pixelSize: root.headingSize
            font.weight: Font.Bold
            elide: Text.ElideRight
        }
        Icon {
            visible: root.several
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-down"
            size: 14
            color: headingArea.containsMouse ? Theme.ink : Theme.textSecondary
        }
    }
    MouseArea {
        id: headingArea
        visible: root.heading
        anchors.fill: headingRow
        hoverEnabled: true
        cursorShape: root.several ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.open()
    }
    ToolTip.visible: root.heading && headingArea.containsMouse && !menu.visible
    ToolTip.delay: 500
    ToolTip.text: root.tip

    Menu {
        id: menu
        topPadding: 8
        bottomPadding: 8
        background: ShelfTile.MenuGlass { implicitWidth: root.block.length > 0 ? 400 : 360 }
        Instantiator {
            model: app.consoles
            delegate: StyledMenuItem {
                required property var modelData
                readonly property var ftp: root.ftpOf(modelData)
                text: modelData.name
                      + (root.typeLabel(modelData).length > 0 ? "  ·  " + root.typeLabel(modelData) : "")
                      + (ftp === true ? "  ·  FTP" : ftp === false ? "  ·  " + qsTr("no FTP") : "")
                      + (modelData.address === root.sessionAddress ? "  ·  " + qsTr("Remote Play") : "")
                iconName: modelData.active ? "check" : modelData.type === "ps5" ? "monitor" : "gamepad"
                enabled: modelData.active || root.block.length === 0
                // Closed first: choosing rebuilds this list under the menu.
                onTriggered: {
                    menu.close()
                    if (!modelData.active)
                        app.selectConsole(modelData.address)
                }
            }
            onObjectAdded: (index, object) => menu.insertItem(index, object)
            onObjectRemoved: (index, object) => menu.removeItem(object)
        }
        MenuSeparator {
            visible: root.block.length > 0
            height: visible ? implicitHeight : 0
        }
        // Why the others are greyed out.
        StyledMenuItem {
            visible: root.block.length > 0
            height: visible ? implicitHeight : 0
            enabled: false
            text: root.block
        }
    }
}
