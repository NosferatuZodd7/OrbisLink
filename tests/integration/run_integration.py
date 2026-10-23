#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""OrbisLink integration test against the fake console.

Covers the whole Direct install path (pkg → local HTTP server with Range →
installer API → progress → completion) and the FTP operations, without
needing a PS4.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "mock-console"))

import make_test_pkg  # noqa: E402


def child_env() -> dict[str, str]:
    """Forces UTF-8 in child processes (OrbisLink always writes UTF-8)."""
    environment = dict(os.environ)
    environment["PYTHONUTF8"] = "1"
    environment["PYTHONIOENCODING"] = "utf-8"
    return environment


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def digest(path: str) -> str:
    hasher = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            hasher.update(block)
    return hasher.hexdigest()


class Runner:
    def __init__(self, cli: str) -> None:
        self.cli = cli
        self.failures: list[str] = []

    def check(self, condition: bool, description: str) -> bool:
        print(("[ok]   " if condition else "[FAIL] ") + description, flush=True)
        if not condition:
            self.failures.append(description)
        return condition

    def run(self, *arguments: str, expect: int | None = 0) -> subprocess.CompletedProcess:
        # explicit encoding: on Windows Python would decode the output as
        # cp1252 and choke on accents and the 🟢/🔴 indicators.
        result = subprocess.run([self.cli, *arguments], capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=180,
                                env=child_env())
        if expect is not None and result.returncode != expect:
            print(f"        command: {' '.join(arguments)}")
            print(f"        output : {result.stdout.strip()}")
            print(f"        error  : {result.stderr.strip()}")
        return result


