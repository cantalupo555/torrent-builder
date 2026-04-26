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
- Auto-naming: output filename generated from tracker domain and content name
- Collision-safe naming: automatically resolves filename conflicts with `(1)`, `(2)`, etc.
- Filename truncation with UTF-8 boundary safety (255-byte filesystem limit)
- Configurable output directory and tracker index for filename prefix

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

**Versioning**: When building from a git tag (e.g., `v0.2.0`), the version is automatically detected. Otherwise, the version defaults to `dev`. You can override with `-DTORRENT_BUILDER_VERSION=x.y.z` during cmake configuration. Use `./torrent_builder --version` to check.

## Usage

### Interactive Mode

```bash
./torrent_builder -i
```

### Command-line Mode

```bash
./torrent_builder --path /path/to/file_or_directory [options]
```

> **Note:** `--output` is optional. When omitted, the output filename is auto-generated from the tracker domain and content name (e.g., `tracker.example.com_myfile.torrent`).

## Options

```
  -h, --help                 Show help
  -v, --version              Show version
  -i, --interactive          Run in interactive mode
  -p, --path arg             Path to file or directory (required)
  -o, --output arg           Output torrent file path (optional; auto-generated if omitted)
  -t, --torrent-version arg  Torrent version (1=v1, 2=v2, 3=hybrid) (default: 3)
  --comment arg              Torrent comment
  --private                  Make torrent private
  --default-trackers         Use default trackers
  -T, --tracker arg          Add tracker URL (can be used multiple times)
  -w, --webseed arg          Add web seed URL (can be used multiple times)
  -s, --piece-size arg       Piece size in KB (must be one of: 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768)
  --creator                  Set "Torrent Builder" as creator
  --creation-date            Set creation date
      --skip-prefix          Omit tracker domain from auto-generated output filename
      --output-dir DIR       Directory for auto-generated output filename (must already exist)
      --tracker-index N      Index of tracker to use for filename prefix (0-based, default: 0)
```

> **Note:** `--version` now shows the software version. For torrent format version, use `--torrent-version` or `-t`.

## Examples

Basic usage:
```bash
./torrent_builder -i
./torrent_builder --path /data/file --output file.torrent
./torrent_builder --path /data/file --output file.torrent --default-trackers
./torrent_builder --path /data/folder --output folder.torrent --torrent-version 2 --private
./torrent_builder --path /data/file --output file.torrent --piece-size 1024
./torrent_builder --version
```

Auto-naming (output filename generated automatically):
```bash
./torrent_builder --path /data/file --tracker "https://tracker.example.com/announce"
# Creates: tracker.example.com_file.torrent

./torrent_builder --path /data/file --tracker "https://tracker.example.com/announce" --skip-prefix
# Creates: file.torrent (no tracker domain prefix)

./torrent_builder --path /data/file --tracker "https://tracker.example.com/announce" --output-dir /torrents
# Creates: /torrents/tracker.example.com_file.torrent
```

Auto-naming with multiple trackers:
```bash
./torrent_builder --path /data/file \
  --tracker "https://alpha.tracker.io/announce" \
  --tracker "https://beta.tracker.net/announce" \
  --tracker-index 1
# Uses the second tracker for filename: beta.tracker.net_file.torrent
```

Collision resolution (automatic when auto-naming):
```bash
# If tracker.example.com_file.torrent already exists:
./torrent_builder --path /data/file --tracker "https://tracker.example.com/announce"
# Creates: tracker.example.com_file(1).torrent
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
