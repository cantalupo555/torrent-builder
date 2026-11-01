include(${CMAKE_CURRENT_LIST_DIR}/nested_configure.cmake)

# Qt may be absent in some jobs (for example the CLI-only build), where the
# flag has no observable effect. When Qt is present, OFF must suppress the
# target on a Qt-equipped host.
tb_nested_configure("${CMAKE_CURRENT_BINARY_DIR}/gui-disabled-with-qt"
    -DTB_ENABLE_GUI=OFF
)
if(NOT NESTED_RESULT EQUAL 0)
    message(FATAL_ERROR "FAIL: TB_ENABLE_GUI=OFF failed to configure:\n${NESTED_OUTPUT}${NESTED_ERROR}")
endif()

file(READ "${CMAKE_CURRENT_BINARY_DIR}/gui-disabled-with-qt/CMakeCache.txt" cache)
if(NOT cache MATCHES "TB_ENABLE_GUI:BOOL=OFF")
    message(FATAL_ERROR "FAIL: TB_ENABLE_GUI did not stay OFF")
endif()

# The status line is emitted only inside the GUI target block.
if("${NESTED_OUTPUT}${NESTED_ERROR}" MATCHES "torrent_builder_gui target configured")
    message(FATAL_ERROR "FAIL: GUI target block ran with TB_ENABLE_GUI=OFF")
endif()

# For Makefiles/Ninja, query the generated target list and assert the GUI
# target is really absent. Other generators are covered by the checks above.
if(TEST_GENERATOR MATCHES "Makefiles|Ninja")
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            --build "${CMAKE_CURRENT_BINARY_DIR}/gui-disabled-with-qt" --target help
        RESULT_VARIABLE help_result
        OUTPUT_VARIABLE help_output
        ERROR_VARIABLE help_error
    )
    if(NOT help_result EQUAL 0)
        message(FATAL_ERROR "FAIL: could not query the generated target list:\n${help_output}${help_error}")
    endif()
    if("${help_output}${help_error}" MATCHES "torrent_builder_gui")
        message(FATAL_ERROR "FAIL: torrent_builder_gui target exists with TB_ENABLE_GUI=OFF")
    endif()
endif()

message(STATUS "PASS: TB_ENABLE_GUI=OFF suppresses the GUI target")
