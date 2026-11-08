# Matrix cases for the optional Qt6 GUI configure (issue #85).
#
# Each CTest entry runs this script with -DTEST_MATRIX_CASE=<case>, so a
# failure in one case neither hides nor reruns the others. CMakeLists.txt
# passes the parent configure values as TEST_*; tb_nested_configure forwards
# them, while TEST_MATRIX_CASE and TEST_QT_FOUND are read directly here.
#
# Cases:
#   defaults, require_without_enable, require_missing_qt, optional_missing_qt,
#   disabled, forwarding, prefix_list, windows_executable

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/gui_checks.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/nested_configure.cmake")

set(valid_cases
    "defaults;require_without_enable;require_missing_qt;optional_missing_qt;disabled;forwarding;prefix_list;windows_executable")

if("${TEST_MATRIX_CASE}" STREQUAL "")
    message(FATAL_ERROR "TEST_MATRIX_CASE is required. Valid cases: ${valid_cases}")
endif()
# list(FIND) instead of IN_LIST: script mode does not inherit the
# cmake_minimum_required policy that enables CMP0057 on older CMake.
list(FIND valid_cases "${TEST_MATRIX_CASE}" valid_case_index)
if(valid_case_index EQUAL -1)
    message(FATAL_ERROR "Unknown TEST_MATRIX_CASE '${TEST_MATRIX_CASE}'. Valid cases: ${valid_cases}")
endif()

# Every TEST_* input is forwarded by CMakeLists.txt. A missing line must fail
# loudly: an empty value is legitimate (Qt may be missing, a build type may not
# apply), but an undefined one means the registration line was removed and the
# checks it feeds would silently skip.
foreach(required_input
        TEST_GENERATOR
        TEST_GENERATOR_PLATFORM
        TEST_CXX_COMPILER
        TEST_C_COMPILER
        TEST_TOOLCHAIN_FILE
        TEST_BUILD_TYPE
        TEST_OSX_ARCHITECTURES
        TEST_OPENSSL_ROOT_DIR
        TEST_CMAKE_PREFIX_PATH
        TEST_QT_FOUND)
    if(NOT DEFINED ${required_input})
        message(FATAL_ERROR "${required_input} is not defined; CMakeLists.txt must forward it to the GUI matrix")
    endif()
endforeach()

# These are always set by a configured parent build; an empty value means the
# forwarding line lost its source, not a valid environment.
foreach(required_value TEST_GENERATOR TEST_CXX_COMPILER TEST_OPENSSL_ROOT_DIR)
    if("${${required_value}}" STREQUAL "")
        message(FATAL_ERROR "${required_value} is empty; CMakeLists.txt must forward the parent configure value")
    endif()
endforeach()

function(tb_expect_configure_ok case started result output error_output)
    if(NOT started)
        message(FATAL_ERROR "FAIL: ${case}: nested configure could not be started:\n${error_output}")
    endif()
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "FAIL: ${case}: nested configure failed (exit ${result}):\n${output}${error_output}")
    endif()
endfunction()

function(tb_expect_configure_rejected case started result output error_output expected_message)
    if(NOT started)
        message(FATAL_ERROR "FAIL: ${case}: nested configure could not be started:\n${error_output}")
    endif()
    if(result EQUAL 0)
        message(FATAL_ERROR "FAIL: ${case}: nested configure succeeded but should have been rejected")
    endif()
    set(combined "${output}${error_output}")
    if(NOT combined MATCHES "${expected_message}")
        message(FATAL_ERROR "FAIL: ${case}: rejection did not mention '${expected_message}':\n${combined}")
    endif()
endfunction()

function(tb_read_cache case cache_file out)
    if(NOT EXISTS "${cache_file}")
        message(FATAL_ERROR "FAIL: ${case}: nested configure produced no CMakeCache.txt")
    endif()
    file(READ "${cache_file}" cache_contents)
    set(${out} "${cache_contents}" PARENT_SCOPE)
endfunction()

function(tb_cache_value out cache name)
    # Match inside the multi-line cache instead of turning it into a CMake
    # list, which would split values that contain semicolons.
    set(value "")
    if("${cache}" MATCHES "(^|\n)${name}:[A-Za-z]+=([^\n\r]*)")
        set(value "${CMAKE_MATCH_2}")
    endif()
    set(${out} "${value}" PARENT_SCOPE)
