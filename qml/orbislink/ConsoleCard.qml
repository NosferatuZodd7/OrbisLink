// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The console found on the network, in a card you click.
//
// One click connects: the app asks the console how it is, wakes it if it is
// in rest mode, waits for it to be ready and connects — or opens registration,
// if this PC is not registered on it yet. While that is going on, another
// click cancels. What is happening is written on the bottom button.
//
// The design is 360×330 and scales as a whole (`scaleFactor`) when there are
// several consoles side by side: that way the proportions never change.
// Without QtQuick.Effects (the screenshots' Qt 6.4 lacks it), the glass,
// glow and shadow are layers and gradients drawn here.
//
// A console of unknown type gets a different card, in a cartoon style:
// flat colour, thick outline and a hard offset shadow, with no glass,
// waves or glow.
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: card

    // "ready", "standby", "offline" or "unknown", as it comes from the stream.
    property string status: "unknown"
    property bool registered: false
    property bool connecting: false
    property bool searching: false
    // The one-click connection step: "", "checking" or "waking".
    property string stage: ""
    property bool available: true   // false in a build without Remote Play
    // "ps4", "ps5", or empty when the console never answered — and then no
    // number is made up: "Unknown PlayStation" appears, on a simpler card,
    // so it does not pass for a known console.
    property string kind: ""
    readonly property bool known: kind === "ps4" || kind === "ps5"
    property string name: ""
    property string address: ""
    // False for the other consoles in the list: there the click selects it
    // (it becomes the console in use), and the corner has a ✕ to remove it.
    property bool current: true
    property real scaleFactor: 1.0

    signal connect()
    signal cancel()
    signal edit()
    signal choose()
    signal remove()

    // The ✕ asks for a second click before removing.
    property bool confirmRemoval: false
    Timer {
        running: card.confirmRemoval
        interval: 3500
        onTriggered: card.confirmRemoval = false
    }

    // What the click does now.
    readonly property string actionName: {
        if (!available) return ""
        if (!current) return "choose"
        if (connecting || stage.length > 0) return "cancel"
        return "connect"
    }
    readonly property bool dimmed: !available || status === "offline"
    readonly property bool checking: searching || stage.length > 0
                                       || (status === "unknown" && available)

    readonly property color statusColor: !available ? Theme.cardTextMuted
                                     : checking ? Theme.cardGlow
                                     : status === "ready" ? Theme.cardBlue
                                     : status === "standby" ? Theme.cardAmber
                                     : status === "offline" ? Theme.cardRed
                                     : Theme.cardTextMuted

    readonly property string statusText: {
        if (!available) return qsTr("Remote Play is not in this build")
        if (confirmRemoval) return qsTr("Click ✕ again to remove")
        if (!current) {
            if (status === "ready") return qsTr("Ready — click to connect")
            if (status === "standby") return qsTr("In rest mode — click to wake and connect")
            if (status === "offline") return qsTr("Not responding — click to try to connect")
            return qsTr("Checking — click to connect")
        }
        if (connecting) return qsTr("Connecting… — click to cancel")
        if (stage === "waking") return qsTr("Waking the console… — click to cancel")
        if (stage === "checking") return qsTr("Checking the console… — click to cancel")
        if (searching) return qsTr("Searching for the console…")
        if (status === "offline") return qsTr("Not responding — click to try to connect")
        if (status === "unknown") return qsTr("Click to connect")
        if (!registered) return qsTr("Not registered — click to register")
        if (status === "standby") return qsTr("In rest mode — click to wake and connect")
        return qsTr("Ready — click to connect")
    }

    implicitWidth: 360 * scaleFactor
    implicitHeight: 330 * scaleFactor
    width: implicitWidth
    height: implicitHeight

    // The mouse over a card that does something when clicked.
    readonly property bool hover: area.containsMouse && actionName.length > 0

    // With no action, the whole card fades back a little.
    opacity: dimmed && !area.containsMouse ? 0.9 : 1.0
    Behavior on opacity { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }

    // With the mouse over it, it grows a little and lifts; when pressed, it sinks.
    scale: area.pressed && hover ? Theme.pressScale : (hover ? 1.03 : 1.0)
    Behavior on scale { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
    transform: Translate {
        y: card.hover && !area.pressed ? -4 : 0
        Behavior on y { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
    }

    Item {
        id: drawing
        width: 360
        height: 330
        scale: card.scaleFactor
        transformOrigin: Item.TopLeft

        // ── Soft shadow: three layers widening and fading.
        Repeater {
            model: card.known ? 3 : 0
            Rectangle {
                anchors.fill: backdrop
                anchors.margins: -(index + 1) * 4
                anchors.topMargin: -(index + 1) * 2
                anchors.bottomMargin: -(index + 1) * 6
                radius: backdrop.radius + (index + 1) * 4
                color: "transparent"
                border.width: 4
                border.color: Qt.rgba(0, 0, 0, (Theme.light ? 0.03 - index * 0.008 : 0.09 - index * 0.025)
                                               * (card.hover ? 1.6 : 1.0))
            }
        }

        // ── Blue glow around it, only with the mouse over it: at rest, the
        // card stays discreet.
        Repeater {
            model: card.known ? 3 : 0
            Rectangle {
                anchors.fill: backdrop
                anchors.margins: -(index + 1) * 3
                radius: backdrop.radius + (index + 1) * 3
                color: "transparent"
                border.width: 3
                border.color: Theme.cardGlow
                opacity: card.hover ? 0.2 - index * 0.06 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.cardEase } }
            }
        }

        // ── Cartoon: the hard shadow, the same shape offset and without blur.
        Rectangle {
            visible: !card.known
            x: area.pressed ? 3 : card.hover ? 10 : 7
            y: area.pressed ? 3 : card.hover ? 10 : 7
            Behavior on x { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
            Behavior on y { NumberAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }
            width: backdrop.width
            height: backdrop.height
            radius: backdrop.radius
            color: Theme.light ? Theme.cardText : Qt.rgba(Theme.cardGlow.r, Theme.cardGlow.g,
                                                         Theme.cardGlow.b, 0.5)
        }

        Rectangle {
            id: backdrop
            anchors.fill: parent
            radius: 30
            border.width: !card.known ? 4 : 1
            border.color: card.hover ? Theme.cardGlow
                        : !card.known ? (Theme.light ? Theme.cardText : Theme.cardTextMuted)
                        : card.current && card.available ? Qt.rgba(Theme.cardGlow.r, Theme.cardGlow.g,
                                                                    Theme.cardGlow.b, 0.4)
                        : Theme.cardEdge
            Behavior on border.color { ColorAnimation { duration: Theme.cardEase } }
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.cardTop }
                GradientStop { position: 1.0; color: Theme.cardBottom }
            }
            clip: true

            // Wide curves and the radial gradient behind the logo, plus an
            // almost invisible noise so the blue does not look flat.
            Canvas {
                id: waves
                anchors.fill: parent
                visible: card.known
                readonly property color waveColor: Theme.cardWave
                readonly property color lightColor: card.statusColor
                readonly property bool light: Theme.light
                onWaveColorChanged: requestPaint()
                onLightColorChanged: requestPaint()
                onLightChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    var w = width, h = height

                    var halo = ctx.createRadialGradient(w / 2, 92, 4, w / 2, 92, 150)
                    halo.addColorStop(0, Qt.rgba(lightColor.r, lightColor.g, lightColor.b, light ? 0.06 : 0.12))
                    halo.addColorStop(1, Qt.rgba(lightColor.r, lightColor.g, lightColor.b, 0))
                    ctx.fillStyle = halo
                    ctx.fillRect(0, 0, w, h)

                    ctx.fillStyle = waveColor
                    ctx.beginPath()
                    ctx.moveTo(0, h * 0.42)
                    ctx.bezierCurveTo(w * 0.30, h * 0.66, w * 0.62, h * 0.72, w, h * 0.46)
                    ctx.lineTo(w, h * 0.58)
                    ctx.bezierCurveTo(w * 0.62, h * 0.84, w * 0.30, h * 0.80, 0, h * 0.54)
                    ctx.closePath()
                    ctx.fill()
                    ctx.beginPath()
                    ctx.moveTo(0, h * 0.30)
                    ctx.bezierCurveTo(w * 0.35, h * 0.52, w * 0.70, h * 0.58, w, h * 0.34)
                    ctx.lineTo(w, h * 0.37)
                    ctx.bezierCurveTo(w * 0.70, h * 0.62, w * 0.35, h * 0.56, 0, h * 0.33)
                    ctx.closePath()
                    ctx.fill()

                    // Noise at 2–3%: scattered dots, always the same ones.
                    var seed = 7
                    function random() {
                        seed = (seed * 16807) % 2147483647
                        return seed / 2147483647
                    }
                    ctx.fillStyle = light ? "rgba(15,23,42,0.035)" : "rgba(255,255,255,0.03)"
                    for (var i = 0; i < 900; ++i)
                        ctx.fillRect(random() * w, random() * h, 1, 1)
                }
            }

            // Cartoon: the flat colour over the gradient, inside the outline.
            Rectangle {
                visible: !card.known
                anchors.fill: parent
                anchors.margins: parent.border.width
                radius: parent.radius - parent.border.width
                color: Theme.cardTop
            }

            // The edge of light at the top.
            Rectangle {
                visible: card.known
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 1
                height: 90
                radius: parent.radius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.7 : 0.06) }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
        }

        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: card.actionName.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                switch (card.actionName) {
                case "connect": card.connect(); break
                case "cancel": card.cancel(); break
                case "choose": card.choose(); break
                }
            }
        }

        // ── Top: the app symbol on the left, and on the right the registration
        // (the "link" between this PC and the console) or, on other consoles, the ✕.
        Image {
            x: 24
            y: 22
            width: 22
            height: 22
            source: "qrc:/icons/mark.png"
            sourceSize.width: 44
            sourceSize.height: 44
            opacity: 0.55
        }

        StyledToolButton {
            x: parent.width - width - 16
            y: 14
            implicitWidth: 36
            implicitHeight: 36
            visible: card.available && card.current
            opacity: 0.7
            text: "🔗"
            ToolTip.visible: hovered
            ToolTip.text: card.registered ? qsTr("Register this PC again")
                                          : qsTr("Register this PC on the console")
            onClicked: card.edit()
        }
        StyledToolButton {
            x: parent.width - width - 16
            y: 14
            implicitWidth: 36
            implicitHeight: 36
            visible: !card.current
            danger: card.confirmRemoval
            opacity: 0.7
            text: "✕"
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Remove this console from the list")
            onClicked: {
                if (card.confirmRemoval)
                    card.remove()
                else
                    card.confirmRemoval = true
            }
        }

        // ── Centre: PS4/PS5 in a thin stroke, the blue line, the name and the IP.
        Column {
            x: 0
            y: 58
            width: parent.width
            spacing: 0

            // The PS4/PS5 wordmark (or "Unknown PlayStation", when the type is
            // not known), generated by scripts/generate-wordmarks.py: white on the
            // dark themes, black on the light one.
            Image {
                id: letters
                anchors.horizontalCenter: parent.horizontalCenter
                source: "qrc:/icons/wordmark-" + (card.known ? card.kind : "unknown")
                        + (Theme.light ? "-black" : "-white") + ".png"
                sourceSize.height: 186
                height: 62
                width: implicitWidth / 3
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }

            Item { width: 1; height: 6 }

            Item { width: 1; height: 8 }

            // The thin line under the logo: blue on the console in use,
            // grey on the others.
            Rectangle {
                id: separator
                anchors.horizontalCenter: parent.horizontalCenter
                width: 118
                height: 3
                radius: 1.5
                readonly property color tone: card.current ? Theme.cardBlue : Theme.cardTextMuted
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.rgba(separator.tone.r, separator.tone.g, separator.tone.b, 0.35) }
                    GradientStop { position: 0.5; color: separator.tone }
                    GradientStop { position: 1.0; color: Qt.rgba(separator.tone.r, separator.tone.g, separator.tone.b, 0.35) }
                }
                opacity: card.current ? 1.0 : 0.6
            }

            Item { width: 1; height: 22 }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 48
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: card.name.length > 0 ? card.name : qsTr("Console")
                color: Theme.cardText
                font.pixelSize: 26
                font.weight: Font.DemiBold
            }

            Item { width: 1; height: 2 }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: card.address.length > 0
                text: card.address
                color: Theme.cardTextMuted
                font.pixelSize: 14
            }
        }

        // ── Footer: the button with the state and what the click does.
        Rectangle {
            id: button
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            width: parent.width * 0.82
            height: 58
            radius: height / 2
            clip: true
            readonly property real alpha: area.containsMouse && card.actionName.length > 0 ? 1.4 : 1.0
            color: {
                var c = card.statusColor
                if (!card.available)
                    return Qt.rgba(c.r, c.g, c.b, 0.12)
                if (card.status === "standby" && !card.checking)
                    return Qt.rgba(c.r, c.g, c.b, (Theme.light ? 0.13 : 0.16) * alpha)
                if (card.status === "offline" && !card.checking)
                    return Qt.rgba(c.r, c.g, c.b, (Theme.light ? 0.10 : 0.16) * alpha)
                return Qt.rgba(c.r, c.g, c.b, (Theme.light ? 0.10 : 0.20) * alpha)
            }
            // On the cartoon card, a thick outline in the state colour.
            border.width: card.known ? 1 : 3
            border.color: card.known
                          ? Qt.rgba(card.statusColor.r, card.statusColor.g, card.statusColor.b,
                                    Theme.light ? 0.10 : 0.28)
                          : card.statusColor
            Behavior on color { ColorAnimation { duration: Theme.cardEase; easing.type: Easing.OutCubic } }

            // The dot on the left, the text (up to two lines) and the chevron
            // on the right, in the state's colour.
            Rectangle {
                id: dot
                x: 24
                anchors.verticalCenter: parent.verticalCenter
                width: 14; height: 14; radius: 7
                color: card.statusColor
                SequentialAnimation on opacity {
                    running: card.connecting || card.checking
                    loops: Animation.Infinite
                    onStopped: dot.opacity = 1
                    NumberAnimation { to: 0.25; duration: 500 }
                    NumberAnimation { to: 1.0; duration: 500 }
                }
            }
            Text {
                anchors.left: dot.right
                anchors.leftMargin: 16
                anchors.right: chevron.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeight: 0.95
                text: card.statusText
                color: Theme.cardText
                font.pixelSize: 15
                font.weight: Font.Medium
            }
            Text {
                id: chevron
                anchors.right: parent.right
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                visible: card.actionName.length > 0
                text: "›"
                color: card.statusColor
                font.pixelSize: 28
            }

            // Searching: a shine running along the bottom of the button.
            Rectangle {
                id: barShine
                visible: card.checking
                anchors.bottom: parent.bottom
                height: 2
                width: parent.width * 0.35
                radius: 1
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.5; color: Theme.cardGlow }
                    GradientStop { position: 1.0; color: "transparent" }
                }
                NumberAnimation on x {
                    running: card.checking
                    loops: Animation.Infinite
                    from: -barShine.width
                    to: button.width
                    duration: 1300
                    easing.type: Easing.InOutQuad
                }
            }
        }
    }
}
