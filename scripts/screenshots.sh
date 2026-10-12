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
CONSOLE5="$WORK/console5"
CONFIG="$WORK/config"
# This PC's home for the app: Desktop, Downloads and Documents are places
# in the games library.
DEMO_HOME="$WORK/home"

[ -x "$GUI" ] || { echo "$GUI is missing — build first (cmake --build build)." >&2; exit 1; }
command -v Xvfb >/dev/null || { echo "Xvfb is missing (apt install xvfb)." >&2; exit 1; }

mkdir -p "$OUT" "$CONSOLE/data/pkg" "$CONSOLE/data/GoldHEN" "$CONFIG/orbislink" \
	"$DEMO_HOME/Desktop" "$DEMO_HOME/Downloads" "$DEMO_HOME/Documents"

cleanup() {
	[ -n "${MOCK_PID:-}" ] && kill "$MOCK_PID" 2>/dev/null || true
	[ -n "${MOCK5_PID:-}" ] && kill "$MOCK5_PID" 2>/dev/null || true
	[ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null || true
	rm -rf "$WORK"
}
trap cleanup EXIT

# Real games, homebrew and payloads, so the pictures show what people really
# use; but nothing of a commercial game is in them: no cover or icon (covers
# carry logos), only its name. The homebrew apps' icons are their own,
# fetched from where their authors publish them (without network, the apps
# go without).
ICONS="$WORK/icons"
mkdir -p "$ICONS"
fetch_icon() { # <url> <out>
	curl -sf --max-time 15 -o "$2" "$1" || rm -f "$2"
}
fetch_icon "https://raw.githubusercontent.com/bucanero/apollo-ps4/master/sce_sys/icon0.png" "$ICONS/apollo.png"
for id in PPSA99169 PPSA85300 PPSA99666 PPSA99303 PPSA59322 PPSA99203; do
	fetch_icon "https://homebrew.page/api/v1/icons/$id-256.png" "$ICONS/$id.png"
done
pkg() { # <out> <title> <content id> [category] [version] [icon] [padding]
	local icon=(--no-icon)
	[ -n "${6:-}" ] && [ -s "$6" ] && icon=(--icon "$6")
	python3 "$REPO/tools/mock-console/make_test_pkg.py" "$1" --title "$2" --content-id "$3" \
		--category "${4:-gd}" --app-version "${5:-01.00}" "${icon[@]}" --padding "${7:-300000}" >/dev/null
}

echo "==> demo pkgs"
pkg "$WORK/jogo.pkg" "Bloodborne" "EP9000-CUSA00207_00-BLOODBORNE0000EU" gd 01.00 "" 6000000
pkg "$WORK/patch.pkg" "Bloodborne" "EP9000-CUSA00207_00-BLOODBORNE0000EU" gp 01.09 "" 2500000
pkg "$WORK/dlc.pkg" "Bloodborne The Old Hunters" "EP9000-CUSA00207_00-BLOODBORNEDLC001" ac 01.00 "" 1500000
pkg "$CONSOLE/data/pkg/Apollo-Save-Tool.pkg" "Apollo Save Tool" "UP9999-APOL00004_00-APOLLO0000000PS4" \
	gd 01.00 "$ICONS/apollo.png" 900000
pkg "$CONSOLE/data/pkg/Bloodborne-Patch-1.09.pkg" "Bloodborne" "EP9000-CUSA00207_00-BLOODBORNE0000EU" \
	gp 01.09 "" 2400000
echo "config" > "$CONSOLE/data/GoldHEN/config.ini"

echo "==> the PS4's saves and payloads"
# Saves of real games, as a PS4 keeps them (random bytes, made-up accounts).
python3 "$REPO/tools/mock-console/make_test_saves.py" "$CONSOLE" >/dev/null
# GoldHEN's payload and plugin folders, with real payload and plugin names
# (the files are stand-ins).
mkdir -p "$CONSOLE/data/GoldHEN/payloads" "$CONSOLE/data/payloads" "$CONSOLE/data/GoldHEN/plugins"
for file in GoldHEN/payloads/goldhen.bin payloads/app2usb.bin payloads/disable-updates.bin \
	payloads/history-blocker.bin payloads/kernel-dumper.bin payloads/rif-renamer.bin \
	GoldHEN/plugins/game_patch.prx GoldHEN/plugins/no_share_watermark.prx GoldHEN/plugins/afr.prx; do
	head -c 20000 /dev/urandom > "$CONSOLE/data/$file"
done
printf '[default]\n/data/GoldHEN/plugins/game_patch.prx=true\n/data/GoldHEN/plugins/no_share_watermark.prx=true\n/data/GoldHEN/plugins/afr.prx=false\n' \
	> "$CONSOLE/data/GoldHEN/plugins.ini"

echo "==> the PS5: etaHEN, the autoloader, homebrew apps"
mkdir -p "$CONSOLE5/data/etaHEN/payloads" "$CONSOLE5/data/ps5_autoloader"
echo "config" > "$CONSOLE5/data/etaHEN/config.ini"
for file in etaHEN/payloads/shadowmountplus.elf ps5_autoloader/kstuff.elf ps5_autoloader/shadowmountplus.elf; do
	head -c 20000 /dev/urandom > "$CONSOLE5/data/$file"
done
printf 'kstuff.elf\n!3000\nshadowmountplus.elf\n' > "$CONSOLE5/data/ps5_autoloader/autoload.txt"
# Real homebrew from the store's catalog: two installed (one older than the
# catalog's), four in the library folder as app folders, beside a PS4 package.
python3 - "$CONSOLE5/data" "$ICONS" <<'PY'
import json, os, shutil, sys
data, icons = sys.argv[1], sys.argv[2]
def app(folder, title_id, name, version):
    os.makedirs(os.path.join(folder, "sce_sys"), exist_ok=True)
    with open(os.path.join(folder, "eboot.bin"), "wb") as f:
        f.write(os.urandom(4096))
    with open(os.path.join(folder, "sce_sys", "param.json"), "w") as f:
        json.dump({"titleId": title_id, "contentVersion": version,
                   "localizedParameters": {"defaultLanguage": "en-US",
                                           "en-US": {"titleName": name}}}, f)
    icon = os.path.join(icons, title_id + ".png")
    if os.path.exists(icon):
        shutil.copy(icon, os.path.join(folder, "sce_sys", "icon0.png"))
app(os.path.join(data, "homebrew", "PPSA99169"), "PPSA99169", "RetroArch", "1.0.0-beta.1")
app(os.path.join(data, "homebrew", "PPSA85300"), "PPSA85300", "VLC", "0.3")
for title_id, name, version in (("PPSA99666", "DOOM Native Port", "1.1.1"),
                                ("PPSA99303", "Quake3e", "1.1.0"),
                                ("PPSA59322", "ccleste", "1.00"),
                                ("PPSA99203", "PS5SX2", "vk-285-162")):
    app(os.path.join(data, "OrbisLinkFPKG", name), title_id, name, version)
PY
pkg "$CONSOLE5/data/OrbisLinkFPKG/Apollo-Save-Tool.pkg" "Apollo Save Tool" \
	"UP9999-APOL00004_00-APOLLO0000000PS4" gd 01.00 "$ICONS/apollo.png"
pkg "$DEMO_HOME/Downloads/Apollo-Save-Tool.pkg" "Apollo Save Tool" \
	"UP9999-APOL00004_00-APOLLO0000000PS4" gd 01.00 "$ICONS/apollo.png"

echo "==> demo discs and stand-ins for the downloaded emulator files"
GAMES="$WORK/games"
mkdir -p "$GAMES/PS2" "$GAMES/PS1" "$WORK/classics/emus/Jak v2"
DISC="$REPO/tools/mock-console/make_test_disc.py"
# Real games' names and serials, on stand-in discs; their covers are not
# fetched (ORBISLINK_NO_COVER_DOWNLOADS, below).
python3 "$DISC" "$GAMES/PS2/Gran Turismo 4 (Europe).iso" --ps2 --serial SCES_517.19 --size-mb 6
python3 "$DISC" "$GAMES/PS2/Shadow of the Colossus (Europe).iso" --ps2 --serial SCES_533.26 --size-mb 5
python3 "$DISC" "$GAMES/PS2/God of War (Europe).iso" --ps2 --serial SCES_531.33 --size-mb 4
python3 "$DISC" "$GAMES/PS1/Crash Bandicoot (Europe).bin" --ps1 --serial SCES_003.44 --size-mb 3
printf 'FILE "Crash Bandicoot (Europe).bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n' \
	> "$GAMES/PS1/Crash Bandicoot (Europe).cue"
python3 "$DISC" "$GAMES/PS1/Final Fantasy VII (Europe) (Disc 1).bin" --ps1 --serial SCES_008.67 --size-mb 2
head -c 4096 /dev/urandom > "$WORK/classics/emus/Jak v2/eboot.bin"
head -c 4096 /dev/urandom > "$WORK/classics/emus/Jak v2/ps2-emu-compiler.self"
# The app would download the real ones (as easy-ps2-fpkg does); this
# points it at the stand-ins instead.
export ORBISLINK_CLASSICS_ASSETS="$WORK/classics"
export ORBISLINK_NO_COVER_DOWNLOADS=1

echo "==> Xvfb on $DISPLAY_NUM"
Xvfb "$DISPLAY_NUM" -screen 0 1400x900x24 >/dev/null 2>&1 &
XVFB_PID=$!

echo "==> fake console"
# Port 987 is Remote Play discovery's real one; if there are no
# privileges to open it, the fake console carries on without it.
python3 "$REPO/tools/mock-console/mock_console.py" --ftp-port 2121 --api-port 12800 \
	--discovery-port 987 --root "$CONSOLE" --slow 0.5 > "$WORK/mock.log" 2>&1 &
MOCK_PID=$!
# The PS5 beside it, on the next loopback address: etaHEN's FTP and DPI v2.
python3 "$REPO/tools/mock-console/mock_console.py" --host 127.0.0.2 --ftp-port 1337 \
	--api-port 12800 --dpi-v2 --discovery-port 9302 --discovery-ps5 --discovery-name "Bedroom PS5" \
	--root "$CONSOLE5" > "$WORK/mock5.log" 2>&1 &
MOCK5_PID=$!
sleep 2

cat > "$CONFIG/orbislink/settings.json" <<JSON
{"console_address":"127.0.0.1","console_name":"Living room PS4","ftp_port":2121,
 "installer_port":12800,"http_bind_address":"127.0.0.1","http_port":8765,
 "restrict_to_console_ip":true,"check_already_installed":false,
 "ftp_upload_directory":"/data/pkg/","default_mode":"direct","debug_logging":false,
 "first_run_done":true,
 "games_folder":"$GAMES","convert_output_folder":"$WORK/packages",
 "consoles":[{"name":"Living room PS4","address":"127.0.0.1","type":"ps4"},
             {"name":"Bedroom PS5","address":"127.0.0.2","type":"ps5"}],
 "ftp_port_ps5":1337,"save_vault_folder":"$WORK/vault",
 "stream_account_id":"782riWdFIwE="}
JSON

shot() {
	local name="$1"; shift
	rm -f "$CONFIG/orbislink/queue.json"
	# The full output is kept: when the screenshot fails, what matters
	# is what the application said, not a bare "failed".
	local output="$WORK/gui-$name.log"
	DISPLAY="$DISPLAY_NUM" XDG_CONFIG_HOME="$CONFIG" HOME="$DEMO_HOME" QT_QPA_PLATFORM=xcb \
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
shot 15-saves.png --screenshot-delay 6000 --demo-view saves
shot 16-payloads.png --screenshot-delay 5000 --demo-view payloads

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

# The PS5 in use: its games library, the homebrew store's catalog (two of
# its apps installed there) and the community's payload list.
python3 - "$CONFIG/orbislink/settings.json" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    settings = json.load(f)
settings["theme"] = "dark"
settings["console_address"] = "127.0.0.2"
settings["console_name"] = "Bedroom PS5"
with open(sys.argv[1], "w") as f:
    json.dump(settings, f)
PY
shot 14-library.png --screenshot-delay 8000 --demo-view games
shot 17-store.png --screenshot-delay 20000 --demo-view store
shot 18-payloads-ps5.png --screenshot-delay 10000 --demo-view payloads-library

echo "Screenshots ready in $OUT"
