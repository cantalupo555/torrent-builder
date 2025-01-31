# Torrent Maker

A command-line tool for creating torrent files with support for both interactive and non-interactive modes.

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

## Installation

### Requirements

- CMake (>= 3.14)
- Libtorrent
- C++20 compatible compiler

### Build Instructions

```bash
mkdir build
cd build
cmake ..
make
```

## Usage

### Interactive Mode

```bash
./torrent_maker -i
```

### Command-line Mode

```bash
./torrent_maker --path /path/to/file_or_directory --output output.torrent [options]
```

## Options

```
  -h, --help         Show help
  -i, --interactive  Run in interactive mode
  --path arg         Path to file or directory (required)
  --output arg       Output torrent file path (required)
  --version arg      Torrent version (1=v1, 2=v2, 3=hybrid) (default: 1)
  --comment arg      Torrent comment
  --private          Make torrent private
  --webseed arg      Add web seed URL (can be used multiple times)
```

## Examples

Create a V1 torrent:

```bash
./torrent_maker --path /data/file --output file.torrent
```

Create a private V2 torrent:

```bash
./torrent_maker --path /data/folder --output folder.torrent --version 2 --private
```

Add web seeds:

```bash
./torrent_maker --path /data/file --output file.torrent \
  --webseed http://example.com/file \
  --webseed http://mirror.com/file
```
