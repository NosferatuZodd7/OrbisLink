// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The application's colours, shapes and motion, in one place.
//
// Calm and premium: satin surfaces a step lighter than the window, thin
// borders, one blue accent and colour only where it says something (the
// state of a console or a service). Three themes share the same shapes:
// Dark (the default), Glass (the dark one with translucent surfaces over a
// blurred-light background) and Light.
//
// It is a singleton and not a .js because themes switch at runtime:
// changing these properties redraws everything bound to them.
pragma Singleton
import QtQuick

QtObject {
    id: theme

    // "dark", "glass" or "light".
    property string name: "dark"
    readonly property bool light: name === "light"
    readonly property bool glass: name === "glass"

    // ── Window background
    property color background: "#0B111A"
    property color backgroundDeep: "#070B12"

    // ── Surfaces
    // "panel" is the base colour of the top bar, side panel and dialogs;
    // "panelAlt" is one step up (rows, fields, cards inside a panel). What
    // gets drawn is panelFill, which already carries the theme's transparency.
    property color panel: "#111925"
    property color panelAlt: "#172131"
    property real panelOpacity: 1.0
    // The faint light at the top of each surface.
    property real glassHighlight: 0.03
    // Strength of the 1px frame around surfaces (on the dark themes it is
    // white at this opacity; the light theme has its own colour).
    property real glassBorder: 0.07

    // ── Dialogs
    // A modal has to read without what is behind showing through.
    property real dialogOpacity: 0.98

    // ── Text
    property color text: "#EBEDF3"
    property color textMuted: "#8A96A6"
    property real textMutedOpacity: 1.0
    // The muted colour with its opacity already applied.
    readonly property color textSecondary: Qt.rgba(textMuted.r, textMuted.g, textMuted.b,
                                                   textMutedOpacity)
    // Text on the accent colour (primary buttons, selected items).
    readonly property color textOnAccent: "#FFFFFF"

    // ── Accent and states
    property color accent: "#388AFF"
    property color accentHover: "#5EA0FF"
    property color accentSoft: "#388AFF"
    property real accentSoftOpacity: 0.16
    property color ok: "#22C55E"
    property color warn: "#F59E0B"
    property color error: "#EF4444"
    // A console with a jailbreak (GoldHEN, etaHEN...): gold, so it stands
    // out from the others at a glance. Text on it is dark.
    property color hen: "#F5B700"
    readonly property color textOnHen: "#1C1500"
    // Set when the person chose their own jailbreak colour.
    property bool henCustom: false
    // The badge's fill: the bright gold on every theme (the light theme's
    // line gold is darker, to show on white) — or the colour chosen.
    readonly property color henFill: light && !henCustom ? "#F5B700" : hen

    // ── Lines
    // The frame of every surface: white at low opacity on the dark themes, a
    // cool grey on the light one (white on white would not show).
    readonly property color glassEdge: light ? "#D8E0EA" : Qt.rgba(1, 1, 1, glassBorder)
    readonly property color border: glassEdge

    // ── Shape
    property int radius: 24          // panels and cards
    property int radiusControl: 12   // buttons
    property int radiusSmall: 12     // rows, fields, chips' containers
    readonly property int radiusField: 12
    readonly property int radiusDialog: 24
    property int spacing: 16
    property int gutter: 20
    property int padding: 20
    readonly property int controlHeight: 40
    readonly property int fieldHeight: 42
    // Inside dialogs: the margin is the radius plus a little slack, so text
    // never sits inside the curve of the corner.
    readonly property int dialogMargin: 28
    readonly property int dialogInner: 18
    readonly property int dialogHeader: 64
    readonly property int dialogFooter: 76

    // ── Type
    readonly property string fontMono: "monospace"
    readonly property int fontTitle: 22
    readonly property int fontCardTitle: 17
    readonly property int fontBody: 14
    readonly property int fontSmall: 12
    readonly property int fontCaption: 11

    // ── Motion
    // Everything that moves uses these numbers.
    readonly property int fast: 150
    readonly property int normal: 220
    readonly property int slow: 320
    readonly property int easeOut: Easing.OutCubic
    readonly property int easeSpring: Easing.OutBack
    readonly property real hoverScale: 1.02
    readonly property real pressScale: 0.97

    // ── Over the video
    // The picture is always dark, whatever the theme, so text over it is light.
    readonly property color onStage: "#FFFFFF"
    readonly property color onStageMuted: Qt.rgba(1, 1, 1, 0.68)
    // The glass of the bar and notices floating over the picture.
    readonly property color hudFill: Qt.rgba(10 / 255, 16 / 255, 26 / 255, 0.78)
    readonly property color hudEdge: Qt.rgba(1, 1, 1, 0.12)

    // ── The stage, with no picture
    property color stageIdle: "#080D15"
    readonly property color onIdleStage: text
    readonly property color onIdleStageMuted: textSecondary

    // ── Console card
    property color cardTop: "#142339"
    property color cardBottom: "#101C2F"
    // The console in use: the same card, lit from below in the accent.
    property color cardActiveBottom: "#0F3570"
    // A console with a jailbreak: the same card, warmed in gold.
    property color cardHenBottom: "#34290A"
    property color cardHenActiveBottom: "#4D3C08"
    readonly property color cardText: text
    readonly property color cardTextMuted: textSecondary
    readonly property color cardEdge: light ? "#D8E0EA" : Qt.rgba(1, 1, 1, 0.08)
    readonly property color cardBlue: accent
    readonly property color cardGlow: accent
    readonly property color cardAmber: warn
    readonly property color cardRed: error
    readonly property int cardEase: 180

    // ── The colours the surfaces actually use
    readonly property color panelFill: Qt.rgba(panel.r, panel.g, panel.b, panelOpacity)
    readonly property color panelAltFill: Qt.rgba(panelAlt.r, panelAlt.g, panelAlt.b,
                                                  Math.min(1, panelOpacity + 0.1))
    readonly property color glassSheen: Qt.rgba(1, 1, 1, glassHighlight)
    readonly property color accentFill: Qt.rgba(accent.r, accent.g, accent.b, accentSoftOpacity)
    readonly property color shadow: Qt.rgba(0, 0, 0, light ? 0.08 : 0.45)
    readonly property color dialogFill: Qt.rgba(panel.r, panel.g, panel.b, dialogOpacity)
    // Menus float over lists of text: always fully opaque, a step above the
    // panels so they read as on top.
    readonly property color menuFill: light ? "#FFFFFF" : glass ? "#1C2B44" : "#182231"
    // Behind a modal: says the rest of the application is waiting.
    readonly property color scrim: Qt.rgba(0, 0, 0, light ? 0.28 : 0.55)
    // A control's own fill (secondary buttons, fields, segmented controls).
    readonly property color controlFill: light ? "#EEF2F7" : Qt.rgba(1, 1, 1, glass ? 0.08 : 0.05)
    readonly property color controlHover: light ? "#E3E9F1" : Qt.rgba(1, 1, 1, glass ? 0.13 : 0.09)

    // The same colour at another opacity, which is needed everywhere.
    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    // Between two colours: 0 is a, 1 is b. The alpha is a's.
    function mix(a, b, t) {
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a)
    }
    function withAlpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    // The base theme, then the colours the person changed (Settings →
    // Personalisation) on top. Each chosen colour also moves the shades
    // made from it, so the whole stays in tune.
    function apply(which, custom) {
        applyBase(which)
        henCustom = false
        if (!custom)
            return
        if (custom.background) {
            background = custom.background
            backgroundDeep = Qt.darker(custom.background, light ? 1.04 : 1.45)
            stageIdle = withAlpha(Qt.darker(custom.background, light ? 1.0 : 1.3), stageIdle.a)
        }
        if (custom.panel) {
            var top = cardTop.a, bottom = cardBottom.a
            panel = custom.panel
            panelAlt = light ? Qt.darker(custom.panel, 1.03) : Qt.lighter(custom.panel, 1.3)
            cardBottom = withAlpha(light ? Qt.darker(custom.panel, 1.02) : Qt.lighter(custom.panel, 1.25), bottom)
            cardTop = withAlpha(light ? custom.panel : Qt.lighter(custom.panel, 1.5), top)
        }
        if (custom.text) {
            text = custom.text
            textMuted = mix(Qt.color(custom.text), background, 0.42)
        }
        if (custom.accent) {
            accent = custom.accent
            accentSoft = custom.accent
            accentHover = light ? Qt.darker(custom.accent, 1.1) : Qt.lighter(custom.accent, 1.2)
            cardActiveBottom = withAlpha(mix(cardBottom, Qt.color(custom.accent), light ? 0.14 : 0.42),
                                         cardActiveBottom.a)
        }
        if (custom.hen) {
            hen = custom.hen
            henCustom = true
            cardHenBottom = withAlpha(mix(cardBottom, Qt.color(custom.hen), light ? 0.16 : 0.2),
                                      cardHenBottom.a)
            cardHenActiveBottom = withAlpha(mix(cardBottom, Qt.color(custom.hen), light ? 0.3 : 0.32),
                                            cardHenActiveBottom.a)
        }
        if (custom.ok) ok = custom.ok
        if (custom.warn) warn = custom.warn
        if (custom.error) error = custom.error
    }

    function applyBase(which) {
        name = which === "glass" || which === "light" ? which : "dark"
        if (name === "light") {
            background = "#EEF3F9"
            backgroundDeep = "#E6ECF4"
            panel = "#FFFFFF"
            panelAlt = "#F5F8FC"
            panelOpacity = 1.0
            dialogOpacity = 1.0
            glassHighlight = 0.0
            glassBorder = 0.0
            text = "#111827"
            textMuted = "#667085"
            textMutedOpacity = 1.0
            accent = "#2563EB"
            accentHover = "#3B76F0"
            accentSoft = "#2563EB"
            accentSoftOpacity = 0.10
            ok = "#16A34A"
            warn = "#D97706"
            error = "#DC2626"
            stageIdle = "#F7FAFD"
            cardTop = "#FFFFFF"
            cardBottom = "#F5F8FD"
            cardActiveBottom = "#E3EDFF"
            hen = "#B98000"
            cardHenBottom = "#FFF5D6"
            cardHenActiveBottom = "#FFE9A6"
        } else if (name === "glass") {
            background = "#10213A"
            backgroundDeep = "#081222"
            panel = "#16243A"
            panelAlt = "#1D2E48"
            panelOpacity = 0.55
            dialogOpacity = 0.94
            glassHighlight = 0.10
            glassBorder = 0.14
            text = "#F1F4F9"
            textMuted = "#A3B1C6"
            textMutedOpacity = 1.0
            accent = "#4A95FF"
            accentHover = "#6AA8FF"
            accentSoft = "#4A95FF"
            accentSoftOpacity = 0.22
            ok = "#2DD36F"
            warn = "#FFB547"
            error = "#FF5F6D"
            stageIdle = Qt.rgba(8 / 255, 16 / 255, 30 / 255, 0.45)
            cardTop = Qt.rgba(40 / 255, 62 / 255, 96 / 255, 0.55)
            cardBottom = Qt.rgba(24 / 255, 40 / 255, 66 / 255, 0.55)
            cardActiveBottom = Qt.rgba(28 / 255, 78 / 255, 160 / 255, 0.62)
            hen = "#FFC51F"
            cardHenBottom = Qt.rgba(120 / 255, 92 / 255, 14 / 255, 0.50)
            cardHenActiveBottom = Qt.rgba(160 / 255, 120 / 255, 12 / 255, 0.62)
        } else {
            background = "#0B111A"
            backgroundDeep = "#070B12"
            panel = "#111925"
            panelAlt = "#172131"
            panelOpacity = 1.0
            dialogOpacity = 0.98
            glassHighlight = 0.03
            glassBorder = 0.07
            text = "#EBEDF3"
            textMuted = "#8A96A6"
            textMutedOpacity = 1.0
            accent = "#388AFF"
            accentHover = "#5EA0FF"
            accentSoft = "#388AFF"
            accentSoftOpacity = 0.16
            ok = "#22C55E"
            warn = "#F59E0B"
            error = "#EF4444"
            stageIdle = "#080D15"
            cardTop = "#142339"
            cardBottom = "#101C2F"
            cardActiveBottom = "#0F3570"
            hen = "#F5B700"
            cardHenBottom = "#34290A"
            cardHenActiveBottom = "#4D3C08"
        }
    }

    function stateColor(state) {
        if (state === "available") return ok
        if (state === "checking") return warn
        if (state === "unavailable") return error
        return textSecondary
    }

    function taskColor(state) {
        if (state === "completed") return ok
        if (state === "error") return error
        if (state === "cancelled") return textSecondary
        if (state === "pending") return textSecondary
        return accent
    }
}
