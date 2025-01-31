cmake_minimum_required(VERSION 3.14)
project(torrent_builder VERSION 1.0 LANGUAGES CXX)

# Configurações básicas do projeto
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Configurações de compilação
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-Wall -Wextra -g -O0)
else()
    add_compile_options(-O2)
endif()

# Encontrar libtorrent
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBTORRENT REQUIRED libtorrent-rasterbar)

# Adicionar cxxopts
include(FetchContent)
FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG        v3.1.1
)
FetchContent_MakeAvailable(cxxopts)

# Adicionar executável principal
add_executable(torrent_builder
    src/torrent_builder.cpp
    src/torrent_creator.cpp
)

# Incluir diretórios
target_include_directories(torrent_builder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LIBTORRENT_INCLUDE_DIRS}
)

# Linkar bibliotecas
target_link_libraries(torrent_builder PRIVATE
    ${LIBTORRENT_LIBRARIES}
    cxxopts::cxxopts
)

# Configurações de instalação (opcional)
install(TARGETS torrent_builder
    RUNTIME DESTINATION bin
)

# Configurações de teste (para adicionar testes futuros)
enable_testing()
