# SPDX-License-Identifier: AGPL-3.0-or-later
set(ORBISLINK_TESTS
	test_json
	test_sfo
	test_pkg_inspector
	test_local_http_server
	test_install_queue
	test_ftp_listing
	test_rpi_protocol
	test_settings
	test_console_probe
	test_update
	test_pcm_convert
)

# Os testes do Remote Play precisam da biblioteca do stream.
if(ORBISLINK_ENABLE_STREAM AND TARGET orbislink_stream)
	list(APPEND ORBISLINK_STREAM_TESTS test_stream_discovery test_stream_credentials
		test_account_id)
	foreach(test_name ${ORBISLINK_STREAM_TESTS})
		add_executable(${test_name} ${test_name}.cpp)
		target_link_libraries(${test_name} PRIVATE orbislink_stream)
		target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
		add_test(NAME ${test_name} COMMAND ${test_name})
	endforeach()
endif()

# A fila de PCM vive no lado do Qt, mas o que interessa testar nela — as
# contas de quanto entrou e quanto saiu — não precisa de placa de som
# nenhuma, e é isso que distingue "o som não chegou" de "o som não saiu".
if(ORBISLINK_BUILD_GUI AND Qt6_FOUND AND TARGET Qt6::Multimedia)
	set(CMAKE_AUTOMOC ON)
	add_executable(test_pcm_queue
		test_pcm_queue.cpp
		${CMAKE_SOURCE_DIR}/src/orbislink/qt/audio_output.cpp)
	target_link_libraries(test_pcm_queue PRIVATE orbislink_core Qt6::Core Qt6::Multimedia)
	target_include_directories(test_pcm_queue PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(NAME test_pcm_queue COMMAND test_pcm_queue)
endif()

# O teclado como comando: as regras de troca de teclas vivem no lado do
# Qt (os códigos são Qt::Key) e usam os botões do chiaki.
if(ORBISLINK_BUILD_GUI AND Qt6_FOUND AND TARGET orbislink_stream)
	add_executable(test_keyboard_map
		test_keyboard_map.cpp
		${CMAKE_SOURCE_DIR}/src/orbislink/qt/input_map.cpp)
	target_link_libraries(test_keyboard_map PRIVATE orbislink_stream Qt6::Core)
	target_include_directories(test_keyboard_map PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(NAME test_keyboard_map COMMAND test_keyboard_map)
endif()

foreach(test_name ${ORBISLINK_TESTS})
	add_executable(${test_name} ${test_name}.cpp)
	target_link_libraries(${test_name} PRIVATE orbislink_core)
	target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# Teste de integração com a consola falsa (tools/mock-console).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND AND ORBISLINK_BUILD_CLI)
	add_test(NAME integration_mock_console
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_integration.py
			--cli $<TARGET_FILE:orbislink-cli>
			--mock ${CMAKE_SOURCE_DIR}/tools/mock-console/mock_console.py)
	set_tests_properties(integration_mock_console PROPERTIES TIMEOUT 180)
endif()
