# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
project(OrbisLink VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ORBISLINK_VERSION_NAME "${PROJECT_VERSION}" CACHE STRING
	"Versão mostrada pela aplicação e escrita no registo")

# De onde a aplicação vai buscar as actualizações, no formato "dono/nome".
#
# Fica vazio de propósito. O CI passa -DORBISLINK_REPOSITORY com o
# repositório onde a compilação está a correr, portanto quem bifurcar este
# projecto fica automaticamente a apontar para a sua própria cópia, sem
# editar uma linha — e não há nome nenhum escrito no código.
#
# Sem isto definido, a verificação de actualizações diz que não está
# configurada, em vez de ir bater a um repositório que não é o dela.
set(ORBISLINK_REPOSITORY "" CACHE STRING
	"Repositório GitHub \"dono/nome\" de onde vêm as actualizações")

option(ORBISLINK_BUILD_TESTS "Compilar os testes do núcleo" ON)
option(ORBISLINK_BUILD_GUI "Compilar a interface gráfica (precisa de Qt 6)" ON)
option(ORBISLINK_BUILD_CLI "Compilar a ferramenta de linha de comandos" ON)
option(ORBISLINK_WARNINGS_AS_ERRORS "Tratar avisos como erros" OFF)
option(ORBISLINK_ENABLE_STREAM "Compilar o Remote Play (precisa do submódulo chiaki-ng)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

if(WIN32)
	# Os recursos (.rc) levam o ícone da aplicação para dentro do executável.
	enable_language(RC)
	# NOMINMAX é obrigatório: <windows.h> define macros min/max que partem
	# std::min/std::max. WIN32_LEAN_AND_MEAN corta cabeçalhos desnecessários e
	# _WIN32_WINNT fixa o mínimo em Windows 7 (GetAdaptersAddresses e afins).
	add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601 WINVER=0x0601)
endif()

if(MSVC)
	# Sem /utf-8 o MSVC lê os literais em português na codepage do sistema e
	# as mensagens saem trocadas (e os testes falham a comparar strings).
	add_compile_options(/utf-8)
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# O Remote Play vive numa biblioteca à parte: o núcleo, a CLI e os testes
# continuam a compilar sem o chiaki e sem o FFmpeg.
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
	set(ORBISLINK_QT_COMPONENTS Core Gui Qml Quick QuickControls2)
	if(ORBISLINK_ENABLE_STREAM)
		# O vídeo do Remote Play entra pelo VideoOutput do QML: é o Qt que
		# converte o YUV na placa gráfica, em vez de o fazermos no processador.
		# Só o módulo Multimedia é preciso para compilar; o VideoOutput vem do
		# plugin em tempo de execução (qml6-module-qtmultimedia em Debian/
		# Ubuntu, tratado pelo windeployqt em Windows).
		list(APPEND ORBISLINK_QT_COMPONENTS Multimedia)
	endif()
	find_package(Qt6 6.2 COMPONENTS ${ORBISLINK_QT_COMPONENTS} QUIET)
	if(Qt6_FOUND)
		set(CMAKE_AUTOMOC ON)
		set(CMAKE_AUTORCC ON)
		qt_add_executable(orbislink-gui
			src/orbislink/qt/main.cpp
			src/orbislink/qt/app_controller.cpp
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
			translations/translations.qrc
			$<$<BOOL:${WIN32}>:${CMAKE_CURRENT_SOURCE_DIR}/src/icons/orbislink.rc>
		)
		target_compile_definitions(orbislink-gui PRIVATE
			ORBISLINK_VERSION_STRING="${ORBISLINK_VERSION_NAME}")
		target_link_libraries(orbislink-gui PRIVATE
			orbislink_core Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
		if(ORBISLINK_ENABLE_STREAM)
			target_link_libraries(orbislink-gui PRIVATE orbislink_stream Qt6::Multimedia)
			target_compile_definitions(orbislink-gui PRIVATE ORBISLINK_HAS_STREAM=1)
			# O comando físico entra pelo SDL. É opcional: sem ele a aplicação
			# compila e o teclado continua a fazer de comando.
			find_package(SDL2 QUIET)
			if(SDL2_FOUND)
				target_compile_definitions(orbislink-gui PRIVATE ORBISLINK_HAS_GAMEPAD=1)
				if(TARGET SDL2::SDL2)
					target_link_libraries(orbislink-gui PRIVATE SDL2::SDL2)
				else()
					target_include_directories(orbislink-gui PRIVATE ${SDL2_INCLUDE_DIRS})
					target_link_libraries(orbislink-gui PRIVATE ${SDL2_LIBRARIES})
				endif()
				message(STATUS "OrbisLink: comando físico activado (SDL2)")
			else()
				message(STATUS "OrbisLink: SDL2 não encontrado — só o teclado faz de comando")
			endif()
		endif()
		set_target_properties(orbislink-gui PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)

		# Em Windows, põe ao lado do .exe as DLLs de que ele depende (libcurl e
		# companhia, vindas do vcpkg). Sem isto o programa arranca e morre logo
		# com "libcurl.dll não foi encontrado". O runtime do MSVC é tratado à
		# parte, pelo windeployqt --compiler-runtime.
		if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
			add_custom_command(TARGET orbislink-gui POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E
					$<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:orbislink-gui>>,copy_if_different,true>
					$<TARGET_RUNTIME_DLLS:orbislink-gui> "$<TARGET_FILE_DIR:orbislink-gui>"
				COMMAND_EXPAND_LISTS
				VERBATIM)
		endif()

		message(STATUS "OrbisLink: interface gráfica activada (Qt ${Qt6_VERSION})")
	else()
		message(STATUS "OrbisLink: Qt 6 não encontrado — só o núcleo e a CLI são compilados")
	endif()
endif()

if(ORBISLINK_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