endfunction()

function(tb_normalized_path out value)
    file(TO_CMAKE_PATH "${value}" normalized)
    if(WIN32)
        string(TOLOWER "${normalized}" normalized)
    endif()
    set(${out} "${normalized}" PARENT_SCOPE)
endfunction()

# Builds a native pass-through wrapper so the forwarding case can prove the
# nested configure used the parent compiler: the default compiler would
# otherwise resolve to the same path and hide a removed forwarding line.
function(tb_build_compiler_wrapper out real_compiler source_file stem output_dir)
    if("${real_compiler}" STREQUAL "")
        set(${out} "" PARENT_SCOPE)
        return()
    endif()

    set(suffix "")
    if(WIN32)
        set(suffix ".exe")
    endif()
    set(wrapper "${output_dir}/${stem}${suffix}")

    execute_process(
        COMMAND "${real_compiler}" "${source_file}" -o "${wrapper}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error_output
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "FAIL: forwarding: could not build ${stem} with '${real_compiler}' (exit ${result}):\n${output}${error_output}")
    endif()
    set(${out} "${wrapper}" PARENT_SCOPE)
endfunction()

# Writes the pass-through wrapper sources into output_dir. Called only by the
# forwarding case so parallel matrix cases never rewrite shared files.
function(tb_write_compiler_wrappers out_cxx_source out_c_source output_dir)
    set(wrapper_cxx_source "${output_dir}/gui-matrix-wrapper.cpp")
    set(wrapper_c_source "${output_dir}/gui-matrix-wrapper.c")

file(WRITE "${wrapper_cxx_source}" [=[
#include <cstddef>
#include <cstdlib>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

int main(int argc, char** argv) {
    const char* real = std::getenv("TB_MATRIX_REAL_CXX_COMPILER");
    if (real == nullptr) {
        return 127;
    }
    std::vector<char*> args;
    args.reserve(static_cast<std::size_t>(argc));
    args.push_back(const_cast<char*>(real));
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    args.push_back(nullptr);
#ifdef _WIN32
    return static_cast<int>(_spawnv(_P_WAIT, real, args.data()));
#else
    execv(real, args.data());
    return 127;
#endif
}
]=])

file(WRITE "${wrapper_c_source}" [=[
#include <stddef.h>
#include <stdlib.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

int main(int argc, char** argv) {
    const char* real = getenv("TB_MATRIX_REAL_C_COMPILER");
    char** args;
    int i;
    if (real == NULL) {
        return 127;
    }
    args = (char**)malloc(sizeof(char*) * (size_t)(argc + 1));
    if (args == NULL) {
        return 127;
    }
    args[0] = (char*)real;
    for (i = 1; i < argc; ++i) {
        args[i] = argv[i];
    }
    args[argc] = NULL;
#ifdef _WIN32
    return (int)_spawnv(_P_WAIT, real, (const char* const*)args);
#else
    execv(real, args);
    return 127;
#endif
}
]=])

    set(${out_cxx_source} "${wrapper_cxx_source}" PARENT_SCOPE)
    set(${out_c_source} "${wrapper_c_source}" PARENT_SCOPE)
endfunction()

function(tb_case_defaults)
    set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/gui-matrix-defaults")
    tb_nested_configure("${case_dir}")
    tb_expect_configure_ok("defaults" "${NESTED_STARTED}" "${NESTED_RESULT}"
                           "${NESTED_OUTPUT}" "${NESTED_ERROR}")

    tb_read_cache("defaults" "${case_dir}/CMakeCache.txt" cache)
    if(NOT cache MATCHES "TB_ENABLE_GUI:BOOL=ON")
        message(FATAL_ERROR "FAIL: defaults: TB_ENABLE_GUI does not default to ON")
    endif()
    if(NOT cache MATCHES "TB_REQUIRE_GUI:BOOL=OFF")
        message(FATAL_ERROR "FAIL: defaults: TB_REQUIRE_GUI does not default to OFF")
    endif()
    message(STATUS "PASS: defaults")
endfunction()

