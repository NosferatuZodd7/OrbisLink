# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Integrates chiaki-lib (the Remote Play core) without changing a line of
# chiaki-ng — a project requirement, so the submodule can still be
# updated from upstream.
#
# chiaki-ng's top-level CMakeLists.txt is not used: it requires a libcurl
# with WebSockets (find_package(CURL REQUIRED COMPONENTS HTTP HTTPS WS WSS)),
# which is only needed for Remote Play over PSN (RUDP/holepunch).
# OrbisLink connects to the console on the local network, where that does
# not come in, and the system curl exports the curl_ws_* symbols anyway, so it links fine.
#
# Instead the two subdirectories that matter are driven directly
# — third-party/ (nanopb and jerasure) and lib/ — with the variables they
# expect from the parent.

set(ORBISLINK_CHIAKI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/chiaki-ng")

# Without the submodule there is no Remote Play. It is not a fatal error — the
# rest of the application builds and works without it — but it has to show in
# the build log, otherwise someone ships a package without streaming unawares.
if(NOT EXISTS "${ORBISLINK_CHIAKI_DIR}/lib/CMakeLists.txt")
	message(WARNING
		"O submódulo do chiaki-ng não está presente: o Remote Play NÃO vai ser "
		"compilado. Corre 'git submodule update --init --recursive' e volta a "
		"configurar, ou compila com -DORBISLINK_ENABLE_STREAM=OFF para calar este aviso.")
	set(ORBISLINK_ENABLE_STREAM OFF)
	return()
endif()

# The version must match the submodule's: chiaki-lib puts it in the packets
# it sends to the console.
set(CHIAKI_VERSION_MAJOR 1)
set(CHIAKI_VERSION_MINOR 10)
set(CHIAKI_VERSION_PATCH 0)
set(CHIAKI_VERSION "${CHIAKI_VERSION_MAJOR}.${CHIAKI_VERSION_MINOR}.${CHIAKI_VERSION_PATCH}")
add_definitions(
	-DCHIAKI_VERSION_MAJOR=${CHIAKI_VERSION_MAJOR}
	-DCHIAKI_VERSION_MINOR=${CHIAKI_VERSION_MINOR}
	-DCHIAKI_VERSION_PATCH=${CHIAKI_VERSION_PATCH}
	-DCHIAKI_VERSION="${CHIAKI_VERSION}")

# The FindFFMPEG/FindOpus modules are chiaki-ng's own.
list(APPEND CMAKE_MODULE_PATH "${ORBISLINK_CHIAKI_DIR}/cmake")

set(CHIAKI_USE_SYSTEM_NANOPB OFF)    # comes with the submodule
set(CHIAKI_USE_SYSTEM_JERASURE OFF)  # likewise (video error correction)
set(CHIAKI_USE_SYSTEM_CURL ON)       # the same curl as the rest of OrbisLink
set(CHIAKI_ENABLE_STEAM_SHORTCUT OFF)
set(CHIAKI_ENABLE_TESTS OFF)
set(CHIAKI_IS_SWITCH OFF)
set(CHIAKI_ENABLE_ANDROID OFF)
set(CHIAKI_LIB_ENABLE_MBEDTLS OFF)
set(CHIAKI_LIB_JSONC_EXTERNAL_PROJECT OFF)
set(CHIAKI_LIB_MINIUPNPC_EXTERNAL_PROJECT OFF)
set(CHIAKI_LIB_OPENSSL_EXTERNAL_PROJECT OFF)
set(CHIAKI_LIB_ENABLE_OPUS ON)       # stream audio
set(CHIAKI_ENABLE_PI_DECODER OFF)

# chiaki's video decoder is built on FFmpeg. Without it there is a session
# and audio, but no picture — so it is required.
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

# chiaki-lib's other dependencies. They are checked here, all at once, so
# whoever lacks them gets a warning saying what is missing — instead of an
# error from inside chiaki's CMakeLists, which explains nothing to someone
# who is only building OrbisLink.
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

# The nanopb generator is a Python script.
if(NOT PYTHON_EXECUTABLE)
	find_package(Python3 COMPONENTS Interpreter REQUIRED)
	set(PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
endif()

# These options are directory-wide: they apply to everything the
# add_subdirectory calls below create, not just chiaki-lib. It has to be this
# way because chiaki's CMakeLists creates more targets (gf_complete, jerasure,
# nanopb) that we have no way to name one by one.
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND MSVC)
	# clang-cl does not declare Microsoft's intrinsics by itself, and MSVC
	# does. gf-complete's gf_cpu.c relies on that:
	#
	#   gf_cpu.c(62,3): error: call to undeclared library function
	#   '__cpuidex' ... include the header <intrin.h>
	#
	# Instead of touching the submodule, intrin.h is forced at the top of each
	# file. It is not silencing: the correct declaration now exists, which is
	# what avoids an implicit declaration returning int where the real
	# function returns void.
	add_compile_options(/FIintrin.h)
	# And if another place shows up with the same problem, it should appear as
	# a warning instead of losing a whole build to it. Only this category.
	add_compile_options(-Wno-error=implicit-function-declaration)
endif()

add_subdirectory("${ORBISLINK_CHIAKI_DIR}/third-party" "${CMAKE_BINARY_DIR}/chiaki/third-party")
add_subdirectory("${ORBISLINK_CHIAKI_DIR}/lib" "${CMAKE_BINARY_DIR}/chiaki/lib")

# chiaki-lib's warnings are not ours to fix. -w is GCC's and Clang's;
# MSVC has its own, and absorbing the difference here avoids a flood
# of warnings from code we do not maintain.
if(TARGET chiaki-lib)
	if(MSVC)
		target_compile_options(chiaki-lib PRIVATE /w)
	else()
		target_compile_options(chiaki-lib PRIVATE -w)
	endif()
endif()
