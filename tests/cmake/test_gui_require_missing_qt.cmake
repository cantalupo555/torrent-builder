# Run the guard in a nested CMake process so this script can assert both the
# abort and its message. Checking only a non-zero exit would pass even when the
# guard does nothing, as long as this script failed for any other reason.
execute_process(
    COMMAND ${CMAKE_COMMAND}
        -DGUARD=qt -DREQUIRE_GUI=ON
        -P ${CMAKE_CURRENT_LIST_DIR}/exercise_gui_guard.cmake
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_output
)
if(result EQUAL 0)
    message(FATAL_ERROR "FAIL: TB_REQUIRE_GUI=ON without Qt6 was accepted")
endif()
if(NOT "${output}${error_output}" MATCHES "Qt6 was not found, but TB_REQUIRE_GUI=ON")
    message(FATAL_ERROR "FAIL: guard aborted without the expected message:\n${output}${error_output}")
endif()
message(STATUS "PASS: missing Qt6 rejected when required")
