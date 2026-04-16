include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/libtorrent_check.cmake)
message(FATAL_ERROR "FAIL: libtorrent 2.0.9 should have been rejected")
