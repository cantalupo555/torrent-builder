if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
        message(FATAL_ERROR
            "GCC 13 or later is required for C++23 Ranges support.\n"
            "Detected version: ${CMAKE_CXX_COMPILER_VERSION}.\n"
            "See README.md for build instructions."
        )
    endif()
endif()
