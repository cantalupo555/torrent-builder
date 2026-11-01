# Run the guard in a nested CMake process so this script can assert both the
# abort and its message. Checking only a non-zero exit would pass even when the
# guard does nothing, as long as this script failed for any other reason.
execute_process(
    COMMAND ${CMAKE_COMMAND}
        -DGUARD=options -DREQUIRE_GUI=ON -DENABLE_GUI=OFF
        -P ${CMAKE_CURRENT_LIST_DIR}/exercise_gui_guard.cmake
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_output
)
if(result EQUAL 0)
    message(FATAL_ERROR "FAIL: TB_REQUIRE_GUI=ON with TB_ENABLE_GUI=OFF was accepted")
endif()
if(NOT "${output}${error_output}" MATCHES "TB_REQUIRE_GUI=ON requires TB_ENABLE_GUI=ON")
    message(FATAL_ERROR "FAIL: guard aborted without the expected message:\n${output}${error_output}")
endif()
message(STATUS "PASS: invalid option combination rejected")
