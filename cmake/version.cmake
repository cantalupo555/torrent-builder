if(DEFINED TORRENT_BUILDER_VERSION)
    set(TORRENT_BUILDER_DISPLAY_VERSION "${TORRENT_BUILDER_VERSION}")
    string(REGEX REPLACE "[-+].*" "" TORRENT_BUILDER_VERSION "${TORRENT_BUILDER_VERSION}")
    message(STATUS "Version override: ${TORRENT_BUILDER_DISPLAY_VERSION}")
else()
    execute_process(
        COMMAND git describe --tags --exact-match
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_TAG_RESULT
    )
    if(GIT_TAG_RESULT EQUAL 0 AND GIT_TAG)
        string(REGEX REPLACE "^[Vv]" "" RAW_VERSION "${GIT_TAG}")
        set(TORRENT_BUILDER_DISPLAY_VERSION "${RAW_VERSION}")
        string(REGEX REPLACE "[-+].*" "" TORRENT_BUILDER_VERSION "${RAW_VERSION}")
        message(STATUS "Version from git tag: ${TORRENT_BUILDER_DISPLAY_VERSION}")
    else()
        set(TORRENT_BUILDER_VERSION "0.0.0")
        execute_process(
            COMMAND git rev-parse --is-inside-work-tree
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_WORKTREE
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE GIT_WORKTREE_RESULT
        )
        if(GIT_WORKTREE_RESULT EQUAL 0 AND GIT_WORKTREE STREQUAL "true")
            execute_process(
                COMMAND git rev-parse --short HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE GIT_COMMIT
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE GIT_COMMIT_RESULT
            )
            if(GIT_COMMIT_RESULT EQUAL 0 AND GIT_COMMIT)
                set(TORRENT_BUILDER_DISPLAY_VERSION "dev (${GIT_COMMIT})")
            else()
                set(TORRENT_BUILDER_DISPLAY_VERSION "dev")
            endif()
            message(STATUS "Version: ${TORRENT_BUILDER_DISPLAY_VERSION} (no git tag found)")
        else()
            set(TORRENT_BUILDER_DISPLAY_VERSION "dev")
            message(STATUS "Version: dev (git not available or not a git repository)")
        endif()
    endif()
endif()
