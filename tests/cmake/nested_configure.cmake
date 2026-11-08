# Runs a nested configure of the real project using the same generator,
# generator platform, compilers, toolchain, build type, architectures and
# dependency prefixes as the parent build, and reuses the dependencies
# already fetched by the parent so the configure stays offline.
#
# Reads (all optional, forwarded only when non-empty):
#   TEST_GENERATOR            - CMake generator
#   TEST_GENERATOR_PLATFORM   - generator platform (Visual Studio)
#   TEST_CXX_COMPILER         - CMAKE_CXX_COMPILER
#   TEST_C_COMPILER           - CMAKE_C_COMPILER
#   TEST_TOOLCHAIN_FILE       - CMAKE_TOOLCHAIN_FILE
#   TEST_BUILD_TYPE           - CMAKE_BUILD_TYPE
#   TEST_OSX_ARCHITECTURES    - CMAKE_OSX_ARCHITECTURES
#   TEST_OPENSSL_ROOT_DIR     - OPENSSL_ROOT_DIR
#   TEST_CMAKE_PREFIX_PATH    - CMAKE_PREFIX_PATH (semicolon list preserved)
#
# Sets in the caller scope:
#   NESTED_STARTED - TRUE when the configure process was started
#   NESTED_RESULT  - process exit code (127 when it could not start)
#   NESTED_OUTPUT  - stdout
#   NESTED_ERROR   - stderr, including start-up failures
#
# Usage: tb_nested_configure(<build_dir> [extra configure arguments...])

# Resolve the project root once, while this file is being included: inside a
# function CMAKE_CURRENT_LIST_DIR points at the caller's file, not this one.
set(tb_source_input "${CMAKE_CURRENT_LIST_DIR}/../..")
cmake_path(ABSOLUTE_PATH tb_source_input NORMALIZE OUTPUT_VARIABLE tb_source_dir)
set_property(GLOBAL PROPERTY TB_NESTED_CONFIGURE_SOURCE_DIR "${tb_source_dir}")

# Validates a nested build directory before it is removed. Returns an error
# string when the directory could delete the source tree, plus the normalized
# absolute path. Kept separate so the dangerous paths can be tested without
# ever reaching file(REMOVE_RECURSE).
function(tb_nested_build_dir_error build_dir out_error out_real)
    get_property(source_dir GLOBAL PROPERTY TB_NESTED_CONFIGURE_SOURCE_DIR)

    set(error "")
    set(build_dir_real "")
    if("${build_dir}" STREQUAL "")
        set(error "nested configure build directory is empty")
    else()
        set(build_input "${build_dir}")
        cmake_path(ABSOLUTE_PATH build_input
            BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            NORMALIZE
            OUTPUT_VARIABLE build_dir_real)

        set(source_cmp "${source_dir}")
        set(build_cmp "${build_dir_real}")
        if(WIN32)
            string(TOLOWER "${source_cmp}" source_cmp)
            string(TOLOWER "${build_cmp}" build_cmp)
        endif()

        if("${build_cmp}" STREQUAL "/" OR "${build_cmp}" MATCHES "^[a-z]:/$")
            set(error "nested configure build directory must not be the filesystem root")
        else()
            # cmake_path keeps a trailing slash for paths ending in '..';
            # strip it so equality and prefix checks compare cleanly.
            string(REGEX REPLACE "/+$" "" source_cmp "${source_cmp}")
            string(REGEX REPLACE "/+$" "" build_cmp "${build_cmp}")

            if("${build_cmp}" STREQUAL "${source_cmp}")
                set(error "nested configure build directory must differ from the source directory")
            else()
                # Removing an ancestor would remove the source tree with it.
                string(FIND "${source_cmp}/" "${build_cmp}/" ancestor_index)
                if(ancestor_index EQUAL 0)
                    set(error "nested configure build directory must not contain the source directory")
                endif()
            endif()
        endif()
    endif()

    set(${out_error} "${error}" PARENT_SCOPE)
    set(${out_real} "${build_dir_real}" PARENT_SCOPE)
endfunction()