def main() -> int:
    # This script's own output carries accents and emojis.
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True)
    parser.add_argument("--mock", required=True)
    arguments = parser.parse_args()

    runner = Runner(arguments.cli)
    workspace = tempfile.mkdtemp(prefix="orbislink-integration-")
    console_root = os.path.join(workspace, "console")
    os.makedirs(console_root)

    # synthetic ~3 MB pkg, enough for several Range requests.
    pkg_path = os.path.join(workspace, "test game.pkg")
    with open(pkg_path, "wb") as handle:
        handle.write(make_test_pkg.build_pkg("UP0001-CUSA12345_00-ORBISLINKTEST001",
                                             "Test Game", "gd", "01.00",
                                             3 * 1024 * 1024))
    pkg_digest = digest(pkg_path)

    ftp_port = free_port()
    api_port = free_port()
    http_port = free_port()

    mock = subprocess.Popen(
        [sys.executable, arguments.mock, "--ftp-port", str(ftp_port),
         "--api-port", str(api_port), "--root", console_root, "--print-ports",
         # Every upload loses its connection once on the way: it has to go on.
         "--ftp-drop-once", str(1024 * 1024),
         # Every download too, with the "426" a console's server answers then.
         "--ftp-drop-download-once", str(1536 * 1024)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        encoding="utf-8", errors="replace", env=child_env())
    try:
        line = mock.stdout.readline()
        info = json.loads(line)
        print(f"fake console: {info}", flush=True)

        common = ["--host", "127.0.0.1", "--ftp-port", str(info["ftp_port"]),
                  "--installer-port", str(info["api_port"])]

        # 1. Service check
        services = runner.run("services", *common)
        runner.check(services.returncode == 0, "services returns success")
        runner.check("🟢" in services.stdout, "services shows at least one available service")
        runner.check(services.stdout.count("🟢") == 2, "FTP and installer are both available")

        # 2. pkg metadata
        inspect = runner.run("inspect", pkg_path)
        runner.check("CUSA12345" in inspect.stdout, "inspect reads the TITLE_ID")
        runner.check("Test Game" in inspect.stdout, "inspect reads the title")

        # 3. Direct install end to end
        install = runner.run("install", *common, "--http-port", str(http_port),
                             "--bind", "127.0.0.1", "--timeout", "120", pkg_path)
        runner.check(install.returncode == 0, "install finishes successfully")
        runner.check("Done" in install.stdout, "the task reaches the Done state")

        downloads = os.path.join(console_root, "data", "downloads")
        received = [os.path.join(downloads, name) for name in os.listdir(downloads)]
        runner.check(len(received) == 1, "the fake console received exactly one pkg")
        if received:
            runner.check(digest(received[0]) == pkg_digest,
                         "the pkg received through Range requests is identical to the original")

        # 4. FTP: upload, listing and download
        remote = "/data/pkg/game.pkg"
        put = runner.run("ftp-put", *common, pkg_path, remote)
        runner.check(put.returncode == 0, "ftp-put uploads the file")
        uploaded = os.path.join(console_root, "data", "pkg", "game.pkg")
        runner.check(os.path.exists(uploaded) and digest(uploaded) == pkg_digest,
                     "the file reaches the console intact")

        listing = runner.run("ftp-ls", *common, "/data/pkg")
        runner.check("game.pkg" in listing.stdout, "ftp-ls shows the uploaded file")

        downloaded = os.path.join(workspace, "round-trip.pkg")
        get = runner.run("ftp-get", *common, remote, downloaded)
        runner.check(get.returncode == 0 and digest(downloaded) == pkg_digest,
                     "ftp-get brings the file back unchanged, going on from where a 426 cut it")

        mkdir = runner.run("ftp-mkdir", *common, "/data/pkg/subfolder")
        runner.check(mkdir.returncode == 0
                     and os.path.isdir(os.path.join(console_root, "data", "pkg", "subfolder")),
                     "ftp-mkdir creates the folder")

        # A folder that is there already is what was asked for (the PS5's
        # ftpsrv refuses MKD for it, as the fake console does).
        again = runner.run("ftp-mkdir", *common, "/data/pkg/subfolder")
        runner.check(again.returncode == 0, "ftp-mkdir of a folder already there succeeds")

        # Moving: on one disk a rename; onto a USB drive, where the console
        # cannot rename, a copy through the PC, and then the original goes.
        usb = os.path.join(console_root, "mnt", "usb0")
        os.makedirs(os.path.join(usb, "PS2"), exist_ok=True)
        moved_here = os.path.join(console_root, "data", "pkg", "subfolder", "game.pkg")
        same = runner.run("ftp-mv", *common, remote, "/data/pkg/subfolder/game.pkg")
        runner.check(same.returncode == 0 and "through" not in same.stdout
                     and os.path.exists(moved_here) and not os.path.exists(uploaded),
                     "ftp-mv renames on the same disk")
        on_usb = os.path.join(usb, "PS2", "game.pkg")
        across = runner.run("ftp-mv", *common, "/data/pkg/subfolder/game.pkg", "/mnt/usb0/PS2/game.pkg")
        runner.check(across.returncode == 0 and "through this PC" in across.stdout
                     and os.path.exists(on_usb) and digest(on_usb) == pkg_digest
                     and not os.path.exists(moved_here),
                     "ftp-mv copies a file onto another disk through the PC, then deletes the original")

        bundle = os.path.join(console_root, "data", "pkg", "bundle")
        os.makedirs(os.path.join(bundle, "inner"))
        with open(os.path.join(bundle, "top.txt"), "w") as handle:
            handle.write("top")
        with open(os.path.join(bundle, "inner", "deep.txt"), "w") as handle:
            handle.write("deep" * 1000)
        folder = runner.run("ftp-mv", *common, "/data/pkg/bundle", "/mnt/usb0/bundle")
        runner.check(folder.returncode == 0 and not os.path.exists(bundle)
                     and open(os.path.join(usb, "bundle", "top.txt")).read() == "top"
                     and open(os.path.join(usb, "bundle", "inner", "deep.txt")).read() == "deep" * 1000,
                     "ftp-mv moves a whole folder onto another disk")

        # A folder with things in it: the console's server refuses RMD for
        # it, so what is inside goes first.
        full = os.path.join(console_root, "data", "pkg", "full")
        os.makedirs(os.path.join(full, "a", "b"))
        for relative in ("one.bin", os.path.join("a", "two.bin"), os.path.join("a", "b", "three.bin")):
            with open(os.path.join(full, relative), "w") as handle:
                handle.write(relative)
        plain = runner.run("ftp-rm", *common, "/data/pkg/full", expect=1)
        runner.check(plain.returncode != 0 and os.path.isdir(full),
                     "ftp-rm of a folder without --recursive leaves it")
        removed = runner.run("ftp-rm", *common, "--recursive", "/data/pkg/full")
        runner.check(removed.returncode == 0 and not os.path.exists(full),
                     "ftp-rm --recursive deletes a folder with everything in it")

        with open(os.path.join(console_root, "data", "pkg", "other.bin"), "w") as handle:
            handle.write("other")
        taken = runner.run("ftp-mv", *common, "/data/pkg/other.bin", "/mnt/usb0/PS2/game.pkg", expect=1)
        runner.check(taken.returncode != 0 and digest(on_usb) == pkg_digest
                     and os.path.exists(os.path.join(console_root, "data", "pkg", "other.bin")),
                     "ftp-mv refuses to overwrite what is already there")
        os.remove(os.path.join(console_root, "data", "pkg", "other.bin"))

        back = runner.run("ftp-mv", *common, "/mnt/usb0/PS2/game.pkg", remote)
        runner.check(back.returncode == 0 and os.path.exists(uploaded) and digest(uploaded) == pkg_digest,
                     "and back again")

        remove = runner.run("ftp-rm", *common, remote)
        runner.check(remove.returncode == 0 and not os.path.exists(uploaded),
                     "ftp-rm deletes the file")

        # 5. Upload via FTP and install afterwards, in a single pass
        before = len(os.listdir(downloads))
        twice = runner.run("install", *common, "--ftp", "--install-after-upload",
                           "--http-port", str(http_port), "--bind", "127.0.0.1",
                           "--timeout", "120", pkg_path)
        runner.check(twice.returncode == 0, "install --ftp --install-after-upload finishes successfully")
        # The remote name comes from the local file, with spaces replaced by
        # underscores (the queue sanitises the name before uploading it).
        sent = os.path.join(console_root, "data", "pkg", "test_game.pkg")
        runner.check(os.path.exists(sent) and digest(sent) == pkg_digest,
                     "the pkg stays stored on the console after the upload")
        runner.check(len(os.listdir(downloads)) == before + 1,
                     "and is installed from the PC right away")
        if os.path.exists(sent):
            os.remove(sent)

        # 6. The same, but deleting the copy on the console at the end
        before = len(os.listdir(downloads))
        cleaned = runner.run("install", *common, "--ftp", "--install-after-upload",
                           "--delete-after-install", "--http-port", str(http_port),
                           "--bind", "127.0.0.1", "--timeout", "120", pkg_path)
        runner.check(cleaned.returncode == 0, "install with --delete-after-install finishes successfully")
        runner.check(len(os.listdir(downloads)) == before + 1, "installed from the PC")
        runner.check(not os.path.exists(sent),
                     "and the uploaded copy is no longer on the console")

        # 7. Protected area stays blocked without advanced mode
        blocked = runner.run("ftp-mkdir", *common, "/system/test", expect=1)
        runner.check(blocked.returncode != 0 and "Protected system area" in blocked.stderr,
                     "writing to /system is refused without Advanced mode")
    finally:
        mock.terminate()
        try:
            mock.wait(timeout=10)
        except subprocess.TimeoutExpired:
            mock.kill()

    if runner.failures:
        print(f"\n{len(runner.failures)} check(s) failed:", flush=True)
        for failure in runner.failures:
            print(f"  - {failure}", flush=True)
        return 1
    print("\nIntegration: everything passed.", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
