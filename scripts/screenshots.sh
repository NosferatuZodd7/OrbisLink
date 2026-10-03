#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Generates the docs/images/ screenshots with the fake console, with no PS4 and
# no graphical environment (uses Xvfb). It is also the interface "smoke test":
# if the window does not open, the script fails.
#
#   sudo apt install xvfb
#   ./scripts/screenshots.sh [output-folder]
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$REPO/docs/images}"
BUILD="${ORBISLINK_BUILD_DIR:-$REPO/build}"
GUI="$BUILD/orbislink-gui"
DISPLAY_NUM="${ORBISLINK_DISPLAY:-:97}"
WORK="$(mktemp -d)"
CONSOLE="$WORK/console"
CONFIG="$WORK/config"

[ -x "$GUI" ] || { echo "$GUI is missing — build first (cmake --build build)." >&2; exit 1; }
command -v Xvfb >/dev/null || { echo "Xvfb is missing (apt install xvfb)." >&2; exit 1; }

mkdir -p "$OUT" "$CONSOLE/data/pkg" "$CONSOLE/data/GoldHEN" "$CONFIG/orbislink"

cleanup() {
	[ -n "${MOCK_PID:-}" ] && kill "$MOCK_PID" 2>/dev/null || true
	[ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null || true
	rm -rf "$WORK"
}
trap cleanup EXIT

echo "==> demo pkgs"
python3 "$REPO/tools/mock-console/make_test_pkg.py" "$WORK/jogo.pkg" \
	--title "Kingdom of Orbis" --content-id "UP0001-CUSA12345_00-KINGDOMORBIS0001" \
	--category gd --padding 6000000 >/dev/null
python3 "$REPO/tools/mock-console/make_test_pkg.py" "$WORK/patch.pkg" \
	--title "Kingdom of Orbis" --content-id "UP0001-CUSA12345_00-KINGDOMORBIS0001" \
	--category gp --app-version "01.08" --padding 2500000 >/dev/null
python3 "$REPO/tools/mock-console/make_test_pkg.py" "$WORK/dlc.pkg" \
	--title "Kingdom of Orbis - Shadow Pack" \
	--content-id "UP0001-CUSA12345_00-KINGDOMDLC000001" --category ac --padding 1500000 >/dev/null
python3 "$REPO/tools/mock-console/make_test_pkg.py" "$CONSOLE/data/pkg/Homebrew-Player.pkg" \
	--title "Homebrew Player" --content-id "UP0001-CUSA00001_00-HOMEBREWPLAYER01" \
	--padding 900000 >/dev/null
python3 "$REPO/tools/mock-console/make_test_pkg.py" "$CONSOLE/data/pkg/Retro-Launcher.pkg" \
	--title "Retro Launcher" --content-id "UP0001-CUSA00002_00-RETROLAUNCHER001" \
	--padding 2400000 >/dev/null
echo "config" > "$CONSOLE/data/GoldHEN/config.ini"

echo "==> demo discs and stand-ins for the downloaded emulator files"
GAMES="$WORK/games"
mkdir -p "$GAMES/PS2" "$GAMES/PS1" "$WORK/classics/emus/Jak v2"
DISC="$REPO/tools/mock-console/make_test_disc.py"
python3 "$DISC" "$GAMES/PS2/Orbis Racing (USA).iso" --ps2 --serial SLUS_209.46 --size-mb 6
python3 "$DISC" "$GAMES/PS2/Shadow Kingdom II (Europe).iso" --ps2 --serial SCES_524.12 --size-mb 5
python3 "$DISC" "$GAMES/PS2/Neon Drift (Japan).iso" --ps2 --serial SLPM_662.80 --size-mb 4
python3 "$DISC" "$GAMES/PS1/Pocket Monsters Arena (USA).bin" --ps1 --serial SLUS_009.12 --size-mb 3
printf 'FILE "Pocket Monsters Arena (USA).bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n' \
	> "$GAMES/PS1/Pocket Monsters Arena (USA).cue"
python3 "$DISC" "$GAMES/PS1/Castle Night (Europe) (Disc 1).bin" --ps1 --serial SLES_005.24 --size-mb 2
head -c 4096 /dev/urandom > "$WORK/classics/emus/Jak v2/eboot.bin"
head -c 4096 /dev/urandom > "$WORK/classics/emus/Jak v2/ps2-emu-compiler.self"
# The app would download the real ones (as easy-ps2-fpkg does); this
# points it at the stand-ins instead.
export ORBISLINK_CLASSICS_ASSETS="$WORK/classics"

echo "==> Xvfb on $DISPLAY_NUM"
Xvfb "$DISPLAY_NUM" -screen 0 1400x900x24 >/dev/null 2>&1 &
XVFB_PID=$!

echo "==> fake console"
# Port 987 is Remote Play discovery's real one; if there are no
# privileges to open it, the fake console carries on without it.
python3 "$REPO/tools/mock-console/mock_console.py" --ftp-port 2121 --api-port 12800 \
	--discovery-port 987 --root "$CONSOLE" --slow 0.5 > "$WORK/mock.log" 2>&1 &
MOCK_PID=$!
sleep 2

cat > "$CONFIG/orbislink/settings.json" <<JSON
{"console_address":"127.0.0.1","console_name":"Living room PS4","ftp_port":2121,
 "installer_port":12800,"http_bind_address":"127.0.0.1","http_port":8765,
 "restrict_to_console_ip":true,"check_already_installed":false,
 "ftp_upload_directory":"/data/pkg/","default_mode":"direct","debug_logging":false,
 "first_run_done":true,
 "games_folder":"$GAMES","convert_output_folder":"$WORK/packages",
 "consoles":[{"name":"Living room PS4","address":"127.0.0.1"},{"name":"Bedroom PS5","address":"192.0.2.10"}],
 "stream_account_id":"782riWdFIwE="}
JSON

shot() {
	local name="$1"; shift
	rm -f "$CONFIG/orbislink/queue.json"
	# The full output is kept: when the screenshot fails, what matters
	# is what the application said, not a bare "failed".
	local output="$WORK/gui-$name.log"
	DISPLAY="$DISPLAY_NUM" XDG_CONFIG_HOME="$CONFIG" QT_QPA_PLATFORM=xcb \
		"$GUI" --screenshot "$OUT/$name" "$@" > "$output" 2>&1 || true
	grep -iE "screenshot|warning: n" "$output" || true
	if [ ! -s "$OUT/$name" ]; then
		echo "Screenshot $name failed. What the application said:" >&2
		sed 's/^/    | /' "$output" >&2
		exit 1
	fi
	echo "    $OUT/$name"
}

echo "==> screenshots"
shot 01-queue.png --screenshot-delay 8000 \
	--enqueue "$WORK/jogo.pkg" --enqueue "$WORK/patch.pkg" --enqueue "$WORK/dlc.pkg"
shot 02-drop-overlay.png --screenshot-delay 2500 --demo-overlay
shot 03-ftp-browser.png --screenshot-delay 3500 --demo-tab 1
shot 04-settings.png --screenshot-delay 2500 --demo-settings
shot 05-file-menu.png --screenshot-delay 4000 --demo-tab 1 --demo-menu
shot 06-remote-play.png --screenshot-delay 3500
shot 07-register-console.png --screenshot-delay 3500 --demo-register
shot 12-keyboard-map.png --screenshot-delay 3500 --demo-keys
shot 08-diagnostics.png --screenshot-delay 4000 --demo-log
shot 09-wizard.png --screenshot-delay 3000 --demo-wizard
shot 10-update.png --screenshot-delay 3000 --demo-update
shot 13-games.png --screenshot-delay 3500 --demo-games

# The idle stage follows the theme (background, grid and text): without a light
# screenshot, white text on the white background would go unnoticed.
# Through JSON and not sed: the app rewrites the file with other formatting.
python3 - "$CONFIG/orbislink/settings.json" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    settings = json.load(f)
settings["theme"] = "light"
with open(sys.argv[1], "w") as f:
    json.dump(settings, f)
PY
shot 11-remote-play-light.png --screenshot-delay 3500

echo "Screenshots ready in $OUT"
