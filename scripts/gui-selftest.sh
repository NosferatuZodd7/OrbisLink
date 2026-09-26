#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Testes da interface que precisam de uma janela a sério: arranca a consola
# falsa e o Xvfb e corre as auto-verificações do orbislink-gui.
#
#   sudo apt install xvfb
#   ./scripts/gui-selftest.sh
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ORBISLINK_BUILD_DIR:-$REPO/build}"
GUI="$BUILD/orbislink-gui"
DISPLAY_NUM="${ORBISLINK_DISPLAY:-:98}"
WORK="$(mktemp -d)"
CONSOLE="$WORK/consola"
CONFIG="$WORK/config"

[ -x "$GUI" ] || { echo "Falta $GUI — compila primeiro (cmake --build build)." >&2; exit 1; }
command -v Xvfb >/dev/null || { echo "Falta o Xvfb (apt install xvfb)." >&2; exit 1; }

mkdir -p "$CONSOLE/data/pkg" "$CONFIG/orbislink"

cleanup() {
	[ -n "${MOCK_PID:-}" ] && kill "$MOCK_PID" 2>/dev/null || true
	[ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null || true
	rm -rf "$WORK"
}
trap cleanup EXIT

Xvfb "$DISPLAY_NUM" -screen 0 1400x900x24 >/dev/null 2>&1 &
XVFB_PID=$!
python3 "$REPO/tools/mock-console/mock_console.py" --ftp-port 2121 --api-port 12800 \
	--root "$CONSOLE" > "$WORK/mock.log" 2>&1 &
MOCK_PID=$!
sleep 2

cat > "$CONFIG/orbislink/settings.json" <<JSON
{"console_address":"127.0.0.1","console_name":"PS4 de teste","ftp_port":2121,
 "installer_port":12800,"http_bind_address":"127.0.0.1","http_port":8765,
 "restrict_to_console_ip":true,"check_already_installed":false,
 "ftp_upload_directory":"/data/pkg/","default_mode":"direct","debug_logging":false,
 "first_run_done":true}
JSON

echo "==> arrastar e largar"
if DISPLAY="$DISPLAY_NUM" XDG_CONFIG_HOME="$CONFIG" QT_QPA_PLATFORM=xcb \
	"$GUI" --software --selftest-drag 2>&1 | grep -vE "^Arranque|^Serviços|^Registo"; then
	echo "    passou"
else
	echo "    FALHOU" >&2
	exit 1
fi
