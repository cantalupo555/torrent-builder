include(${CMAKE_CURRENT_LIST_DIR}/nested_configure.cmake)

tb_nested_configure("${CMAKE_CURRENT_BINARY_DIR}/gui-wiring-missing-qt"
    -DTB_ENABLE_GUI=ON
    -DTB_REQUIRE_GUI=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
)
if(NESTED_RESULT EQUAL 0)
    message(FATAL_ERROR "FAIL: project configured with TB_REQUIRE_GUI=ON and Qt6 disabled")
endif()
if(NOT "${NESTED_OUTPUT}${NESTED_ERROR}" MATCHES "Qt6 was not found, but TB_REQUIRE_GUI=ON")
    message(FATAL_ERROR "FAIL: configure failed without the expected guard message:\n${NESTED_OUTPUT}${NESTED_ERROR}")
endif()
message(STATUS "PASS: configure rejects a required GUI when Qt6 is missing")