function(tb_nested_configure build_dir)
    get_property(source_dir GLOBAL PROPERTY TB_NESTED_CONFIGURE_SOURCE_DIR)

    set(NESTED_STARTED FALSE)
    set(result 127)
    set(output "")
    set(error_output "")

    tb_nested_build_dir_error("${build_dir}" setup_error build_dir_real)

    if(NOT "${setup_error}" STREQUAL "")
        set(error_output "${setup_error}")
    elseif(NOT EXISTS "${source_dir}/CMakeLists.txt")
        set(error_output "project source not found at ${source_dir}")
    else()
        set(forward_args)
        if(NOT "${TEST_GENERATOR}" STREQUAL "")
            list(APPEND forward_args -G "${TEST_GENERATOR}")
        endif()
        if(NOT "${TEST_GENERATOR_PLATFORM}" STREQUAL "")
            list(APPEND forward_args -A "${TEST_GENERATOR_PLATFORM}")
        endif()
        if(NOT "${TEST_CXX_COMPILER}" STREQUAL "")
            list(APPEND forward_args "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}")
        endif()
        if(NOT "${TEST_C_COMPILER}" STREQUAL "")
            list(APPEND forward_args "-DCMAKE_C_COMPILER=${TEST_C_COMPILER}")
        endif()
        if(NOT "${TEST_TOOLCHAIN_FILE}" STREQUAL "")
            list(APPEND forward_args "-DCMAKE_TOOLCHAIN_FILE=${TEST_TOOLCHAIN_FILE}")
        endif()
        if(NOT "${TEST_BUILD_TYPE}" STREQUAL "")
            list(APPEND forward_args "-DCMAKE_BUILD_TYPE=${TEST_BUILD_TYPE}")
        endif()
        if(NOT "${TEST_OSX_ARCHITECTURES}" STREQUAL "")
            # Universal builds pass a semicolon list; escape it like the
            # prefix path so it stays one command argument.
            string(REPLACE ";" "\\;" osx_architectures "${TEST_OSX_ARCHITECTURES}")
            list(APPEND forward_args "-DCMAKE_OSX_ARCHITECTURES=${osx_architectures}")
        endif()
        if(NOT "${TEST_OPENSSL_ROOT_DIR}" STREQUAL "")
            list(APPEND forward_args "-DOPENSSL_ROOT_DIR=${TEST_OPENSSL_ROOT_DIR}")
        endif()
        if(NOT "${TEST_CMAKE_PREFIX_PATH}" STREQUAL "")
            # Escape semicolons so the list append keeps one argument; otherwise
            # a multi-entry prefix path is split into several command arguments.
            string(REPLACE ";" "\\;" prefix_path "${TEST_CMAKE_PREFIX_PATH}")
            list(APPEND forward_args "-DCMAKE_PREFIX_PATH=${prefix_path}")
        endif()

        set(fetch_args)
        foreach(dep cxxopts yaml-cpp googletest nlohmann_json)
            string(TOUPPER "${dep}" dep_upper)
            set(dep_src "${CMAKE_CURRENT_BINARY_DIR}/_deps/${dep}-src")
            if(EXISTS "${dep_src}")
                list(APPEND fetch_args "-DFETCHCONTENT_SOURCE_DIR_${dep_upper}=${dep_src}")
            endif()
        endforeach()

        file(REMOVE_RECURSE "${build_dir_real}")

        execute_process(
            COMMAND ${CMAKE_COMMAND}
                -S "${source_dir}"
                -B "${build_dir_real}"
                ${forward_args}
                ${fetch_args}
                ${ARGN}
            RESULT_VARIABLE start_result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE error_output
        )

        # execute_process returns a string instead of a number when the
        # command cannot be started; never let that look like a normal
        # failed configure.
        if(start_result MATCHES "^-?[0-9]+$")
            set(NESTED_STARTED TRUE)
            set(result "${start_result}")
        else()
            set(error_output "nested configure could not be started: ${start_result}\n${error_output}")
        endif()
    endif()

    set(NESTED_STARTED "${NESTED_STARTED}" PARENT_SCOPE)
    set(NESTED_RESULT "${result}" PARENT_SCOPE)
    set(NESTED_OUTPUT "${output}" PARENT_SCOPE)
    set(NESTED_ERROR "${error_output}" PARENT_SCOPE)
endfunction()
