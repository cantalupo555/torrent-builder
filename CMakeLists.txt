cmake_minimum_required(VERSION 3.28)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake)

project(torrent_builder VERSION ${TORRENT_BUILDER_VERSION} LANGUAGES CXX)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.hpp
    @ONLY
)

# Minimum GCC version check for C++23 Ranges support
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_checks.cmake)

# Basic project settings
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compilation settings
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-Wall -Wextra -g -O0)
else()
    add_compile_options(-O2)
endif()

# Find Boost (required by libtorrent headers)
find_package(Boost REQUIRED)

# Find libtorrent
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBTORRENT REQUIRED libtorrent-rasterbar>=2.0.10)

# Explicit libtorrent version check
if(LIBTORRENT_FOUND)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/libtorrent_check.cmake)
    message(STATUS "libtorrent-rasterbar ${LIBTORRENT_VERSION} found.")
endif()

# Add compile definitions for libtorrent
add_compile_definitions(
    TORRENT_USE_LIBCRYPTO
    TORRENT_USE_OPENSSL
    TORRENT_USE_IPV6=1
)

# ─── Dependencies ────────────────────────────────────────────
include(FetchContent)

# cxxopts (CLI parsing)
FetchContent_Declare(
    cxxopts
    URL https://github.com/jarro2783/cxxopts/archive/refs/tags/v3.1.1.tar.gz
)
FetchContent_MakeAvailable(cxxopts)

# GoogleTest (testing framework)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ─── Core library ────────────────────────────────────────────
add_library(torrent_builder_core STATIC
    src/torrent_creator.cpp
    src/logger.cpp
    src/utils.cpp
)

target_include_directories(torrent_builder_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBTORRENT_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(torrent_builder_core PUBLIC
    ${LIBTORRENT_LIBRARIES}
    ${LIBTORRENT_LDFLAGS}
)

target_compile_options(torrent_builder_core PRIVATE ${LIBTORRENT_CFLAGS})
target_link_options(torrent_builder_core PRIVATE ${LIBTORRENT_LDFLAGS})

# ─── CLI executable ──────────────────────────────────────────
add_executable(torrent_builder
    src/torrent_builder.cpp
)

target_include_directories(torrent_builder PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(torrent_builder PRIVATE
    torrent_builder_core
    cxxopts::cxxopts
)

# ─── Tests ───────────────────────────────────────────────────
enable_testing()

file(GLOB TEST_SOURCES tests/*.cpp)
if(TEST_SOURCES)
    add_executable(torrent_builder_tests ${TEST_SOURCES})

    target_compile_definitions(torrent_builder_tests PRIVATE
        BINARY_PATH="${CMAKE_BINARY_DIR}/torrent_builder"
    )

    target_include_directories(torrent_builder_tests PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
    )

    target_link_libraries(torrent_builder_tests PRIVATE
        GTest::gtest_main
        torrent_builder_core
    )

    include(GoogleTest)
    gtest_discover_tests(torrent_builder_tests)
endif()

# CMake validation tests (issue #7)
add_test(NAME cmake_gcc_accepts_13
    COMMAND ${CMAKE_COMMAND}
        -DCMAKE_CXX_COMPILER_ID=GNU
        -DCMAKE_CXX_COMPILER_VERSION=13.0.0
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_gcc_accept.cmake)

add_test(NAME cmake_gcc_rejects_12
    COMMAND ${CMAKE_COMMAND}
        -DCMAKE_CXX_COMPILER_ID=GNU
        -DCMAKE_CXX_COMPILER_VERSION=12.0.0
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_gcc_reject.cmake)
set_tests_properties(cmake_gcc_rejects_12 PROPERTIES WILL_FAIL TRUE)

add_test(NAME cmake_libtorrent_accepts_2_0_10
    COMMAND ${CMAKE_COMMAND}
        -DLIBTORRENT_VERSION=2.0.10
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_libtorrent_accept.cmake)

add_test(NAME cmake_libtorrent_rejects_2_0_9
    COMMAND ${CMAKE_COMMAND}
        -DLIBTORRENT_VERSION=2.0.9
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_libtorrent_reject.cmake)
set_tests_properties(cmake_libtorrent_rejects_2_0_9 PROPERTIES WILL_FAIL TRUE)

add_test(NAME cmake_version_override
    COMMAND ${CMAKE_COMMAND}
        -DTORRENT_BUILDER_VERSION=1.2.3
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_version_override.cmake)

add_test(NAME cmake_version_prerelease
    COMMAND ${CMAKE_COMMAND}
        -DTORRENT_BUILDER_VERSION=2.0.0-rc1
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_version_prerelease.cmake)

add_test(NAME cmake_version_fallback
    COMMAND ${CMAKE_COMMAND}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_version_fallback.cmake)

# ─── Installation ────────────────────────────────────────────
install(TARGETS torrent_builder
    RUNTIME DESTINATION bin
)
