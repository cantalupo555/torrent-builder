# Runs a nested configure of the real project using the same generator,
# compilers and toolchain as the parent build, and reuses the dependencies
# already fetched by the parent so the configure stays offline.
#
# Sets in the caller scope:
#   NESTED_RESULT  - process exit code
#   NESTED_OUTPUT  - stdout
#   NESTED_ERROR   - stderr
#
# Usage: tb_nested_configure(<build_dir> [extra configure arguments...])

function(tb_nested_configure build_dir)
    set(forward_args)
    if(TEST_GENERATOR)
        list(APPEND forward_args -G "${TEST_GENERATOR}")
    endif()
    if(TEST_CXX_COMPILER)
        list(APPEND forward_args "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}")
    endif()
    if(TEST_C_COMPILER)
        list(APPEND forward_args "-DCMAKE_C_COMPILER=${TEST_C_COMPILER}")
    endif()
    if(TEST_TOOLCHAIN_FILE)
        list(APPEND forward_args "-DCMAKE_TOOLCHAIN_FILE=${TEST_TOOLCHAIN_FILE}")
    endif()
    if(TEST_OPENSSL_ROOT_DIR)
        list(APPEND forward_args "-DOPENSSL_ROOT_DIR=${TEST_OPENSSL_ROOT_DIR}")
    endif()
    if(TEST_CMAKE_PREFIX_PATH)
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

    file(REMOVE_RECURSE "${build_dir}")

    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -S "${CMAKE_CURRENT_LIST_DIR}/../.."
            -B "${build_dir}"
            ${forward_args}
            ${fetch_args}
            ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error_output
    )

    set(NESTED_RESULT "${result}" PARENT_SCOPE)
    set(NESTED_OUTPUT "${output}" PARENT_SCOPE)
    set(NESTED_ERROR "${error_output}" PARENT_SCOPE)
endfunction()
