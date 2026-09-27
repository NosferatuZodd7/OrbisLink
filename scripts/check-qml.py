#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Catches a class of bug that tests cannot see.

A QtQuick.Controls.Basic Dialog, Popup or Menu without its own "background"
is drawn WHITE. Since the rest of the application is dark, the text on top
takes the theme's light colours — and the result is a white window with
invisible text.

Tests do not catch it: the QML loads, there is no error at all, and the
window opens. It is caught by reading the file, which is what this does.

It also refuses Theme.onStage inside a dialog: that colour is white on
purpose, for writing over the video, and has no business on a glass
surface.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
KINDS = ("Dialog", "Popup", "Menu")


def blocks(text, kind):
    """Returns (line, body) for each balanced `Kind {` … `}` block."""
    for m in re.finditer(r"(?<![A-Za-z_.])" + kind + r"\s*\{", text):
        # An "Overlay.modal: Rectangle" or a "property var x: Dialog"
        # does not open one of these blocks; the regex above already excludes them by the dot.
        start = m.end() - 1
        level = 0
        for i in range(start, len(text)):
            if text[i] == "{":
                level += 1
            elif text[i] == "}":
                level -= 1
                if level == 0:
                    yield text[: m.start()].count("\n") + 1, text[start : i + 1]
                    break


def no_nesting(body):
    """The body without the Dialog/Popup/Menu blocks inside it.

    Without this, a dialog without a background would pass because a child
    has one — which is exactly the case worth catching.
    """
    for kind in KINDS:
        for _, inner in list(blocks(body[1:], kind)):
            body = body.replace(inner, "")
    return body


def main():
    problems = []
    for file in sorted((ROOT / "qml").rglob("*.qml")):
        text = file.read_text(encoding="utf-8")
        for kind in KINDS:
            for line, body in blocks(text, kind):
                own = no_nesting(body)
                name = f"{file.relative_to(ROOT)}:{line} ({kind})"
                if not re.search(r"^\s*background\s*:", own, re.M):
                    problems.append(
                        f"{name} does not define a background — "
                        f"the Basic style paints it white and the theme's text disappears"
                    )
                if "Theme.onStage" in own:
                    problems.append(
                        f"{name} uses Theme.onStage, which is white and meant for "
                        f"writing over the video, not inside a dialog"
                    )

    if problems:
        print("QML: found surfaces that would come out white:\n", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1

    print("QML: every dialog, popup and menu has its own background.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
