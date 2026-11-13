// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The application's colours, shapes, type and motion, in one place.
//
// Liquid Glass: a spatial interface brought to the desktop. Frosted glass
// floats over a soft, blurred environment of warm light; champagne marks what
// is chosen, a restrained cool blue what is active or technical, and colour
// otherwise only says a state. Three appearances share the same shapes:
//   Dark  ("dark")  — Graphite: smoked glass over a dim, warm room.
//   Glass ("glass") — Ambient: the same, clearer glass, more light through it.
//   Light ("light") — Daylight: white frosted glass over warm stone.
//
// The window is a room: the navigation floats on the left, the toolbar on
// top, the page in the middle and the transfers on the right — each a pane of
// glass. Glass comes in three weights (Plate.level): primary for the panes,
// secondary for toolbars, menus and popovers, subtle for what sits inside a
// pane. One modern sans (Figtree) for everything; a monospace for what is
// copied or compared — addresses, ports, sizes, versions.
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

    // ── The floating panes: navigation, toolbar, transfers. Primary glass.
    property color sidebar: "#26262A"
    readonly property color sidebarFill: Qt.rgba(sidebar.r, sidebar.g, sidebar.b, glassPrimary)
    // Champagne: what is chosen, the small capitals over a title. A chosen
    // jailbreak colour does not move it. (Named brass for the components
    // that already use it.)
    property color brass: "#D8C3A0"

    // ── Glass: how much of the room shows through each weight of pane.
    property real glassWindow: 0.38
    property real glassPrimary: 0.07
    property real glassSecondary: 0.66
    property real glassSubtle: 0.06
    // The luminous edge: brightest along the top, where the light falls.
    readonly property color glassStrokeTop: light ? Qt.rgba(1, 1, 1, 0.95) : Qt.rgba(1, 1, 1, glassBorder + 0.12)
    readonly property color glassStrokeBottom: light ? Qt.rgba(1, 1, 1, 0.45) : Qt.rgba(1, 1, 1, glassBorder * 0.45)

    // ── Surfaces
    // "panel" is the base colour of the plates, menus and dialogs;
    // "panelAlt" is one step up (rows, fields, cards inside a panel). What
    // gets drawn is panelFill, which already carries the theme's transparency.
    property color panel: "#2A2A2E"
    property color panelAlt: "#333338"
    property real panelOpacity: 0.58
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
    readonly property color henFill: hen
    // Ink for headings: indigo on paper, ivory at night.
    property color ink: "#F3E8CC"

    // ── Lines
    // The frame of every surface: ivory at low opacity on the dark themes, an
    // indigo hairline on the light one.
    readonly property color glassEdge: light ? Qt.rgba(30 / 255, 28 / 255, 24 / 255, 0.12)
                                             : Qt.rgba(1, 1, 1, glassBorder)
    readonly property color border: glassEdge
    // An engraved rule: a little stronger than a border, for the dividers
    // that carry a page's structure.
    readonly property color rule: light ? Qt.rgba(30 / 255, 28 / 255, 24 / 255, 0.16)
                                        : Qt.rgba(1, 1, 1, 0.16)

    // ── Shape
    // Glass has generous, continuous corners; what is pressed — buttons,
    // tags, chips — is round-ended. Never so round it looks like a toy.
    property int radius: 18          // cards and plates
    readonly property int radiusWindow: 34 // the one pane the application sits in
    readonly property int radiusPane: 24  // the panes inside it
    property int radiusControl: 18   // buttons: half their height
    property int radiusSmall: 12     // rows and the containers of chips
    readonly property int radiusField: 12
    readonly property int radiusDialog: 24
    property int spacing: 16
    property int gutter: 24
    property int padding: 20
    readonly property int controlHeight: 36
    readonly property int fieldHeight: 38
    // The spines' widths, and the navigation folded down to its icons.
    readonly property int sideNavWidth: 232
    readonly property int sideNavCompact: 76
    readonly property int inspectorWidth: 360
    // Room between the panes and around them, and the toolbar's height.
    readonly property int paneGap: 12
    readonly property int toolbarHeight: 56

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
    // One sans for everything (Figtree, the application's own font): titles
    // are the same family, heavier. The monospace is for technical values.
    readonly property string fontSans: "Figtree"
    readonly property string fontDisplay: "Figtree"
    readonly property string fontMono: "JetBrains Mono"
    readonly property int fontHero: 36       // the About plate, the empty pages
    readonly property int fontTitle: 28      // a page's title
    readonly property int fontHeading: 20    // a section inside a page, a dialog's title
    readonly property int fontCardTitle: 18
    readonly property int fontBody: 14
    readonly property int fontSmall: 12
    readonly property int fontCaption: 11
    // The eyebrow: small capitals, spaced, over a title.
    readonly property int fontEyebrow: 10
    readonly property real eyebrowSpacing: 1.4

    // ── Motion
    // Everything that moves uses these numbers.
    readonly property int fast: 160
    readonly property int normal: 260
    readonly property int slow: 380
    readonly property int easeOut: Easing.OutCubic
    readonly property int easeSpring: Easing.OutBack
    readonly property real hoverScale: 1.02
    readonly property real pressScale: 0.97

    // ── Over the video
    // The picture is always dark, whatever the theme, so text over it is light.
    readonly property color onStage: "#F6F3EE"
    readonly property color onStageMuted: Qt.rgba(246 / 255, 243 / 255, 238 / 255, 0.70)
    // The glass of the bar and notices floating over the picture.
    readonly property color hudFill: Qt.rgba(28 / 255, 28 / 255, 32 / 255, 0.62)
    readonly property color hudEdge: Qt.rgba(1, 1, 1, 0.20)

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
    readonly property color cardEdge: glassEdge
    readonly property color cardBlue: accent
    readonly property color cardGlow: accent
    readonly property color cardAmber: warn
    readonly property color cardRed: error
    readonly property int cardEase: 180

    // ── The colours the surfaces actually use
    readonly property color panelFill: Qt.rgba(panel.r, panel.g, panel.b, panelOpacity)
    readonly property color panelAltFill: Qt.rgba(panelAlt.r, panelAlt.g, panelAlt.b,
                                                  Math.min(1, panelOpacity + 0.12))
    readonly property color glassSheen: Qt.rgba(1, 1, 1, glassHighlight)
    readonly property color accentFill: Qt.rgba(accent.r, accent.g, accent.b, accentSoftOpacity)
    readonly property color shadow: Qt.rgba(0, 0, 0, light ? 0.10 : 0.35)
    readonly property color dialogFill: Qt.rgba(panel.r, panel.g, panel.b, dialogOpacity)
    // Menus float over lists of text: always fully opaque, a step above the
    // panels so they read as on top.
    readonly property color menuFill: light ? "#F8F6F2" : "#2C2C31"
    // Behind a modal: says the rest of the application is waiting.
    readonly property color scrim: light ? Qt.rgba(40 / 255, 36 / 255, 30 / 255, 0.22) : Qt.rgba(0, 0, 0, 0.45)
    // A control's own fill (secondary buttons, fields, segmented controls).
    readonly property color controlFill: light ? Qt.rgba(1, 1, 1, 0.55)
                                               : Qt.rgba(1, 1, 1, glass ? 0.09 : 0.07)
    readonly property color controlHover: light ? Qt.rgba(1, 1, 1, 0.85)
                                                : Qt.rgba(1, 1, 1, glass ? 0.15 : 0.12)

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
        if (custom.brass) brass = custom.brass
        if (custom.ok) ok = custom.ok
        if (custom.warn) warn = custom.warn
        if (custom.error) error = custom.error
    }

    // The colours a picture of one's own gives the theme (Settings →
    // Personalisation): its overall colour tints the background and the
    // panes, its main hue becomes the accent, a second one the champagne and
    // the jailbreak's colour. Lightness stays the appearance's, so Daylight
    // stays light and Graphite dark whatever the picture; text keeps its own
    // colours for the contrast. `tones` is { average, primary, secondary }.
    function paletteFrom(which, tones) {
        var out = {}
        if (!tones || !tones.average)
            return out
        var day = which === "light"
        function hueOf(c) { return c.hslHue < 0 ? 0 : c.hslHue }
        function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }
        function hex(c) { return Qt.rgba(c.r, c.g, c.b, 1).toString().toUpperCase() }
        var avg = Qt.color(tones.average)
        var h = hueOf(avg)
        var s = avg.hslHue < 0 ? 0 : avg.hslSaturation
        out.background = hex(day ? Qt.hsla(h, Math.min(s, 0.30), 0.87, 1)
                                 : Qt.hsla(h, Math.min(s, 0.32), which === "glass" ? 0.10 : 0.08, 1))
        out.panel = hex(day ? Qt.hsla(h, Math.min(s, 0.45), 0.985, 1)
                            : Qt.hsla(h, Math.min(s, 0.16), 0.17, 1))
        if (tones.primary) {
            var p = Qt.color(tones.primary)
            out.accent = hex(day ? Qt.hsla(hueOf(p), clamp(p.hslSaturation, 0.40, 0.70), 0.40, 1)
                                 : Qt.hsla(hueOf(p), clamp(p.hslSaturation, 0.35, 0.65), 0.72, 1))
        }
        if (tones.secondary) {
            var q = Qt.color(tones.secondary)
            out.brass = hex(day ? Qt.hsla(hueOf(q), clamp(q.hslSaturation * 0.6, 0.20, 0.45), 0.36, 1)
                                : Qt.hsla(hueOf(q), clamp(q.hslSaturation * 0.6, 0.20, 0.45), 0.76, 1))
            out.hen = hex(day ? Qt.hsla(hueOf(q), clamp(q.hslSaturation, 0.35, 0.60), 0.40, 1)
                              : Qt.hsla(hueOf(q), clamp(q.hslSaturation, 0.35, 0.60), 0.64, 1))
        }
        return out
    }

    function applyBase(which) {
        name = which === "glass" || which === "light" ? which : "dark"
        if (name === "light") {
            // Daylight: white frosted glass over warm stone.
            background = "#E7E2DA"
            backgroundDeep = "#D6CFC4"
            panel = "#FFFFFF"
            panelAlt = "#FFFFFF"
            panelOpacity = 0.52
            glassWindow = 0.30
            glassPrimary = 0.34
            glassSecondary = 0.80
            glassSubtle = 0.45
            dialogOpacity = 0.96
            glassHighlight = 0.0
            glassBorder = 0.0
            text = "#1C1B19"
            ink = "#141310"
            textMuted = "#66625B"
            textMutedOpacity = 1.0
            textOnAccent = "#FFFFFF"
            accent = "#4A7AA3"
            accentHover = "#3C6A92"
            accentSoft = "#4A7AA3"
            accentSoftOpacity = 0.14
            ok = "#3F8459"
            warn = "#A8711F"
            error = "#B04339"
            stageIdle = "#E2DCD3"
            sidebar = "#FFFFFF"
            brass = "#8A6E45"
            cardTop = Qt.rgba(1, 1, 1, 0.62)
            cardBottom = Qt.rgba(1, 1, 1, 0.48)
            cardActiveBottom = Qt.rgba(220 / 255, 232 / 255, 242 / 255, 0.70)
            hen = "#9C7442"
            cardHenBottom = Qt.rgba(242 / 255, 228 / 255, 204 / 255, 0.70)
            cardHenActiveBottom = Qt.rgba(236 / 255, 216 / 255, 180 / 255, 0.78)
        } else if (name === "glass") {
            // Ambient: clearer glass, more of the room through it.
            background = "#17161A"
            backgroundDeep = "#0D0D10"
            panel = "#2C2B30"
            panelAlt = "#38373D"
            panelOpacity = 0.40
            glassWindow = 0.22
            glassPrimary = 0.07
            glassSecondary = 0.62
            glassSubtle = 0.08
            dialogOpacity = 0.93
            glassHighlight = 0.10
            glassBorder = 0.18
            text = "#F6F3EE"
            ink = "#FBF9F5"
            textMuted = "#B3AEA6"
            textMutedOpacity = 1.0
            textOnAccent = "#11161B"
            accent = "#A9C6E0"
            accentHover = "#BDD4E9"
            accentSoft = "#A9C6E0"
            accentSoftOpacity = 0.18
            ok = "#93CDA7"
            warn = "#E6BC74"
            error = "#EA9086"
            stageIdle = Qt.rgba(14 / 255, 14 / 255, 16 / 255, 0.40)
            sidebar = "#2A292E"
            brass = "#DCC8A6"
            cardTop = Qt.rgba(1, 1, 1, 0.10)
            cardBottom = Qt.rgba(1, 1, 1, 0.05)
            cardActiveBottom = Qt.rgba(169 / 255, 198 / 255, 224 / 255, 0.16)
            hen = "#CFAA78"
            cardHenBottom = Qt.rgba(207 / 255, 170 / 255, 120 / 255, 0.14)
            cardHenActiveBottom = Qt.rgba(207 / 255, 170 / 255, 120 / 255, 0.22)
        } else {
            // Graphite: smoked glass over a dim, warm room.
            background = "#141416"
            backgroundDeep = "#0B0B0D"
            panel = "#28282C"
            panelAlt = "#323237"
            panelOpacity = 0.58
            glassWindow = 0.30
            glassPrimary = 0.06
            glassSecondary = 0.78
            glassSubtle = 0.06
            dialogOpacity = 0.96
            glassHighlight = 0.07
            glassBorder = 0.14
            text = "#F4F1EA"
            ink = "#FAF8F4"
            textMuted = "#A9A49C"
            textMutedOpacity = 1.0
            textOnAccent = "#10151A"
            accent = "#9EBCD8"
            accentHover = "#B3CBE2"
            accentSoft = "#9EBCD8"
            accentSoftOpacity = 0.16
            ok = "#8CC7A1"
            warn = "#E2B66E"
            error = "#E58A80"
            stageIdle = "#101012"
            sidebar = "#252529"
            brass = "#D8C3A0"
            cardTop = Qt.rgba(1, 1, 1, 0.08)
            cardBottom = Qt.rgba(1, 1, 1, 0.04)
            cardActiveBottom = Qt.rgba(158 / 255, 188 / 255, 216 / 255, 0.14)
            hen = "#C9A473"
            cardHenBottom = Qt.rgba(201 / 255, 164 / 255, 115 / 255, 0.12)
            cardHenActiveBottom = Qt.rgba(201 / 255, 164 / 255, 115 / 255, 0.20)
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
