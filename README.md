# Torrent Builder

The **Torrent Builder** is a command-line tool for creating torrent files, offering a complete and customizable experience. With support for interactive and non-interactive modes, you can create torrents quickly and efficiently, either through a step-by-step wizard or with direct commands. The tool allows you to configure various aspects of the torrent, such as version (V1, V2, or Hybrid), comments, privacy, web seeds, and trackers, ensuring your torrents are created exactly as you need them.

## Features

- Create torrent files from single files or directories
- Support for torrent versions: V1, V2, and Hybrid
- Interactive mode with step-by-step configuration
- Command-line interface with options for all features
- Automatic piece size calculation based on file size
- Support for private torrents
- Add multiple trackers and web seeds
- Include comments in torrent metadata
- Detailed summary output after creation

## Prerequisites

Before building, ensure you have the following installed:

### Linux

-   **Build Tools:** `build-essential`
-   **CMake:**  `cmake` (>= 3.28.3)
-   **libtorrent:** `libtorrent-rasterbar-dev` (>= 2.0.11)

Install them using:
```bash
sudo apt-get install build-essential cmake libtorrent-rasterbar-dev
```

### macOS

-   **CMake:** `cmake` (>= 3.28.3)
-   **libtorrent:** `libtorrent-rasterbar` (>= 2.0.11)

Install them using:
```bash
brew install cmake libtorrent-rasterbar
```

## Installation
To build the project, you will need a C++20 compatible compiler.

### Build Instructions

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

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
  -h, --help         Show help
  -i, --interactive  Run in interactive mode
  --path arg         Path to file or directory (required)
  --output arg       Output torrent file path (required)
  --version arg      Torrent version (1=v1, 2=v2, 3=hybrid) (default: 3)
  --comment arg      Torrent comment
  --private          Make torrent private
  --webseed arg      Add web seed URL (can be used multiple times)
```

## Examples

### Create a hybrid torrent (default)
```bash
./torrent_builder --path /data/file --output file.torrent
```

### Create a private V2 torrent
```bash
./torrent_builder --path /data/folder --output folder.torrent --version 2 --private
```

### Add multiple web seeds
```bash
./torrent_builder --path /data/file --output file.torrent \
  --webseed http://example.com/file \
  --webseed http://mirror.com/file
```

### Create torrent with comment
```bash
./torrent_builder --path /data/file --output file.torrent \
  --comment "My important file"
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
