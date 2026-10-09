# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
project(OrbisLink VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ORBISLINK_VERSION_NAME "${PROJECT_VERSION}" CACHE STRING
	"Version shown by the application and written to the log")

# Where the application fetches updates from, as "owner/name".
#
# It is empty on purpose. CI passes -DORBISLINK_REPOSITORY with the
# repository the build is running in, so whoever forks this project
# automatically points at their own copy, without editing a line — and
# no name is written in the code.
#
# Without this set, the update check says it is not configured, instead
# of hitting a repository that is not its own.
set(ORBISLINK_REPOSITORY "" CACHE STRING
	"GitHub repository \"owner/name\" the updates come from")

option(ORBISLINK_BUILD_TESTS "Build the core tests" ON)
option(ORBISLINK_BUILD_GUI "Build the graphical interface (needs Qt 6)" ON)
option(ORBISLINK_BUILD_CLI "Build the command-line tool" ON)
option(ORBISLINK_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(ORBISLINK_ENABLE_STREAM "Build Remote Play (needs the chiaki-ng submodule)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

if(WIN32)
	# The resources (.rc) put the application icon inside the executable.
	enable_language(RC)
	# NOMINMAX is required: <windows.h> defines min/max macros that break
	# std::min/std::max. WIN32_LEAN_AND_MEAN trims unneeded headers and
	# _WIN32_WINNT sets the minimum at Windows 7 (GetAdaptersAddresses and co.).
	add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601 WINVER=0x0601)
endif()

if(MSVC)
	# Without /utf-8 MSVC reads non-ASCII literals in the system codepage and
	# the messages come out garbled (and the tests fail comparing strings).
	add_compile_options(/utf-8)
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Remote Play lives in a separate library: the core, the CLI and the tests
# still build without chiaki and without FFmpeg.
if(ORBISLINK_ENABLE_STREAM)
	include(ChiakiLib)
endif()

add_library(orbislink_core STATIC
	src/orbislink/common/json.cpp
	src/orbislink/common/log.cpp
	src/orbislink/common/pcm_convert.cpp
	src/orbislink/common/util.cpp
	src/orbislink/console/console_manager.cpp
	src/orbislink/ftp/ftp_client.cpp
	src/orbislink/http/local_http_server.cpp
	src/orbislink/installer/error_codes.cpp
	src/orbislink/installer/rpi_client.cpp
	src/orbislink/net/http_client.cpp
	src/orbislink/net/net_utils.cpp
	src/orbislink/net/socket_compat.cpp
	src/orbislink/pkg/pkg_inspector.cpp
	src/orbislink/pkg/sfo_parser.cpp
	src/orbislink/queue/install_queue.cpp
	src/orbislink/saves/save_vault.cpp
	src/orbislink/settings/settings_store.cpp
	src/orbislink/update/sha256.cpp
	src/orbislink/update/update_checker.cpp
	src/orbislink/update/version.cpp
)

if(ORBISLINK_ENABLE_STREAM)
	add_library(orbislink_stream STATIC
		src/orbislink/stream/chiaki_log_bridge.cpp
		src/orbislink/stream/credentials.cpp
		src/orbislink/stream/account_id.cpp
		src/orbislink/stream/discovery.cpp
		src/orbislink/stream/registration.cpp
		src/orbislink/stream/session.cpp
		src/orbislink/stream/stream_trace.cpp
		src/orbislink/stream/stream_types.cpp
	)
	target_include_directories(orbislink_stream PUBLIC src)
	target_link_libraries(orbislink_stream PUBLIC orbislink_core chiaki-lib)
	target_compile_definitions(orbislink_stream PUBLIC ORBISLINK_HAS_STREAM=1)
endif()

# PS1/PS2 discs into PS4 packages: the fake PKG builder (ported from
# LibOrbisPkg, LGPL-3.0) needs OpenSSL, which Remote Play already brings.
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
	add_library(orbislink_fpkg STATIC
		src/orbislink/fpkg/classic_converter.cpp
		src/orbislink/fpkg/classics_assets.cpp
		src/orbislink/fpkg/disc_scanner.cpp
		src/orbislink/fpkg/fpkg_crypto.cpp
		src/orbislink/fpkg/fpkg_keys.cpp
		src/orbislink/fpkg/param_sfo.cpp
		src/orbislink/fpkg/pfs_builder.cpp
		src/orbislink/fpkg/pkg_builder.cpp
		src/orbislink/fpkg/targz.cpp
	)
	target_include_directories(orbislink_fpkg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
	target_link_libraries(orbislink_fpkg PUBLIC OpenSSL::Crypto Threads::Threads)
	target_compile_definitions(orbislink_fpkg PUBLIC ORBISLINK_HAS_FPKG=1)
	if(MSVC)
		target_compile_options(orbislink_fpkg PRIVATE /W4)
	else()
		target_compile_options(orbislink_fpkg PRIVATE -Wall -Wextra)
		if(ORBISLINK_WARNINGS_AS_ERRORS)
			target_compile_options(orbislink_fpkg PRIVATE -Werror)
		endif()
	endif()
	add_executable(orbislink-fpkg tools/fpkg/main.cpp)
	target_link_libraries(orbislink-fpkg PRIVATE orbislink_fpkg)
endif()

# The PS5 homebrew store: the signed catalog of homebrew.page (Ed25519,
# through OpenSSL) and its ZIP apps (the inflater of the fpkg library).
if(TARGET orbislink_fpkg)
	add_library(orbislink_store STATIC
		src/orbislink/store/store_catalog.cpp
		src/orbislink/store/store_installer.cpp
		src/orbislink/store/zip_reader.cpp
	)
	target_include_directories(orbislink_store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
	target_link_libraries(orbislink_store PUBLIC orbislink_core orbislink_fpkg OpenSSL::Crypto)
	target_compile_definitions(orbislink_store PUBLIC ORBISLINK_HAS_STORE=1)
	if(MSVC)
		target_compile_options(orbislink_store PRIVATE /W4)
	else()
		target_compile_options(orbislink_store PRIVATE -Wall -Wextra)
		if(ORBISLINK_WARNINGS_AS_ERRORS)
			target_compile_options(orbislink_store PRIVATE -Werror)
		endif()
	endif()
endif()

target_compile_definitions(orbislink_core PUBLIC
	ORBISLINK_REPOSITORY_STRING="${ORBISLINK_REPOSITORY}")
target_include_directories(orbislink_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(orbislink_core PUBLIC CURL::libcurl Threads::Threads)

if(WIN32)
	target_link_libraries(orbislink_core PUBLIC ws2_32 iphlpapi)
endif()

if(MSVC)
	target_compile_options(orbislink_core PRIVATE /W4)
	if(ORBISLINK_WARNINGS_AS_ERRORS)
		target_compile_options(orbislink_core PRIVATE /WX)
	endif()
else()
	target_compile_options(orbislink_core PRIVATE -Wall -Wextra)
	if(ORBISLINK_WARNINGS_AS_ERRORS)
		target_compile_options(orbislink_core PRIVATE -Werror)
	endif()
endif()

if(ORBISLINK_BUILD_CLI)
	add_executable(orbislink-cli tools/cli/main.cpp
		$<$<BOOL:${WIN32}>:${CMAKE_CURRENT_SOURCE_DIR}/src/icons/orbislink.rc>)
	target_link_libraries(orbislink-cli PRIVATE orbislink_core)
	if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
		add_custom_command(TARGET orbislink-cli POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E
				$<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:orbislink-cli>>,copy_if_different,true>
				$<TARGET_RUNTIME_DLLS:orbislink-cli> "$<TARGET_FILE_DIR:orbislink-cli>"
			COMMAND_EXPAND_LISTS
			VERBATIM)
	endif()
endif()

if(ORBISLINK_BUILD_GUI)
	# Sql (its SQLite driver) edits a copy of the PS4's list of saves when a
	# save it lost is put back.
	set(ORBISLINK_QT_COMPONENTS Core Gui Qml Quick QuickControls2 Sql)
	if(ORBISLINK_ENABLE_STREAM)
		# The Remote Play video goes through QML's VideoOutput: Qt converts
		# the YUV on the GPU, instead of us doing it on the CPU.
		# Only the Multimedia module is needed to build; VideoOutput comes from
		# the plugin at runtime (qml6-module-qtmultimedia on Debian/
		# Ubuntu, handled by windeployqt on Windows).
		list(APPEND ORBISLINK_QT_COMPONENTS Multimedia)
	endif()
	find_package(Qt6 6.2 COMPONENTS ${ORBISLINK_QT_COMPONENTS} QUIET)
	if(Qt6_FOUND)
		set(CMAKE_AUTOMOC ON)
		set(CMAKE_AUTORCC ON)
		qt_add_executable(orbislink-gui
			src/orbislink/qt/main.cpp
			src/orbislink/qt/app_controller.cpp
			src/orbislink/qt/games_controller.cpp
			src/orbislink/qt/saves_controller.cpp
			src/orbislink/qt/save_database.cpp
			src/orbislink/qt/queue_model.cpp
			src/orbislink/qt/ftp_model.cpp
			src/orbislink/qt/startup.cpp
			src/orbislink/qt/dragselftest.cpp
			src/orbislink/qt/diagnostics.cpp
			src/orbislink/qt/notifier.cpp
			src/orbislink/qt/window_chrome.cpp
			$<$<BOOL:${ORBISLINK_ENABLE_STREAM}>:${CMAKE_CURRENT_SOURCE_DIR}/src/orbislink/qt/audio_output.cpp>
			$<$<BOOL:${ORBISLINK_ENABLE_STREAM}>:${CMAKE_CURRENT_SOURCE_DIR}/src/orbislink/qt/audio_input.cpp>
			$<$<BOOL:${ORBISLINK_ENABLE_STREAM}>:${CMAKE_CURRENT_SOURCE_DIR}/src/orbislink/qt/gamepad.cpp>
			$<$<BOOL:${ORBISLINK_ENABLE_STREAM}>:${CMAKE_CURRENT_SOURCE_DIR}/src/orbislink/qt/input_map.cpp>
			$<$<BOOL:${ORBISLINK_ENABLE_STREAM}>:${CMAKE_CURRENT_SOURCE_DIR}/src/orbislink/qt/stream_controller.cpp>
			$<$<BOOL:${ORBISLINK_ENABLE_STREAM}>:${CMAKE_CURRENT_SOURCE_DIR}/src/orbislink/qt/video_bridge.cpp>
			qml/orbislink/orbislink.qrc
			src/icons/icons.qrc
			third-party/inter/fonts.qrc
			translations/translations.qrc
			$<$<BOOL:${WIN32}>:${CMAKE_CURRENT_SOURCE_DIR}/src/icons/orbislink.rc>
		)
		target_compile_definitions(orbislink-gui PRIVATE
			ORBISLINK_VERSION_STRING="${ORBISLINK_VERSION_NAME}")
		target_link_libraries(orbislink-gui PRIVATE
			orbislink_core Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Sql)
		if(TARGET orbislink_fpkg)
			target_link_libraries(orbislink-gui PRIVATE orbislink_fpkg)
		endif()
		if(TARGET orbislink_store)
			target_sources(orbislink-gui PRIVATE src/orbislink/qt/store_controller.cpp)
			target_link_libraries(orbislink-gui PRIVATE orbislink_store)
		endif()
		if(ORBISLINK_ENABLE_STREAM)
			target_link_libraries(orbislink-gui PRIVATE orbislink_stream Qt6::Multimedia)
			target_compile_definitions(orbislink-gui PRIVATE ORBISLINK_HAS_STREAM=1)
			# The physical controller comes in through SDL. It is optional: without
			# it the application builds and the keyboard still acts as the controller.
			find_package(SDL2 QUIET)
			if(SDL2_FOUND)
				target_compile_definitions(orbislink-gui PRIVATE ORBISLINK_HAS_GAMEPAD=1)
				if(TARGET SDL2::SDL2)
					target_link_libraries(orbislink-gui PRIVATE SDL2::SDL2)
				else()
					target_include_directories(orbislink-gui PRIVATE ${SDL2_INCLUDE_DIRS})
					target_link_libraries(orbislink-gui PRIVATE ${SDL2_LIBRARIES})
				endif()
				message(STATUS "OrbisLink: physical controller enabled (SDL2)")
			else()
				message(STATUS "OrbisLink: SDL2 not found — only the keyboard acts as the controller")
			endif()
		endif()
		set_target_properties(orbislink-gui PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)

		# On Windows, puts the DLLs it depends on (libcurl and co., from vcpkg)
		# next to the .exe. Without this the program starts and dies at once
		# with "libcurl.dll was not found". The MSVC runtime is handled
		# separately, by windeployqt --compiler-runtime.
		if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
			add_custom_command(TARGET orbislink-gui POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E
					$<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:orbislink-gui>>,copy_if_different,true>
					$<TARGET_RUNTIME_DLLS:orbislink-gui> "$<TARGET_FILE_DIR:orbislink-gui>"
				COMMAND_EXPAND_LISTS
				VERBATIM)
		endif()

		message(STATUS "OrbisLink: graphical interface enabled (Qt ${Qt6_VERSION})")
	else()
		message(STATUS "OrbisLink: Qt 6 not found — only the core and the CLI are built")
	endif()
endif()

if(ORBISLINK_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
