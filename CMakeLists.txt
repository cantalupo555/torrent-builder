cmake_minimum_required(VERSION 3.14)
project(torrent_builder VERSION 1.0 LANGUAGES CXX)

# Basic project settings
set(CMAKE_CXX_STANDARD 20)
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
pkg_check_modules(LIBTORRENT REQUIRED libtorrent-rasterbar>=2.0)

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
