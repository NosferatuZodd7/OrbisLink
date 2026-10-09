#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Puts fake PS4 saves in a mock console's folder, laid out as on a real one:

    user/home/<account>/savedata/<TITLE_ID>/sdimg_<dir>, <dir>.bin
        (and the system's own copy of both: sdimg_sce_bu_<dir>, sce_bu_<dir>.bin)
    user/home/<account>/savedata_meta/user/<TITLE_ID>/<dir>, sce_bu_<dir>
        (files; older layouts had a folder with param.sfo and icon0.png —
        the first game keeps that one, so both are exercised)
    user/appmeta/<TITLE_ID>/icon0.png, param.sfo
    system_data/savedata/<account>/db/user/savedata.db
        (the console's list of saves: one row per save, as the PS4 keeps it)
    user/home/<account>/username.dat
        (the user's name, as a PS5 keeps it)

With --ps5, a PS5 of the same PSN account instead: its own user folder for
it (with one PS4 game's save, and its list of PS4 saves, as a PS5 keeps
them) and a second user whose account is not known.

The "images" are random bytes: only the save vault's listing, copying and
comparing are exercised.

Usage: make_test_saves.py <mock console root> [--ps5]
"""
import os
import pathlib
import sqlite3
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


# The PS5's users: the same PSN account as the PS4's, and a guest.
PS5_ACCOUNT = "5a6b7c8d"
PS5_GUEST = "6c7d8e9f"


def write_name(home: pathlib.Path, name: str) -> None:
    home.mkdir(parents=True, exist_ok=True)
    (home / "username.dat").write_bytes(name.encode() + bytes(16 - len(name)))


def make_ps5(root: pathlib.Path) -> int:
    home = root / "user" / "home" / PS5_ACCOUNT
    write_name(home, "PlayerOne")
    write_name(root / "user" / "home" / PS5_GUEST, "Guest")
    title_id, game, saves = GAMES[1]
    appmeta = root / "user" / "appmeta" / title_id
    appmeta.mkdir(parents=True, exist_ok=True)
    (appmeta / "icon0.png").write_bytes(build_icon_png(128, 1))
    (appmeta / "param.sfo").write_bytes(build_sfo({"TITLE": game, "TITLE_ID": title_id}))
    data = home / "savedata" / title_id
    data.mkdir(parents=True, exist_ok=True)
    for dir_name, _title, _detail in saves:
        (data / f"sdimg_{dir_name}").write_bytes(os.urandom(64 * 1024))
        (data / f"{dir_name}.bin").write_bytes(os.urandom(96))
    write_save_list(root, home, PS5_ACCOUNT, ps5=True)
    print(f"A PS5 with user {PS5_ACCOUNT} (the PS4's account) and {PS5_GUEST}")
    return 0


def main() -> int:
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != "--ps5"):
        print(__doc__)
        return 2
    root = pathlib.Path(sys.argv[1])
    if len(sys.argv) == 3:
        return make_ps5(root)
    home = root / "user" / "home" / ACCOUNT
    write_name(home, "PlayerOne")
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
    write_save_list(root, home, ACCOUNT)
    print(f"Saves of {len(GAMES)} games under {home}")
    return 0


# The columns Apollo Save Tool fills when it adds a save to the list.
SAVE_LIST_SCHEMA = (
    "CREATE TABLE savedata(id INTEGER PRIMARY KEY, title_id TEXT NOT NULL, dir_name TEXT NOT NULL, "
    "main_title TEXT, sub_title TEXT, detail TEXT, tmp_dir_name TEXT, is_broken INTEGER, user_param INTEGER, "
    "blocks INTEGER, free_blocks INTEGER, size_kib INTEGER, mtime TEXT, fake_broken INTEGER, account_id INTEGER, "
    "user_id INTEGER, faked_owner INTEGER, cloud_icon_url TEXT, cloud_revision INTEGER, game_title_id TEXT)")


def write_save_list(root: pathlib.Path, home: pathlib.Path, account: str, ps5: bool = False) -> None:
    """One row per save found under the user's savedata folder."""
    folder = root / "system_data" / "savedata" / account / "db" / "user"
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / "savedata.db"
    if path.exists():
        path.unlink()
    db = sqlite3.connect(path)
    # A PS5's list of PS4 saves has one more column.
    db.execute(SAVE_LIST_SCHEMA[:-1] + ", system_blocks INTEGER)" if ps5 else SAVE_LIST_SCHEMA)
    for title in sorted((home / "savedata").iterdir()):
        for image in sorted(title.glob("sdimg_*")):
            name = image.name[len("sdimg_"):]
            if name.startswith("sce_bu_"):
                continue
            blocks = image.stat().st_size // 32768
            db.execute("INSERT INTO savedata(title_id, dir_name, main_title, sub_title, detail, tmp_dir_name, "
                       "is_broken, user_param, blocks, free_blocks, size_kib, mtime, fake_broken, account_id, "
                       "user_id, faked_owner, cloud_icon_url, cloud_revision, game_title_id) "
                       "VALUES (?, ?, '', '', '', '', 0, 0, ?, ?, ?, '2026-10-01T12:00:00.00Z', 0, ?, ?, 0, '', 0, ?)",
                       (title.name, name, blocks, blocks, blocks * 32,
                        int.from_bytes(ACCOUNT_IDS[0], "little"), int(account, 16), title.name))
    db.commit()
    db.close()


if __name__ == "__main__":
    sys.exit(main())
