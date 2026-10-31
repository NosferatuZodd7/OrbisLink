// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The application's colours, shapes, type and motion, in one place.
//
// The Celestial Archive: an astronomical atlas printed in a woodblock
// workshop. Deep indigo ink on aged paper, one restrained brass for what is
// chosen or jailbroken, and colour only where it says something (a state).
// Three themes share the same shapes:
//   Light ("light") — the Archive: indigo ink on parchment, a faint grain.
//   Dark  ("dark")  — the Observatory: midnight navy, warm ivory text.
//   Glass ("glass") — the Observatory with translucent surfaces over a sky.
//
// The window is an atlas laid open: a spine on the left with where to go
// (SideNav), the sky in the middle with the page, and a second spine on the
// right with what is being sent (the inspector). Headings use a soft
// old-style serif (Fraunces), everything that is worked with a friendly
// sans (Figtree), and what is copied or compared — addresses, ports, sizes,
// versions — a monospace.
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
    property color background: "#10283D"
    property color backgroundDeep: "#0B1D2D"

    // ── The spines: the navigation on the left, the inspector on the right.
    // Solid, a shade off the sky, so the page in the middle reads as the
    // open sheet.
    property color sidebar: "#0C2134"
    readonly property color sidebarFill: glass ? Qt.rgba(sidebar.r, sidebar.g, sidebar.b, 0.78) : sidebar
    // A plate's eyebrow — the small capitals over a title — in brass, on
    // every theme; a chosen jailbreak colour does not move it.
    property color brass: "#C9A15C"

    // ── Surfaces
    // "panel" is the base colour of the plates, menus and dialogs;
    // "panelAlt" is one step up (rows, fields, cards inside a panel). What
    // gets drawn is panelFill, which already carries the theme's transparency.
    property color panel: "#132F47"
    property color panelAlt: "#183A55"
    property real panelOpacity: 1.0
    // The faint light at the top of each surface.
    property real glassHighlight: 0.03
    // Strength of the 1px frame around surfaces (on the dark themes it is
    // ivory at this opacity; the light theme has its own ink line).
    property real glassBorder: 0.12

    // ── Dialogs
    // A modal has to read without what is behind showing through.
    property real dialogOpacity: 0.98

    // ── Text
    property color text: "#F3E8CC"
    property color textMuted: "#A7B3B1"
    property real textMutedOpacity: 1.0
    // The muted colour with its opacity already applied.
    readonly property color textSecondary: Qt.rgba(textMuted.r, textMuted.g, textMuted.b,
                                                   textMutedOpacity)
    // Text on the accent colour (primary buttons, selected items).
    property color textOnAccent: "#0E2233"

    // ── Accent and states
    property color accent: "#6C9DA0"
    property color accentHover: "#82B0B2"
    property color accentSoft: "#6C9DA0"
    property real accentSoftOpacity: 0.16
    property color ok: "#7FA184"
    property color warn: "#D49C52"
    property color error: "#C0625A"
    // A console with a jailbreak (GoldHEN, etaHEN...): antique brass, so it
    // stands out from the others at a glance. Text on it is dark.
    property color hen: "#C9A15C"
    readonly property color textOnHen: "#1E1608"
    // Set when the person chose their own jailbreak colour.
    property bool henCustom: false
    // The badge's fill: the brass on every theme — or the colour chosen.
    readonly property color henFill: light && !henCustom ? "#B28A4A" : hen
    // Ink for headings: indigo on paper, ivory at night.
    property color ink: "#F3E8CC"

    // ── Lines
    // The frame of every surface: ivory at low opacity on the dark themes, an
    // indigo hairline on the light one.
    readonly property color glassEdge: light ? Qt.rgba(23 / 255, 59 / 255, 92 / 255, 0.24)
                                             : Qt.rgba(243 / 255, 232 / 255, 204 / 255, glassBorder)
    readonly property color border: glassEdge
    // An engraved rule: a little stronger than a border, for the dividers
    // that carry a page's structure.
    readonly property color rule: light ? Qt.rgba(23 / 255, 59 / 255, 92 / 255, 0.38)
                                        : Qt.rgba(243 / 255, 232 / 255, 204 / 255, 0.22)

    // ── Shape
    // Plates have soft corners; what is pressed — buttons, tags, chips — is
    // round-ended, so it never reads as a plate.
    property int radius: 16          // plates and cards
    property int radiusControl: 19   // buttons: half their height
    property int radiusSmall: 10     // rows and the containers of chips
    readonly property int radiusField: 10
    readonly property int radiusDialog: 18
    property int spacing: 16
    property int gutter: 28
    property int padding: 20
    readonly property int controlHeight: 38
    readonly property int fieldHeight: 40
    // The spines' widths, and the navigation folded down to its icons.
    readonly property int sideNavWidth: 236
    readonly property int sideNavCompact: 72
    readonly property int inspectorWidth: 356

    // ── Game pictures: the same sizes on every page
    // A game's card in a grid (the library, the PS1/PS2 converter).
    readonly property int gameTile: 148
    // A place's card on the library's home.
    readonly property int placeTile: 176
    // Room around a card in a grid: across, and under its picture for its
    // name and details.
    readonly property int gameTileGap: 34
    readonly property int gameTileNames: 74
    // A game's picture in a row (the queue, the saves, the conversions).
    readonly property int gameIcon: 44
    // The large picture of a game's details.
    readonly property int gameArt: 188
    // Inside dialogs: the margin is the radius plus a little slack, so text
    // never sits inside the curve of the corner.
    readonly property int dialogMargin: 26
    readonly property int dialogInner: 18
    readonly property int dialogHeader: 64
    readonly property int dialogFooter: 76

    // ── Type
    // The sans (Figtree, the application's own font), the serif of the
    // titles (Fraunces, soft) and the monospace of technical values.
    readonly property string fontSans: "Figtree"
    readonly property string fontDisplay: "Fraunces"
    readonly property string fontMono: "JetBrains Mono"
    readonly property int fontHero: 38       // the About plate, the empty pages
    readonly property int fontTitle: 30      // a page's title
    readonly property int fontHeading: 20    // a section inside a page, a dialog's title
    readonly property int fontCardTitle: 18
    readonly property int fontBody: 14
    readonly property int fontSmall: 12
    readonly property int fontCaption: 11
    // The eyebrow: small capitals, spaced, over a title.
    readonly property int fontEyebrow: 10
    readonly property real eyebrowSpacing: 1.8

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
    readonly property color onStage: "#F3E8CC"
    readonly property color onStageMuted: Qt.rgba(243 / 255, 232 / 255, 204 / 255, 0.70)
    // The glass of the bar and notices floating over the picture.
    readonly property color hudFill: Qt.rgba(16 / 255, 40 / 255, 61 / 255, 0.80)
    readonly property color hudEdge: Qt.rgba(243 / 255, 232 / 255, 204 / 255, 0.14)

    // ── The stage, with no picture
    property color stageIdle: "#0D2234"
    readonly property color onIdleStage: text
    readonly property color onIdleStageMuted: textSecondary

    // ── Console card
    property color cardTop: "#17344F"
    property color cardBottom: "#122C43"
    // The console in use: the same card, lit from below in the accent.
    property color cardActiveBottom: "#1D4A66"
    // A console with a jailbreak: the same card, warmed in brass.
    property color cardHenBottom: "#32301F"
    property color cardHenActiveBottom: "#463F25"
    readonly property color cardText: text
    readonly property color cardTextMuted: textSecondary
    readonly property color cardEdge: light ? Qt.rgba(23 / 255, 59 / 255, 92 / 255, 0.22)
                                            : Qt.rgba(243 / 255, 232 / 255, 204 / 255, 0.12)
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
    readonly property color shadow: Qt.rgba(0, 0, 0, light ? 0.10 : 0.45)
    readonly property color dialogFill: Qt.rgba(panel.r, panel.g, panel.b, dialogOpacity)
    // Menus float over lists of text: always fully opaque, a step above the
    // panels so they read as on top.
    readonly property color menuFill: light ? "#F7EFDB" : glass ? "#1A3D5A" : "#1A3D59"
    // Behind a modal: says the rest of the application is waiting.
    readonly property color scrim: light ? Qt.rgba(32 / 255, 43 / 255, 50 / 255, 0.30) : Qt.rgba(0, 0, 0, 0.55)
    // A control's own fill (secondary buttons, fields, segmented controls).
    readonly property color controlFill: light ? Qt.rgba(23 / 255, 59 / 255, 92 / 255, 0.06)
                                               : Qt.rgba(243 / 255, 232 / 255, 204 / 255, glass ? 0.08 : 0.05)
    readonly property color controlHover: light ? Qt.rgba(23 / 255, 59 / 255, 92 / 255, 0.11)
                                                : Qt.rgba(243 / 255, 232 / 255, 204 / 255, glass ? 0.13 : 0.09)

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
            sidebar = Qt.darker(custom.background, light ? 1.06 : 1.3)
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
            ink = custom.text
            textMuted = mix(Qt.color(custom.text), background, 0.42)
        }
        if (custom.accent) {
            accent = custom.accent
            accentSoft = custom.accent
            accentHover = light ? Qt.darker(custom.accent, 1.1) : Qt.lighter(custom.accent, 1.2)
            // Text on it: dark on a light accent, light on a dark one.
            var c = Qt.color(custom.accent)
            textOnAccent = (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) > 0.6 ? "#0E2233" : "#F3E8CC"
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
            // The Archive: indigo ink on aged paper.
            background = "#E9DCBB"
            backgroundDeep = "#E1D1AC"
            panel = "#F3E8CC"
            panelAlt = "#EEE1C1"
            panelOpacity = 1.0
            dialogOpacity = 1.0
            glassHighlight = 0.0
            glassBorder = 0.0
            text = "#202B32"
            ink = "#173B5C"
            textMuted = "#5C6A6E"
            textMutedOpacity = 1.0
            textOnAccent = "#F3E8CC"
            accent = "#173B5C"
            accentHover = "#24507A"
            accentSoft = "#426F89"
            accentSoftOpacity = 0.12
            ok = "#557A5B"
            warn = "#B07A35"
            error = "#A84D46"
            stageIdle = "#EFE3C5"
            sidebar = "#E2CFA6"
            brass = "#8E6C33"
            cardTop = "#F5EBD2"
            cardBottom = "#EDDFBD"
            cardActiveBottom = "#DCDDCB"
            hen = "#8E6C33"
            cardHenBottom = "#EBD9AE"
            cardHenActiveBottom = "#E3CC94"
        } else if (name === "glass") {
            // The Observatory under an open sky: translucent navy surfaces.
            background = "#0E2438"
            backgroundDeep = "#071624"
            panel = "#16344F"
            panelAlt = "#1D4160"
            panelOpacity = 0.60
            dialogOpacity = 0.95
            glassHighlight = 0.08
            glassBorder = 0.16
            text = "#F5ECD4"
            ink = "#F5ECD4"
            textMuted = "#ADBAB8"
            textMutedOpacity = 1.0
            textOnAccent = "#0E2233"
            accent = "#7FADB0"
            accentHover = "#96BFC1"
            accentSoft = "#7FADB0"
            accentSoftOpacity = 0.22
            ok = "#86A98B"
            warn = "#D9A55E"
            error = "#C86A62"
            stageIdle = Qt.rgba(8 / 255, 22 / 255, 36 / 255, 0.45)
            sidebar = "#0A1B2B"
            brass = "#D1A963"
            cardTop = Qt.rgba(32 / 255, 64 / 255, 92 / 255, 0.58)
            cardBottom = Qt.rgba(20 / 255, 44 / 255, 66 / 255, 0.58)
            cardActiveBottom = Qt.rgba(36 / 255, 86 / 255, 110 / 255, 0.66)
            hen = "#D1A963"
            cardHenBottom = Qt.rgba(92 / 255, 76 / 255, 40 / 255, 0.52)
            cardHenActiveBottom = Qt.rgba(118 / 255, 96 / 255, 46 / 255, 0.62)
        } else {
            // The Observatory: midnight navy, ivory text.
            background = "#10283D"
            backgroundDeep = "#0B1D2D"
            panel = "#132F47"
            panelAlt = "#183A55"
            panelOpacity = 1.0
            dialogOpacity = 0.98
            glassHighlight = 0.03
            glassBorder = 0.12
            text = "#F3E8CC"
            ink = "#F3E8CC"
            textMuted = "#A7B3B1"
            textMutedOpacity = 1.0
            textOnAccent = "#0E2233"
            accent = "#6C9DA0"
            accentHover = "#82B0B2"
            accentSoft = "#6C9DA0"
            accentSoftOpacity = 0.16
            ok = "#7FA184"
            warn = "#D49C52"
            error = "#C0625A"
            stageIdle = "#0D2234"
            sidebar = "#0B1F31"
            brass = "#C9A15C"
            cardTop = "#17344F"
            cardBottom = "#122C43"
            cardActiveBottom = "#1D4A66"
            hen = "#C9A15C"
            cardHenBottom = "#32301F"
            cardHenActiveBottom = "#463F25"
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
