#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Builds qml/orbislink/Icons.js from the Lucide icon set.

The icons are drawn in QML with Shape/PathSvg, not loaded as SVG images:
that way the app needs neither the Qt SVG module nor the image plugin
that windeployqt would have to ship, and each icon takes the theme's
colour like any text.

Each Lucide icon is a handful of <path>, <circle>, <rect>, <line>,
<polyline>, <polygon> and <ellipse> elements on a 24×24 grid. They are
all turned into a single SVG path string.

Usage:
    npm pack lucide-static   (or download the tarball) and unpack it, then
    python3 scripts/generate-icons.py path/to/package/icons

Lucide is under the ISC licence: see third-party/lucide/LICENSE.
"""
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "qml" / "orbislink" / "Icons.js"

# Name in the app → Lucide file name.
ICONS = {
    "play": "play",
    "moon": "moon",
    "sun": "sun",
    "contrast": "contrast",
    "download": "download",
    "upload": "upload",
    "cloud-upload": "cloud-upload",
    "pin": "pin",
    "pin-off": "pin-off",
    "plus": "plus",
    "archive": "archive",
    "archive-restore": "archive-restore",
    "vault": "vault",
    "history": "history",
    "check-check": "check-check",
    "ratio": "ratio",
    "plus-circle": "circle-plus",
    "refresh": "refresh-cw",
    "settings": "settings",
    "close": "x",
    "info": "info",
    "panel": "panel-right",
    "log": "file-text",
    "terminal": "square-terminal",
    "gamepad": "gamepad-2",
    "mic": "mic",
    "mic-off": "mic-off",
    "volume": "volume-2",
    "volume-off": "volume-x",
    "keyboard": "keyboard",
    "maximize": "maximize",
    "minimize": "minimize",
    "power": "power",
    "package": "package",
    "store": "store",
    "zap": "zap",
    "plug": "plug",
    "list-ordered": "list-ordered",
    "folder": "folder",
    "file": "file",
    "link": "link",
    "trash": "trash-2",
    "check": "check",
    "check-circle": "circle-check",
    "alert": "circle-alert",
    "warning": "triangle-alert",
    "loader": "loader-circle",
    "monitor": "monitor",
    "user": "user-round",
    "users": "users-round",
    "sliders": "sliders-horizontal",
    "server": "server",
    "chevron-right": "chevron-right",
    "chevron-down": "chevron-down",
    "chevron-left": "chevron-left",
    "arrow-up": "arrow-up",
    "arrow-down": "arrow-down",
    "pause": "pause",
    "search": "search",
    "more": "ellipsis-vertical",
    "copy": "copy",
    "save": "save",
    "pencil": "pencil",
    "wifi": "wifi",
    "globe": "globe",
    "home": "house",
    "id-card": "id-card",
    "cast": "cast",
    "hard-drive": "hard-drive",
    "usb": "usb",
    "star": "star",
    "laptop": "laptop",
    "arrow-up-down": "arrow-up-down",
    "folder-plus": "folder-plus",
    "filter": "funnel",
    "rotate-ccw": "rotate-ccw",
    "external-link": "external-link",
    "sparkles": "sparkles",
    "disc": "disc-3",
    "folder-open": "folder-open",
    "package-plus": "package-plus",
    "square": "square",
    "square-check": "square-check",
    "image": "image",
    "unlock": "lock-keyhole-open",
    "cpu": "cpu",
}

NUM = r"-?\d*\.?\d+(?:e-?\d+)?"


def fmt(value):
    text = f"{float(value):.3f}".rstrip("0").rstrip(".")
    return text if text not in ("-0", "") else "0"


def circle(cx, cy, r):
    return (f"M{fmt(cx - r)} {fmt(cy)}a{fmt(r)} {fmt(r)} 0 1 0 {fmt(2 * r)} 0"
            f"a{fmt(r)} {fmt(r)} 0 1 0 {fmt(-2 * r)} 0Z")


def ellipse(cx, cy, rx, ry):
    return (f"M{fmt(cx - rx)} {fmt(cy)}a{fmt(rx)} {fmt(ry)} 0 1 0 {fmt(2 * rx)} 0"
            f"a{fmt(rx)} {fmt(ry)} 0 1 0 {fmt(-2 * rx)} 0Z")


def rect(x, y, w, h, rx, ry):
    if rx == 0 and ry == 0:
        return f"M{fmt(x)} {fmt(y)}h{fmt(w)}v{fmt(h)}h{fmt(-w)}Z"
    rx = min(rx or ry, w / 2)
    ry = min(ry or rx, h / 2)
    return (f"M{fmt(x + rx)} {fmt(y)}h{fmt(w - 2 * rx)}"
            f"a{fmt(rx)} {fmt(ry)} 0 0 1 {fmt(rx)} {fmt(ry)}v{fmt(h - 2 * ry)}"
            f"a{fmt(rx)} {fmt(ry)} 0 0 1 {fmt(-rx)} {fmt(ry)}h{fmt(-(w - 2 * rx))}"
            f"a{fmt(rx)} {fmt(ry)} 0 0 1 {fmt(-rx)} {fmt(-ry)}v{fmt(-(h - 2 * ry))}"
            f"a{fmt(rx)} {fmt(ry)} 0 0 1 {fmt(rx)} {fmt(-ry)}Z")


def points(text, close):
    values = re.findall(NUM, text)
    pairs = [(values[i], values[i + 1]) for i in range(0, len(values) - 1, 2)]
    out = "M" + " L".join(f"{fmt(x)} {fmt(y)}" for x, y in pairs)
    return out + ("Z" if close else "")


def absolute_start(d):
    """A path that opens with a relative "m" becomes absolute.

    Alone, the first "m" of a path is absolute anyway; joined after other
    shapes it would be taken relative to where they ended. The pairs that
    follow it are relative line-tos, so they keep an explicit "l".
    """
    m = re.match(r"m\s*(" + NUM + r")[\s,]*(" + NUM + r")(.*)$", d, re.S)
    if not m:
        return d
    x, y, rest = m.groups()
    rest = rest.lstrip(" ,")
    if rest and re.match(r"-?[\d.]", rest):
        rest = "l" + rest
    return f"M{x} {y}" + ((" " + rest) if rest else "")


def convert(file):
    tree = ET.parse(file)
    parts = []
    for element in tree.getroot().iter():
        tag = element.tag.split("}")[-1]
        a = element.attrib
        f = lambda key, default=0.0: float(a.get(key, default))
        if tag == "path":
            parts.append(absolute_start(a["d"].strip()))
        elif tag == "circle":
            parts.append(circle(f("cx"), f("cy"), f("r")))
        elif tag == "ellipse":
            parts.append(ellipse(f("cx"), f("cy"), f("rx"), f("ry")))
        elif tag == "rect":
            parts.append(rect(f("x"), f("y"), f("width"), f("height"), f("rx"), f("ry")))
        elif tag == "line":
            parts.append(f"M{fmt(f('x1'))} {fmt(f('y1'))}L{fmt(f('x2'))} {fmt(f('y2'))}")
        elif tag in ("polyline", "polygon"):
            parts.append(points(a["points"], tag == "polygon"))
    return " ".join(parts)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    source = pathlib.Path(sys.argv[1])
    lines = [
        "// SPDX-License-Identifier: AGPL-3.0-or-later",
        "//",
        "// Generated by scripts/generate-icons.py from Lucide (ISC licence, see",
        "// third-party/lucide/LICENSE). Do not edit by hand: add the icon to the",
        "// script and run it again.",
        "//",
        "// Each entry is one SVG path on Lucide's 24×24 grid, drawn by Icon.qml.",
        ".pragma library",
        "",
        "var paths = {",
    ]
    for name, lucide in sorted(ICONS.items()):
        lines.append(f'    "{name}": "{convert(source / (lucide + ".svg"))}",')
    lines.append("}")
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(ICONS)} icons written to {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
