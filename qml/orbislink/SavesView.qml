// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The save vault: every save on the console next to the copies kept on
// this PC. One click backs up everything new or changed; another puts back
// what the console lost. The backups are laid out as the PS4 copies saves
// to a USB drive (PS4/SAVEDATA/<PSID>/<game>).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    signal back()

    readonly property bool online: app.canUseFtp
    // "all", "console" (not backed up), "changed", "vault" (missing on the console).
    property string filter: "all"
    property var selected: ({})
    // The console user shown (its folder under /user/home): "" every one.
    property string account: ""
    function inAccount(s) {
        return account === "" || s.account === account
    }
    // Each console user found in the saves: its name (the Account ID linked
    // to it, or the one its saves name), and its PSID as the PS4 names its
    // USB folders, which the USB copies need.
    readonly property var accounts: {
        var seen = {}
        var list = []
        for (var i = 0; i < saves.saves.length; ++i) {
            var s = saves.saves[i]
            if (seen[s.account] === undefined) {
                seen[s.account] = list.length
                list.push({ user: s.account, name: "", psidFolder: "", linked: false, count: 0 })
            }
            var e = list[seen[s.account]]
            e.count++
            if (e.name.length === 0)
                e.name = s.userName.length > 0 ? s.userName : s.psidName
            if (e.psidFolder.length === 0)
                e.psidFolder = s.psidFolder
            if (s.linked)
                e.linked = true
        }
        return list
    }
    function userLabel(e) {
        return e.name.length > 0 ? e.name : qsTr("User %1").arg(e.user)
    }
    readonly property var shownUser: {
        for (var i = 0; i < accounts.length; ++i)
            if (accounts[i].user === account)
                return accounts[i]
        return null
    }
    function accountLabel(psid, name) {
        if (psid === "-" || psid.length === 0)
            return qsTr("No account")
        return name.length > 0 ? name : psid
    }
    readonly property int selectedCount: Object.keys(selected).length

    readonly property var counts: {
        var c = { all: 0, same: 0, changed: 0, console: 0, vault: 0 }
        for (var i = 0; i < saves.saves.length; ++i) {
            if (!root.inAccount(saves.saves[i]))
                continue
            c.all++
            c[saves.saves[i].sync]++
        }
        return c
    }
    // What "Back up everything" would copy.
    readonly property int pending: counts.changed + counts.console

    readonly property var shown: {
        var list = []
        for (var i = 0; i < saves.saves.length; ++i) {
            var s = saves.saves[i]
            if ((filter === "all" || s.sync === filter) && inAccount(s))
                list.push(s)
        }
        list.sort(function (a, b) {
            if (a.account !== b.account) return a.account < b.account ? -1 : 1
            if (a.gameTitle !== b.gameTitle) return a.gameTitle.localeCompare(b.gameTitle)
            return a.saveTitle.localeCompare(b.saveTitle)
        })
        return list
    }
    readonly property bool manyAccounts: {
        var seen = {}
        for (var i = 0; i < saves.saves.length; ++i)
            seen[saves.saves[i].account] = true
        return Object.keys(seen).length > 1
    }

    // The game open on the console (its title ID, from Remote Play's
    // discovery): its saves are not copied either way while it runs, or the
    // save comes out corrupted.
    readonly property string openGame: typeof stream !== "undefined" && stream !== null
                                       ? stream.runningAppTitleId : ""
    readonly property string openGameName: typeof stream !== "undefined" && stream !== null
                                           ? stream.runningApp : ""
    // The keys, without the open game's saves; says so when it left some out.
    function withoutOpenGame(keys) {
        if (openGame.length === 0)
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
    // user to be linked to an Account ID; the rest go now.
    property var waitingForLink: []
    function backupKeys(keys) {
        var kept = withoutOpenGame(keys)
        var ready = []
        var waiting = []
        var user = null
        for (var i = 0; i < saves.saves.length; ++i) {
            var s = saves.saves[i]
            if (kept.indexOf(s.key) < 0)
                continue
            if (s.psidFolder.length > 0) {
                ready.push(s.key)
            } else {
                waiting.push(s.key)
                if (user === null)
                    for (var a = 0; a < accounts.length; ++a)
                        if (accounts[a].user === s.account)
                            user = accounts[a]
            }
        }
        if (ready.length > 0)
            saves.backup(ready)
        if (waiting.length > 0 && user !== null) {
            waitingForLink = waiting
            linkDialog.ask(user)
        }
    }

    // Putting back always asks first.
    function askRestore(keys) {
        var kept = withoutOpenGame(keys)
        if (kept.length > 0)
            confirmRestore.ask(kept)
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

    onVisibleChanged: if (visible && !saves.busy) saves.refresh()
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
            for (var i = 0; i < saves.saves.length; ++i)
                if (root.waitingForLink.indexOf(saves.saves[i].key) >= 0 && saves.saves[i].psidFolder.length > 0)
                    keys.push(saves.saves[i].key)
            root.waitingForLink = []
            if (keys.length > 0)
                saves.backup(keys)
        }
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
                            : qsTr("%1 on the console · %2 in the vault")
                                .arg(root.counts.all - root.counts.vault)
                                .arg(root.counts.same + root.counts.changed + root.counts.vault)
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                }
                Item { Layout.fillWidth: true }
                StyledButton {
                    iconName: "folder-open"
                    chip: true
                    text: saves.vaultFolder
                    Layout.maximumWidth: 300
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Open the vault folder (right click to change it)")
                    onClicked: saves.openVaultFolder()
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: vaultFolderDialog.open()
                    }
                }
                StyledToolButton {
                    iconName: "settings"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Change the vault folder")
                    onClicked: vaultFolderDialog.open()
                }
                StyledToolButton {
                    iconName: saves.busy ? "loader" : "refresh"
                    enabled: !saves.busy
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Read the console again")
                    onClicked: saves.refresh()
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
                        text: qsTr("Back up")
                        enabled: root.online && root.countWhere(function (s) { return s.onConsole }) > 0
                        onClicked: root.backupKeys(root.selectedKeys())
                    }
                    StyledButton {
                        visible: root.selectedCount > 0 && !saves.busy
                        iconName: "archive-restore"
                        text: qsTr("Put back")
                        enabled: root.online && root.countWhere(function (s) { return s.inVault }) > 0
                        onClicked: root.askRestore(root.selectedKeys())
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
                            text: !root.online ? qsTr("The console's FTP is not answering: this is what the vault holds.")
                                : saves.busy ? saves.status
                                : root.pending > 0 ? qsTr("%n save(s) on the console not backed up yet.", "", root.pending)
                                : root.counts.all > 0 ? qsTr("Everything on the console is in the vault.")
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
                            visible: !saves.busy && root.openGame.length > 0
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: qsTr("%1 is open: its saves wait until it closes.")
                                  .arg(root.openGameName.length > 0 ? root.openGameName : root.openGame)
                            color: Theme.warn
                            font.pixelSize: 12
                        }
                        Text {
                            visible: !saves.busy && root.counts.vault > 0 && root.online
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: qsTr("%n save(s) the console no longer has are kept here.", "", root.counts.vault)
                            color: Theme.textSecondary
                            font.pixelSize: 12
                        }
                    }
                    StyledButton {
                        visible: root.online && root.counts.vault > 0 && root.selectedCount === 0
                        text: qsTr("Put back what's missing")
                        iconName: "archive-restore"
                        enabled: !saves.busy
                        onClicked: {
                            var keys = []
                            for (var i = 0; i < saves.saves.length; ++i)
                                if (saves.saves[i].sync === "vault" && root.inAccount(saves.saves[i]))
                                    keys.push(saves.saves[i].key)
                            root.askRestore(keys)
                        }
                    }
                    StyledButton {
                        visible: root.selectedCount === 0 || saves.busy
                        text: qsTr("Back up everything")
                        iconName: "download"
                        primary: true
                        enabled: root.online && !saves.busy && root.pending > 0
                        // Everything, or everything of the account shown.
                        onClicked: {
                            var keys = []
                            for (var i = 0; i < saves.saves.length; ++i) {
                                var s = saves.saves[i]
                                if (s.onConsole && s.sync !== "same" && root.inAccount(s))
                                    keys.push(s.key)
                            }
                            root.backupKeys(keys)
                        }
                    }
                }
            }

            // ── whose saves: one chip per console user
            Flow {
                Layout.fillWidth: true
                visible: root.accounts.length > 0
                spacing: 6
                StyledButton {
                    visible: root.accounts.length > 1
                    chip: true
                    iconName: "users"
                    primary: root.account === ""
                    text: qsTr("All users")
                    onClicked: { root.account = ""; root.selected = ({}) }
                }
                Repeater {
                    model: root.accounts
                    StyledButton {
                        required property var modelData
                        chip: true
                        iconName: "user"
                        primary: root.account === modelData.user
                        text: root.userLabel(modelData) + "  " + modelData.count
                        Layout.maximumWidth: 280
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.psidFolder.length > 0
                                      ? qsTr("Console user %1 · PSID %2").arg(modelData.user).arg(modelData.psidFolder)
                                      : qsTr("Console user %1 · PSID unknown: link it to an Account ID to back up its saves.")
                                            .arg(modelData.user)
                        onClicked: { root.account = modelData.user; root.selected = ({}) }
                    }
                }
                // The shown user's link to an Account ID: names it, and gives
                // the PSID its USB copies go under.
                StyledButton {
                    readonly property var user: root.accounts.length === 1 ? root.accounts[0] : root.shownUser
                    visible: user !== null
                    chip: true
                    iconName: "link"
                    text: user === null ? "" : user.linked ? qsTr("Change the link…")
                        : user.psidFolder.length > 0 ? qsTr("Link…") : qsTr("Link to an Account ID…")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Which of the app's Account IDs this console user is")
                    onClicked: linkDialog.ask(user)
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
                        { id: "changed", label: qsTr("Changed"), n: root.counts.changed },
                        { id: "vault", label: qsTr("Only on this PC"), n: root.counts.vault }
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
                                text: (root.manyAccounts && parent.parent.first
                                       ? qsTr("Account %1").arg(parent.parent.first.account) + "  ·  " : "") + section
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
                                        readonly property string owner: row.modelData.psid.length > 0
                                            ? root.accountLabel(row.modelData.psid, row.modelData.psidName)
                                            : row.modelData.userName
                                        readonly property bool named: row.modelData.psidName.length > 0
                                                                      || (row.modelData.psid.length === 0 && row.modelData.userName.length > 0)
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
                                                            : sync === "vault" ? Theme.accent : Theme.textSecondary
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
                                            : parent.parent.sync === "vault" ? "archive" : "upload"
                                        size: 13
                                        color: parent.parent.tone
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: parent.parent.sync === "same" ? qsTr("In the vault")
                                            : parent.parent.sync === "changed" ? qsTr("Changed since the backup")
                                            : parent.parent.sync === "vault" ? qsTr("Only on this PC")
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
                            : root.online ? qsTr("No saves found on the console or in the vault.")
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
                    text: qsTr("Backups are kept as the PS4 copies saves to a USB drive: PS4/SAVEDATA/<PSID>/<game>, "
                               + "in the vault folder. A save only goes back to the console user it came from.")
                }
            }
        }

    }

    FolderDialog {
        id: vaultFolderDialog
        title: qsTr("Folder for the save vault")
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
            title: qsTr("Delete %n save(s) from the vault?", "", confirmDelete.inVault)
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
                text: qsTr("Their copies on this PC go, every backup kept. The console's saves are not "
                           + "touched: those are deleted on the PS4 itself (Settings → Application Saved "
                           + "Data Management), so it stays in order.")
            }
            Text {
                visible: confirmDelete.lastCopies > 0
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                wrapMode: Text.WordWrap
                color: Theme.warn
                font.pixelSize: 12
                text: qsTr("%n of them are no longer on the console: this is their only copy.", "",
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
                    text: qsTr("Delete from the vault")
                    iconName: "trash"
                    danger: true
                    solid: true
                    enabled: confirmDelete.inVault > 0
                    onClicked: { saves.removeFromVault(root.selectedKeys()); confirmDelete.close() }
                }
            }
        }
    }

    // Putting back asks first: what it does, and what it needs.
    Dialog {
        id: confirmRestore
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 32)
        modal: true
        padding: 0
        property var keys: []
        // Of those, how many the console no longer lists, and how many have
        // no console user to go back to.
        property int missing: 0
        property int noUser: 0
        function ask(chosen) {
            keys = chosen
            var gone = 0
            var unknown = 0
            for (var i = 0; i < saves.saves.length; ++i) {
                var s = saves.saves[i]
                if (chosen.indexOf(s.key) < 0)
                    continue
                if (!s.onConsole)
                    ++gone
                if (s.account.length === 0)
                    ++unknown
            }
            missing = gone
            noUser = unknown
            open()
        }

        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle {
            color: Theme.dialogFill
            border.color: Theme.border
            radius: Theme.radiusDialog
        }
        header: DialogHeader {
            title: qsTr("Put back %n save(s)?", "", confirmRestore.keys.length)
            dialog: confirmRestore
        }
        contentItem: ColumnLayout {
            spacing: 10
            Repeater {
                model: [
                    qsTr("The game must be closed (the console on its home screen): a save written while "
                         + "its game runs comes out corrupted."),
                    qsTr("Each goes back to the console user it came from, replacing the console's copy "
                         + "with the latest backup, and is checked to have arrived whole.")
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
                visible: confirmRestore.missing > 0
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
                    text: qsTr("%n of them the console no longer has: they are added back to its list of saves "
                               + "too, so it shows them again. A copy of that list stays on this PC first.", "",
                               confirmRestore.missing)
                }
            }
            RowLayout {
                visible: confirmRestore.noUser > 0
                Layout.fillWidth: true
                Layout.leftMargin: Theme.dialogMargin
                Layout.rightMargin: Theme.dialogMargin
                spacing: 10
                Icon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                    name: "warning"
                    size: 14
                    color: Theme.warn
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.warn
                    font.pixelSize: 12
                    text: qsTr("%n of them have no console user yet: link their PSID to a console user first "
                               + "(the user chips at the top).", "", confirmRestore.noUser)
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
                    onClicked: confirmRestore.close()
                }
                StyledButton {
                    text: qsTr("The game is closed — put back")
                    iconName: "archive-restore"
                    primary: true
                    onClicked: { saves.restore(confirmRestore.keys); confirmRestore.close() }
                }
            }
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
            title: linkDialog.user ? qsTr("Console user %1").arg(linkDialog.user.user) : ""
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
