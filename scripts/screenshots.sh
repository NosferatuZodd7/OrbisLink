#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Gera as capturas de ecrã de docs/images/ com a consola falsa, sem PS4 e sem
# ambiente gráfico (usa Xvfb). É também o "smoke test" da interface: se a
# janela não abrir, o script falha.
#
#   sudo apt install xvfb
#   ./scripts/screenshots.sh [pasta-de-saida]
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$REPO/docs/images}"
BUILD="${ORBISLINK_BUILD_DIR:-$REPO/build}"
GUI="$BUILD/orbislink-gui"
DISPLAY_NUM="${ORBISLINK_DISPLAY:-:97}"
WORK="$(mktemp -d)"
CONSOLE="$WORK/consola"
CONFIG="$WORK/config"

[ -x "$GUI" ] || { echo "Falta $GUI — compila primeiro (cmake --build build)." >&2; exit 1; }
command -v Xvfb >/dev/null || { echo "Falta o Xvfb (apt install xvfb)." >&2; exit 1; }

mkdir -p "$OUT" "$CONSOLE/data/pkg" "$CONSOLE/data/GoldHEN" "$CONFIG/orbislink"

cleanup() {
	[ -n "${MOCK_PID:-}" ] && kill "$MOCK_PID" 2>/dev/null || true
	[ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null || true
	rm -rf "$WORK"
}
trap cleanup EXIT

echo "==> pkg de demonstração"
python3 "$REPO/tools/mock-console/make_test_pkg.py" "$WORK/jogo.pkg" \
	--title "Kingdom of Orbis" --content-id "UP0001-CUSA12345_00-KINGDOMORBIS0001" \
	--category gd --padding 6000000 >/dev/null
python3 "$REPO/tools/mock-console/make_test_pkg.py" "$WORK/patch.pkg" \
	--title "Kingdom of Orbis" --content-id "UP0001-CUSA12345_00-KINGDOMORBIS0001" \
	--category gp --app-version "01.08" --padding 2500000 >/dev/null
python3 "$REPO/tools/mock-console/make_test_pkg.py" "$WORK/dlc.pkg" \
	--title "Kingdom of Orbis - Pacote Sombrio" \
	--content-id "UP0001-CUSA12345_00-KINGDOMDLC000001" --category ac --padding 1500000 >/dev/null
python3 "$REPO/tools/mock-console/make_test_pkg.py" "$CONSOLE/data/pkg/Homebrew-Player.pkg" \
	--title "Homebrew Player" --content-id "UP0001-CUSA00001_00-HOMEBREWPLAYER01" \
	--padding 900000 >/dev/null
python3 "$REPO/tools/mock-console/make_test_pkg.py" "$CONSOLE/data/pkg/Retro-Launcher.pkg" \
	--title "Retro Launcher" --content-id "UP0001-CUSA00002_00-RETROLAUNCHER001" \
	--padding 2400000 >/dev/null
echo "config" > "$CONSOLE/data/GoldHEN/config.ini"

echo "==> Xvfb em $DISPLAY_NUM"
Xvfb "$DISPLAY_NUM" -screen 0 1400x900x24 >/dev/null 2>&1 &
XVFB_PID=$!

echo "==> consola falsa"
# A porta 987 é a real da descoberta do Remote Play; se não houver
# privilégios para a abrir, a consola falsa continua sem ela.
python3 "$REPO/tools/mock-console/mock_console.py" --ftp-port 2121 --api-port 12800 \
	--discovery-port 987 --root "$CONSOLE" --slow 0.5 > "$WORK/mock.log" 2>&1 &
MOCK_PID=$!
sleep 2

cat > "$CONFIG/orbislink/settings.json" <<JSON
{"console_address":"127.0.0.1","console_name":"PS4 da sala","ftp_port":2121,
 "installer_port":12800,"http_bind_address":"127.0.0.1","http_port":8765,
 "restrict_to_console_ip":true,"check_already_installed":false,
 "ftp_upload_directory":"/data/pkg/","default_mode":"direct","debug_logging":false,
 "first_run_done":true,
 "stream_account_id":"782riWdFIwE="}
JSON

shot() {
	local name="$1"; shift
	rm -f "$CONFIG/orbislink/queue.json"
	# A saída completa fica guardada: quando a captura falha, o que
	# interessa é o que a aplicação disse, não um "falhou" seco.
	local registo="$WORK/gui-$name.log"
	DISPLAY="$DISPLAY_NUM" XDG_CONFIG_HOME="$CONFIG" QT_QPA_PLATFORM=xcb \
		"$GUI" --screenshot "$OUT/$name" "$@" > "$registo" 2>&1 || true
	grep -iE "captura|warning: n" "$registo" || true
	if [ ! -s "$OUT/$name" ]; then
		echo "Falhou a captura $name. O que a aplicação disse:" >&2
		sed 's/^/    | /' "$registo" >&2
		exit 1
	fi
	echo "    $OUT/$name"
}

echo "==> capturas"
shot 01-fila.png --screenshot-delay 8000 \
	--enqueue "$WORK/jogo.pkg" --enqueue "$WORK/patch.pkg" --enqueue "$WORK/dlc.pkg"
shot 02-drop-overlay.png --screenshot-delay 2500 --demo-overlay
shot 03-ftp-browser.png --screenshot-delay 3500 --demo-tab 1
shot 04-definicoes.png --screenshot-delay 2500 --demo-settings
shot 05-menu-ficheiro.png --screenshot-delay 4000 --demo-tab 1 --demo-menu
shot 06-remote-play.png --screenshot-delay 3500
shot 07-registar-consola.png --screenshot-delay 3500 --demo-register
shot 12-mapa-teclado.png --screenshot-delay 3500 --demo-keys
shot 08-diagnostico.png --screenshot-delay 4000 --demo-log
shot 09-assistente.png --screenshot-delay 3000 --demo-wizard
shot 10-actualizacao.png --screenshot-delay 3000 --demo-update

# O palco parado segue o tema (fundo, grelha e texto): sem uma captura no
# claro, um texto branco sobre o fundo branco passaria sem se ver.
# Pelo JSON e não por sed: a app regrava o ficheiro com outra formatação.
python3 - "$CONFIG/orbislink/settings.json" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    definicoes = json.load(f)
definicoes["theme"] = "claro"
with open(sys.argv[1], "w") as f:
    json.dump(definicoes, f)
PY
shot 11-remote-play-claro.png --screenshot-delay 3500

echo "Capturas prontas em $OUT"
