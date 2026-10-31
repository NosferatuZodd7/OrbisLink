// SPDX-License-Identifier: AGPL-3.0-or-later
//
// About OrbisLink: the app's plate (its emblem, version and channel, where
// its source is), who made it — the team's mark and Claude, side by side —
// and the work it stands on, with each licence.
//
// The team's mark follows the theme: indigo ink on paper, ivory at night.
// Claude is credited in words: Anthropic's logo is theirs to place.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: about

    // Asks the settings dialog to look for a new version now.
    signal checkUpdates()

    Layout.fillWidth: true
    spacing: 16

    readonly property var settings: app.settingsMap()
    readonly property string repository: settings.updateRepository !== undefined ? settings.updateRepository : ""
    readonly property string repositoryUrl: repository.length > 0 ? "https://github.com/" + repository : ""
    readonly property bool testing: settings.updateChannel === "testing"

    // ── The hero: emblem, name, version, where it lives. Not on a plate: it
    // is the page's title, and the plate below is kept for the makers.
    Item {
        Layout.fillWidth: true
        implicitHeight: hero.implicitHeight + 8

        // The planet, faint, behind the right side of the hero.
        Image {
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            width: 230
            height: 230
            source: "qrc:/icons/planet.png"
            sourceSize.width: 512
            sourceSize.height: 512
            opacity: Theme.light ? 0.14 : 0.10
            smooth: true
            mipmap: true
        }

        RowLayout {
            id: hero
            anchors.left: parent.left
            anchors.right: parent.right
            // Room on the right for the settings dialog's round ✕.
            anchors.rightMargin: 40
            anchors.top: parent.top
            spacing: 26

            Image {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 6
                Layout.preferredWidth: 108
                Layout.preferredHeight: 108
                source: "qrc:/icons/mark.png"
                sourceSize.width: 216
                sourceSize.height: 216
                smooth: true
                mipmap: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Eyebrow { text: qsTr("About") }
                Text {
                    text: "OrbisLink"
                    color: Theme.ink
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.fontHero
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Remote Play, packages, files, saves and homebrew for the PS4 and the PS5, "
                               + "from one window.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    spacing: 6
                    Tag { text: "v" + app.version; mono: true; tone: "accent" }
                    Tag {
                        text: about.testing ? qsTr("Testing channel") : qsTr("Stable channel")
                        tone: about.testing ? "warn" : "neutral"
                    }
                    Tag { text: "AGPL-3.0-or-later"; mono: true }
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    spacing: 8
                    StyledButton {
                        primary: true
                        iconName: app.updateState === "checking" ? "loader" : "refresh"
                        text: qsTr("Check for updates")
                        enabled: app.updateState !== "checking" && app.updateState !== "downloading"
                        onClicked: about.checkUpdates()
                    }
                    StyledButton {
                        visible: about.repositoryUrl.length > 0
                        iconName: "external-link"
                        text: qsTr("Source code")
                        ToolTip.visible: hovered
                        ToolTip.text: about.repositoryUrl
                        onClicked: Qt.openUrlExternally(about.repositoryUrl)
                    }
                    StyledButton {
                        visible: about.repositoryUrl.length > 0
                        iconName: "download"
                        text: qsTr("Releases")
                        onClicked: Qt.openUrlExternally(about.repositoryUrl + "/releases")
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: app.updateMessage.length > 0
                    wrapMode: Text.WordWrap
                    text: app.updateMessage
                    color: app.updateState === "error" ? (Theme.light ? Qt.darker(Theme.error, 1.3) : Theme.error)
                         : app.updateState === "available" ? (Theme.light ? Qt.darker(Theme.ok, 1.3) : Theme.ok)
                         : Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                }
            }
        }
    }

    EngravedRule { Layout.fillWidth: true; Layout.topMargin: 4 }

    // ── Who made it: the team's mark and Claude, side by side, centred on
    // the page's one ornate plate.
    Plate {
        Layout.fillWidth: true
        Layout.topMargin: 4
        ornate: true
        implicitHeight: makersColumn.implicitHeight + 56
        color: Theme.light ? Qt.tint(Theme.panelFill, Theme.alpha(Theme.henFill, 0.06))
                           : Qt.tint(Theme.panelFill, Theme.alpha(Theme.henFill, 0.05))

        ColumnLayout {
            id: makersColumn
            anchors.centerIn: parent
            spacing: 16

            // "Made by" between two short brass rules, as a plate's caption.
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12
                Rectangle { Layout.preferredWidth: 40; Layout.preferredHeight: 1; color: Theme.alpha(Theme.brass, 0.6) }
                Sparkle { size: 8 }
                Eyebrow { text: qsTr("Made by") }
                Sparkle { size: 8 }
                Rectangle { Layout.preferredWidth: 40; Layout.preferredHeight: 1; color: Theme.alpha(Theme.brass, 0.6) }
            }

            RowLayout {
                id: makers
                Layout.alignment: Qt.AlignHCenter
                spacing: 34

                // The team.
                ColumnLayout {
                    spacing: 10
                    Image {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 120
                        fillMode: Image.PreserveAspectFit
                        source: Theme.light ? "qrc:/icons/team-ink.png" : "qrc:/icons/team-ivory.png"
                        sourceSize.width: 300
                        smooth: true
                        mipmap: true
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Creator · design and direction")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.bottomMargin: 22
                    text: "×"
                    color: Theme.brass
                    font.family: Theme.fontDisplay
                    font.pixelSize: 44
                }

                // Claude, in words: Anthropic's logo is theirs to place.
                ColumnLayout {
                    spacing: 10
                    Item {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 120
                        Column {
                            anchors.centerIn: parent
                            spacing: 2
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "Claude"
                                color: Theme.ink
                                font.family: Theme.fontDisplay
                                font.pixelSize: 50
                                font.weight: Font.DemiBold
                            }
                            Eyebrow {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "Anthropic"
                                color: Theme.textSecondary
                            }
                        }
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Code, written together with AI")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }
        }
    }

    // ── The work it stands on.
    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: 6
        spacing: 2
        Eyebrow { text: qsTr("Credits") }
        Text {
            text: qsTr("With thanks to")
            color: Theme.ink
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontHeading
            font.weight: Font.DemiBold
        }
    }

    Plate {
        Layout.fillWidth: true
        implicitHeight: thanksGrid.implicitHeight + 40

        GridLayout {
            id: thanksGrid
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 20
            columns: 2
            columnSpacing: 28
            rowSpacing: 14

            Repeater {
                model: [
                    { name: "chiaki-ng", what: qsTr("Remote Play · AGPL-3.0") },
                    { name: "Qt 6", what: qsTr("The interface · LGPL-3.0") },
                    { name: "LibOrbisPkg (maxton)", what: qsTr("The fake package builder, ported to C++ · LGPL-3.0") },
                    { name: "PS4 PKG Tool (pearlxcore)", what: qsTr("Package checks and organising · GPL-3.0") },
                    { name: "easy-ps2-fpkg · PS Classics fPKG Builder", what: qsTr("How PS1/PS2 packages are made") },
                    { name: "GoldHEN · etaHEN", what: qsTr("The consoles' homebrew enablers and their cheats") },
                    { name: "homebrew.page · PKG-Zone", what: qsTr("The homebrew stores") },
                    { name: "Figtree · Fraunces · JetBrains Mono", what: qsTr("Typefaces · SIL OFL 1.1") },
                    { name: "Lucide", what: qsTr("Icons · ISC") },
                    { name: "FFmpeg · SDL2 · libcurl · OpenSSL", what: qsTr("Video, controllers and the network") }
                ]
                delegate: RowLayout {
                    id: credit
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    spacing: 10
                    Sparkle {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 4
                        size: 8
                        color: Theme.alpha(Theme.brass, 0.8)
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: credit.modelData.name
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: credit.modelData.what
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }
            }
        }
    }

    // The licence, last, as the colophon of a book.
    Text {
        Layout.fillWidth: true
        Layout.topMargin: 4
        Layout.leftMargin: 4
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        text: qsTr("OrbisLink is free software: you can share and change it under the GNU Affero General "
                   + "Public License, version 3 or later. No Sony firmware, emulator or game is part of it.")
        color: Theme.textSecondary
        font.pixelSize: 12
    }
}