function(tb_case_require_without_enable)
    set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/gui-matrix-require-without-enable")
    tb_nested_configure("${case_dir}"
        -DTB_ENABLE_GUI=OFF
        -DTB_REQUIRE_GUI=ON)
    tb_expect_configure_rejected("require_without_enable" "${NESTED_STARTED}" "${NESTED_RESULT}"
                                 "${NESTED_OUTPUT}" "${NESTED_ERROR}"
                                 "TB_REQUIRE_GUI=ON requires TB_ENABLE_GUI=ON")
    message(STATUS "PASS: require_without_enable")
endfunction()

function(tb_case_require_missing_qt)
    set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/gui-matrix-require-missing-qt")
    tb_nested_configure("${case_dir}"
        -DTB_ENABLE_GUI=ON
        -DTB_REQUIRE_GUI=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON)
    tb_expect_configure_rejected("require_missing_qt" "${NESTED_STARTED}" "${NESTED_RESULT}"
                                 "${NESTED_OUTPUT}" "${NESTED_ERROR}"
                                 "Qt6 was not found, but TB_REQUIRE_GUI=ON")
    message(STATUS "PASS: require_missing_qt")
endfunction()

function(tb_case_optional_missing_qt)
    # Non-fatal combinations must stay non-fatal.
    tb_validate_gui_options(ON ON)
    tb_validate_gui_options(OFF ON)
    tb_validate_gui_options(OFF OFF)
    tb_require_qt_or_fail(OFF)

    set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/gui-matrix-optional-missing-qt")
    tb_nested_configure("${case_dir}"
        -DTB_ENABLE_GUI=ON
        -DTB_REQUIRE_GUI=OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON)
    tb_expect_configure_ok("optional_missing_qt" "${NESTED_STARTED}" "${NESTED_RESULT}"
                           "${NESTED_OUTPUT}" "${NESTED_ERROR}")

    set(combined "${NESTED_OUTPUT}${NESTED_ERROR}")
    if(NOT combined MATCHES "GUI target skipped this configure run")
        message(FATAL_ERROR "FAIL: optional_missing_qt: configure succeeded without the GUI-skip message:\n${combined}")
    endif()
    message(STATUS "PASS: optional_missing_qt")
endfunction()

function(tb_case_disabled)
    set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/gui-matrix-disabled")
    tb_nested_configure("${case_dir}" -DTB_ENABLE_GUI=OFF)
    tb_expect_configure_ok("disabled" "${NESTED_STARTED}" "${NESTED_RESULT}"
                           "${NESTED_OUTPUT}" "${NESTED_ERROR}")

    tb_read_cache("disabled" "${case_dir}/CMakeCache.txt" cache)
    if(NOT cache MATCHES "TB_ENABLE_GUI:BOOL=OFF")
        message(FATAL_ERROR "FAIL: disabled: TB_ENABLE_GUI did not stay OFF")
    endif()

    set(combined "${NESTED_OUTPUT}${NESTED_ERROR}")
    if(combined MATCHES "torrent_builder_gui target configured")
        message(FATAL_ERROR "FAIL: disabled: GUI target block ran with TB_ENABLE_GUI=OFF")
    endif()

    if(TEST_GENERATOR MATCHES "Makefiles|Ninja")
        execute_process(
            COMMAND ${CMAKE_COMMAND} --build "${case_dir}" --target help
            RESULT_VARIABLE help_result
            OUTPUT_VARIABLE help_output
            ERROR_VARIABLE help_error
        )
        if(NOT help_result EQUAL 0)
            message(FATAL_ERROR "FAIL: disabled: could not query the generated target list:\n${help_output}${help_error}")
        endif()
        if("${help_output}${help_error}" MATCHES "torrent_builder_gui")
            message(FATAL_ERROR "FAIL: disabled: torrent_builder_gui target exists with TB_ENABLE_GUI=OFF")
        endif()
    endif()

    if(NOT TEST_QT_FOUND)
        message(STATUS "NOTE: disabled: Qt is unavailable in the parent configure; target-absence is only partially observable")
    endif()
    message(STATUS "PASS: disabled")
endfunction()

