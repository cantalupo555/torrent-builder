include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/gui_checks.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/nested_configure.cmake)

# Every non-fatal combination, including the "skip the GUI" path, must stay
# non-fatal. Only the two rejected combinations may abort configuration.
tb_validate_gui_options(ON ON)
tb_validate_gui_options(OFF ON)
tb_validate_gui_options(OFF OFF)
tb_require_qt_or_fail(OFF)

# Configure an optional-GUI build with Qt6 disabled. This covers the wiring:
# a regression that makes the optional path require Qt (for example, calling
# tb_require_qt_or_fail(ON) unconditionally) fails here even though the direct
# helper checks above still pass. Default option values are covered separately
# by cmake_gui_defaults.
tb_nested_configure("${CMAKE_CURRENT_BINARY_DIR}/gui-optional-missing-qt"
    -DTB_ENABLE_GUI=ON
    -DTB_REQUIRE_GUI=OFF
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
)
if(NOT NESTED_RESULT EQUAL 0)
    message(FATAL_ERROR "FAIL: optional GUI project failed to configure with Qt6 disabled:\n${NESTED_OUTPUT}${NESTED_ERROR}")
endif()
if(NOT "${NESTED_OUTPUT}${NESTED_ERROR}" MATCHES "GUI target skipped this configure run")
    message(FATAL_ERROR "FAIL: configure succeeded without the GUI-skip message:\n${NESTED_OUTPUT}${NESTED_ERROR}")
endif()
message(STATUS "PASS: optional GUI configures and skips the target without Qt6")
