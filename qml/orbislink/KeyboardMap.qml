// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The keyboard map drawn as a keyboard: each key that does something shows
// its letter and, inside the key itself, the PlayStation button it presses
// (P carries "PS", V carries △…). The other keys are dimmed, just so the
// drawing is recognisable as a keyboard.
//
// The keys come from the stream (stream.keyBindings), so the drawing always
// shows the map in use. In edit mode, clicking a key picks its action;
// then pressing a key on the keyboard (or clicking another one on the
// drawing) moves it there.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: keyMap

    // The button under the mouse, for the controller drawing to light up.
    property string highlight: chosen.length > 0 ? actions[chosen].target
                             : padChosen.length > 0 ? actions[padChosen].target : hoverHighlight
    property string hoverHighlight: ""
    property string description: ""

    property bool editing: false
    // The action waiting for a new key ("" when none).
    property string chosen: ""
    // The action waiting for a button on the physical controller.
    property string padChosen: ""
    property string notice: ""

    // Live: the keys held on the keyboard, and the actions held on the
    // controller. Both light up their keys and the controller drawing.
    property var pressedKeys: ({})
    readonly property var padActions: hasStream ? stream.padPressed : []
    readonly property var litActions: {
        var list = padActions.slice()
        for (var code in pressedKeys) {
            var action = byKey[code]
            if (action !== undefined && list.indexOf(action) < 0)
                list.push(action)
        }
        return list
    }
    // The controller parts those actions light.
    readonly property var litTargets: {
        var list = []
        for (var i = 0; i < litActions.length; ++i) {
            var known = actions[litActions[i]]
            if (known && list.indexOf(known.target) < 0)
                list.push(known.target)
        }
        return list
    }
    // Physical button → the PS button printed on it, to name it.
    readonly property var padDefaults: ({
        "a": "cross", "b": "circle", "x": "square", "y": "triangle", "dpup": "dpad_up",
        "dpdown": "dpad_down", "dpleft": "dpad_left", "dpright": "dpad_right", "leftshoulder": "l1",
        "rightshoulder": "r1", "leftstick": "l3", "rightstick": "r3", "start": "options",
        "back": "share", "guide": "ps", "touchpad": "touchpad"
    })
    // What was changed on the controller, as "◯ → ✕   ·   L1 → R1".
    readonly property string padChanges: {
        if (!hasStream)
            return ""
        var lines = []
        var map = stream.padBindings
        for (var physical in map) {
            var printed = padDefaults[physical]
            if (printed && map[physical] !== printed && actions[printed] && actions[map[physical]])
                lines.push(actions[printed].sign + " → " + actions[map[physical]].sign)
        }
        return lines.join("   ·   ")
    }

    function choosePad(action) {
        notice = ""
        chosen = ""
        padChosen = padChosen === action ? "" : action
        forceActiveFocus()
    }

    Connections {
        target: keyMap.hasStream ? stream : null
        function onPadButtonDown(physical) {
            if (!keyMap.editing || keyMap.padChosen.length === 0)
                return
            stream.setPadBinding(keyMap.padChosen, physical)
            keyMap.notice = qsTr("Done: that controller button now presses %1.")
                .arg(keyMap.actions[keyMap.padChosen].sign)
            keyMap.padChosen = ""
        }
    }

    readonly property bool hasStream: typeof stream !== "undefined" && stream !== null
    readonly property var bindings: hasStream ? stream.keyBindings : ({})

    // key → action, the reverse of what comes from the stream.
    readonly property var byKey: {
        var inverse = {}
        for (var action in bindings)
            inverse[bindings[action]] = action
        return inverse
    }

    // The text of the box under the controller.
    readonly property string message: {
        if (chosen.length > 0)
            return qsTr("Changing %1: press the new key, or click a key in the drawing. Esc "
                        + "cancels.").arg(actions[chosen].name)
        if (padChosen.length > 0)
            return qsTr("Changing %1 on the controller: press the controller button that should do "
                        + "it. Esc cancels.").arg(actions[padChosen].name)
        if (notice.length > 0)
            return notice
        return description
    }

    readonly property color crossColor: "#7CB2E8"
    readonly property color circleColor: "#FF6B6B"
    readonly property color squareColor: "#E88BD6"
    readonly property color triangleColor: "#40E0B0"

    // What each action shows inside the key. "target" is the part of the
    // controller that lights up; "kind" says how the sign is drawn.
    readonly property var actions: ({
        "cross":        { sign: "✕", kind: "symbol", tone: crossColor, target: "cross", name: qsTr("Cross") },
        "circle":       { sign: "◯", kind: "symbol", tone: circleColor, target: "circle", name: qsTr("Circle") },
        "square":       { sign: "▢", kind: "symbol", tone: squareColor, target: "square", name: qsTr("Square") },
        "triangle":     { sign: "△", kind: "symbol", tone: triangleColor, target: "triangle", name: qsTr("Triangle") },
        "dpad_up":      { sign: "✚↑", kind: "button", target: "dpad", name: qsTr("D-pad up") },
        "dpad_down":    { sign: "✚↓", kind: "button", target: "dpad", name: qsTr("D-pad down") },
        "dpad_left":    { sign: "✚←", kind: "button", target: "dpad", name: qsTr("D-pad left") },
        "dpad_right":   { sign: "✚→", kind: "button", target: "dpad", name: qsTr("D-pad right") },
        "l1":           { sign: "L1", kind: "button", target: "l1", name: qsTr("L1") },
        "l2":           { sign: "L2", kind: "button", target: "l2", name: qsTr("L2 (trigger)") },
        "r1":           { sign: "R1", kind: "button", target: "r1", name: qsTr("R1") },
        "r2":           { sign: "R2", kind: "button", target: "r2", name: qsTr("R2 (trigger)") },
        "l3":           { sign: "L3", kind: "button", target: "lstick", name: qsTr("L3 (press the left stick)") },
        "r3":           { sign: "R3", kind: "button", target: "rstick", name: qsTr("R3 (press the right stick)") },
        "lstick_up":    { sign: "L↑", kind: "button", target: "lstick", name: qsTr("left stick up") },
        "lstick_left":  { sign: "L←", kind: "button", target: "lstick", name: qsTr("left stick left") },
        "lstick_down":  { sign: "L↓", kind: "button", target: "lstick", name: qsTr("left stick down") },
        "lstick_right": { sign: "L→", kind: "button", target: "lstick", name: qsTr("left stick right") },
        "rstick_up":    { sign: "R↑", kind: "button", target: "rstick", name: qsTr("right stick up") },
        "rstick_left":  { sign: "R←", kind: "button", target: "rstick", name: qsTr("right stick left") },
        "rstick_down":  { sign: "R↓", kind: "button", target: "rstick", name: qsTr("right stick down") },
        "rstick_right": { sign: "R→", kind: "button", target: "rstick", name: qsTr("right stick right") },
        "options":      { sign: "OPT", kind: "button", target: "options", name: qsTr("Options") },
        "share":        { sign: "SHR", kind: "button", target: "share", name: qsTr("Share") },
        "touchpad":     { sign: "PAD", kind: "button", target: "touchpad", name: qsTr("press the touchpad") },
        "ps":           { sign: "PS", kind: "ps", target: "ps", name: qsTr("PS button") }
    })

    // The keys, in units of one key: [label, code, x, row, width].
    // A real keyboard, reduced to the five rows that matter and the arrows.
    readonly property var keys: [
        ["Esc", Qt.Key_Escape, 0, 0, 1],
        ["1", Qt.Key_1, 1.5, 0, 1], ["2", Qt.Key_2, 2.5, 0, 1], ["3", Qt.Key_3, 3.5, 0, 1],
        ["4", Qt.Key_4, 4.5, 0, 1], ["5", Qt.Key_5, 5.5, 0, 1], ["6", Qt.Key_6, 6.5, 0, 1],
        ["7", Qt.Key_7, 7.5, 0, 1], ["8", Qt.Key_8, 8.5, 0, 1], ["9", Qt.Key_9, 9.5, 0, 1],
        ["0", Qt.Key_0, 10.5, 0, 1], ["⌫", Qt.Key_Backspace, 11.5, 0, 2],
        ["Tab", Qt.Key_Tab, 0, 1, 1.5],
        ["Q", Qt.Key_Q, 1.5, 1, 1], ["W", Qt.Key_W, 2.5, 1, 1], ["E", Qt.Key_E, 3.5, 1, 1],
        ["R", Qt.Key_R, 4.5, 1, 1], ["T", Qt.Key_T, 5.5, 1, 1], ["Y", Qt.Key_Y, 6.5, 1, 1],
        ["U", Qt.Key_U, 7.5, 1, 1], ["I", Qt.Key_I, 8.5, 1, 1], ["O", Qt.Key_O, 9.5, 1, 1],
        ["P", Qt.Key_P, 10.5, 1, 1],
        ["Caps", Qt.Key_CapsLock, 0, 2, 1.75],
        ["A", Qt.Key_A, 1.75, 2, 1], ["S", Qt.Key_S, 2.75, 2, 1], ["D", Qt.Key_D, 3.75, 2, 1],
        ["F", Qt.Key_F, 4.75, 2, 1], ["G", Qt.Key_G, 5.75, 2, 1], ["H", Qt.Key_H, 6.75, 2, 1],
        ["J", Qt.Key_J, 7.75, 2, 1], ["K", Qt.Key_K, 8.75, 2, 1], ["L", Qt.Key_L, 9.75, 2, 1],
        ["Enter", Qt.Key_Return, 10.75, 2, 2.75],
        ["Shift", Qt.Key_Shift, 0, 3, 2.25],
        ["Z", Qt.Key_Z, 2.25, 3, 1], ["X", Qt.Key_X, 3.25, 3, 1], ["C", Qt.Key_C, 4.25, 3, 1],
        ["V", Qt.Key_V, 5.25, 3, 1], ["B", Qt.Key_B, 6.25, 3, 1], ["N", Qt.Key_N, 7.25, 3, 1],
        ["M", Qt.Key_M, 8.25, 3, 1],
        ["Ctrl", Qt.Key_Control, 0, 4, 1.5], ["Alt", Qt.Key_Alt, 1.5, 4, 1.25],
        [qsTr("Space"), Qt.Key_Space, 2.75, 4, 6.5], ["AltGr", Qt.Key_AltGr, 9.25, 4, 1.25],
        ["↑", Qt.Key_Up, 15, 3, 1],
        ["←", Qt.Key_Left, 14, 4, 1], ["↓", Qt.Key_Down, 15, 4, 1], ["→", Qt.Key_Right, 16, 4, 1]
    ]

    // Actions whose keys are not on the drawing (F1, numeric keypad…):
    // they are written below, so no action disappears from the map.
    readonly property string offDrawing: {
        var drawn = {}
        for (var i = 0; i < keys.length; ++i)
            drawn[keys[i][1]] = true
        var lines = []
        for (var action in bindings) {
            var code = bindings[action]
            if (!drawn[code] && actions[action])
                lines.push((hasStream ? stream.keyName(code) : code) + " → "
                            + actions[action].sign + " " + actions[action].name)
        }
        return lines.join("   ·   ")
    }

    // 17 units wide; each unit shrinks if the window is narrow.
    readonly property real unit: Math.min(40, width / 17)
    readonly property real slack: Math.max(3, unit * 0.1)

    implicitWidth: 17 * 40
    implicitHeight: 5 * unit + (offDrawing.length > 0 ? 22 : 0)

    function assign(code) {
        if (!hasStream || chosen.length === 0)
            return
        if (code === Qt.Key_Escape) {
            chosen = ""
            return
        }
        if (stream.setKeyBinding(chosen, code)) {
            notice = ""
            chosen = ""
        } else {
            notice = qsTr("That key can't be used: Esc leaves the stream and F11 toggles full "
                         + "screen.")
            chosen = ""
        }
    }

    onEditingChanged: { chosen = ""; padChosen = ""; notice = "" }
    onChosenChanged: if (chosen.length > 0) forceActiveFocus()

    // The new key comes from here. The event is accepted so the dialog does
    // not use it (Enter does not press a button, Esc does not close the window).
    Keys.onPressed: function (event) {
        if (editing && padChosen.length > 0 && event.key === Qt.Key_Escape) {
            padChosen = ""
            event.accepted = true
            return
        }
        if (!editing || chosen.length === 0) {
            // Not choosing: the key just lights up while it is held.
            if (!event.isAutoRepeat && event.key !== Qt.Key_Escape) {
                var held = Object.assign({}, pressedKeys)
                held[event.key] = true
                pressedKeys = held
            }
            return
        }
        if (event.isAutoRepeat) {
            event.accepted = true
            return
        }
        assign(event.key)
        event.accepted = true
    }

    Keys.onReleased: function (event) {
        if (event.isAutoRepeat || pressedKeys[event.key] === undefined)
            return
        var held = Object.assign({}, pressedKeys)
        delete held[event.key]
        pressedKeys = held
    }
    onActiveFocusChanged: if (!activeFocus) pressedKeys = ({})

    Repeater {
        model: keyMap.keys

        Rectangle {
            id: keyCap
            readonly property string keyLabel: modelData[0]
            readonly property int code: modelData[1]
            readonly property bool fixed: code === Qt.Key_Escape
            readonly property string action: keyMap.byKey[code] !== undefined
                                           ? keyMap.byKey[code] : ""
            readonly property var binding: fixed
                ? { sign: qsTr("exit"), kind: "notice", target: "", name: qsTr("leaves the stream") }
                : (action.length > 0 ? keyMap.actions[action] : undefined)
            readonly property bool useful: binding !== undefined
            readonly property bool chosenHere: action.length > 0 && keyMap.chosen === action
            // Held right now: on the keyboard, or its action on the controller.
            readonly property bool held: keyMap.pressedKeys[code] === true
                                         || (action.length > 0 && keyMap.padActions.indexOf(action) >= 0)
            // In edit mode, any key on the drawing responds to the mouse: it is
            // a possible destination for the chosen action.
            readonly property bool hover: area.containsMouse
                                          && (useful || (keyMap.editing && keyMap.chosen.length > 0))

            x: modelData[2] * keyMap.unit
            y: modelData[3] * keyMap.unit
            width: modelData[4] * keyMap.unit - keyMap.slack
            height: keyMap.unit - keyMap.slack
            radius: Math.round(keyMap.unit * 0.2)

            // On the light theme the panel is white, like the dialog: the useful
            // keys get an icy grey and a dark edge, otherwise the dimmed ones
            // would be the ones standing out.
            color: held ? Theme.alpha(Theme.accent, 0.42)
                 : chosenHere || hover ? Theme.accentFill
                 : !useful ? "transparent"
                 : Theme.light ? "#EEF1F6"
                 : Qt.rgba(Theme.panelAlt.r, Theme.panelAlt.g, Theme.panelAlt.b, 0.9)
            border.width: chosenHere || held ? 2 : 1
            border.color: chosenHere || hover || held ? Theme.accent
                        : !useful ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.10)
                        : Theme.light ? Qt.rgba(0, 0, 0, 0.22)
                        : Theme.glassEdge
            scale: held ? 0.94 : hover || chosenHere ? 1.06 : 1.0
            z: hover || chosenHere || held ? 1 : 0
            Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Theme.easeOut } }
            Behavior on color { ColorAnimation { duration: Theme.fast } }

            // The key waiting blinks slowly, to show which one is changing.
            SequentialAnimation on opacity {
                running: keyCap.chosenHere
                loops: Animation.Infinite
                onStopped: keyCap.opacity = 1
                NumberAnimation { to: 0.55; duration: 520; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 520; easing.type: Easing.InOutSine }
            }

            // The key as printed on the keyboard: top left.
            Text {
                x: 5
                y: 3
                text: keyCap.keyLabel
                color: keyCap.useful ? Theme.text
                                  : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.28)
                font.pixelSize: Math.max(8, Math.round(keyMap.unit * (keyCap.keyLabel.length > 1 ? 0.24 : 0.3)))
                font.bold: keyCap.useful
            }

            // And the PlayStation button, inside the same key, bottom
            // right. This is what you look for when looking at the map.
            Rectangle {
                visible: keyCap.useful
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 3
                readonly property string kind: keyCap.useful ? keyCap.binding.kind : ""
                width: Math.max(height, sign.implicitWidth + 6)
                height: Math.round(keyMap.unit * 0.42)
                radius: height / 2
                color: kind === "ps" ? Theme.accent
                     : kind === "notice" ? Qt.rgba(Theme.warn.r, Theme.warn.g, Theme.warn.b, 0.20)
                     : kind === "symbol" ? "transparent"
                     : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.10)
                Text {
                    id: sign
                    anchors.centerIn: parent
                    text: keyCap.useful ? keyCap.binding.sign : ""
                    color: parent.kind === "ps" ? "#FFFFFF"
                         : parent.kind === "symbol" ? keyCap.binding.tone
                         : parent.kind === "notice" ? Theme.warn
                         : Theme.text
                    font.pixelSize: parent.kind === "symbol"
                                    ? Math.round(keyMap.unit * 0.36)
                                    : Math.max(7, Math.round(keyMap.unit * 0.22))
                    font.bold: true
                }
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: keyMap.editing && (keyCap.useful || keyMap.chosen.length > 0)
                             && !keyCap.fixed ? Qt.PointingHandCursor : Qt.ArrowCursor
                onContainsMouseChanged: {
                    if (containsMouse && keyCap.useful) {
                        keyMap.hoverHighlight = keyCap.binding.target
                        keyMap.description = keyCap.keyLabel + " — " + keyCap.binding.name
                    } else if (!containsMouse && keyCap.useful
                               && keyMap.description === keyCap.keyLabel + " — " + keyCap.binding.name) {
                        keyMap.hoverHighlight = ""
                        keyMap.description = ""
                    }
                }
                onClicked: {
                    if (!keyMap.editing)
                        return
                    if (keyMap.chosen.length > 0) {
                        if (keyCap.chosenHere)
                            keyMap.chosen = ""
                        else
                            keyMap.assign(keyCap.code)
                        return
                    }
                    if (keyCap.action.length > 0) {
                        keyMap.notice = ""
                        keyMap.chosen = keyCap.action
                    }
                }
            }
        }
    }

    Text {
        visible: keyMap.offDrawing.length > 0
        y: 5 * keyMap.unit + 6
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        text: qsTr("Not in the drawing: %1").arg(keyMap.offDrawing)
        color: Theme.textSecondary
        font.pixelSize: 11
    }
}
