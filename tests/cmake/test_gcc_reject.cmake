include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/compiler_checks.cmake)
message(FATAL_ERROR "FAIL: GCC 12 should have been rejected")
