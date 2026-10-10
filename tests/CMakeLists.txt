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
	test_save_vault
	test_payload
	test_library
)

# The Remote Play tests need the stream library.
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

# The fake PKG builder, when OpenSSL is there to build it.
if(TARGET orbislink_fpkg)
	add_executable(test_fpkg test_fpkg.cpp)
	target_link_libraries(test_fpkg PRIVATE orbislink_fpkg orbislink_core)
	target_include_directories(test_fpkg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	target_compile_definitions(test_fpkg PRIVATE EXPECTED_SHA="aa809a8c16a99d553f5891efcd50dc3d9ff36adf82a69b4827c439266905ead4")
	add_test(NAME test_fpkg COMMAND test_fpkg)

	add_executable(test_targz test_targz.cpp)
	target_link_libraries(test_targz PRIVATE orbislink_fpkg orbislink_core)
	target_include_directories(test_targz PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(NAME test_targz COMMAND test_targz)
endif()

# The PCM queue lives on the Qt side, but what matters to test in it — the
# accounting of how much went in and how much came out — needs no sound card
# at all, and that is what tells "the sound did not arrive" from "the sound did not come out".
if(ORBISLINK_BUILD_GUI AND Qt6_FOUND AND TARGET Qt6::Multimedia)
	set(CMAKE_AUTOMOC ON)
	add_executable(test_pcm_queue
		test_pcm_queue.cpp
		${CMAKE_SOURCE_DIR}/src/orbislink/qt/audio_output.cpp)
	target_link_libraries(test_pcm_queue PRIVATE orbislink_core Qt6::Core Qt6::Multimedia)
	target_include_directories(test_pcm_queue PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(NAME test_pcm_queue COMMAND test_pcm_queue)
endif()

# The homebrew store: signed catalog, ZIP apps, install into place.
if(TARGET orbislink_store)
	add_executable(test_store test_store.cpp)
	target_link_libraries(test_store PRIVATE orbislink_store)
	target_include_directories(test_store PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(NAME test_store COMMAND test_store)
endif()

# Adding a lost save's row to a copy of the PS4's list of saves (SQLite,
# through Qt Sql): what goes back to the console must be whole.
if(ORBISLINK_BUILD_GUI AND Qt6_FOUND AND TARGET Qt6::Sql)
	add_executable(test_save_database
		test_save_database.cpp
		${CMAKE_SOURCE_DIR}/src/orbislink/qt/save_database.cpp)
	target_link_libraries(test_save_database PRIVATE orbislink_core Qt6::Core Qt6::Sql)
	target_include_directories(test_save_database PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(NAME test_save_database COMMAND test_save_database)
endif()

# Keyboard as controller: the key swap rules live on the Qt side
# (the codes are Qt::Key) and use chiaki's buttons.
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

# Integration test with the fake console (tools/mock-console).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND AND ORBISLINK_BUILD_CLI)
	add_test(NAME integration_mock_console
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_integration.py
			--cli $<TARGET_FILE:orbislink-cli>
			--mock ${CMAKE_SOURCE_DIR}/tools/mock-console/mock_console.py)
	set_tests_properties(integration_mock_console PROPERTIES TIMEOUT 180)
endif()
