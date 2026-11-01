include(${CMAKE_CURRENT_LIST_DIR}/nested_configure.cmake)

# No GUI -D flags: the documented defaults must resolve to TB_ENABLE_GUI=ON
# and TB_REQUIRE_GUI=OFF, whether or not Qt6 is installed. The target may be
# skipped when Qt6 is absent, but the option values must not change.
tb_nested_configure("${CMAKE_CURRENT_BINARY_DIR}/gui-defaults")

if(NOT NESTED_RESULT EQUAL 0)
    message(FATAL_ERROR "FAIL: default configure failed:\n${NESTED_OUTPUT}${NESTED_ERROR}")
endif()

file(READ "${CMAKE_CURRENT_BINARY_DIR}/gui-defaults/CMakeCache.txt" cache)
if(NOT cache MATCHES "TB_ENABLE_GUI:BOOL=ON")
    message(FATAL_ERROR "FAIL: TB_ENABLE_GUI does not default to ON")
endif()
if(NOT cache MATCHES "TB_REQUIRE_GUI:BOOL=OFF")
    message(FATAL_ERROR "FAIL: TB_REQUIRE_GUI does not default to OFF")
endif()
message(STATUS "PASS: documented GUI option defaults hold")
