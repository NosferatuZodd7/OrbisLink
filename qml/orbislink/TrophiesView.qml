// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The trophy vault: a console user's trophies, laid out as a console's
// trophy screen — the games in a list, then one game: its summary on top,
// its trophies in a list with the one picked lit, and that trophy large on
// the right (keys: ↑ ↓, Enter, Esc, R, Shift). In the app's own theme. Read from
// the console (TrophiesController); nothing is written there.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

FocusScope {
    id: root

    signal back()
    // In the main area (framed) the page sits on the sheet with its margins;
    // over Remote Play the overlay is the frame.
    property bool framed: true

    function gradeColor(grade) {
        switch (grade) {
        case "platinum": return "#75CFFF"
        case "gold": return "#E8B64D"
        case "silver": return "#BEC7D8"
        }
        return "#C77B50"
    }
    function gradeName(grade) {
        switch (grade) {
        case "platinum": return qsTr("Platinum")
        case "gold": return qsTr("Gold")
        case "silver": return qsTr("Silver")
        }
        return qsTr("Bronze")
    }

    // The light of the one picked, neutral in every theme: white on dark,
    // ink on light.
    readonly property color hi: Theme.light ? "#000000" : "#FFFFFF"
    readonly property color pickedFill: Theme.alpha(hi, Theme.light ? 0.05 : 0.07)
    readonly property color pickedEdge: Theme.alpha(hi, Theme.light ? 0.45 : 0.60)

    // ── What is shown
    // "games" (the list) or "game" (one game's trophies); in the list,
    // every game or only the ones copied to this PC.
    property string level: "games"
    property bool onlyCopied: false
    property var game: ({})
    property int gameIndex: 0
    property int trophyIndex: 0
    // "default", "rarity", "grade", "date"; "all", "earned", "unearned".
    property string sort: "default"
    property string filter: "all"
    readonly property bool hasRarity: allTrophies.some(function (t) { return t.rarity >= 0 })
    readonly property var sorts: hasRarity ? ["default", "rarity", "grade", "date"] : ["default", "grade", "date"]
    function sortLabel(s) {
        switch (s) {
        case "rarity": return qsTr("Rarity")
        case "grade": return qsTr("Type")
        case "date": return qsTr("Date earned")
        }
        return qsTr("Default")
    }
    function rarityName(r) {
        if (r < 0) return ""
        return r < 5 ? qsTr("Ultra Rare") : r < 15 ? qsTr("Very Rare") : r < 50 ? qsTr("Rare") : qsTr("Common")
    }

    readonly property int copiedCount: trophies.titles.filter(function (g) { return g.backedUp }).length
    readonly property var games: onlyCopied ? trophies.titles.filter(function (g) { return g.backedUp })
                                            : trophies.titles
    readonly property var allTrophies: game.id !== undefined ? trophies.trophiesOf(game.id) : []
    readonly property var shown: {
        var order = { platinum: 0, gold: 1, silver: 2, bronze: 3 }
        var list = allTrophies.filter(function (t) {
            return filter === "all" || (filter === "earned") === t.unlocked
        })
        if (sort === "grade")
            list.sort(function (a, b) { return order[a.grade] - order[b.grade] || a.id - b.id })
        else if (sort === "date")
            list.sort(function (a, b) { return (b.when || 0) - (a.when || 0) || a.id - b.id })
        else if (sort === "rarity")
            list.sort(function (a, b) {
                return (a.rarity < 0 ? 999 : a.rarity) - (b.rarity < 0 ? 999 : b.rarity) || a.id - b.id
            })
        return list
    }
    readonly property var current: trophyIndex >= 0 && trophyIndex < shown.length ? shown[trophyIndex] : null
    function artOf(t) {
        return t && game.npId ? (trophies.art[game.npId + "/" + t.id] || "") : ""
    }
    function concealed(t) { return t && t.hidden && !t.unlocked }
    readonly property var me: {
        for (var i = 0; i < trophies.users.length; ++i)
            if (trophies.users[i].folder === trophies.user)
                return trophies.users[i]
        return ({ folder: "", name: "" })
    }

    function openGame(i) {
        if (i < 0 || i >= games.length)
            return
        gameIndex = i
        game = games[i]
        trophyIndex = 0
        level = "game"
        trophies.loadArt(game.id)
        list.positionViewAtBeginning()
    }
    function goBack() {
        if (compareDialog.opened) { compareDialog.close(); return }
        if (bigDialog.opened) { bigDialog.close(); return }
        if (level === "game") { level = "games"; return }
        root.back()
    }
    function move(step) {
        if (level === "game") {
            trophyIndex = Math.max(0, Math.min(shown.length - 1, trophyIndex + step))
            list.positionViewAtIndex(trophyIndex, ListView.Contain)
        } else {
            gameIndex = Math.max(0, Math.min(games.length - 1, gameIndex + step))
            gamesList.positionViewAtIndex(gameIndex, ListView.Contain)
        }
    }

    onSortChanged: trophyIndex = 0
    onFilterChanged: trophyIndex = 0
    onOnlyCopiedChanged: gameIndex = 0
    onVisibleChanged: {
        if (!visible)
            return
        forceActiveFocus()
        // Opened again: one more read by itself, if nothing is read yet.
        autoReadFor = ""
        readWhenReady()
    }
    // Read by itself once per console: when the page opens, and when another
    // console is chosen while it is open (as soon as that one's FTP answers).
    // A read that fails is not tried again by itself (the button is there).
    property string autoReadFor: ""
    readonly property bool canRead: visible && !trophies.scanned && !trophies.busy && app.canUseFtp && !trophies.ps5
    onCanReadChanged: readWhenReady()
    readonly property string consoleNow: app.consoleAddress
    onConsoleNowChanged: {
        level = "games"
        gameIndex = 0
        autoReadFor = ""
        Qt.callLater(readWhenReady)
    }
    function readWhenReady() {
        if (!canRead || autoReadFor === app.consoleAddress)
            return
        autoReadFor = app.consoleAddress
        trophies.refresh()
    }
    Connections {
        target: trophies
        function onFinished(message, error) { toast.show(message, error) }
        function onTitlesChanged() {
            // The game shown, read again.
            if (root.level !== "game")
                return
            for (var i = 0; i < trophies.titles.length; ++i)
                if (trophies.titles[i].npId === root.game.npId) {
                    root.game = trophies.titles[i]
                    return
                }
            root.level = "games"
        }
    }

    Keys.onUpPressed: move(-1)
    Keys.onDownPressed: move(1)
    Keys.onReturnPressed: if (level === "games") openGame(gameIndex)
    Keys.onEnterPressed: if (level === "games") openGame(gameIndex)
    Keys.onEscapePressed: goBack()
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_R && level === "game" && current) {
            bigDialog.open()
            event.accepted = true
        } else if (event.key === Qt.Key_Shift && level === "game") {
            sort = sorts[(sorts.indexOf(sort) + 1) % sorts.length]
            event.accepted = true
        }
    }

    // ── The page's pieces
    // The light around the row picked, in the theme's accent.
    component PickedGlow: Item {
        id: glow
        property bool on: false
        property real radius: 0
        anchors.fill: parent
        opacity: on ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.normal } }
        Repeater {
            model: [[-2, 0.22], [-4, 0.10], [-7, 0.04]]
            delegate: Rectangle {
                required property var modelData
                anchors.fill: parent
                anchors.margins: modelData[0]
                radius: glow.radius - modelData[0]
                color: "transparent"
                border.width: 2
                border.color: Theme.alpha(root.hi, modelData[1])
            }
        }
    }
    // A game: as an entry of the list (row) and as the head of its page.
    component GameSummary: Plate {
        id: summary
        property var g: ({})
        property bool lit: false
        property bool row: false
        readonly property bool wide: width > 900
        // Over Remote Play the page is narrow: smaller pieces, room for the title.
        readonly property bool narrow: width < 720
        implicitHeight: row ? 92 : 104
        radius: Theme.radiusSmall + 2
        hoverable: row
        color: lit ? root.pickedFill : Qt.rgba(1, 1, 1, Theme.light ? 0.5 : Theme.glassSubtle)
        border.color: lit ? root.pickedEdge : Theme.light ? Theme.lightHairline : Theme.glassEdge
        border.width: lit ? 1.5 : 1
        PickedGlow {
            on: summary.lit
            radius: summary.radius
        }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            anchors.rightMargin: 20
            spacing: summary.wide ? 22 : summary.narrow ? 10 : 14
            // Its icon.
            Rectangle {
                Layout.preferredWidth: summary.narrow ? 92 : summary.row ? 136 : 146
                Layout.fillHeight: true
                radius: Theme.radiusSmall - 2
                color: Theme.light ? Theme.lightWell : Qt.rgba(0, 0, 0, 0.25)
                clip: true
                Image {
                    anchors.fill: parent
                    source: summary.g.icon || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: status === Image.Ready
                }
                TrophyGlyph {
                    anchors.centerIn: parent
                    visible: !(summary.g.icon || "").length
                    grade: "platinum"
                    size: 36
                    dim: true
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: summary.g.title || ""
                    color: Theme.ink
                    font.family: Theme.fontDisplay
                    font.pixelSize: summary.row ? Theme.fontCardTitle : Theme.fontHeading
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Row {
                    spacing: 8
                    Tag {
                        text: "PS4"
                        mono: true
                    }
                    Tag {
                        visible: !!summary.g.backedUp
                        text: qsTr("On the PC")
                        tone: "ok"
                        iconName: "check"
                    }
                }
            }
            // The progress.
            ColumnLayout {
                readonly property real span: summary.wide ? 190 : summary.narrow ? 84 : 120
                Layout.preferredWidth: span
                Layout.minimumWidth: span
                Layout.maximumWidth: span
                spacing: 8
                Text {
                    Layout.alignment: Qt.AlignRight
                    text: (summary.g.progress || 0) + "%"
                    color: Theme.ink
                    font.family: Theme.fontDisplay
                    font.pixelSize: summary.row ? 17 : 20
                    font.weight: Font.DemiBold
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 4
                    radius: 2
                    color: Theme.light ? Theme.lightTrack : Qt.rgba(1, 1, 1, 0.12)
                    Rectangle {
                        width: parent.width * Math.min(1, (summary.g.progress || 0) / 100)
                        height: parent.height
                        radius: 2
                        color: Theme.text
                    }
                }
            }
            // Platinum, gold, silver, bronze.
            Repeater {
                model: ["platinum", "gold", "silver", "bronze"]
                delegate: Row {
                    required property string modelData
                    Layout.preferredWidth: summary.wide ? 58 : summary.narrow ? 38 : 50
                    spacing: summary.narrow ? 3 : 5
                    opacity: (summary.g[modelData + "Total"] || 0) > 0 ? 1 : 0.4
                    TrophyGlyph {
                        anchors.verticalCenter: parent.verticalCenter
                        grade: parent.modelData
                        size: summary.narrow ? 20 : 26
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: summary.g[parent.modelData] || 0
                        color: Theme.textSecondary
                        font.pixelSize: summary.narrow ? 13 : 16
                    }
                }
            }
        }
    }
    // A trophy's picture: its own when the console has it, else its cup,
    // over a soft light of its colour.
    component TrophyArt: Item {
        id: art
        property var trophy: null
        Canvas {
            anchors.fill: parent
            visible: !!art.trophy
            property color tint: art.trophy ? root.gradeColor(art.trophy.grade) : "#FFFFFF"
            onTintChanged: requestPaint()
            onWidthChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var r = width / 2
                var g = ctx.createRadialGradient(r, r, 0, r, r, r)
                var rgb = Math.round(tint.r * 255) + ", " + Math.round(tint.g * 255) + ", " + Math.round(tint.b * 255)
                g.addColorStop(0, "rgba(" + rgb + ", 0.28)")
                g.addColorStop(0.55, "rgba(" + rgb + ", 0.09)")
                g.addColorStop(1, "rgba(" + rgb + ", 0)")
                ctx.fillStyle = g
                ctx.fillRect(0, 0, width, height)
            }
        }
        Image {
            anchors.fill: parent
            anchors.margins: 14
            source: art.trophy && !root.concealed(art.trophy) ? root.artOf(art.trophy) : ""
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            visible: status === Image.Ready
        }
        TrophyGlyph {
            anchors.centerIn: parent
            visible: !!art.trophy && (root.artOf(art.trophy).length === 0 || root.concealed(art.trophy))
            grade: art.trophy ? art.trophy.grade : "bronze"
            dim: !!art.trophy && !art.trophy.unlocked
            size: parent.width * 0.55
        }
    }

    readonly property bool sideDetails: width >= 900

    // The page, straight on the sheet, as the other pages.
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.framed ? Theme.gutter : 18
        anchors.rightMargin: root.framed ? Theme.gutter : 18
        anchors.topMargin: root.framed ? 22 : 18
        anchors.bottomMargin: root.framed ? 10 : 12
        spacing: 16

        // ── The head: whose trophies, and the page's actions
        PageHeader {
            id: pageHeader
            visible: root.framed
            title: qsTr("Trophy vault")
            rule: false
            // Only while the console is being read.
            subtitle: trophies.busy ? trophies.status : ""
            Item {
                id: headerSlot
                implicitWidth: childrenRect.width
                implicitHeight: childrenRect.height
            }
        }
        RowLayout {
            visible: !root.framed
            Layout.fillWidth: true
            spacing: 12
            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: pageHeader.subtitle
                color: Theme.textSecondary
                font.pixelSize: 12
            }
            Item {
                id: overlaySlot
                implicitWidth: childrenRect.width
                implicitHeight: childrenRect.height
            }
        }
        Row {
            parent: root.framed ? headerSlot : overlaySlot
            spacing: 8
            // The console whose trophies are read: another one is a click away.
            ConsoleSwitch {
                anchors.verticalCenter: parent.verticalCenter
            }
            // Back to the list of games, from one game.
            StyledButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.level === "game"
                chip: true
                iconName: "chevron-left"
                text: qsTr("All games")
                onClicked: root.level = "games"
            }
            // Whose trophies: another user of the console is a click away.
            StyledButton {
                id: userChip
                anchors.verticalCenter: parent.verticalCenter
                chip: true
                iconName: "user"
                visible: trophies.users.length > 0
                text: (root.me.name || root.me.folder || qsTr("User")) + (trophies.users.length > 1 ? "  ▾" : "")
                width: Math.min(implicitWidth, 220)
                enabled: !trophies.busy
                ToolTip.visible: hovered
                ToolTip.text: trophies.users.length > 1 ? qsTr("The user whose trophies are shown: click for another")
                                                       : qsTr("The user whose trophies are shown")
                onClicked: if (trophies.users.length > 1) userMenu.popup(userChip, 0, userChip.height + 4)
            }
            StyledButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "sliders"
                text: qsTr("Compare Trophies")
                enabled: root.level === "game" || root.games.length > 0
                onClicked: {
                    // In the list, the game picked there.
                    if (root.level !== "game")
                        root.game = root.games[root.gameIndex] || ({})
                    compareDialog.open()
                }
            }
            // The rest: what is shown, copies, the folder, reading again.
            StyledToolButton {
                id: optionsButton
                anchors.verticalCenter: parent.verticalCenter
                iconName: "menu"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Options")
                Accessible.name: ToolTip.text
                onClicked: optionsMenu.popup(optionsButton, optionsButton.width - optionsMenu.implicitWidth,
                                             optionsButton.height + 4)
            }
        }

        // ── The list of games
        Item {
            visible: root.level === "games"
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: gamesList
                anchors.fill: parent
                clip: true
                spacing: 6
                // Room for the light around the one picked.
                topMargin: 8
                bottomMargin: 8
                leftMargin: 8
                model: root.games
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: GameSummary {
                    required property var modelData
                    required property int index
                    width: gamesList.width - 24
                    g: modelData
                    row: true
                    lit: index === root.gameIndex
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: root.gameIndex = parent.index
                        onClicked: { root.forceActiveFocus(); root.openGame(parent.index) }
                    }
                }
            }
            Column {
                anchors.centerIn: parent
                visible: root.games.length === 0 && !trophies.busy
                spacing: 14
                TrophyGlyph {
                    anchors.horizontalCenter: parent.horizontalCenter
                    grade: "silver"
                    size: 54
                    dim: true
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(460, root.width - 80)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                    text: trophies.ps5 ? qsTr("A PS5 keeps its trophies another way, which this does not read yet.")
                        : !app.canUseFtp ? qsTr("The trophies are read over the console's FTP (GoldHEN).")
                        : root.onlyCopied ? qsTr("No game's trophies are copied to this PC yet.")
                        : qsTr("Read the console to see its users' trophies.")
                }
            }
        }

        // ── One game: its summary, its trophies, the one picked
        GameSummary {
            visible: root.level === "game"
            Layout.fillWidth: true
            g: root.game
        }
        RowLayout {
            visible: root.level === "game"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 18
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                topMargin: 8
                bottomMargin: 28
                leftMargin: 8
                model: root.shown
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Plate {
                    id: trow
                    required property var modelData
                    required property int index
                    readonly property bool picked: index === root.trophyIndex
                    width: list.width - 24
                    height: 96
                    radius: Theme.radiusSmall + 2
                    hoverable: true
                    color: picked ? root.pickedFill : Qt.rgba(1, 1, 1, Theme.light ? 0.5 : Theme.glassSubtle)
                    border.color: picked ? root.pickedEdge : Theme.light ? Theme.lightHairline : Theme.glassEdge
                    border.width: picked ? 1.5 : 1
                    PickedGlow {
                        on: trow.picked
                        radius: trow.radius
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        anchors.rightMargin: 20
                        spacing: 18
                        // Its own picture, or its cup.
                        Rectangle {
                            Layout.preferredWidth: 96
                            Layout.fillHeight: true
                            radius: Theme.radiusSmall - 2
                            color: Theme.light ? Theme.lightWell : Qt.rgba(0, 0, 0, 0.25)
                            clip: true
                            Image {
                                anchors.fill: parent
                                source: root.concealed(trow.modelData) ? "" : root.artOf(trow.modelData)
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                visible: status === Image.Ready
                            }
                            TrophyGlyph {
                                anchors.centerIn: parent
                                visible: root.artOf(trow.modelData).length === 0 || root.concealed(trow.modelData)
                                grade: trow.modelData.grade
                                dim: !trow.modelData.unlocked
                                size: 42
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: root.concealed(trow.modelData) ? qsTr("Hidden trophy") : trow.modelData.title
                                color: Theme.ink
                                font.family: Theme.fontDisplay
                                font.pixelSize: Theme.fontCardTitle
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.concealed(trow.modelData) ? qsTr("Shown once it is earned.")
                                                                     : trow.modelData.description
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideRight
                            }
                            Row {
                                visible: trow.modelData.rarity >= 0
                                spacing: 6
                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "triangle"
                                    size: 12
                                    color: Theme.textSecondary
                                }
                                Text {
                                    text: root.rarityName(trow.modelData.rarity) + "  "
                                          + Number(trow.modelData.rarity).toFixed(1) + "%"
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSmall
                                }
                            }
                        }
                        Text {
                            text: trow.modelData.unlocked ? trow.modelData.day + "  " + (trow.modelData.time || "")
                                                          : qsTr("Not earned")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                        }
                        Rectangle {
                            Layout.fillHeight: true
                            Layout.topMargin: 16
                            Layout.bottomMargin: 16
                            width: 1
                            color: Theme.rule
                        }
                        TrophyGlyph {
                            grade: trow.modelData.grade
                            dim: !trow.modelData.unlocked
                            size: 42
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { root.forceActiveFocus(); root.trophyIndex = trow.index }
                        onDoubleClicked: bigDialog.open()
                    }
                }
                // More below.
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 2
                    visible: list.contentHeight > list.height && !list.atYEnd
                    name: "chevron-down"
                    size: 20
                    color: Theme.textSecondary
                }
                Text {
                    anchors.centerIn: parent
                    visible: root.shown.length === 0
                    text: root.filter === "earned" ? qsTr("No trophy earned in this game yet.")
                                                   : qsTr("Every trophy of this game is earned.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                }
            }

            // The trophy picked, large; scrolls when the window is short.
            Plate {
                id: detailPlate
                visible: root.sideDetails
                Layout.preferredWidth: Math.min(380, root.width * 0.34)
                Layout.preferredHeight: Math.min(detailColumn.implicitHeight + 28, parent.height - 8)
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 8
                clip: true
                Flickable {
                    anchors.fill: parent
                    contentHeight: detailColumn.implicitHeight + 28
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: contentHeight > height
                ColumnLayout {
                    id: detailColumn
                    x: 20
                    y: 6
                    width: detailPlate.width - 40
                    spacing: 4
                    TrophyArt {
                        readonly property real side: root.height < 760 ? 150 : 200
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: side
                        Layout.preferredHeight: side
                        trophy: root.current
                    }
                    Text {
                        Layout.fillWidth: true
                        text: !root.current ? "" : root.concealed(root.current) ? qsTr("Hidden trophy") : root.current.title
                        color: Theme.ink
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.fontHeading
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.bottomMargin: 12
                        text: !root.current ? "" : root.concealed(root.current) ? qsTr("Shown once it is earned.")
                                                                               : root.current.description
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                    }
                    Repeater {
                        // [label, value, its mark, the value's second part].
                        model: root.current ? [
                            [qsTr("Rarity"), root.current.rarity >= 0
                                ? root.rarityName(root.current.rarity) + "  " + Number(root.current.rarity).toFixed(1) + "%"
                                : qsTr("Not known"), root.current.rarity >= 0 ? "triangle" : "", ""],
                            [qsTr("Earned"), root.current.unlocked ? root.current.day : qsTr("Not yet"), "",
                                root.current.unlocked ? root.current.time : ""],
                            [qsTr("Details"), root.gradeName(root.current.grade) + " · "
                                + qsTr("Trophy %1 of %2 (%3)").arg(root.current.id + 1)
                                .arg(root.allTrophies.length).arg(root.game.npId || ""), "", ""]
                        ] : []
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 0
                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: Theme.rule
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 12
                                Layout.bottomMargin: 12
                                spacing: 12
                                Text {
                                    Layout.preferredWidth: 96
                                    Layout.alignment: Qt.AlignTop
                                    text: modelData[0]
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontBody
                                }
                                Icon {
                                    visible: modelData[2].length > 0
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: 2
                                    name: modelData[2] || "triangle"
                                    size: 14
                                    color: Theme.textSecondary
                                }
                                Text {
                                    Layout.fillWidth: !modelData[3]
                                    text: modelData[1]
                                    color: Theme.text
                                    font.pixelSize: modelData[3] ? 16 : Theme.fontBody
                                    font.weight: modelData[3] ? Font.DemiBold : Font.Normal
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    visible: !!modelData[3]
                                    Layout.fillWidth: true
                                    text: modelData[3]
                                    color: Theme.text
                                    font.pixelSize: Theme.fontBody
                                }
                            }
                        }
                    }
                }
                }
            }
        }

    }

    // ── Menus and windows
    Menu {
        id: userMenu
        topPadding: 8
        bottomPadding: 8
        background: ShelfTile.MenuGlass { implicitWidth: 240 }
        Repeater {
            model: trophies.users
            delegate: StyledMenuItem {
                required property var modelData
                text: (modelData.name || modelData.folder) + (modelData.hasTrophies ? "" : "  · " + qsTr("no trophies"))
                      + (modelData.folder === trophies.user ? "  ✓" : "")
                enabled: modelData.hasTrophies
                onTriggered: { root.level = "games"; trophies.setUser(modelData.folder) }
            }
        }
    }
    Menu {
        id: optionsMenu
        topPadding: 8
        bottomPadding: 8
        background: ShelfTile.MenuGlass { implicitWidth: 280 }
        // In the list: every game, or the ones copied here.
        StyledMenuItem {
            visible: root.level === "games"
            height: visible ? implicitHeight : 0
            text: qsTr("All games") + (!root.onlyCopied ? "  ✓" : "")
            onTriggered: root.onlyCopied = false
        }
        StyledMenuItem {
            visible: root.level === "games"
            height: visible ? implicitHeight : 0
            text: qsTr("Only the ones on this PC") + (root.onlyCopied ? "  ✓" : "")
            onTriggered: root.onlyCopied = true
        }
        // In a game: every trophy, or some.
        StyledMenuItem {
            visible: root.level === "game"
            height: visible ? implicitHeight : 0
            text: qsTr("Show every trophy") + (root.filter === "all" ? "  ✓" : "")
            onTriggered: root.filter = "all"
        }
        StyledMenuItem {
            visible: root.level === "game"
            height: visible ? implicitHeight : 0
            text: qsTr("Only the ones earned") + (root.filter === "earned" ? "  ✓" : "")
            onTriggered: root.filter = "earned"
        }
        StyledMenuItem {
            visible: root.level === "game"
            height: visible ? implicitHeight : 0
            text: qsTr("Only the ones not earned") + (root.filter === "unearned" ? "  ✓" : "")
            onTriggered: root.filter = "unearned"
        }
        StyledMenuItem {
            visible: root.level === "game"
            height: visible ? implicitHeight : 0
            text: qsTr("Order: %1").arg(root.sortLabel(root.sort))
            onTriggered: root.sort = root.sorts[(root.sorts.indexOf(root.sort) + 1) % root.sorts.length]
        }
        MenuSeparator {}
        StyledMenuItem {
            text: root.level === "game" ? qsTr("Copy this game to this PC") : qsTr("Copy all to this PC")
            enabled: !trophies.busy && trophies.titles.length > 0
            onTriggered: trophies.backup(root.level === "game" ? [root.game.id] : [])
        }
        StyledMenuItem {
            text: qsTr("Open the folder on this PC")
            onTriggered: trophies.openFolder()
        }
        StyledMenuItem {
            text: qsTr("Read the console again")
            enabled: !trophies.busy && app.canUseFtp && !trophies.ps5
            onTriggered: trophies.refresh()
        }
    }

    Dialog {
        id: bigDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 520)
        modal: true
        padding: 0
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: ShelfTile.DialogGlass {}
        header: DialogHeader {
            eyebrow: root.game.title || ""
            title: !root.current ? "" : root.concealed(root.current) ? qsTr("Hidden trophy") : root.current.title
            dialog: bigDialog
        }
        contentItem: ColumnLayout {
            spacing: 10
            TrophyArt {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 280
                Layout.preferredHeight: 280
                trophy: root.current
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.bottomMargin: Theme.dialogMargin
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
                text: !root.current ? "" : (root.concealed(root.current) ? qsTr("Shown once it is earned.")
                                                                         : root.current.description)
                      + "\n" + (root.current.unlocked ? qsTr("Earned %1").arg(root.current.date) : qsTr("Not earned"))
            }
        }
    }

    // The same game, every user of the console who has it.
    Dialog {
        id: compareDialog
        readonly property var rows: opened ? trophies.compareOf(root.game.npId || "") : []
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 820)
        // Short when there is nobody to compare with.
        height: rows.length > 1 ? Math.min(parent.height - 48, 640) : implicitHeight
        modal: true
        padding: 0
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: ShelfTile.DialogGlass {}
        header: DialogHeader {
            eyebrow: qsTr("Compare Trophies")
            title: root.game.title || ""
            dialog: compareDialog
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                visible: compareDialog.rows.length < 2
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
                text: qsTr("No other user of this console has played it: the comparison is between the console's users.")
            }
            // Each user: progress and trophies by type.
            Repeater {
                model: compareDialog.rows
                delegate: Plate {
                    id: urow
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    implicitHeight: 60
                    radius: Theme.radiusSmall
                    color: modelData.user === trophies.user ? root.pickedFill
                                                            : Qt.rgba(1, 1, 1, Theme.light ? 0.5 : Theme.glassSubtle)
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 18
                        spacing: 14
                        Rectangle {
                            width: 32
                            height: 32
                            radius: 16
                            color: Theme.alpha(root.hi, 0.12)
                            Text {
                                anchors.centerIn: parent
                                text: urow.modelData.name.charAt(0).toUpperCase()
                                color: Theme.ink
                                font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            Layout.preferredWidth: 150
                            text: urow.modelData.name
                            color: Theme.ink
                            font.pixelSize: 15
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 4
                            radius: 2
                            color: Theme.light ? Theme.lightTrack : Qt.rgba(1, 1, 1, 0.12)
                            Rectangle {
                                width: parent.width * Math.min(1, urow.modelData.progress / 100)
                                height: parent.height
                                radius: 2
                                color: Theme.text
                            }
                        }
                        Text {
                            text: urow.modelData.progress + "%"
                            color: Theme.ink
                            font.pixelSize: 15
                        }
                        Repeater {
                            model: ["platinum", "gold", "silver", "bronze"]
                            delegate: Row {
                                required property string modelData
                                spacing: 4
                                TrophyGlyph {
                                    anchors.verticalCenter: parent.verticalCenter
                                    grade: parent.modelData
                                    size: 20
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: urow.modelData[parent.modelData] || 0
                                    color: Theme.textSecondary
                                    font.pixelSize: 13
                                }
                            }
                        }
                    }
                }
            }
            // The names over the columns below.
            RowLayout {
                visible: compareDialog.rows.length > 1
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin + 12
                Layout.topMargin: 6
                spacing: 12
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Trophy")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                }
                Repeater {
                    model: compareDialog.rows
                    delegate: Text {
                        required property var modelData
                        Layout.preferredWidth: 92
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData.name
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                }
            }
            // Each trophy: who has it.
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.bottomMargin: 12
                clip: true
                visible: compareDialog.rows.length > 1
                model: root.allTrophies
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: RowLayout {
                    id: crow
                    required property var modelData
                    width: ListView.view.width - 12
                    height: 36
                    spacing: 12
                    TrophyGlyph {
                        grade: crow.modelData.grade
                        size: 20
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.concealed(crow.modelData) ? qsTr("Hidden trophy") : crow.modelData.title
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }
                    Repeater {
                        model: compareDialog.rows
                        delegate: Item {
                            required property var modelData
                            implicitWidth: 92
                            implicitHeight: 24
                            Icon {
                                anchors.centerIn: parent
                                name: modelData.earned.indexOf(crow.modelData.id) >= 0 ? "check" : "minus"
                                size: 16
                                color: modelData.earned.indexOf(crow.modelData.id) >= 0 ? Theme.ok : Theme.textSecondary
                            }
                        }
                    }
                }
            }
            Item {
                visible: compareDialog.rows.length < 2
                implicitHeight: 14
            }
        }
    }
}
