#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Puts fake PS4 saves in a mock console's folder, laid out as on a real one:

    user/home/<account>/savedata/<TITLE_ID>/sdimg_<dir>, <dir>.bin
        (and the system's own copy of both: sdimg_sce_bu_<dir>, sce_bu_<dir>.bin)
    user/home/<account>/savedata_meta/user/<TITLE_ID>/<dir>, sce_bu_<dir>
        (files; older layouts had a folder with param.sfo and icon0.png —
        the first game keeps that one, so both are exercised)
    user/appmeta/<TITLE_ID>/icon0.png, param.sfo

The "images" are random bytes: only the save vault's listing, copying and
comparing are exercised.

Usage: make_test_saves.py <mock console root>
"""
import os
import pathlib
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_test_pkg import build_icon_png, build_sfo  # noqa: E402

ACCOUNT = "1a2b3c4d"
# Two made-up PSN accounts, as the 8 bytes of ACCOUNT_ID (little-endian):
# most saves are the first's, the last game's are the second's.
ACCOUNT_IDS = [(0x0123456789ABCDEF).to_bytes(8, "little"), (0x0FEDCBA987654321).to_bytes(8, "little")]
GAMES = [
    ("CUSA00001", "Orbis Racing", [("SAVEDATA00", "Career", "Season 2 - 48%"),
                                   ("SAVEDATA01", "Time trials", "12 tracks")]),
    ("CUSA00002", "Neon Drift", [("SAVE0", "Story", "Chapter 5 - The Docks")]),
    ("CUSA00003", "Starfield Tactics", [("AUTOSAVE", "Autosave", "Sector 7, 23 h 10 min"),
                                        ("SLOT1", "Manual save 1", "Sector 6")]),
]


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = pathlib.Path(sys.argv[1])
    home = root / "user" / "home" / ACCOUNT
    for seed, (title_id, game, saves) in enumerate(GAMES):
        appmeta = root / "user" / "appmeta" / title_id
        appmeta.mkdir(parents=True, exist_ok=True)
        (appmeta / "icon0.png").write_bytes(build_icon_png(128, seed))
        (appmeta / "param.sfo").write_bytes(build_sfo({"TITLE": game, "TITLE_ID": title_id}))
        for n, (dir_name, title, detail) in enumerate(saves):
            data = home / "savedata" / title_id
            data.mkdir(parents=True, exist_ok=True)
            (data / f"sdimg_{dir_name}").write_bytes(os.urandom(64 * 1024 * (n + 1)))
            (data / f"{dir_name}.bin").write_bytes(os.urandom(96))
            sfo = build_sfo({
                "MAINTITLE": game, "SUBTITLE": title, "DETAIL": detail,
                "SAVEDATA_DIRECTORY": dir_name, "TITLE_ID": title_id,
                "ACCOUNT_ID": ACCOUNT_IDS[1 if seed == len(GAMES) - 1 else 0]})
            if seed == 0:
                meta = home / "savedata_meta" / "user" / title_id / dir_name
                meta.mkdir(parents=True, exist_ok=True)
                (meta / "param.sfo").write_bytes(sfo)
                (meta / "icon0.png").write_bytes(build_icon_png(64, seed + 10))
            else:
                (data / f"sdimg_sce_bu_{dir_name}").write_bytes(os.urandom(64 * 1024 * (n + 1)))
                (data / f"sce_bu_{dir_name}.bin").write_bytes(os.urandom(96))
                meta = home / "savedata_meta" / "user" / title_id
                meta.mkdir(parents=True, exist_ok=True)
                (meta / dir_name).write_bytes(sfo)
                (meta / f"sce_bu_{dir_name}").write_bytes(sfo)
    print(f"Saves of {len(GAMES)} games under {home}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
