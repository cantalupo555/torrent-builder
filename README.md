# Torrent Builder

The **Torrent Builder** is a command-line tool for creating torrent files, offering a complete and customizable experience. With support for interactive and non-interactive modes, you can create torrents quickly and efficiently, either through a step-by-step wizard or with direct commands. The tool allows you to configure various aspects of the torrent, such as version (V1, V2, or Hybrid), comments, privacy, web seeds, and trackers, ensuring your torrents are created exactly as you need them.

## Features

- Create torrent files from single files or directories
- Support for torrent versions: V1, V2, and Hybrid
- Interactive mode with step-by-step configuration
- Command-line interface with options for all features
- Automatic piece size calculation based on file size
- Manual piece size configuration
- Support for private torrents
- Add multiple trackers and web seeds
- Include comments in torrent metadata
- Detailed summary output after creation

## Prerequisites

### General Requirements
- **C++23 Compiler**: GCC 13+, Clang 15+.
- **CMake**: >= 3.28
- **libtorrent-rasterbar**: >= 2.0.10 (for C++17/20+ support and compatibility)
- **pkg-config**: Required for libtorrent detection via PkgConfig in CMake
- **Build Tools**: Varies by OS (see below)

### Linux

Ubuntu/Debian
```
sudo apt install build-essential cmake libtorrent-rasterbar-dev pkg-config
```

Fedora
```
sudo dnf install gcc-c++ cmake rb_libtorrent-devel
```

### macOS
```
brew install cmake libtorrent-rasterbar
```

## Installation

To build, you need a compatible C++23 compiler (see Prerequisites). CMake will automatically check and display a clear error if the compiler is inadequate (e.g., GCC < 13).

### Build Instructions
```
mkdir build
cd build
cmake ..
cmake --build .
```
For an optimized Release build: `cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build .`.

**Note**: The executable will be generated at `build/torrent_builder`. For debugging, use `-DCMAKE_BUILD_TYPE=Debug`. If pkg-config fails, check your installation in Prerequisites.

## Usage

### Interactive Mode

```bash
./torrent_builder -i
```

### Command-line Mode

```bash
./torrent_builder --path /path/to/file_or_directory --output output.torrent [options]
```

## Options

```
  -h, --help                 Show help
  -i, --interactive          Run in interactive mode
  --path arg                 Path to file or directory (required)
  --output arg               Output torrent file path (required)
  --version arg              Torrent version (1=v1, 2=v2, 3=hybrid) (default: 3)
  --comment arg              Torrent comment
  --private                  Make torrent private
  --default-trackers         Use default trackers
  --tracker arg              Add tracker URL (can be used multiple times)
  --webseed arg              Add web seed URL (can be used multiple times)
  --piece-size arg           Piece size in KB (must be one of: 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768)
  --creator                  Set "Torrent Builder" as creator
  --creation-date            Set creation date
```

## Examples

Basic usage:
```bash
./torrent_builder -i
./torrent_builder --path /data/file --output file.torrent
./torrent_builder --path /data/file --output file.torrent --default-trackers
./torrent_builder --path /data/folder --output folder.torrent --version 2 --private
./torrent_builder --path /data/file --output file.torrent --piece-size 1024
```

Add multiple trackers (added in ascending order of priority; the first tracker has the highest priority — tier 0):
```bash
./torrent_builder --path /data/file --output file.torrent \
  --tracker udp://tracker.example.com:80 \
  --tracker http://backup-tracker.org:6969
```

Add multiple web seeds:
```bash
./torrent_builder --path /data/file --output file.torrent \
  --webseed http://example.com/file \
  --webseed http://mirror.com/file
```

Create torrent with comment:
```bash
./torrent_builder --path /data/file --output file.torrent \
  --comment "My important file"
```

Create a torrent with default trackers and custom trackers:
```bash
./torrent_builder --path /data/file --output file.torrent --default-trackers --tracker udp://mytracker.com:8080
```

## Troubleshooting

- **Compilation Error: 'contains' is not a member of 'std::ranges'**: This occurs with older compilers (e.g., GCC 12 on Debian 12). CMakeLists.txt requires GCC 13+ for C++23. Solutions:
  - Upgrade GCC: `sudo apt install g++-13` (via backports on Debian 12) or upgrade to Debian 13/Ubuntu 24.04.
  - Use Docker for testing: `docker run -v $(pwd):/app -w /app ubuntu:24.04 bash -c "apt update && apt install -y build-essential cmake libtorrent-rasterbar-dev pkg-config && mkdir build && cd build && cmake .. && make"`.
- **libtorrent Not Found or pkg-config Error**: Check your installation: `pkg-config --modversion libtorrent-rasterbar`. Reinstall if < 2.0.10 (e.g., `sudo apt install libtorrent-rasterbar-dev pkg-config`). pkg-config is required for dependency detection.
- **FetchContent/cxxopts Failure**: Make sure you have an internet connection (it downloads the library during the configure step).
- **Need More Help**: Open a [GitHub issue](https://github.com/cantalupo555/torrent-builder/issues) with logs (e.g., `cmake .. 2>&1 | tee cmake.log` and `make 2>&1 | tee make.log`).

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
