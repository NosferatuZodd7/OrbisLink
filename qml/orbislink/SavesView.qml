// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The save vault: a console's saves next to the copies kept on this PC.
// One click backs up to the PC everything new or changed; another sends
// saves to a console — the one they came from, or another console of the
// same PSN account (a PS4 game's save goes from a PS4 to a PS5 and back).
// The backups are laid out as the PS4 copies saves to a USB drive
// (PS4/SAVEDATA/<PSID>/<game>).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    signal back()

    readonly property bool online: saves.online
    // "all", "console" (not backed up), "changed" (and older), "vault" (only on the PC).
    property string filter: "all"
    property var selected: ({})
    // Whose saves are shown: an owner's PSID, or "user:<folder>" for a
    // console user whose PSID is not known yet; "" everyone's.
    property string group: ""
    function inGroup(s) {
        return group === "" || s.group === group
    }
    // Each owner found in the saves: its name (the app's name for its PSID,
    // else the console's for its user), its console user here, its PSID.
    readonly property var groups: {
        var seen = {}
        var list = []
        for (var i = 0; i < saves.saves.length; ++i) {
            var s = saves.saves[i]
            if (seen[s.group] === undefined) {
                seen[s.group] = list.length
                list.push({ id: s.group, user: "", name: "", consoleName: "", psidFolder: s.psidFolder,
                            linked: false, count: 0 })
            }
            var e = list[seen[s.group]]
            e.count++
            if (e.user.length === 0 && s.account.length > 0)
                e.user = s.account
            if (e.consoleName.length === 0)
                e.consoleName = s.consoleUser
            if (e.name.length === 0)
                e.name = s.psidName.length > 0 ? s.psidName : s.userName.length > 0 ? s.userName : s.consoleUser
            if (s.linked)
                e.linked = true
        }
        return list
    }
    function groupLabel(e) {
        if (e.name.length > 0)
            return e.name
        if (e.psidFolder.length > 0)
            return qsTr("PSID %1").arg(e.psidFolder)
        return qsTr("User %1").arg(e.user)
    }
    readonly property var shownGroup: {
        for (var i = 0; i < groups.length; ++i)
            if (groups[i].id === group)
                return groups[i]
        return null
    }
    // Whose a save is, in a word: the app's name for its PSID, the
    // console's name for its user, or the PSID.
    function ownerOf(s) {
        if (s.psidName.length > 0)
            return s.psidName
        if (s.consoleUser.length > 0)
            return s.consoleUser
        if (s.psidFolder.length > 0)
            return s.psidFolder
        return s.userName
    }
    readonly property int selectedCount: Object.keys(selected).length

    readonly property var counts: {
        var c = { all: 0, same: 0, changed: 0, older: 0, console: 0, vault: 0, missing: 0 }
        for (var i = 0; i < saves.saves.length; ++i) {
            var s = saves.saves[i]
            if (!root.inGroup(s))
                continue
            c.all++
            c[s.sync]++
            if (s.sync === "vault" && s.wasHere && s.account.length > 0)
                c.missing++
        }
        return c
    }
    // What "Back up all" would copy: never an older copy over a newer backup.
    readonly property int pending: counts.changed + counts.console

    function matchesFilter(s) {
        return filter === "all" || s.sync === filter || (filter === "changed" && s.sync === "older")
    }
    readonly property var shown: {
        var list = []
        for (var i = 0; i < saves.saves.length; ++i) {
            var s = saves.saves[i]
            if (matchesFilter(s) && inGroup(s))
                list.push(s)
        }
        list.sort(function (a, b) {
            if (a.gameTitle !== b.gameTitle) return a.gameTitle.localeCompare(b.gameTitle)
            if (a.group !== b.group) return a.group < b.group ? -1 : 1
            return a.saveTitle.localeCompare(b.saveTitle)
        })
        return list
    }
    readonly property bool manyOwners: groups.length > 1

    // The consoles saves can go to: the ones whose FTP answers.
    readonly property var targets: {
        var list = []
        var all = saves.consoles
        for (var i = 0; i < all.length; ++i)
            if (all[i].ftp)
                list.push(all[i])
        return list
    }
    // Where "Send to" sends: the one picked, else the console shown, else
    // the first that answers.
    property string sendTarget: ""
    readonly property var sendConsole: {
        var list = targets
        for (var i = 0; i < list.length; ++i)
            if (list[i].address === sendTarget)
                return list[i]
        for (i = 0; i < list.length; ++i)
            if (list[i].source)
                return list[i]
        return list.length > 0 ? list[0] : null
    }
    function typeLabel(c) {
        return c.type === "ps5" ? "PS5" : c.type === "ps4" ? "PS4" : ""
    }

    // The game open on the console in use (its title ID, from Remote Play's
    // discovery): its saves are not copied either way while it runs, or the
    // save comes out corrupted.
    readonly property string openGame: typeof stream !== "undefined" && stream !== null
                                       ? stream.runningAppTitleId : ""
    readonly property string openGameName: typeof stream !== "undefined" && stream !== null
                                           ? stream.runningApp : ""
    // The keys, without the open game's saves when the console is the one in
    // use; says so when it left some out.
    function withoutOpenGame(keys, address) {
        if (openGame.length === 0 || address !== app.consoleAddress)
            return keys
        var kept = []
        for (var i = 0; i < saves.saves.length; ++i) {
            var s = saves.saves[i]
            if (keys.indexOf(s.key) >= 0 && s.titleId !== openGame)
                kept.push(s.key)
        }
        if (kept.length < keys.length)
            toast.show(qsTr("%1 is open on the console: its saves were left alone. Close it and try again.")
                       .arg(openGameName.length > 0 ? openGameName : openGame), true)
        return kept
    }
    // Saves whose owner is not known yet (no PSID) wait for their console
    // user to be linked to an Account ID; the rest go now. One older than
    // the PC's copy is left out: backing it up would put an earlier save
    // over a newer one.
    property var waitingForLink: []
    function backupKeys(keys) {
        var kept = withoutOpenGame(keys, saves.source)
        var ready = []
        var waiting = []
        var older = 0
        var user = null
        for (var i = 0; i < saves.saves.length; ++i) {
            var s = saves.saves[i]
            if (kept.indexOf(s.key) < 0 || !s.onConsole)
                continue
            if (s.sync === "older") {
                ++older
            } else if (s.psidFolder.length > 0) {
                ready.push(s.key)
            } else {
                // By where it is: its key changes once its PSID is known.
                waiting.push(s.account + "/" + s.titleId + "/" + s.dir)
                if (user === null)
                    for (var g = 0; g < groups.length; ++g)
                        if (groups[g].user === s.account)
                            user = groups[g]
            }
        }
        if (older > 0)
            toast.show(qsTr("%n of them are older than the copy on the PC and were left out: send the PC's copy "
                            + "to the console instead.", "", older), false)
        if (ready.length > 0)
            saves.backup(ready)
        if (waiting.length > 0 && user !== null) {
            waitingForLink = waiting
            linkDialog.ask(user)
        }
    }

    // Sending always asks first, naming the console.
    function askSend(keys, address) {
        var kept = withoutOpenGame(keys, address)
        if (kept.length > 0)
            confirmSend.ask(kept, address)
    }

    function isSelected(key) { return selected[key] === true }
    function toggle(key) {
        var next = Object.assign({}, selected)
        if (next[key]) delete next[key]
        else next[key] = true
        selected = next
    }
    function selectAll() {
        var next = {}
        for (var i = 0; i < shown.length; ++i)
            next[shown[i].key] = true
        selected = next
    }
    function selectedKeys() { return Object.keys(selected) }
    // Of the selection, how many can go each way.
    function countWhere(test) {
        var n = 0
        for (var i = 0; i < saves.saves.length; ++i)
            if (selected[saves.saves[i].key] && test(saves.saves[i]))
                ++n
        return n
    }
    // Every console's FTP: which ones saves can be read from and sent to.
    function probeConsoles() {
        var all = []
        var items = saves.consoles
        for (var i = 0; i < items.length; ++i)
            all.push(items[i].address)
        app.probeFtp(all)
    }

    onVisibleChanged: {
        if (!visible)
            return
        probeConsoles()
        if (!saves.busy)
            saves.refresh()
    }
    Connections {
        target: saves
        function onSavesChanged() {
            // What is no longer there is no longer selected.
            var next = {}
            for (var i = 0; i < saves.saves.length; ++i)
                if (root.selected[saves.saves[i].key])
                    next[saves.saves[i].key] = true
            root.selected = next
        }
        function onFinished(message, error) { toast.show(message, error) }
        // Linked: what was waiting for it is backed up.
        function onBusyChanged() {
            if (saves.busy || root.waitingForLink.length === 0 || linkDialog.visible)
                return
            var keys = []
            for (var i = 0; i < saves.saves.length; ++i) {
                var s = saves.saves[i]
                if (root.waitingForLink.indexOf(s.account + "/" + s.titleId + "/" + s.dir) >= 0
                        && s.psidFolder.length > 0)
                    keys.push(s.key)
            }
            root.waitingForLink = []
            if (keys.length > 0)
                saves.backup(keys)
        }
    }
    // Another console shown: everyone's saves again, none selected.
    readonly property string shownSource: saves.source
    onShownSourceChanged: {
        group = ""
        selected = ({})
    }

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: Theme.gutter
        anchors.rightMargin: window.panelShown ? 8 : Theme.gutter
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        radius: Theme.radius
        color: Theme.panelFill
        border.width: 1
        border.color: Theme.glassEdge
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 14

            // ── header
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                StyledToolButton {
                    iconName: "chevron-left"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Back to the consoles")
                    onClicked: root.back()
                }
                ColumnLayout {
                    spacing: 2
                    Text {
                        text: qsTr("Save vault")
                        color: Theme.text
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: saves.busy && saves.status.length > 0 ? saves.status
                            : qsTr("%1 on the console · %2 on the PC")
                                .arg(root.counts.all - root.counts.vault)
                                .arg(root.counts.same + root.counts.changed + root.counts.older + root.counts.vault)
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                }
                // Whose saves these are: the console read. Another one with
                // FTP is a click away.
                StyledButton {
                    id: sourceChip
                    chip: true
                    iconName: "gamepad"
                    text: saves.sourceName + (saves.consoles.length > 1 ? "  ▾" : "")
                    Layout.maximumWidth: 260
                    enabled: !saves.busy
                    ToolTip.visible: hovered
                    ToolTip.text: saves.consoles.length > 1 ? qsTr("The console whose saves are shown: click for another")
                                                            : qsTr("The console whose saves are shown")
                    onClicked: if (saves.consoles.length > 1) sourceMenu.popup(sourceChip, 0, sourceChip.height + 4)
                }
                Item { Layout.fillWidth: true }
                StyledButton {
                    iconName: "folder-open"
                    chip: true
                    text: saves.vaultFolder
                    Layout.maximumWidth: 300
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Open the folder on the PC (right click to change it)")
                    onClicked: saves.openVaultFolder()
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: vaultFolderDialog.open()
                    }
                }
                StyledToolButton {
                    iconName: "settings"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Change the folder on the PC")
                    onClicked: vaultFolderDialog.open()
                }
                StyledToolButton {
                    iconName: saves.busy ? "loader" : "refresh"
                    enabled: !saves.busy
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Read the console again")
                    onClicked: { root.probeConsoles(); saves.refresh() }
                }
            }

            // ── the one thing most people come for
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: heroRow.implicitHeight + 28
                radius: 16
                color: Theme.accentFill
                border.width: 1
                border.color: Theme.alpha(Theme.accent, root.selectedCount > 0 ? 0.7 : 0.35)
                Behavior on border.color { ColorAnimation { duration: Theme.fast } }

                RowLayout {
                    id: heroRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 18
                    anchors.rightMargin: 14
                    spacing: 14

                    Icon {
                        name: saves.busy ? "loader" : root.selectedCount > 0 ? "square-check" : "archive"
                        spinning: saves.busy
                        size: 26
                        color: Theme.accent
                    }

                    // With some selected, what to do with them.
                    Text {
                        visible: root.selectedCount > 0 && !saves.busy
                        Layout.fillWidth: true
                        text: qsTr("%n selected", "", root.selectedCount)
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    StyledButton {
                        visible: root.selectedCount > 0 && !saves.busy
                        iconName: "download"
                        text: qsTr("Back up to PC")
                        enabled: root.online && root.countWhere(function (s) { return s.onConsole }) > 0
                        onClicked: root.backupKeys(root.selectedKeys())
                    }
                    // Named after the console it goes to; the arrow picks
                    // another when more than one has FTP.
                    Row {
                        visible: root.selectedCount > 0 && !saves.busy
                        spacing: 2
                        StyledButton {
                            id: sendButton
                            iconName: "upload"
                            text: root.sendConsole !== null ? qsTr("Send to %1").arg(root.sendConsole.name)
                                                           : qsTr("Send to a console")
                            enabled: root.sendConsole !== null && root.countWhere(function (s) { return s.inVault }) > 0
                            ToolTip.visible: hovered && root.sendConsole === null
                            ToolTip.text: qsTr("No console's FTP is answering.")
                            onClicked: root.askSend(root.selectedKeys(), root.sendConsole.address)
                        }
                        StyledToolButton {
                            visible: root.targets.length > 1
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "chevron-down"
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Send to another console")
                            onClicked: targetMenu.popup(sendButton, 0, sendButton.height + 4)
                        }
                    }
                    StyledButton {
                        visible: root.selectedCount > 0 && !saves.busy
                        danger: true
                        iconName: "trash"
                        text: qsTr("Delete…")
                        onClicked: confirmDelete.open()
                    }
                    StyledToolButton {
                        visible: root.selectedCount > 0 && !saves.busy
                        iconName: "close"
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Clear the selection")
                        onClicked: root.selected = ({})
                    }

                    ColumnLayout {
                        visible: root.selectedCount === 0 || saves.busy
                        Layout.fillWidth: true
                        // Its width comes from the row alone: the long lines
                        // wrap to it instead of asking for more.
                        Layout.preferredWidth: 1
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            text: !root.online ? qsTr("%1's FTP is not answering: this is what the PC holds.").arg(saves.sourceName)
                                : saves.busy ? saves.status
                                : root.pending > 0 ? qsTr("%n save(s) on the console without a backup on the PC.", "", root.pending)
                                : root.counts.all > 0 ? qsTr("Everything on the console is backed up on the PC.")
                                : qsTr("Read the console to see its saves.")
                        }
                        // The progress of whatever is under way.
                        Rectangle {
                            visible: saves.busy
                            Layout.fillWidth: true
                            height: 4
                            radius: 2
                            color: Theme.alpha(Theme.accent, 0.18)
                            Rectangle {
                                width: parent.width * Math.max(0.03, Math.min(1, saves.progress))
                                height: parent.height
                                radius: parent.radius
                                color: Theme.accent
                                Behavior on width { NumberAnimation { duration: 200 } }
                            }
                        }
                        Text {
                            visible: !saves.busy && root.openGame.length > 0 && saves.source === app.consoleAddress
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: qsTr("%1 is open: its saves wait until it closes.")
                                  .arg(root.openGameName.length > 0 ? root.openGameName : root.openGame)
                            color: Theme.warn
                            font.pixelSize: 12
                        }
                        Text {
                            visible: !saves.busy && root.counts.missing > 0 && root.online
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: qsTr("%n save(s) the console no longer has are kept on the PC.", "", root.counts.missing)
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                        Text {
                            visible: !saves.busy && root.counts.older > 0 && root.online
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: qsTr("%n save(s) on the console are older than the PC's copy (from another console).", "",
                                       root.counts.older)
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                    }
                    StyledButton {
                        visible: root.online && root.counts.missing > 0 && root.selectedCount === 0
                        text: qsTr("Send back what's missing")
                        iconName: "archive-restore"
                        enabled: !saves.busy
                        onClicked: {
                            var keys = []
                            for (var i = 0; i < saves.saves.length; ++i) {
                                var s = saves.saves[i]
                                if (s.sync === "vault" && s.wasHere && s.account.length > 0 && root.inGroup(s))
                                    keys.push(s.key)
                            }
                            root.askSend(keys, saves.source)
                        }
                    }
                    StyledButton {
                        visible: root.selectedCount === 0 || saves.busy
                        text: qsTr("Back up all to PC")
                        iconName: "download"
                        primary: true
                        enabled: root.online && !saves.busy && root.pending > 0
                        // Everything, or everything of the owner shown.
                        onClicked: {
                            var keys = []
                            for (var i = 0; i < saves.saves.length; ++i) {
                                var s = saves.saves[i]
                                if (s.onConsole && (s.sync === "console" || s.sync === "changed") && root.inGroup(s))
                                    keys.push(s.key)
                            }
                            root.backupKeys(keys)
                        }
                    }
                }
            }

            // ── whose saves: one chip per owner (PSID)
            Flow {
                Layout.fillWidth: true
                visible: root.groups.length > 0
                spacing: 6
                StyledButton {
                    visible: root.groups.length > 1
                    chip: true
                    iconName: "users"
                    primary: root.group === ""
                    text: qsTr("Everyone")
                    onClicked: { root.group = ""; root.selected = ({}) }
                }
                Repeater {
                    model: root.groups
                    StyledButton {
                        required property var modelData
                        chip: true
                        iconName: "user"
                        primary: root.group === modelData.id
                        text: root.groupLabel(modelData) + "  " + modelData.count
                        Layout.maximumWidth: 280
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.psidFolder.length > 0
                                      ? (modelData.user.length > 0
                                         ? qsTr("PSID %1 · console user %2").arg(modelData.psidFolder)
                                               .arg(modelData.consoleName.length > 0 ? modelData.consoleName : modelData.user)
                                         : qsTr("PSID %1 · no user of %2 has it").arg(modelData.psidFolder).arg(saves.sourceName))
                                      : qsTr("Console user %1 · PSID unknown: link it to an Account ID to back up its saves.")
                                            .arg(modelData.consoleName.length > 0 ? modelData.consoleName : modelData.user)
                        onClicked: { root.group = modelData.id; root.selected = ({}) }
                    }
                }
                // The shown owner's console user and its link to an Account
                // ID: names it, and gives the PSID its saves go under.
                StyledButton {
                    readonly property var owner: root.groups.length === 1 ? root.groups[0] : root.shownGroup
                    visible: owner !== null && owner.user.length > 0
                    chip: true
                    iconName: "link"
                    text: owner === null ? "" : owner.linked ? qsTr("Change the link…")
                        : owner.psidFolder.length > 0 ? qsTr("Link…") : qsTr("Link to an Account ID…")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Which of the app's Account IDs this console user is")
                    onClicked: linkDialog.ask(owner)
                }
            }

            // ── filters and selection
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [
                        { id: "all", label: qsTr("All"), n: root.counts.all },
                        { id: "console", label: qsTr("Not backed up"), n: root.counts.console },
                        { id: "changed", label: qsTr("Changed"), n: root.counts.changed + root.counts.older },
                        { id: "vault", label: qsTr("Only on the PC"), n: root.counts.vault }
                    ]
                    StyledButton {
                        required property var modelData
                        chip: true
                        primary: root.filter === modelData.id
                        text: modelData.label + "  " + modelData.n
                        onClicked: { root.filter = modelData.id; root.selected = ({}) }
                    }
                }
                Item { Layout.fillWidth: true }
                StyledButton {
                    chip: true
                    iconName: root.selectedCount > 0 && root.selectedCount === root.shown.length ? "square" : "square-check"
                    text: root.selectedCount > 0 && root.selectedCount === root.shown.length
                          ? qsTr("Select none") : qsTr("Select all")
                    enabled: root.shown.length > 0
                    onClicked: root.selectedCount === root.shown.length ? root.selected = ({}) : root.selectAll()
                }
            }

            // ── the saves, by game
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: list
                    anchors.fill: parent
                    clip: true
                    spacing: 6
                    model: root.shown
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }

                    section.property: "titleId"
                    section.delegate: Item {
                        required property string section
                        readonly property var first: {
                            for (var i = 0; i < root.shown.length; ++i)
                                if (root.shown[i].titleId === section)
                                    return root.shown[i]
                            return null
                        }
                        width: list.width
                        height: 54
                        RowLayout {
                            anchors.fill: parent
                            anchors.topMargin: 10
                            anchors.rightMargin: 14
                            spacing: 12
                            Rectangle {
                                implicitWidth: 38
                                implicitHeight: 38
                                radius: 9
                                color: Theme.controlFill
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    source: parent.parent.parent.first ? parent.parent.parent.first.gameIcon : ""
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    visible: status === Image.Ready
                                }
                                Icon {
                                    anchors.centerIn: parent
                                    visible: parent.children[0].status !== Image.Ready
                                    name: "gamepad"
                                    size: 18
                                    color: Theme.textSecondary
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: parent.parent.first ? parent.parent.first.gameTitle : section
                                color: Theme.text
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: section
                                color: Theme.textMuted
                                font.pixelSize: 11
                            }
                        }
                    }

                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        readonly property bool picked: root.isSelected(modelData.key)
                        width: list.width - 14
                        height: 58
                        radius: 12
                        color: picked ? Theme.accentFill
                             : rowArea.containsMouse ? Theme.controlHover : Theme.panelAltFill
                        border.width: 1
                        border.color: picked ? Theme.alpha(Theme.accent, 0.6) : Theme.glassEdge
                        Behavior on color { ColorAnimation { duration: Theme.fast } }

                        MouseArea {
                            id: rowArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.toggle(row.modelData.key)
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 12

                            Icon {
                                name: row.picked ? "square-check" : "square"
                                size: 18
                                color: row.picked ? Theme.accent : Theme.textSecondary
                            }
                            Rectangle {
                                implicitWidth: 40
                                implicitHeight: 40
                                radius: 8
                                color: Theme.controlFill
                                clip: true
                                Image {
                                    id: saveIcon
                                    anchors.fill: parent
                                    // The save's own icon, or else the game's.
                                    source: row.modelData.icon.toString().length > 0 ? row.modelData.icon
                                                                                   : row.modelData.gameIcon
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                }
                                Icon {
                                    anchors.centerIn: parent
                                    visible: saveIcon.status !== Image.Ready
                                    name: "save"
                                    size: 18
                                    color: Theme.textSecondary
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: row.modelData.saveTitle
                                    color: Theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    // Whose it is: the name kept in the app, or the PSID.
                                    Row {
                                        readonly property string owner: root.ownerOf(row.modelData)
                                        readonly property bool named: row.modelData.psidName.length > 0
                                                                      || row.modelData.consoleUser.length > 0
                                        visible: owner.length > 0
                                        spacing: 4
                                        Icon {
                                            anchors.verticalCenter: parent.verticalCenter
                                            name: "user"
                                            size: 11
                                            color: parent.named ? Theme.accent : Theme.textMuted
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: parent.owner
                                            color: parent.named ? Theme.accent : Theme.textMuted
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        // What the game says about it (chapter, level…), then the size.
                                        text: (parent.children[0].visible ? "·  " : "")
                                              + [row.modelData.detail, row.modelData.size]
                                                .filter(function (t) { return t && t.length > 0 }).join("  ·  ")
                                        color: Theme.textSecondary
                                        font.pixelSize: 11
                                    }
                                }
                            }
                            // Where it stands.
                            Rectangle {
                                readonly property string sync: row.modelData.sync
                                readonly property color tone: sync === "same" ? Theme.ok
                                                            : sync === "changed" ? Theme.warn
                                                            : sync === "vault" || sync === "older" ? Theme.accent
                                                            : Theme.textSecondary
                                implicitWidth: badgeRow.implicitWidth + 18
                                implicitHeight: 26
                                radius: 13
                                color: Theme.alpha(tone, 0.14)
                                border.width: 1
                                border.color: Theme.alpha(tone, 0.4)
                                Row {
                                    id: badgeRow
                                    anchors.centerIn: parent
                                    spacing: 6
                                    Icon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: parent.parent.sync === "same" ? "check"
                                            : parent.parent.sync === "changed" ? "history"
                                            : parent.parent.sync === "older" ? "arrow-down"
                                            : parent.parent.sync === "vault" ? "archive" : "upload"
                                        size: 13
                                        color: parent.parent.tone
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: parent.parent.sync === "same" ? qsTr("On the PC")
                                            : parent.parent.sync === "changed" ? qsTr("Changed since the backup")
                                            : parent.parent.sync === "older" ? qsTr("The PC has a newer one")
                                            : parent.parent.sync === "vault" ? qsTr("Only on the PC")
                                            : qsTr("Not backed up")
                                        color: parent.parent.tone
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                }
                                ToolTip.visible: badgeHover.hovered && row.modelData.backedUpAt.length > 0
                                ToolTip.text: qsTr("Last backup: %1 (%n kept)", "", row.modelData.versions)
                                              .arg(row.modelData.backedUpAt)
                                HoverHandler { id: badgeHover }
                            }
                        }
                    }
                }

                // Empty, or reading.
                Column {
                    anchors.centerIn: parent
                    visible: root.shown.length === 0
                    spacing: 12
                    BusyIndicator {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: saves.busy
                        running: saves.busy
                    }
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: !saves.busy
                        name: "archive"
                        size: 44
                        color: Theme.alpha(Theme.textSecondary, 0.7)
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        text: saves.busy ? qsTr("Reading the console's saves…")
                            : root.filter !== "all" ? qsTr("Nothing here.")
                            : root.online ? qsTr("No saves on the console or on the PC.")
                            : qsTr("Connect to a console with FTP (GoldHEN, etaHEN) to see its saves.")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                }
            }

            // The one thing to know, said quietly.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Icon {
                    name: "info"
                    size: 13
                    color: Theme.textMuted
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: 11
                    text: qsTr("Saves are kept on the PC as the PS4 copies them to a USB drive: PS4/SAVEDATA/<PSID>/<game>. "
                               + "A save only goes to a console user with its PSID, on this console or another — a PS4 "
                               + "game's save works on a PS5 too.")
                }
            }
        }

    }

    FolderDialog {
        id: vaultFolderDialog
        title: qsTr("Folder for the saves on the PC")
        currentFolder: saves.vaultUrl
        onAccepted: saves.setVaultFolder(selectedFolder)
    }

    // Deleting is from the vault only. The PS4 keeps a database of its saves:
    // taking the files away over FTP would leave it with a broken entry.
    Dialog {
        id: confirmDelete
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 480
        modal: true
        padding: 0
        readonly property int inVault: root.countWhere(function (s) { return s.inVault })
        // Saves the vault is the only copy of.
        readonly property int lastCopies: root.countWhere(function (s) { return s.inVault && !s.onConsole })

        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }
        header: DialogHeader {
            title: qsTr("Delete %n save(s) from the PC?", "", confirmDelete.inVault)
            dialog: confirmDelete
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: 6
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 13
                text: qsTr("Their copies on this PC go, every backup kept. The consoles' saves are not "
                           + "touched: those are deleted on the console itself (Settings → Saved Data), so "
                           + "it stays in order.")
            }
            Text {
                visible: confirmDelete.lastCopies > 0
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                color: Theme.warn
                font.pixelSize: 12
                text: qsTr("%n of them are not on the console: this may be their only copy.", "",
                           confirmDelete.lastCopies)
            }
        }
        footer: Item {
            implicitHeight: Theme.dialogFooter
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 8
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancel")
                    onClicked: confirmDelete.close()
                }
                StyledButton {
                    text: qsTr("Delete from the PC")
                    iconName: "trash"
                    danger: true
                    solid: true
                    enabled: confirmDelete.inVault > 0
                    onClicked: { saves.removeFromVault(root.selectedKeys()); confirmDelete.close() }
                }
            }
        }
    }

    // Sending asks first: to which console, to whom there, and what it needs.
    Dialog {
        id: confirmSend
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 32)
        modal: true
        padding: 0
        property var keys: []
        property string address: ""
        readonly property var targetConsole: {
            var all = saves.consoles
            for (var i = 0; i < all.length; ++i)
                if (all[i].address === address)
                    return all[i]
            return null
        }
        readonly property string name: targetConsole !== null ? targetConsole.name : address
        readonly property bool here: address === saves.source
        // That console's users: the ones of the console shown are known; for
        // another, they are read when the dialog opens.
        readonly property var users: here ? saves.users
                                          : (saves.target.address === address ? saves.target.users : [])
        readonly property bool ready: here || (saves.target.address === address && saves.target.ready === true)
        readonly property bool reachable: here || (ready && saves.target.ok === true)
        // One line per owner of the saves sent: who gets them there.
        readonly property var owners: {
            var seen = {}
            var list = []
            for (var i = 0; i < saves.saves.length; ++i) {
                var s = saves.saves[i]
                if (keys.indexOf(s.key) < 0 || !s.inVault)
                    continue
                var id = s.psidFolder.length > 0 ? s.psidFolder : "user:" + s.account
                if (seen[id] === undefined) {
                    seen[id] = list.length
                    list.push({ psidFolder: s.psidFolder, name: root.ownerOf(s), count: 0, user: null })
                }
                var o = list[seen[id]]
                o.count++
                // Who gets it there: here, the user the save would go to;
                // elsewhere, the user with its PSID.
                if (o.user === null)
                    for (var u = 0; u < users.length; ++u) {
                        var candidate = users[u]
                        if (here ? candidate.user === s.account
                                 : s.psidFolder.length > 0 && candidate.psidFolder === s.psidFolder)
                            o.user = candidate
                    }
            }
            return list
        }
        readonly property int unplaced: {
            var n = 0
            for (var i = 0; i < owners.length; ++i)
                if (owners[i].user === null)
                    n += owners[i].count
            return n
        }
        // Users there whose PSID is not known: the ones a PSID can be given to.
        readonly property var freeUsers: {
            var list = []
            for (var i = 0; i < users.length; ++i)
                if (users[i].psidFolder.length === 0)
                    list.push(users[i])
            return list
        }
        readonly property int missing: {
            var n = 0
            for (var i = 0; i < saves.saves.length; ++i)
                if (keys.indexOf(saves.saves[i].key) >= 0 && saves.saves[i].inVault && (!here || !saves.saves[i].onConsole))
                    ++n
            return n
        }
        function userName(u) {
            if (u.name.length > 0)
                return u.name
            if (u.accountLabel !== undefined && u.accountLabel.length > 0)
                return u.accountLabel
            return qsTr("user %1").arg(u.user)
        }
        function ask(chosen, to) {
            keys = chosen
            address = to
            if (to !== saves.source)
                saves.inspectTarget(to)
            open()
        }

        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }
        header: DialogHeader {
            title: qsTr("Send %n save(s) to %1?", "", confirmSend.keys.length).arg(confirmSend.name)
            dialog: confirmSend
        }
        contentItem: ColumnLayout {
            spacing: 10
            // Where: the console, by name and kind.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                spacing: 10
                Icon {
                    name: "gamepad"
                    size: 16
                    color: Theme.accent
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: confirmSend.targetConsole !== null && root.typeLabel(confirmSend.targetConsole).length > 0
                          ? confirmSend.name + "  ·  " + root.typeLabel(confirmSend.targetConsole) : confirmSend.name
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                BusyIndicator {
                    visible: !confirmSend.ready
                    running: visible
                    implicitWidth: 22
                    implicitHeight: 22
                }
            }
            Text {
                visible: confirmSend.ready && !confirmSend.reachable
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                color: Theme.error
                font.pixelSize: 12
                text: qsTr("%1's FTP did not answer: %2").arg(confirmSend.name)
                      .arg(saves.target.error !== undefined ? saves.target.error : "")
            }
            // To whom: each owner's user there.
            Repeater {
                model: confirmSend.ready && confirmSend.reachable ? confirmSend.owners : []
                ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Icon {
                            name: parent.parent.modelData.user !== null ? "user" : "warning"
                            size: 14
                            color: parent.parent.modelData.user !== null ? Theme.textSecondary : Theme.warn
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            color: parent.parent.modelData.user !== null ? Theme.text : Theme.warn
                            text: parent.parent.modelData.user !== null
                                  ? qsTr("%1 (%n save(s)) → %2", "", parent.parent.modelData.count)
                                        .arg(parent.parent.modelData.name)
                                        .arg(confirmSend.userName(parent.parent.modelData.user))
                                  : parent.parent.modelData.psidFolder.length > 0
                                    ? qsTr("%1: no user of %2 has this PSID. Which one is it?")
                                          .arg(parent.parent.modelData.name).arg(confirmSend.name)
                                    : qsTr("%1: whose these are is not known; link the console user first.")
                                          .arg(parent.parent.modelData.name)
                        }
                    }
                    // Giving that PSID to a user there whose PSID is not known.
                    Flow {
                        visible: parent.modelData.user === null && parent.modelData.psidFolder.length > 0
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        spacing: 6
                        Repeater {
                            model: confirmSend.freeUsers
                            StyledButton {
                                required property var modelData
                                chip: true
                                iconName: "user"
                                text: confirmSend.userName(modelData)
                                onClicked: saves.linkPsid(modelData.user, parent.parent.modelData.psidFolder)
                            }
                        }
                        Text {
                            visible: confirmSend.freeUsers.length === 0
                            text: qsTr("Sign in with that account on the console once, then try again.")
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                    }
                }
            }
            Repeater {
                model: [
                    qsTr("The game must be closed (the console on its home screen): a save written while "
                         + "its game runs comes out corrupted."),
                    confirmSend.here
                    ? qsTr("Each replaces the console's copy with the latest backup, and is checked to have "
                           + "arrived whole.")
                    : qsTr("Each goes to the user with its PSID there, replacing that console's copy with the "
                           + "latest backup, and is checked to have arrived whole.")
                ]
                RowLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    spacing: 10
                    Icon {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                        name: "info"
                        size: 14
                        color: Theme.textSecondary
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        text: parent.modelData
                    }
                }
            }
            RowLayout {
                visible: confirmSend.targetConsole !== null && confirmSend.targetConsole.type === "ps5"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                spacing: 10
                Icon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    name: "info"
                    size: 14
                    color: Theme.accent
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: 12
                    text: qsTr("On a PS5 these are saves of the PS4 version of each game (CUSA). A game's PS5 "
                               + "version keeps saves of its own.")
                }
            }
            RowLayout {
                visible: confirmSend.missing > 0
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                spacing: 10
                Icon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    name: "archive"
                    size: 14
                    color: Theme.accent
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: 12
                    text: qsTr("Any of them the console does not list are added to its list of saves too, so "
                               + "it shows them. A copy of that list stays on this PC first.")
                }
            }
            Item { implicitHeight: 2 }
        }
        footer: Item {
            implicitHeight: Theme.dialogFooter
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 8
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Cancel")
                    onClicked: confirmSend.close()
                }
                StyledButton {
                    text: qsTr("The game is closed — send to %1").arg(confirmSend.name)
                    iconName: "upload"
                    primary: true
                    enabled: !saves.busy && confirmSend.ready && confirmSend.reachable
                             && confirmSend.unplaced < confirmSend.keys.length
                    onClicked: { saves.send(confirmSend.keys, confirmSend.address); confirmSend.close() }
                }
            }
        }
    }

    // The console whose saves are shown.
    Menu {
        id: sourceMenu
        topPadding: 8
        bottomPadding: 8
        background: Rectangle {
            implicitWidth: 280
            color: Theme.menuFill
            border.color: Theme.glassEdge
            border.width: 1
            radius: 14
        }
        Instantiator {
            model: saves.consoles
            delegate: StyledMenuItem {
                required property var modelData
                text: modelData.name + (root.typeLabel(modelData).length > 0 ? "  ·  " + root.typeLabel(modelData) : "")
                      + (modelData.ftp ? "" : "  ·  " + qsTr("no FTP"))
                iconName: modelData.source ? "check" : "gamepad"
                enabled: modelData.ftp || modelData.source
                onTriggered: saves.setSource(modelData.address)
            }
            onObjectAdded: (index, object) => sourceMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => sourceMenu.removeItem(object)
        }
    }

    // The console "Send to" sends to.
    Menu {
        id: targetMenu
        topPadding: 8
        bottomPadding: 8
        background: Rectangle {
            implicitWidth: 280
            color: Theme.menuFill
            border.color: Theme.glassEdge
            border.width: 1
            radius: 14
        }
        Instantiator {
            model: root.targets
            delegate: StyledMenuItem {
                required property var modelData
                text: qsTr("Send to %1").arg(modelData.name)
                      + (root.typeLabel(modelData).length > 0 ? "  ·  " + root.typeLabel(modelData) : "")
                iconName: root.sendConsole !== null && root.sendConsole.address === modelData.address ? "check" : "upload"
                onTriggered: {
                    root.sendTarget = modelData.address
                    root.askSend(root.selectedKeys(), modelData.address)
                }
            }
            onObjectAdded: (index, object) => targetMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => targetMenu.removeItem(object)
        }
    }

    // Which of the app's Account IDs a console user is.
    Dialog {
        id: linkDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 32)
        modal: true
        padding: 0
        property var user: null
        function ask(u) {
            user = u
            open()
        }

        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }
        header: DialogHeader {
            title: linkDialog.user ? qsTr("Console user %1").arg(linkDialog.user.consoleName !== undefined
                                                                 && linkDialog.user.consoleName.length > 0
                                                                 ? linkDialog.user.consoleName : linkDialog.user.user) : ""
            dialog: linkDialog
        }
        contentItem: ColumnLayout {
            spacing: 6
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                Layout.topMargin: 4
                Layout.bottomMargin: 4
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: 13
                text: app.accounts.length > 0
                      ? (root.waitingForLink.length > 0
                         ? qsTr("To back up this user's saves, say which Account ID it is: they are kept under its PSID, as the PS4 keeps them on a USB drive.")
                         : qsTr("Which Account ID is this user? Its saves take its name, and are kept under its PSID."))
                      : qsTr("No Account IDs kept in the app yet: add them under Settings → Account IDs.")
            }
            Repeater {
                model: app.accounts
                Rectangle {
                    required property var modelData
                    readonly property bool current: linkDialog.user !== null && linkDialog.user.linked
                                                    && linkDialog.user.name === modelData.label
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.dialogMargin
                    Layout.rightMargin: Theme.dialogMargin
                    implicitHeight: 44
                    radius: 10
                    color: current ? Theme.accentFill : pick.containsMouse ? Theme.controlHover : Theme.panelAltFill
                    border.width: 1
                    border.color: current ? Theme.alpha(Theme.accent, 0.6) : Theme.glassEdge
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 10
                        Icon {
                            name: parent.parent.current ? "check" : "user"
                            size: 15
                            color: parent.parent.current ? Theme.accent : Theme.textSecondary
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: parent.parent.modelData.label
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }
                    }
                    MouseArea {
                        id: pick
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            saves.linkAccount(linkDialog.user.user, parent.modelData.accountId)
                            linkDialog.close()
                        }
                    }
                }
            }
            Item { implicitHeight: 4 }
        }
        footer: Item {
            implicitHeight: Theme.dialogFooter
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.dialogInner
                anchors.leftMargin: Theme.dialogMargin
                anchors.rightMargin: Theme.dialogMargin
                spacing: 8
                StyledButton {
                    visible: linkDialog.user !== null && linkDialog.user.linked
                    text: qsTr("Unlink")
                    onClicked: { saves.linkAccount(linkDialog.user.user, ""); linkDialog.close() }
                }
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: qsTr("Close")
                    onClicked: linkDialog.close()
                }
            }
        }
    }
}
