include(${CMAKE_CURRENT_LIST_DIR}/nested_configure.cmake)

tb_nested_configure("${CMAKE_CURRENT_BINARY_DIR}/gui-wiring-invalid-combo"
    -DTB_ENABLE_GUI=OFF
    -DTB_REQUIRE_GUI=ON
)
if(NESTED_RESULT EQUAL 0)
    message(FATAL_ERROR "FAIL: project configured with TB_REQUIRE_GUI=ON and TB_ENABLE_GUI=OFF")
endif()
if(NOT "${NESTED_OUTPUT}${NESTED_ERROR}" MATCHES "TB_REQUIRE_GUI=ON requires TB_ENABLE_GUI=ON")
    message(FATAL_ERROR "FAIL: configure failed without the expected guard message:\n${NESTED_OUTPUT}${NESTED_ERROR}")
endif()
message(STATUS "PASS: configure rejects the invalid GUI option combination")
