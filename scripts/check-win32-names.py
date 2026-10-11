#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Looks through OrbisLink's own sources for names <windows.h> turns into
macros (the list is scripts/win32-macro-names.h). A struct, function or
value with one of those names builds on Linux and breaks the Windows build:
where <windows.h> comes in the middle of a file, the name means one thing
above it and another below it (CopyFile becomes CopyFileW). The Windows
build only runs on pull requests and by hand, so this catches it on every
push. Code meant for Windows only (inside #ifdef _WIN32 and the like) is
left alone: there those names are the Windows functions themselves.

Usage: check-win32-names.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "scripts", "win32-macro-names.h")
FOLDERS = [os.path.join(ROOT, "src", "orbislink"), os.path.join(ROOT, "tests")]
WINDOWS_ONLY = {"_WIN32", "_WIN64", "WIN32", "_MSC_VER", "Q_OS_WIN", "Q_OS_WINDOWS", "__MINGW32__"}
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def reserved_names():
    names = set()
    with open(HEADER) as handle:
        for line in handle:
            match = re.match(r"\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)", line)
            if match:
                names.add(match.group(1))
    return names


def without_comments_and_strings(text):
    """The text with comments and literals blanked out, lines kept."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append(" " * (end - i))
            i = end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append(re.sub(r"[^\n]", " ", text[i:end]))
            i = end
        elif c == "R" and text.startswith('R"', i):
            match = re.match(r'R"([^(\s]*)\(', text[i:])
            if match:
                close = ")" + match.group(1) + '"'
                end = text.find(close, i)
                end = n if end < 0 else end + len(close)
                out.append(re.sub(r"[^\n]", " ", text[i:end]))
                i = end
            else:
                out.append(c)
                i += 1
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(" " * (min(j + 1, n) - i))
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def windows_test(condition):
    """True when a #if condition holds only on Windows, False when only off
    it, None when it says nothing about Windows."""
    for symbol in WINDOWS_ONLY:
        if re.search(r"!\s*defined\s*\(?\s*" + symbol + r"\b", condition):
            return False
        if re.search(r"\bdefined\s*\(?\s*" + symbol + r"\b", condition) or re.fullmatch(r"\s*" + symbol + r"\s*", condition):
            return True
    return None


def check(path, names):
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = without_comments_and_strings(handle.read())
    found = []
    # Each open #if: whether its branch under way is Windows-only.
    stack = []
    for number, line in enumerate(text.splitlines(), 1):
        directive = re.match(r"\s*#\s*(\w+)\s*(.*)", line)
        if directive:
            word, rest = directive.group(1), directive.group(2)
            if word == "ifdef":
                stack.append([windows_test("defined(" + rest.strip() + ")"), False])
            elif word == "ifndef":
                test = windows_test("defined(" + rest.strip() + ")")
                stack.append([None if test is None else not test, False])
            elif word == "if":
                stack.append([windows_test(rest), False])
            elif word in ("elif",) and stack:
                stack[-1][0] = windows_test(rest)
            elif word == "else" and stack:
                if stack[-1][0] is not None:
                    stack[-1][0] = not stack[-1][0]
            elif word == "endif" and stack:
                stack.pop()
            if word in ("define", "include", "pragma", "error"):
                continue
            if word in ("if", "ifdef", "ifndef", "elif", "else", "endif", "undef"):
                continue
        if any(state is True for state, _ in stack):
            continue
        for match in IDENTIFIER.finditer(line):
            if match.group(0) in names:
                found.append((number, match.group(0)))
    return found


def main():
    names = reserved_names()
    problems = []
    files = 0
    for folder in FOLDERS:
        for base, _, entries in os.walk(folder):
            for entry in sorted(entries):
                if entry.endswith((".cpp", ".h", ".hpp")):
                    files += 1
                    path = os.path.join(base, entry)
                    for number, name in check(path, names):
                        problems.append(f"{os.path.relpath(path, ROOT)}:{number}: {name}")
    if problems:
        print("\n".join(problems))
        print(f"\nWindows names: {len(problems)} use(s) of a name <windows.h> turns into a macro "
              "(see scripts/win32-macro-names.h): rename them.", file=sys.stderr)
        sys.exit(1)
    print(f"Windows names: none of the {files} files uses a name <windows.h> turns into a macro.")


if __name__ == "__main__":
    main()
