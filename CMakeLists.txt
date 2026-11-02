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

# Qt6 (optional desktop GUI). NOT vendored via FetchContent — too large.
# CI / developers install Qt6 system-wide (apt install qt6-base-dev, brew,
# MSYS2 mingw-w64-<prefix>-qt6-base). The option defaults ON; the
# torrent_builder_gui target is skipped when Qt6 is not found. Force OFF
# with -DTB_ENABLE_GUI=OFF for CLI-only build matrices, or set
# -DTB_REQUIRE_GUI=ON to turn a missing Qt6 into a configure error.
option(TB_ENABLE_GUI "Build the Qt6 desktop GUI (requires Qt6)" ON)
option(TB_REQUIRE_GUI "Fail configuration when Qt6 is required but unavailable" OFF)

# The guards live in a standalone module so their fatal branches stay covered
# by script-mode tests (tests/cmake/test_gui_*.cmake, registered below).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/gui_checks.cmake)
tb_validate_gui_options(${TB_REQUIRE_GUI} ${TB_ENABLE_GUI})

if(TB_ENABLE_GUI)
    find_package(Qt6 QUIET COMPONENTS Widgets Gui Core)
    if(Qt6_FOUND)
        message(STATUS "Qt6 ${Qt6_VERSION} found — GUI target enabled.")
    else()
        # Fatal only when CI (or a packager) explicitly requires the GUI.
        tb_require_qt_or_fail(${TB_REQUIRE_GUI})
        # No cache FORCE here: an explicit -DTB_ENABLE_GUI=ON must survive
        # re-configure after the user installs Qt6. The target block below
        # guards on TB_ENABLE_GUI AND Qt6_FOUND, so nothing breaks meanwhile.
        message(STATUS "Qt6 not found — GUI target skipped this configure run.")
        message(STATUS "Install Qt6 and re-run cmake to enable torrent_builder_gui.")
    endif()
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
    src/creation_planner.cpp
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

    # MACOSX_BUNDLE is ignored outside Apple platforms. On macOS it makes the
    # target a build/torrent_builder_gui.app bundle, matching the README and
    # the macdeployqt packaging planned for the release workflow.
    qt_add_executable(torrent_builder_gui MACOSX_BUNDLE ${GUI_SOURCES})

    # The README and release packaging assume a macOS .app bundle. Fail loudly
    # if the bundle keyword is ever dropped instead of shipping a bare binary.
    if(APPLE)
        get_target_property(tb_gui_macosx_bundle torrent_builder_gui MACOSX_BUNDLE)
        if(NOT tb_gui_macosx_bundle)
            message(FATAL_ERROR
                "torrent_builder_gui must be a macOS app bundle; "
                "the documented run and install paths expect .app.")
        endif()
    endif()

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

# CI configures with TB_REQUIRE_GUI=ON: a missing target must not silently
# drop the GUI (and its smoke test) from the build.
if(TB_REQUIRE_GUI AND NOT TARGET torrent_builder_gui)
    message(FATAL_ERROR
        "TB_REQUIRE_GUI=ON, but the torrent_builder_gui target was not created.")
endif()

# ─── Tests ───────────────────────────────────────────────────
enable_testing()

if(TARGET torrent_builder_gui AND UNIX AND NOT APPLE)
    add_test(NAME gui_smoke COMMAND torrent_builder_gui --smoke-test)
    set_tests_properties(gui_smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 30
    )
endif()

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

add_test(NAME cmake_gui_require_without_enable
    COMMAND ${CMAKE_COMMAND}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_gui_require_without_enable.cmake)

add_test(NAME cmake_gui_require_missing_qt
    COMMAND ${CMAKE_COMMAND}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_gui_require_missing_qt.cmake)

add_test(NAME cmake_gui_optional_missing_qt
    COMMAND ${CMAKE_COMMAND}
        -DTEST_GENERATOR=${CMAKE_GENERATOR}
        -DTEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DTEST_C_COMPILER=${CMAKE_C_COMPILER}
        -DTEST_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        "-DTEST_OPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}"
        "-DTEST_CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_gui_optional_missing_qt.cmake)

add_test(NAME cmake_gui_wiring_invalid_combo
    COMMAND ${CMAKE_COMMAND}
        -DTEST_GENERATOR=${CMAKE_GENERATOR}
        -DTEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DTEST_C_COMPILER=${CMAKE_C_COMPILER}
        -DTEST_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        "-DTEST_OPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}"
        "-DTEST_CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_gui_wiring_invalid_combo.cmake)

add_test(NAME cmake_gui_wiring_missing_qt
    COMMAND ${CMAKE_COMMAND}
        -DTEST_GENERATOR=${CMAKE_GENERATOR}
        -DTEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DTEST_C_COMPILER=${CMAKE_C_COMPILER}
        -DTEST_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        "-DTEST_OPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}"
        "-DTEST_CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_gui_wiring_missing_qt.cmake)

add_test(NAME cmake_gui_defaults
    COMMAND ${CMAKE_COMMAND}
        -DTEST_GENERATOR=${CMAKE_GENERATOR}
        -DTEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DTEST_C_COMPILER=${CMAKE_C_COMPILER}
        -DTEST_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        "-DTEST_OPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}"
        "-DTEST_CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_gui_defaults.cmake)

add_test(NAME cmake_gui_disabled_with_qt
    COMMAND ${CMAKE_COMMAND}
        -DTEST_GENERATOR=${CMAKE_GENERATOR}
        -DTEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DTEST_C_COMPILER=${CMAKE_C_COMPILER}
        -DTEST_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        "-DTEST_OPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}"
        "-DTEST_CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_gui_disabled_with_qt.cmake)

add_test(NAME cmake_gui_prefix_path_list
    COMMAND ${CMAKE_COMMAND}
        -DTEST_GENERATOR=${CMAKE_GENERATOR}
        -DTEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DTEST_C_COMPILER=${CMAKE_C_COMPILER}
        -DTEST_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        "-DTEST_OPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}"
        "-DTEST_CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_gui_prefix_path_list.cmake)

# ─── Installation ────────────────────────────────────────────
install(TARGETS torrent_builder
    RUNTIME DESTINATION bin
)

if(TARGET torrent_builder_gui)
    install(TARGETS torrent_builder_gui
        BUNDLE DESTINATION .
        RUNTIME DESTINATION bin
    )
endif()