function(tb_case_forwarding)
    set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/gui-matrix-forwarding")

    # A configure that cannot even start must be reported, never treated as
    # an ordinary failed configure that a case could misread as PASS.
    tb_nested_configure("")
    if(NESTED_STARTED)
        message(FATAL_ERROR "FAIL: forwarding: an empty build directory must not start a configure")
    endif()
    if(NOT NESTED_RESULT EQUAL 127)
        message(FATAL_ERROR "FAIL: forwarding: expected exit 127 for a setup failure, got ${NESTED_RESULT}")
    endif()
    if(NOT NESTED_ERROR MATCHES "build directory is empty")
        message(FATAL_ERROR "FAIL: forwarding: setup failure did not explain the empty build directory:\n${NESTED_ERROR}")
    endif()

    # Dangerous build directories must be rejected before removal; validate
    # them without ever calling the path that deletes.
    tb_nested_build_dir_error("${CMAKE_CURRENT_LIST_DIR}/../.." path_error path_real)
    if(NOT "${path_error}" MATCHES "must differ from the source directory")
        message(FATAL_ERROR "FAIL: forwarding: the source directory was accepted as a build directory (${path_error})")
    endif()
    tb_nested_build_dir_error("${CMAKE_CURRENT_LIST_DIR}/../../.." path_error path_real)
    # At a shallow checkout the parent is the filesystem root, which the
    # validator rejects with the root message rather than the ancestor one.
    if(NOT "${path_error}" MATCHES "must not contain the source directory|must not be the filesystem root")
        message(FATAL_ERROR "FAIL: forwarding: an ancestor of the source directory was accepted as a build directory (${path_error})")
    endif()
    tb_nested_build_dir_error("/" path_error path_real)
    if(NOT "${path_error}" MATCHES "must not be the filesystem root")
        message(FATAL_ERROR "FAIL: forwarding: the filesystem root was accepted as a build directory (${path_error})")
    endif()
    tb_nested_build_dir_error("${CMAKE_CURRENT_BINARY_DIR}/gui-matrix-path-check" path_error path_real)
    if(NOT "${path_error}" STREQUAL "")
        message(FATAL_ERROR "FAIL: forwarding: a normal build directory was rejected: ${path_error}")
    endif()

    # Prove the -G line is forwarded: an unknown generator must reach CMake and
    # fail; without forwarding the default generator would configure normally.
    set(real_generator "${TEST_GENERATOR}")
    set(TEST_GENERATOR "tb-matrix-no-such-generator")
    tb_nested_configure("${CMAKE_CURRENT_BINARY_DIR}/gui-matrix-forwarding-generator")
    if(NOT NESTED_STARTED)
        message(FATAL_ERROR "FAIL: forwarding: the bogus generator did not start a configure:\n${NESTED_ERROR}")
    endif()
    if(NESTED_RESULT EQUAL 0)
        message(FATAL_ERROR "FAIL: forwarding: the bogus generator was not forwarded")
    endif()
    if(NOT "${NESTED_OUTPUT}${NESTED_ERROR}" MATCHES "tb-matrix-no-such-generator")
        message(FATAL_ERROR "FAIL: forwarding: the generator failure did not name the forwarded generator:\n${NESTED_OUTPUT}${NESTED_ERROR}")
    endif()
    set(TEST_GENERATOR "${real_generator}")

    # Prove the -A line is forwarded: a platform the generator cannot honour
    # must fail; without forwarding the platform would be ignored.
    set(real_generator_platform "${TEST_GENERATOR_PLATFORM}")
    set(TEST_GENERATOR_PLATFORM "tb-matrix-no-such-platform")
    tb_nested_configure("${CMAKE_CURRENT_BINARY_DIR}/gui-matrix-forwarding-platform")
    if(NOT NESTED_STARTED)
        message(FATAL_ERROR "FAIL: forwarding: the bogus platform did not start a configure:\n${NESTED_ERROR}")
    endif()
    if(NESTED_RESULT EQUAL 0)
        message(FATAL_ERROR "FAIL: forwarding: the bogus platform was not forwarded")
    endif()
    set(TEST_GENERATOR_PLATFORM "${real_generator_platform}")

    # Values that may be empty in the parent get a safe sentinel in this
    # process only, so removing the corresponding forwarding line still fails.
    set(expected_build_type "${TEST_BUILD_TYPE}")
    if("${expected_build_type}" STREQUAL "")
        set(expected_build_type "RelWithDebInfo")
        set(TEST_BUILD_TYPE "${expected_build_type}")
    endif()
    if("${TEST_TOOLCHAIN_FILE}" STREQUAL "")
        set(TEST_TOOLCHAIN_FILE "${CMAKE_CURRENT_BINARY_DIR}/gui-matrix-empty-toolchain.cmake")
        file(WRITE "${TEST_TOOLCHAIN_FILE}"
            "# Empty toolchain file: proves CMAKE_TOOLCHAIN_FILE forwarding.\n")
    endif()
    if("${TEST_OSX_ARCHITECTURES}" STREQUAL "")
        if(APPLE)
            set(TEST_OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
        else()
            # Ignored outside Apple, but the nested cache still proves the
            # semicolon list stayed one argument.
            set(TEST_OSX_ARCHITECTURES "tb-matrix-arch-a;tb-matrix-arch-b")
        endif()
    endif()

    # Point the nested configure at pass-through wrappers so the cache value
    # can only match when the parent compiler was really forwarded. The
    # sources live in a forwarding-only directory so a parallel matrix case
    # never rewrites a file this case is compiling.
    set(wrapper_dir "${CMAKE_CURRENT_BINARY_DIR}/gui-matrix-forwarding-wrappers")
    file(MAKE_DIRECTORY "${wrapper_dir}")
    tb_write_compiler_wrappers(wrapper_cxx_source wrapper_c_source "${wrapper_dir}")

    set(real_cxx_compiler "${TEST_CXX_COMPILER}")
    set(real_c_compiler "${TEST_C_COMPILER}")
    tb_build_compiler_wrapper(wrapper_cxx "${real_cxx_compiler}" "${wrapper_cxx_source}" "gui-matrix-cxx-wrapper" "${wrapper_dir}")
    tb_build_compiler_wrapper(wrapper_c "${real_c_compiler}" "${wrapper_c_source}" "gui-matrix-c-wrapper" "${wrapper_dir}")
    if(NOT "${wrapper_cxx}" STREQUAL "")
        set(TEST_CXX_COMPILER "${wrapper_cxx}")
        set(ENV{TB_MATRIX_REAL_CXX_COMPILER} "${real_cxx_compiler}")
    endif()
    if(NOT "${wrapper_c}" STREQUAL "")
        set(TEST_C_COMPILER "${wrapper_c}")
        set(ENV{TB_MATRIX_REAL_C_COMPILER} "${real_c_compiler}")
    endif()

    tb_nested_configure("${case_dir}" -DTB_ENABLE_GUI=OFF)
    tb_expect_configure_ok("forwarding" "${NESTED_STARTED}" "${NESTED_RESULT}"
                           "${NESTED_OUTPUT}" "${NESTED_ERROR}")
    tb_read_cache("forwarding" "${case_dir}/CMakeCache.txt" cache)

    if(NOT "${TEST_GENERATOR}" STREQUAL "")
        tb_cache_value(nested_generator "${cache}" "CMAKE_GENERATOR")
        if(NOT "${nested_generator}" STREQUAL "${TEST_GENERATOR}")
            message(FATAL_ERROR "FAIL: forwarding: CMAKE_GENERATOR '${nested_generator}' does not match '${TEST_GENERATOR}'")
        endif()
    endif()

    foreach(lang C CXX)
        if(NOT "${TEST_${lang}_COMPILER}" STREQUAL "")
            tb_cache_value(nested_compiler "${cache}" "CMAKE_${lang}_COMPILER")
            tb_normalized_path(nested_compiler_path "${nested_compiler}")
            tb_normalized_path(expected_compiler_path "${TEST_${lang}_COMPILER}")
            if(NOT "${nested_compiler_path}" STREQUAL "${expected_compiler_path}")
                message(FATAL_ERROR "FAIL: forwarding: CMAKE_${lang}_COMPILER '${nested_compiler}' does not match '${TEST_${lang}_COMPILER}'")
            endif()
        endif()
    endforeach()

    if(NOT "${TEST_TOOLCHAIN_FILE}" STREQUAL "")
        tb_cache_value(nested_toolchain "${cache}" "CMAKE_TOOLCHAIN_FILE")
        tb_normalized_path(nested_toolchain "${nested_toolchain}")
        tb_normalized_path(expected_toolchain "${TEST_TOOLCHAIN_FILE}")
        if(NOT "${nested_toolchain}" STREQUAL "${expected_toolchain}")
            message(FATAL_ERROR "FAIL: forwarding: CMAKE_TOOLCHAIN_FILE '${nested_toolchain}' does not match '${expected_toolchain}'")
        endif()
    endif()

    tb_cache_value(nested_openssl "${cache}" "OPENSSL_ROOT_DIR")
    tb_normalized_path(nested_openssl "${nested_openssl}")
    tb_normalized_path(expected_openssl "${TEST_OPENSSL_ROOT_DIR}")
    if(NOT "${nested_openssl}" STREQUAL "${expected_openssl}")
        message(FATAL_ERROR "FAIL: forwarding: OPENSSL_ROOT_DIR '${nested_openssl}' does not match '${expected_openssl}'")
    endif()

    # Multi-config generators ignore CMAKE_BUILD_TYPE and expose
    # CMAKE_CONFIGURATION_TYPES instead.
    if(NOT cache MATCHES "CMAKE_CONFIGURATION_TYPES:")
        tb_cache_value(nested_build_type "${cache}" "CMAKE_BUILD_TYPE")
        string(TOLOWER "${nested_build_type}" nested_build_type_lower)
        string(TOLOWER "${expected_build_type}" expected_build_type_lower)
        if(NOT "${nested_build_type_lower}" STREQUAL "${expected_build_type_lower}")
            message(FATAL_ERROR "FAIL: forwarding: CMAKE_BUILD_TYPE '${nested_build_type}' does not match '${expected_build_type}'")
        endif()
    endif()

    if(NOT "${TEST_OSX_ARCHITECTURES}" STREQUAL "")
        tb_cache_value(nested_architectures "${cache}" "CMAKE_OSX_ARCHITECTURES")
        if(NOT "${nested_architectures}" STREQUAL "${TEST_OSX_ARCHITECTURES}")
            message(FATAL_ERROR "FAIL: forwarding: CMAKE_OSX_ARCHITECTURES '${nested_architectures}' does not match '${TEST_OSX_ARCHITECTURES}'")
        endif()
    endif()

    if(NOT "${TEST_GENERATOR_PLATFORM}" STREQUAL "")
        tb_cache_value(nested_platform "${cache}" "CMAKE_GENERATOR_PLATFORM")
        if(NOT "${nested_platform}" STREQUAL "${TEST_GENERATOR_PLATFORM}")
            message(FATAL_ERROR "FAIL: forwarding: CMAKE_GENERATOR_PLATFORM '${nested_platform}' does not match '${TEST_GENERATOR_PLATFORM}'")
        endif()
    endif()

    message(STATUS "PASS: forwarding")
endfunction()

function(tb_case_prefix_list)
    set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/gui-matrix-prefix-list")
    set(prefix_a "${CMAKE_CURRENT_BINARY_DIR}/tb-matrix-prefix-a")
    set(prefix_b "${CMAKE_CURRENT_BINARY_DIR}/tb-matrix-prefix-b")

    if(NOT "${TEST_CMAKE_PREFIX_PATH}" STREQUAL "")
        set(TEST_CMAKE_PREFIX_PATH "${TEST_CMAKE_PREFIX_PATH};${prefix_a};${prefix_b}")
    else()
        set(TEST_CMAKE_PREFIX_PATH "${prefix_a};${prefix_b}")
    endif()

    tb_nested_configure("${case_dir}" -DTB_ENABLE_GUI=OFF)
    tb_expect_configure_ok("prefix_list" "${NESTED_STARTED}" "${NESTED_RESULT}"
                           "${NESTED_OUTPUT}" "${NESTED_ERROR}")
    tb_read_cache("prefix_list" "${case_dir}/CMakeCache.txt" cache)

    tb_cache_value(nested_prefix_path "${cache}" "CMAKE_PREFIX_PATH")
    if(NOT "${nested_prefix_path}" STREQUAL "${TEST_CMAKE_PREFIX_PATH}")
        message(FATAL_ERROR "FAIL: prefix_list: CMAKE_PREFIX_PATH '${nested_prefix_path}' does not match '${TEST_CMAKE_PREFIX_PATH}'")
    endif()
    message(STATUS "PASS: prefix_list")
endfunction()

# The windows_executable case asserts an exe path that only single-config
# generators produce; multi-config generators place it under <config>/.
function(tb_single_config_generator out generator)
    set(supported FALSE)
    if("${generator}" MATCHES "Makefiles|^Ninja$")
        set(supported TRUE)
    endif()
    set(${out} "${supported}" PARENT_SCOPE)
endfunction()

function(tb_case_windows_executable)
    # The build below only runs on Windows, but the generator filter is
    # asserted everywhere so accepting Ninja Multi-Config fails in any CI.
    tb_single_config_generator(generator_ok "Ninja")
    if(NOT generator_ok)
        message(FATAL_ERROR "FAIL: windows_executable: the generator filter rejects single-config Ninja")
    endif()
    tb_single_config_generator(generator_ok "Unix Makefiles")
    if(NOT generator_ok)
        message(FATAL_ERROR "FAIL: windows_executable: the generator filter rejects Makefiles")
    endif()
    tb_single_config_generator(generator_ok "Ninja Multi-Config")
    if(generator_ok)
        message(FATAL_ERROR "FAIL: windows_executable: the generator filter accepts Ninja Multi-Config")
    endif()

    set(readme "${CMAKE_CURRENT_LIST_DIR}/../../README.md")
    if(NOT EXISTS "${readme}")
        message(FATAL_ERROR "FAIL: windows_executable: README.md not found at ${readme}")
    endif()
    file(READ "${readme}" readme_text)
    if(NOT readme_text MATCHES "torrent_builder_gui\\.exe")
        message(FATAL_ERROR "FAIL: windows_executable: README does not document torrent_builder_gui.exe")
    endif()

    if(NOT WIN32)
        message(STATUS "SKIP: windows_executable (not Windows)")
        return()
    endif()
    if(NOT TEST_QT_FOUND)
        message(STATUS "SKIP: windows_executable (Qt unavailable in the parent configure)")
        return()
    endif()
    # Ninja Multi-Config also contains "Ninja" but puts the executable in a
    # per-config subdirectory; only single-config generators are built here.
    tb_single_config_generator(generator_ok "${TEST_GENERATOR}")
    if(NOT generator_ok)
        message(STATUS "SKIP: windows_executable (unsupported generator: ${TEST_GENERATOR})")
        return()
    endif()

    set(case_dir "${CMAKE_CURRENT_BINARY_DIR}/gui-matrix-windows-executable")
    tb_nested_configure("${case_dir}"
        -DTB_ENABLE_GUI=ON
        -DTB_REQUIRE_GUI=ON)
    tb_expect_configure_ok("windows_executable" "${NESTED_STARTED}" "${NESTED_RESULT}"
                           "${NESTED_OUTPUT}" "${NESTED_ERROR}")

    set(combined "${NESTED_OUTPUT}${NESTED_ERROR}")
    if(NOT combined MATCHES "torrent_builder_gui target configured")
        message(FATAL_ERROR "FAIL: windows_executable: GUI target was not configured:\n${combined}")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${case_dir}" --target torrent_builder_gui
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "FAIL: windows_executable: building torrent_builder_gui failed (exit ${build_result}):\n${build_output}${build_error}")
    endif()
    if(NOT EXISTS "${case_dir}/torrent_builder_gui.exe")
        message(FATAL_ERROR "FAIL: windows_executable: torrent_builder_gui.exe was not produced in ${case_dir}")
    endif()
    message(STATUS "PASS: windows_executable")
endfunction()

if(TEST_MATRIX_CASE STREQUAL "defaults")
    tb_case_defaults()
elseif(TEST_MATRIX_CASE STREQUAL "require_without_enable")
    tb_case_require_without_enable()
elseif(TEST_MATRIX_CASE STREQUAL "require_missing_qt")
    tb_case_require_missing_qt()
elseif(TEST_MATRIX_CASE STREQUAL "optional_missing_qt")
    tb_case_optional_missing_qt()
elseif(TEST_MATRIX_CASE STREQUAL "disabled")
    tb_case_disabled()
elseif(TEST_MATRIX_CASE STREQUAL "forwarding")
    tb_case_forwarding()
elseif(TEST_MATRIX_CASE STREQUAL "prefix_list")
    tb_case_prefix_list()
elseif(TEST_MATRIX_CASE STREQUAL "windows_executable")
    tb_case_windows_executable()
else()
    message(FATAL_ERROR "No dispatch for TEST_MATRIX_CASE '${TEST_MATRIX_CASE}'")
endif()
