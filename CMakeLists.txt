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

# Find OpenSSL (required for SHA-256 computation)
find_package(OpenSSL REQUIRED)

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

# yaml-cpp (YAML parsing for presets and batch config)
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    FetchContent_Declare(
        yaml-cpp
        URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.9.0.tar.gz
    )
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
endif()

# GoogleTest (testing framework)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# nlohmann/json (JSON parsing for update command)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
)
FetchContent_MakeAvailable(nlohmann_json)

# Qt6 (optional desktop GUI). NOT vendored via FetchContent — too large.
# CI / developers install Qt6 system-wide (apt install qt6-base-dev, brew,
# Chocolatey). Default is ON when Qt6 is found, OFF otherwise. Force OFF
# with -DTB_ENABLE_GUI=OFF for CLI-only build matrices.
option(TB_ENABLE_GUI "Build the Qt6 desktop GUI (requires Qt6)" ON)
if(TB_ENABLE_GUI)
    find_package(Qt6 QUIET COMPONENTS Widgets Gui Core)
    if(Qt6_FOUND)
        message(STATUS "Qt6 ${Qt6_VERSION} found — GUI target enabled.")
    else()
        # No cache FORCE here: an explicit -DTB_ENABLE_GUI=ON must survive
        # re-configure after the user installs Qt6. The target block below
        # guards on TB_ENABLE_GUI AND Qt6_FOUND, so nothing breaks meanwhile.
        message(STATUS "Qt6 not found — GUI target skipped this configure run.")
        message(STATUS "Install Qt6 and re-run cmake to enable torrent_builder_gui.")
    endif()
endif()

# ─── Core library ────────────────────────────────────────────
add_library(torrent_builder_core STATIC
    src/torrent_creator.cpp
    src/torrent_inspector.cpp
    src/torrent_modifier.cpp
    src/torrent_checker.cpp
    src/logger.cpp
    src/utils.cpp
    src/terminal.cpp
    src/output.cpp
    src/season_pack.cpp
    src/updater.cpp
)

target_include_directories(torrent_builder_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${LIBTORRENT_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(torrent_builder_core PUBLIC
    ${LIBTORRENT_LIBRARIES}
    ${LIBTORRENT_LDFLAGS}
    nlohmann_json::nlohmann_json
    OpenSSL::Crypto
)

target_compile_options(torrent_builder_core PRIVATE ${LIBTORRENT_CFLAGS})
target_link_options(torrent_builder_core PRIVATE ${LIBTORRENT_LDFLAGS})

# ─── Config library (preset + batch, separate from core) ──────
add_library(torrent_builder_config STATIC
    src/preset.cpp
    src/batch.cpp
    src/tracker_rules.cpp
)

target_include_directories(torrent_builder_config PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBTORRENT_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(torrent_builder_config PUBLIC
    torrent_builder_core
    yaml-cpp::yaml-cpp
)

# ─── CLI executable ──────────────────────────────────────────
add_executable(torrent_builder
    src/torrent_builder.cpp
)

target_include_directories(torrent_builder PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(torrent_builder PRIVATE
    torrent_builder_core
    torrent_builder_config
    cxxopts::cxxopts
)

# ─── Qt6 desktop GUI executable (optional) ───────────────────
# Only built when Qt6 is found and TB_ENABLE_GUI is ON. The GUI links the same
# core/config static libs as the CLI — it is a sibling frontend, never a fork.
if(TB_ENABLE_GUI AND Qt6_FOUND)
    # Equivalent to qt_standard_project_setup(), which requires Qt >= 6.3.
    # Explicit flags keep the GUI buildable on hosts shipping Qt 6.2
    # (e.g. Ubuntu 22.04 qt6-base-dev).
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)

    set(GUI_SOURCES
        gui/main.cpp
        gui/main_window.cpp
        gui/qt_output_sink.cpp
    )

    qt_add_executable(torrent_builder_gui ${GUI_SOURCES})

    target_include_directories(torrent_builder_gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}
    )

    target_link_libraries(torrent_builder_gui PRIVATE
        torrent_builder_core
        torrent_builder_config
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
    )

    # On Windows, build as a windowed app (no console window pops up).
    if(WIN32)
        set_target_properties(torrent_builder_gui PROPERTIES WIN32_EXECUTABLE ON)
    endif()

    message(STATUS "torrent_builder_gui target configured.")
endif()

# ─── Tests ───────────────────────────────────────────────────
enable_testing()

file(GLOB TEST_SOURCES tests/*.cpp)
if(TEST_SOURCES)
    add_executable(torrent_builder_tests ${TEST_SOURCES})

    target_compile_definitions(torrent_builder_tests PRIVATE
        BINARY_PATH="${CMAKE_BINARY_DIR}/torrent_builder"
        SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        TORRENT_BUILDER_EXCLUDE_MAIN
    )

    target_include_directories(torrent_builder_tests PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
    )

    target_link_libraries(torrent_builder_tests PRIVATE
        GTest::gtest_main
        torrent_builder_core
        torrent_builder_config
        cxxopts::cxxopts
    )

    target_sources(torrent_builder_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/torrent_builder.cpp
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

if(TARGET torrent_builder_gui)
    install(TARGETS torrent_builder_gui
        RUNTIME DESTINATION bin
    )
endif()
