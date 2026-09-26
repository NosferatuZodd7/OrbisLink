# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Integra o chiaki-lib (o núcleo do Remote Play) sem alterar uma linha do
# chiaki-ng — requisito do projeto, para o submódulo continuar a poder ser
# actualizado a partir do upstream.
#
# Não se usa o CMakeLists.txt de topo do chiaki-ng: esse exige um libcurl
# com WebSockets (find_package(CURL REQUIRED COMPONENTS HTTP HTTPS WS WSS)),
# que só é preciso para o Remote Play através da PSN (RUDP/holepunch). O
# OrbisLink liga-se à consola na rede local, onde isso não entra, e o curl
# do sistema exporta na mesma os símbolos curl_ws_*, portanto liga bem.
#
# Em vez disso conduzem-se directamente os dois subdiretórios que interessam
# — third-party/ (nanopb e jerasure) e lib/ — com as variáveis que eles
# esperam do pai.

set(ORBISLINK_CHIAKI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/chiaki-ng")

# Sem o submódulo não há Remote Play. Não é um erro fatal — o resto da
# aplicação compila e funciona sem ele — mas tem de se ver no registo da
# compilação, senão alguém distribui um pacote sem stream sem dar por isso.
if(NOT EXISTS "${ORBISLINK_CHIAKI_DIR}/lib/CMakeLists.txt")
	message(WARNING
		"O submódulo do chiaki-ng não está presente: o Remote Play NÃO vai ser "
		"compilado. Corre 'git submodule update --init --recursive' e volta a "
		"configurar, ou compila com -DORBISLINK_ENABLE_STREAM=OFF para calar este aviso.")
	set(ORBISLINK_ENABLE_STREAM OFF)
	return()
endif()

# A versão tem de bater certo com a do submódulo: o chiaki-lib põe-na nos
# pacotes que envia à consola.
set(CHIAKI_VERSION_MAJOR 1)
set(CHIAKI_VERSION_MINOR 10)
set(CHIAKI_VERSION_PATCH 0)
set(CHIAKI_VERSION "${CHIAKI_VERSION_MAJOR}.${CHIAKI_VERSION_MINOR}.${CHIAKI_VERSION_PATCH}")
add_definitions(
	-DCHIAKI_VERSION_MAJOR=${CHIAKI_VERSION_MAJOR}
	-DCHIAKI_VERSION_MINOR=${CHIAKI_VERSION_MINOR}
	-DCHIAKI_VERSION_PATCH=${CHIAKI_VERSION_PATCH}
	-DCHIAKI_VERSION="${CHIAKI_VERSION}")

# Os módulos FindFFMPEG/FindOpus são do próprio chiaki-ng.
list(APPEND CMAKE_MODULE_PATH "${ORBISLINK_CHIAKI_DIR}/cmake")

set(CHIAKI_USE_SYSTEM_NANOPB OFF)    # vem no submódulo
set(CHIAKI_USE_SYSTEM_JERASURE OFF)  # idem (correção de erros do vídeo)
set(CHIAKI_USE_SYSTEM_CURL ON)       # o mesmo curl que o resto do OrbisLink
set(CHIAKI_ENABLE_STEAM_SHORTCUT OFF)
set(CHIAKI_ENABLE_TESTS OFF)
set(CHIAKI_IS_SWITCH OFF)
set(CHIAKI_ENABLE_ANDROID OFF)
set(CHIAKI_LIB_ENABLE_MBEDTLS OFF)
set(CHIAKI_LIB_JSONC_EXTERNAL_PROJECT OFF)
set(CHIAKI_LIB_MINIUPNPC_EXTERNAL_PROJECT OFF)
set(CHIAKI_LIB_OPENSSL_EXTERNAL_PROJECT OFF)
set(CHIAKI_LIB_ENABLE_OPUS ON)       # áudio do stream
set(CHIAKI_ENABLE_PI_DECODER OFF)

# O descodificador de vídeo do chiaki assenta no FFmpeg. Sem ele há sessão e
# há áudio, mas não há imagem — por isso é obrigatório.
find_package(FFMPEG COMPONENTS avcodec avutil avformat)
if(NOT FFMPEG_FOUND)
	message(WARNING
		"O FFmpeg (avcodec/avutil/avformat) não foi encontrado: o Remote Play NÃO "
		"vai ser compilado. Em Debian/Ubuntu: apt install libavcodec-dev "
		"libavutil-dev libavformat-dev.")
	set(ORBISLINK_ENABLE_STREAM OFF)
	return()
endif()
set(CHIAKI_ENABLE_FFMPEG_DECODER ON)

# As outras dependências do chiaki-lib. São verificadas aqui, todas de uma
# vez, para quem não as tiver receber um aviso que diz o que falta — em vez
# de um erro vindo de dentro do CMakeLists do chiaki, que não explica nada a
# quem está só a compilar o OrbisLink.
set(ORBISLINK_CHIAKI_EM_FALTA "")

find_package(Opus QUIET)
if(NOT Opus_FOUND)
	list(APPEND ORBISLINK_CHIAKI_EM_FALTA "opus (libopus-dev)")
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
	pkg_check_modules(ORBISLINK_JSONC QUIET json-c)
	pkg_check_modules(ORBISLINK_MINIUPNPC QUIET miniupnpc)
	pkg_check_modules(ORBISLINK_LIBEVENT QUIET libevent)
	if(NOT ORBISLINK_JSONC_FOUND)
		list(APPEND ORBISLINK_CHIAKI_EM_FALTA "json-c (libjson-c-dev)")
	endif()
	if(NOT ORBISLINK_MINIUPNPC_FOUND)
		list(APPEND ORBISLINK_CHIAKI_EM_FALTA "miniupnpc (libminiupnpc-dev)")
	endif()
	if(NOT ORBISLINK_LIBEVENT_FOUND)
		list(APPEND ORBISLINK_CHIAKI_EM_FALTA "libevent (libevent-dev)")
	endif()
else()
	list(APPEND ORBISLINK_CHIAKI_EM_FALTA "pkg-config")
endif()

find_package(OpenSSL QUIET)
if(NOT OpenSSL_FOUND)
	list(APPEND ORBISLINK_CHIAKI_EM_FALTA "openssl (libssl-dev)")
endif()

if(ORBISLINK_CHIAKI_EM_FALTA)
	list(JOIN ORBISLINK_CHIAKI_EM_FALTA ", " ORBISLINK_CHIAKI_EM_FALTA_TEXTO)
	message(WARNING
		"O Remote Play NÃO vai ser compilado: falta ${ORBISLINK_CHIAKI_EM_FALTA_TEXTO}. "
		"Instala o que falta e volta a configurar, ou compila com "
		"-DORBISLINK_ENABLE_STREAM=OFF para calar este aviso.")
	set(ORBISLINK_ENABLE_STREAM OFF)
	return()
endif()

# O gerador do nanopb é um script de Python.
if(NOT PYTHON_EXECUTABLE)
	find_package(Python3 COMPONENTS Interpreter REQUIRED)
	set(PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
endif()

# Estas opções são de directório: valem para tudo o que os add_subdirectory
# abaixo criarem, e não só para o chiaki-lib. É preciso ser assim porque o
# CMakeLists do chiaki cria mais alvos (gf_complete, jerasure, nanopb) que
# não temos onde nomear um a um.
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND MSVC)
	# O clang-cl não declara sozinho os intrínsecos da Microsoft, e o MSVC
	# declara. O gf_cpu.c do gf-complete conta com isso:
	#
	#   gf_cpu.c(62,3): error: call to undeclared library function
	#   '__cpuidex' ... include the header <intrin.h>
	#
	# Em vez de mexer no submódulo, força-se o intrin.h à cabeça de cada
	# ficheiro. Não é um silenciar: a declaração correcta passa a existir,
	# que é o que evita um implicit declaration a devolver int onde a função
	# real devolve void.
	add_compile_options(/FIintrin.h)
	# E se aparecer outro sítio com o mesmo problema, quero vê-lo como aviso
	# em vez de perder um build inteiro por causa dele. Só esta categoria.
	add_compile_options(-Wno-error=implicit-function-declaration)
endif()

add_subdirectory("${ORBISLINK_CHIAKI_DIR}/third-party" "${CMAKE_BINARY_DIR}/chiaki/third-party")
add_subdirectory("${ORBISLINK_CHIAKI_DIR}/lib" "${CMAKE_BINARY_DIR}/chiaki/lib")

# Os avisos do chiaki-lib não são nossos para corrigir. O -w é do GCC e do
# Clang; o MSVC tem o seu próprio, e engolir a diferença aqui evita uma
# enxurrada de avisos de código que não mantemos.
if(TARGET chiaki-lib)
	if(MSVC)
		target_compile_options(chiaki-lib PRIVATE /w)
	else()
		target_compile_options(chiaki-lib PRIVATE -w)
	endif()
endif()
