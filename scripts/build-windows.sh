#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Produz o executável Windows x64 do OrbisLink e o instalador, a partir de
# Linux, com mingw-w64 + NSIS. Não precisa de Windows nem de MSVC.
#
#   sudo apt install mingw-w64 nsis cmake ninja-build git zip
#   ./scripts/build-windows.sh
#
# ORBISLINK_WINDOWS_TESTS=ON compila também os testes como .exe (úteis para
# os correr sob Wine — ver scripts/test-windows-wine.sh).
#
# Resultado em dist/:
#   orbislink-cli.exe                     executável autónomo (sem DLLs extra)
#   OrbisLink-<versão>-windows-x64.zip    versão portátil
#   OrbisLink-<versão>-setup.exe          instalador
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${ORBISLINK_VERSION:-0.1.0}"
VERSION="${VERSION#v}"   # "v1.2.3" -> "1.2.3"

# O VIProductVersion do Windows tem de ser X.X.X.X só com números; a versão
# visível pode ser "1.2.3-dev". Extrai-se a parte numérica e completa-se.
version_numeric() {
	local numeric
	numeric="$(printf '%s' "$1" | grep -oE '^[0-9]+(\.[0-9]+)*' || true)"
	[ -n "$numeric" ] || numeric="0"
	local IFS='.'
	# shellcheck disable=SC2206
	local parts=($numeric)
	while [ "${#parts[@]}" -lt 4 ]; do parts+=("0"); done
	echo "${parts[0]}.${parts[1]}.${parts[2]}.${parts[3]}"
}
VERSION_NUMERIC="$(version_numeric "$VERSION")"
CURL_TAG="${CURL_TAG:-curl-8_11_1}"
WORK="${ORBISLINK_BUILD_DIR:-$REPO/build-windows}"
CURL_PREFIX="$WORK/curl-install"
DIST="$REPO/dist"
STAGE="$DIST/windows"

need() { command -v "$1" >/dev/null || { echo "Falta o comando '$1'." >&2; exit 1; }; }
need x86_64-w64-mingw32-g++
need cmake
need git

GENERATOR=(-G "Unix Makefiles")
if command -v ninja >/dev/null; then GENERATOR=(-G Ninja); fi

echo "==> 1/4 libcurl estático para mingw-w64 ($CURL_TAG)"
if [ ! -f "$CURL_PREFIX/lib/libcurl.a" ]; then
	if [ ! -d "$WORK/curl-src" ]; then
		git clone --depth 1 --branch "$CURL_TAG" https://github.com/curl/curl "$WORK/curl-src"
	fi
	# Sem OpenSSL: o Schannel do próprio Windows chega e evita dependências.
	cmake -S "$WORK/curl-src" -B "$WORK/curl-build" "${GENERATOR[@]}" \
		-DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/toolchain-mingw-w64.cmake" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$CURL_PREFIX" \
		-DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
		-DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF \
		-DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF -DCURL_USE_LIBPSL=OFF \
		-DCURL_USE_LIBSSH2=OFF -DUSE_LIBIDN2=OFF -DCURL_ZLIB=OFF \
		-DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DCURL_DISABLE_LDAP=ON \
		-DENABLE_UNIX_SOCKETS=OFF
	cmake --build "$WORK/curl-build"
	cmake --install "$WORK/curl-build"
else
	echo "    (já compilado em $CURL_PREFIX)"
fi

echo "==> 2/4 orbislink-cli.exe"
cmake -S "$REPO" -B "$WORK/orbislink" "${GENERATOR[@]}" \
	-DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/toolchain-mingw-w64.cmake" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH="$CURL_PREFIX" \
	-DORBISLINK_BUILD_TESTS="${ORBISLINK_WINDOWS_TESTS:-OFF}" \
	-DORBISLINK_VERSION_NAME="$VERSION" \
	-DORBISLINK_REPOSITORY="${ORBISLINK_REPOSITORY:-}" \
	`# Este build só faz a linha de comandos, que não tem Remote Play. A` \
	`# interface gráfica (essa sim com stream) vem compilada do job de MSVC.` \
	-DORBISLINK_ENABLE_STREAM=OFF \
	-DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"
cmake --build "$WORK/orbislink"
x86_64-w64-mingw32-strip "$WORK/orbislink/orbislink-cli.exe"

echo "==> 3/4 pasta de distribuição"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$WORK/orbislink/orbislink-cli.exe" "$STAGE/"
# Ícone da aplicação: usado pelo instalador, pelos atalhos e pela entrada em
# "Aplicações instaladas".
cp "$REPO/src/icons/orbisDDM.ico" "$STAGE/orbislink.ico"
# Ficheiros de texto em CRLF, para abrirem bem no Bloco de Notas.
sed 's/$/\r/' "$REPO/LICENSE" > "$STAGE/LICENSE.txt"
# O endereço do projecto vem de fora (ORBISLINK_REPO_URL); no CI é o
# repositório onde a compilação correu. Quando não vem, a linha do
# código-fonte desaparece em vez de ficar meia escrita.
if [ -n "${ORBISLINK_REPO_URL:-}" ]; then
	sed "s/@VERSION@/$VERSION/g; s|@REPO_URL@|${ORBISLINK_REPO_URL}|g" \
		"$REPO/packaging/windows/LEIA-ME.txt"
else
	sed "s/@VERSION@/$VERSION/g; /@REPO_URL@/d" "$REPO/packaging/windows/LEIA-ME.txt"
fi | sed 's/$/\r/' > "$STAGE/LEIA-ME.txt"
# Os .bat precisam mesmo de CRLF.
sed 's/$/\r/' "$REPO/packaging/windows/diagnostico.bat" > "$STAGE/diagnostico.bat"

# Se houver uma interface gráfica já compilada (vem do job de Qt+MSVC, que
# corre noutra máquina), entra no instalador e no zip.
GUI_DEFINE=()
if [ -n "${ORBISLINK_GUI_DIR:-}" ] && [ -d "$ORBISLINK_GUI_DIR" ]; then
	echo "    a incluir a interface gráfica de $ORBISLINK_GUI_DIR"
	mkdir -p "$STAGE/gui"
	cp -r "$ORBISLINK_GUI_DIR"/. "$STAGE/gui/"
	GUI_DEFINE=("-DINCLUDE_GUI=1")
fi

if command -v zip >/dev/null; then
	(cd "$STAGE" && zip -q -r "$DIST/OrbisLink-$VERSION-windows-x64.zip" .)
	echo "    dist/OrbisLink-$VERSION-windows-x64.zip"
fi

echo "==> 4/4 instalador"
if command -v makensis >/dev/null; then
	NSIS_URL=()
	[ -n "${ORBISLINK_REPO_URL:-}" ] && NSIS_URL=("-DAPP_URL=${ORBISLINK_REPO_URL}")
	makensis -V2 \
		"${GUI_DEFINE[@]}" \
		"${NSIS_URL[@]+"${NSIS_URL[@]}"}" \
		"-DAPP_VERSION=$VERSION" \
		"-DAPP_VERSION_NUMERIC=$VERSION_NUMERIC" \
		"-DSOURCE_DIR=$STAGE" \
		"-DOUTPUT_FILE=$DIST/OrbisLink-$VERSION-setup.exe" \
		"$REPO/packaging/windows/orbislink.nsi"
	echo "    dist/OrbisLink-$VERSION-setup.exe"
else
	echo "    makensis não encontrado: instalador não gerado (apt install nsis)."
fi

echo
echo "Pronto. Conteúdo de dist/:"
ls -lh "$DIST" | tail -n +2
