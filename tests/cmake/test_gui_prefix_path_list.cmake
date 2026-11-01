include(${CMAKE_CURRENT_LIST_DIR}/nested_configure.cmake)

# Force a multi-entry prefix path; the helper must pass it as one argument
# instead of splitting it into several command arguments. Keep the parent's
# prefixes so dependency discovery stays the same.
if(TEST_CMAKE_PREFIX_PATH)
    set(TEST_CMAKE_PREFIX_PATH "${TEST_CMAKE_PREFIX_PATH};/a;/b")
else()
    set(TEST_CMAKE_PREFIX_PATH "/a;/b")
endif()

tb_nested_configure("${CMAKE_CURRENT_BINARY_DIR}/gui-prefix-path-list"
    -DTB_ENABLE_GUI=OFF
)
if(NOT NESTED_RESULT EQUAL 0)
    message(FATAL_ERROR "FAIL: configure with a multi-entry CMAKE_PREFIX_PATH failed:\n${NESTED_OUTPUT}${NESTED_ERROR}")
endif()

file(READ "${CMAKE_CURRENT_BINARY_DIR}/gui-prefix-path-list/CMakeCache.txt" cache)
# The parent prefixes come first, so match the appended suffix rather than the
# whole cache value.
if(NOT cache MATCHES "/a;/b")
    message(FATAL_ERROR "FAIL: CMAKE_PREFIX_PATH entries were split or lost")
endif()
message(STATUS "PASS: multi-entry CMAKE_PREFIX_PATH reaches the nested configure")
