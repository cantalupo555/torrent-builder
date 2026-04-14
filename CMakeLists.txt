cmake_minimum_required(VERSION 3.28)
project(torrent_builder VERSION 1.0 LANGUAGES CXX)

# Minimum GCC version check for C++23 Ranges support
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
        message(FATAL_ERROR
            "GCC 13 or later is required for C++23 Ranges support.\n"
            "Detected version: ${CMAKE_CXX_COMPILER_VERSION}.\n"
            "See README.md for build instructions."
        )
    endif()
endif()

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

# Find libtorrent
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBTORRENT REQUIRED libtorrent-rasterbar>=2.0.10)

# Explicit libtorrent version check
if(LIBTORRENT_FOUND)
    if(LIBTORRENT_VERSION VERSION_LESS 2.0.10)
        message(FATAL_ERROR
            "libtorrent-rasterbar >= 2.0.10 is required.\n"
            "Detected version: ${LIBTORRENT_VERSION}.\n"
            "See README.md for build instructions."
        )
    endif()
    message(STATUS "libtorrent-rasterbar ${LIBTORRENT_VERSION} found.")
endif()

# Add compile definitions for libtorrent
add_compile_definitions(
    TORRENT_USE_LIBCRYPTO
    TORRENT_USE_OPENSSL
    TORRENT_USE_IPV6=1
)

# Add cxxopts
include(FetchContent)
FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG        v3.1.1
)
FetchContent_MakeAvailable(cxxopts)

# Add main executable
add_executable(torrent_builder
    src/torrent_builder.cpp
    src/torrent_creator.cpp
)

# Include directories
target_include_directories(torrent_builder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBTORRENT_INCLUDE_DIRS}
)

# Add constants header
target_sources(torrent_builder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/constants.hpp
)

# Link libraries
target_link_libraries(torrent_builder PRIVATE
    ${LIBTORRENT_LIBRARIES}
    ${LIBTORRENT_LDFLAGS}
    cxxopts::cxxopts
)

# Add compiler and linker flags
target_compile_options(torrent_builder PRIVATE ${LIBTORRENT_CFLAGS})
target_link_options(torrent_builder PRIVATE ${LIBTORRENT_LDFLAGS})

# Installation settings (optional)
install(TARGETS torrent_builder
    RUNTIME DESTINATION bin
)

# Test settings (to add future tests)
enable_testing()

