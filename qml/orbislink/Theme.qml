// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The application's colours, shapes and motion, in one place.
//
// The language is "liquid glass": stacked translucent surfaces, an edge
// of light at the top, soft shadows, wide corners and motion with
// physics. Depth comes from layers, not lines — which is why there are
// almost no separators in this interface.
//
// It is a singleton and not a .js because themes switch at runtime:
// changing these properties redraws everything bound to them.
pragma Singleton
import QtQuick

QtObject {
    id: tema

    // "dark", "glass" or "light". "glass" is the dark one with more
    // transparency.
    property string nome: "escuro"
    readonly property bool claro: nome === "claro"

    // ── Background
    // A deep black, not neutral: it carries a touch of blue so the glass
    // on top has somewhere to take colour from.
    property color background: "#07070A"
    property color backgroundDeep: "#050507"

    // ── Glass surfaces
    // "panel" is the base colour; what gets drawn is panelFill, which already
    // carries the theme's transparency.
    property color panel: "#16161C"
    property color panelAlt: "#1D1D25"
    property real panelOpacity: 0.55
    // The edge of light at the top of each surface, which is what makes the
    // glass look curved instead of flat.
    property real glassHighlight: 0.08
    // The 1px frame separating the glass from what is behind.
    property real glassBorder: 0.12

    // ── Dialogs
    // A modal is not a background surface: it has to read without what is
    // behind showing through. Glass is for panels, not for a box asking
    // for a decision.
    property real dialogOpacity: 0.97

    // ── Texto
    property color text: "#FFFFFF"
    property color textMuted: "#FFFFFF"
    property real textMutedOpacity: 0.62
    // The colour with the opacity already applied, for whoever just wants to paint.
    readonly property color textSecondary: Qt.rgba(textMuted.r, textMuted.g, textMuted.b,
                                                   textMutedOpacity)

    // ── Acento e estados
    property color accent: "#0A84FF"
    property color accentSoft: "#0A84FF"
    property real accentSoftOpacity: 0.18
    property color ok: "#30D158"
    property color warn: "#FFD60A"
    property color error: "#FF453A"

    // Compatibility: some code paints borders with "border".
    readonly property color border: Qt.rgba(1, 1, 1, glassBorder)

    // ── Shape
    // Wide, continuous corners. 28 on cards, 20 on controls, capsule
    // for navigation.
    property int radius: 28
    property int radiusControl: 20
    property int radiusSmall: 14
    property int spacing: 16
    // Generous margins: air is half the design.
    property int gutter: 28
    property int padding: 24
    // Inside dialogs. Where the corner is curved, text at the radius's
    // distance is already inside the curve, and that reads as sloppy. So the
    // margin is the radius plus visible slack, not the radius at the tangent.
    readonly property int dialogMargin: radius + 8
    // Inside the notice and conversion boxes, which are themselves already
    // up against the top margin.
    readonly property int dialogInner: 18
    // Header and footer heights. The title is vertically centred, so this
    // is what decides the room above it — and the top corner is what makes
    // it visible.
    readonly property int dialogHeader: 68
    readonly property int dialogFooter: 74

    // ── Motion
    // Everything that moves uses these numbers, so the whole interface has
    // the same physics instead of each piece inventing its own.
    readonly property int fast: 180
    readonly property int normal: 260
    readonly property int slow: 380
    readonly property int easeOut: Easing.OutCubic
    // Qt's "spring" without springs: a restrained OutBack is enough to give
    // a sense of weight without the exaggeration of a bounce.
    readonly property int easeSpring: Easing.OutBack
    readonly property real hoverScale: 1.02
    readonly property real pressScale: 0.985

    // ── Text drawn over the video
    // The stream area is always dark, regardless of the theme, so the
    // text over it is always light.
    readonly property color onStage: "#FFFFFF"
    readonly property color onStageMuted: Qt.rgba(1, 1, 1, 0.65)

    // ── The idle stage, with no picture from the console
    // There is no video dictating darkness there: the stage follows the theme,
    // with each one's background (black with the green grid, white with the
    // cyan one). Only when the picture arrives does it go back to black and onStage.
    // The white is pure white on purpose: the light image has a white
    // background, and on a tinted stage the strip where it ends would show.
    readonly property url stageBackdrop: claro ? "qrc:/icons/backdrop-light.png"
                                               : "qrc:/icons/backdrop.png"
    // Light cyan on white is barely visible at the opacity that works for
    // green on black; so each theme has its own.
    readonly property real stageBackdropOpacity: claro ? 0.45 : 0.15
    readonly property color stageIdle: claro ? "#FFFFFF" : "#000000"
    readonly property color stageGrid: claro ? "#C9D3E3" : "#1B2130"
    readonly property color onIdleStage: claro ? text : onStage
    readonly property color onIdleStageMuted: claro ? textSecondary : onStageMuted

    // ── Console card
    // Near-black navy on the dark themes, icy white on the light one. Colour
    // only appears in the state indicators.
    readonly property color cardTop: claro ? "#FFFFFF" : "#071426"
    readonly property color cardBottom: claro ? "#EEF4FC" : "#0A1E3A"
    readonly property color cardText: claro ? "#0F172A" : "#FFFFFF"
    readonly property color cardTextMuted: claro ? "#5B6B82" : "#A8B3C7"
    readonly property color cardEdge: claro ? Qt.rgba(15 / 255, 23 / 255, 42 / 255, 0.08)
                                            : Qt.rgba(45 / 255, 140 / 255, 1, 0.28)
    readonly property color cardWave: claro ? Qt.rgba(0, 112 / 255, 243 / 255, 0.07)
                                            : Qt.rgba(45 / 255, 140 / 255, 1, 0.07)
    readonly property color cardBlue: "#0070F3"
    readonly property color cardGlow: "#2D8CFF"
    readonly property color cardAmber: "#F5A524"
    readonly property color cardRed: "#E5484D"
    readonly property int cardEase: 250

    // ── The colours the surfaces actually use
    readonly property color panelFill: Qt.rgba(panel.r, panel.g, panel.b, panelOpacity)
    readonly property color panelAltFill: Qt.rgba(panelAlt.r, panelAlt.g, panelAlt.b,
                                                  panelOpacity)
    readonly property color glassEdge: Qt.rgba(1, 1, 1, glassBorder)
    readonly property color glassSheen: Qt.rgba(1, 1, 1, glassHighlight)
    readonly property color accentFill: Qt.rgba(accent.r, accent.g, accent.b, accentSoftOpacity)
    readonly property color shadow: Qt.rgba(0, 0, 0, claro ? 0.10 : 0.45)
    readonly property color dialogFill: Qt.rgba(panel.r, panel.g, panel.b, dialogOpacity)
    // The dimming behind a modal: it is what says the rest of the
    // application is waiting.
    readonly property color scrim: Qt.rgba(0, 0, 0, claro ? 0.32 : 0.58)

    function aplicar(qual) {
        nome = qual === "vidro" || qual === "claro" ? qual : "escuro"
        if (nome === "claro") {
            // Icy white, white glass surfaces, near-black text.
            background = "#F6F7FB"
            backgroundDeep = "#EDEFF5"
            panel = "#FFFFFF"
            panelAlt = "#FFFFFF"
            panelOpacity = 0.68
            dialogOpacity = 0.98
            glassHighlight = 0.55
            glassBorder = 0.55
            text = "#111111"
            textMuted = "#111111"
            textMutedOpacity = 0.55
            accent = "#007AFF"
            accentSoft = "#007AFF"
            accentSoftOpacity = 0.12
            ok = "#28A745"
            warn = "#B26B00"
            error = "#D7362B"
            radius = 28
            radiusControl = 20
        } else if (nome === "vidro") {
            // The same dark, but with more of what is behind showing through.
            background = "#050507"
            backgroundDeep = "#030304"
            panel = "#14141B"
            panelAlt = "#1B1B24"
            panelOpacity = 0.38
            dialogOpacity = 0.94
            glassHighlight = 0.12
            glassBorder = 0.16
            text = "#FFFFFF"
            textMuted = "#FFFFFF"
            textMutedOpacity = 0.66
            accent = "#0A84FF"
            accentSoft = "#0A84FF"
            accentSoftOpacity = 0.22
            ok = "#30D158"
            warn = "#FFD60A"
            error = "#FF453A"
            radius = 32
            radiusControl = 22
        } else {
            background = "#07070A"
            backgroundDeep = "#050507"
            panel = "#16161C"
            panelAlt = "#1D1D25"
            panelOpacity = 0.55
            dialogOpacity = 0.97
            glassHighlight = 0.08
            glassBorder = 0.12
            text = "#FFFFFF"
            textMuted = "#FFFFFF"
            textMutedOpacity = 0.62
            accent = "#0A84FF"
            accentSoft = "#0A84FF"
            accentSoftOpacity = 0.18
            ok = "#30D158"
            warn = "#FFD60A"
            error = "#FF453A"
            radius = 28
            radiusControl = 20
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
