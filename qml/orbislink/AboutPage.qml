// SPDX-License-Identifier: AGPL-3.0-or-later
//
// About OrbisLink: the app itself (the orb, its version and channel, where
// its source is), who made it — the team's mark and Claude, side by side —
// and the work it stands on, with each licence.
//
// The team's mark follows the appearance: ink on Daylight, ivory on
// Graphite. Claude is credited in words: Anthropic's logo is theirs to place.
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

    // ── The hero: the orb, the name, the version, where it lives. Not on a
    // pane: it is the page's title, and the one ornate pane below is kept for
    // the makers.
    Item {
        Layout.fillWidth: true
        implicitHeight: hero.implicitHeight + 8

        RowLayout {
            id: hero
            anchors.left: parent.left
            anchors.right: parent.right
            // Room on the right for the settings dialog's round close button.
            anchors.rightMargin: 40
            anchors.top: parent.top
            spacing: 26

            // The orb, floating in a soft halo of its own light.
            Item {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: 132
                Layout.preferredHeight: 132

                Canvas {
                    id: halo
                    anchors.centerIn: parent
                    width: 220
                    height: 220
                    // Repainted when the appearance changes: the halo is
                    // warmer and brighter on Graphite, softer on Daylight.
                    readonly property bool lit: Theme.light
                    onLitChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        var r = width / 2
                        var g = ctx.createRadialGradient(r, r, 0, r, r, r)
                        if (Theme.light) {
                            g.addColorStop(0.0, "rgba(255, 255, 255, 0.85)")
                            g.addColorStop(0.35, "rgba(255, 244, 226, 0.45)")
                            g.addColorStop(1.0, "rgba(255, 244, 226, 0)")
                        } else {
                            g.addColorStop(0.0, "rgba(216, 195, 160, 0.30)")
                            g.addColorStop(0.40, "rgba(216, 195, 160, 0.10)")
                            g.addColorStop(1.0, "rgba(216, 195, 160, 0)")
                        }
                        ctx.fillStyle = g
                        ctx.fillRect(0, 0, width, height)
                    }
                }
                Image {
                    anchors.centerIn: parent
                    width: 112
                    height: 112
                    source: "qrc:/icons/planet.png"
                    sourceSize.width: 224
                    sourceSize.height: 224
                    smooth: true
                    mipmap: true
                }
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
                    font.weight: Font.Bold
                    font.letterSpacing: -0.6
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
    // the page's one ornate pane.
    Plate {
        Layout.fillWidth: true
        Layout.topMargin: 4
        ornate: true
        implicitHeight: makersColumn.implicitHeight + 56
        // A breath of champagne in the glass.
        color: Theme.light ? Qt.rgba(1, 1, 1, 0.55) : Theme.alpha(Theme.brass, 0.05)

        ColumnLayout {
            id: makersColumn
            anchors.centerIn: parent
            spacing: 16

            // "Made by" between two short champagne hairlines that fade outwards.
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12
                Rectangle {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 1
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Theme.alpha(Theme.brass, 0) }
                        GradientStop { position: 1.0; color: Theme.alpha(Theme.brass, 0.7) }
                    }
                }
                Eyebrow { text: qsTr("Made by") }
                Rectangle {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 1
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Theme.alpha(Theme.brass, 0.7) }
                        GradientStop { position: 1.0; color: Theme.alpha(Theme.brass, 0) }
                    }
                }
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
                    font.pixelSize: 36
                    font.weight: Font.Light
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
                                font.pixelSize: 48
                                font.weight: Font.Bold
                                font.letterSpacing: -1
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
            font.weight: Font.Bold
            font.letterSpacing: -0.2
        }
    }

    // Each piece of work on its own small pane of glass, two to a row.
    GridLayout {
        id: thanksGrid
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 10
        rowSpacing: 10

        Repeater {
            model: [
                { name: "chiaki-ng", what: qsTr("Remote Play · AGPL-3.0") },
                { name: "Qt 6", what: qsTr("The interface · LGPL-3.0") },
                { name: "LibOrbisPkg (maxton)", what: qsTr("The fake package builder, ported to C++ · LGPL-3.0") },
                { name: "PS4 PKG Tool (pearlxcore)", what: qsTr("Package checks and organising · GPL-3.0") },
                { name: "easy-ps2-fpkg · PS Classics fPKG Builder", what: qsTr("How PS1/PS2 packages are made") },
                { name: "GoldHEN · etaHEN", what: qsTr("The consoles' homebrew enablers and their cheats") },
                { name: "homebrew.page · PKG-Zone", what: qsTr("The homebrew stores") },
                { name: "Figtree · JetBrains Mono", what: qsTr("Typefaces · SIL OFL 1.1") },
                { name: "Lucide", what: qsTr("Icons · ISC") },
                { name: "FFmpeg · SDL2 · libcurl · OpenSSL", what: qsTr("Video, controllers and the network") }
            ]
            delegate: Plate {
                id: credit
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                implicitHeight: creditColumn.implicitHeight + 28
                radius: Theme.radiusSmall + 2
                hoverable: true

                ColumnLayout {
                    id: creditColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: credit.modelData.name
                        color: Theme.ink
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

    // The licence, last, quietly centred.
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
