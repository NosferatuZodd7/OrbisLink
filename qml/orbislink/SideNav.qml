// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The navigation: a pane of glass floating on the left. From the top, the
// emblem (the way home); the pages; the transfers and the log; and at the
// foot the appearances and the settings. The console in use is in the
// toolbar, not here.
//
// Folded (a narrow window, or by choice) it keeps only the icons, each with
// its name on hover. Nothing is lost: every page and tool is one click away
// in both.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Plate {
    id: nav
    level: "primary"

    property bool compact: false
    // The queue's count and whether the transfers' panel is open (Main.qml).
    property int transfers: 0
    property bool transfersOpen: false
    property bool transfersAllowed: true

    signal foldToggled()
    signal transfersToggled()
    signal settingsRequested()
    signal logRequested()
    signal customThemeRequested()

    readonly property bool live: typeof stream !== "undefined" && stream !== null && stream.streaming
    // The page lit: the one open over the picture, or in the main area.
    readonly property string current: window.overSection.length > 0 ? window.overSection : window.view

    // One page or tool. Lit: a pill of brighter glass behind it, as a lens
    // over what is chosen.
    component NavItem: Item {
        id: item
        property string iconName: ""
        property string text: ""
        property string hint: ""
        property bool selected: false
        property int badge: 0
        property bool pulse: false
        signal clicked()

        Layout.fillWidth: true
        implicitHeight: 40
        Accessible.role: Accessible.Button
        Accessible.name: text

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: item.selected ? (Theme.light ? Qt.rgba(1, 1, 1, 0.92) : Qt.rgba(1, 1, 1, 0.14))
                 : mouse.containsMouse ? (Theme.light ? Qt.rgba(1, 1, 1, 0.50) : Qt.rgba(1, 1, 1, 0.06)) : "transparent"
            border.width: item.selected ? 1 : 0
            border.color: Theme.light ? Qt.rgba(1, 1, 1, 1) : Qt.rgba(1, 1, 1, Theme.glassBorder + 0.06)
            Behavior on color { ColorAnimation { duration: Theme.fast } }
            // The sheen on the lit pill.
            Rectangle {
                visible: item.selected
                anchors.fill: parent
                anchors.margins: 1
                radius: height / 2
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.6 : 0.12) }
                    GradientStop { position: 0.6; color: Qt.rgba(1, 1, 1, 0) }
                }
            }
        }

        Icon {
            id: glyph
            x: nav.compact ? (parent.width - width) / 2 : 14
            anchors.verticalCenter: parent.verticalCenter
            name: item.iconName
            size: 18
            color: item.selected ? Theme.ink : mouse.containsMouse ? Theme.text : Theme.textSecondary
        }
        Text {
            visible: !nav.compact
            anchors.left: glyph.right
            anchors.leftMargin: 12
            anchors.right: marks.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: item.text
            color: item.selected ? Theme.ink : Theme.text
            font.pixelSize: 14
            font.weight: item.selected ? Font.DemiBold : Font.Medium
            elide: Text.ElideRight
        }
        Row {
            id: marks
            visible: !nav.compact
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Rectangle {
                visible: item.badge > 0
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(20, count.implicitWidth + 10)
                height: 18
                radius: 9
                color: Theme.light ? "#1E1D1B" : Qt.rgba(1, 1, 1, 0.9)
                Text {
                    id: count
                    anchors.centerIn: parent
                    text: item.badge
                    color: Theme.light ? "#FFFFFF" : "#16161A"
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
            }
        }
        // Folded: the count at the icon's corner; a live dot for Remote Play.
        Rectangle {
            visible: nav.compact && (item.badge > 0 || item.pulse)
            x: glyph.x + glyph.width - 4
            y: glyph.y - 5
            width: item.badge > 0 ? Math.max(16, foldCount.implicitWidth + 8) : 8
            height: item.badge > 0 ? 16 : 8
            radius: height / 2
            color: item.pulse ? Theme.ok : Theme.accent
            Text {
                id: foldCount
                visible: item.badge > 0
                anchors.centerIn: parent
                text: item.badge
                color: Theme.textOnAccent
                font.pixelSize: 10
                font.weight: Font.Bold
            }
        }
        Rectangle {
            visible: !nav.compact && item.pulse
            anchors.right: parent.right
            anchors.rightMargin: item.selected ? 28 : 12
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: Theme.ok
            SequentialAnimation on opacity {
                running: item.pulse && item.visible
                loops: Animation.Infinite
                NumberAnimation { to: 0.35; duration: 900; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutSine }
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: item.clicked()
        }
        ToolTip.visible: mouse.containsMouse && (nav.compact || item.hint.length > 0)
        ToolTip.delay: nav.compact ? 150 : 700
        ToolTip.text: nav.compact ? item.text + (item.hint.length > 0 ? " — " + item.hint : "") : item.hint
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 18
        anchors.bottomMargin: 14
        anchors.leftMargin: nav.compact ? 12 : 14
        anchors.rightMargin: nav.compact ? 12 : 14
        spacing: 0

        // ── The emblem: the way home.
        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 22
            Layout.leftMargin: nav.compact ? 0 : 4
            spacing: 12
            LogoButton {
                Layout.alignment: nav.compact ? Qt.AlignHCenter : Qt.AlignLeft
                size: 40
                onClicked: window.showHome()
            }
            Text {
                visible: !nav.compact
                Layout.fillWidth: true
                text: "OrbisLink"
                color: Theme.ink
                font.pixelSize: 20
                font.weight: Font.Bold
                font.letterSpacing: -0.3
                elide: Text.ElideRight
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: middle.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height

            ColumnLayout {
                id: middle
                width: parent.width
                spacing: 4

                // ── The pages.
                NavItem {
                    iconName: nav.live ? "play" : "home"
                    text: nav.live ? qsTr("Remote Play") : qsTr("Consoles")
                    hint: nav.live ? qsTr("The session is still running") : qsTr("Every console, and Remote Play")
                    selected: nav.current === "home"
                    pulse: nav.live
                    onClicked: window.showHome()
                }
                NavItem {
                    iconName: "disc"
                    text: qsTr("Games")
                    hint: games.available ? qsTr("Every game and app in the console's library, and the PS1/PS2 converter")
                                          : qsTr("Every game and app in the console's library")
                    selected: nav.current === "games"
                    onClicked: window.goTo("games")
                }
                NavItem {
                    visible: typeof store !== "undefined" && store !== null
                    iconName: "store"
                    text: qsTr("Homebrew store")
                    hint: qsTr("Find PS5 homebrew apps and install them on the console")
                    selected: nav.current === "store"
                    onClicked: window.goTo("store")
                }
                NavItem {
                    iconName: "archive"
                    text: qsTr("Save vault")
                    hint: qsTr("Back up the console's saves on this PC")
                    selected: nav.current === "saves"
                    onClicked: window.goTo("saves")
                }
                NavItem {
                    iconName: "zap"
                    text: qsTr("Payloads")
                    hint: qsTr("See and manage the console's payloads and plugins, and run one now")
                    selected: nav.current === "payloads"
                    onClicked: window.goTo("payloads")
                }

                // ── The transfers.
                EngravedRule {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    Layout.bottomMargin: 6
                }
                NavItem {
                    iconName: "panel"
                    text: qsTr("Transfers")
                    hint: nav.transfersAllowed ? qsTr("The queue and the console's files (F9)") : window.panelLockReason
                    selected: nav.transfersOpen
                    badge: nav.transfers
                    opacity: nav.transfersAllowed ? 1.0 : 0.5
                    onClicked: nav.transfersToggled()
                }
                NavItem {
                    iconName: "log"
                    text: qsTr("Log and diagnostics")
                    hint: qsTr("Ctrl+L")
                    onClicked: nav.logRequested()
                }
            }
        }

        // ── The foot: themes, settings, folding.
        ThemeSwitcher {
            Layout.fillWidth: !nav.compact
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 10
            Layout.bottomMargin: 8
            vertical: nav.compact
            onCustomRequested: nav.customThemeRequested()
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            NavItem {
                visible: !nav.compact
                iconName: "settings"
                text: qsTr("Settings")
                hint: qsTr("Ctrl+,")
                onClicked: nav.settingsRequested()
            }
            StyledToolButton {
                visible: nav.compact
                Layout.alignment: Qt.AlignHCenter
                iconName: "settings"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Settings") + " — " + qsTr("Ctrl+,")
                onClicked: nav.settingsRequested()
            }
            StyledToolButton {
                visible: !nav.compact
                iconName: "chevron-left"
                iconSize: 16
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Fold the navigation")
                onClicked: nav.foldToggled()
            }
        }
        StyledToolButton {
            visible: nav.compact
            Layout.alignment: Qt.AlignHCenter
            iconName: "chevron-right"
            iconSize: 16
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Open the navigation")
            onClicked: nav.foldToggled()
        }
    }
}
