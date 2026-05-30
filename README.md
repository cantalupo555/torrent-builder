# Torrent Builder

The **Torrent Builder** is a command-line tool for creating torrent files, offering a complete and customizable experience. With support for interactive and non-interactive modes, you can create torrents quickly and efficiently, either through a step-by-step wizard or with direct commands. The tool allows you to configure various aspects of the torrent, such as version (V1, V2, or Hybrid), comments, privacy, web seeds, and trackers, ensuring your torrents are created exactly as you need them.

## Features

- Create torrent files from single files or directories
- Inspect existing torrent files (metadata, file tree, verification, magnet link)
- Modify existing torrent metadata without re-hashing file content
- Support for torrent versions: V1, V2, and Hybrid
- Interactive mode with step-by-step configuration
- Command-line interface with options for all features
- Automatic piece size calculation based on file size
- Manual piece size configuration
- Support for private torrents
- Add multiple trackers and web seeds
- Include comments in torrent metadata
- Cross-seeding support: source field and info hash randomization
- Detailed summary output after creation
- Verbose mode for detailed creation diagnostics
- Quiet mode for scripts and CI pipelines
- JSON output for programmatic consumption
- Auto-naming: output filename generated from tracker domain and content name
- Collision-safe naming: automatically resolves filename conflicts with `(1)`, `(2)`, etc.
- Filename truncation with UTF-8 boundary safety (255-byte filesystem limit)
- YAML-based preset system for per-tracker configuration
- Batch mode: create multiple torrents in parallel from a YAML config
- Configurable output directory (auto-created if needed) and tracker index for filename prefix
- Season pack detection: warn or fail on incomplete TV season packs (missing episodes)

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

### Inspect Torrent Files

```bash
./torrent_builder inspect file.torrent [options]
```

### Modify Torrent Metadata

```bash
./torrent_builder modify file.torrent [options]
```

Edit metadata of an existing .torrent file in-place without re-hashing file content. Supports V1, V2, and hybrid torrents. Changes are written atomically (temp file + rename).

> **Note:** `--output` is optional. When omitted, the output filename is auto-generated from the tracker domain and content name (e.g., `tracker.example.com_myfile.torrent`).

### Batch Mode

```bash
./torrent_builder batch batch.yaml [--workers N]
```

Process multiple torrent creation jobs from a YAML config file in parallel. See the [Batch Mode](#batch-mode-1) section below for details.

## Options

```
  -h, --help                 Show help
  -v, --version              Show version
  --verbose                  Enable verbose output (conflicts with --quiet, --json)
  -q, --quiet                Suppress non-essential output, auto-decline prompts (conflicts with --verbose)
  --json                     Output torrent metadata as JSON (implies --quiet, conflicts with --verbose)
  -i, --interactive          Run in interactive mode
  -p, --path arg             Path to file or directory (required)
  -o, --output arg           Output torrent file path (optional; auto-generated if omitted)
  -t, --torrent-version arg  Torrent version (1=v1, 2=v2, 3=hybrid) (default: 3)
  --comment arg              Torrent comment
  -n, --name arg             Set custom torrent name (overrides default inferred from path)
  --private                  Make torrent private
  --default-trackers         Use default trackers
  -T, --tracker arg          Add tracker URL (can be used multiple times)
  -w, --webseed arg          Add web seed URL (can be used multiple times)
  -s, --piece-size arg       Piece size in KB (must be one of: 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768)
  --creator                  Set "Torrent Builder" as creator
  --creation-date            Set creation date
  --source arg               Add source string to torrent info for cross-seeding
  -e, --entropy              Randomize info hash by adding entropy field
  -x, --exclude arg          Exclude files matching glob pattern (can be used multiple times)
  -I, --include arg          Include only files matching glob pattern (can be used multiple times)
       --skip-prefix          Omit tracker domain from auto-generated output filename
       --output-dir DIR       Directory for auto-generated output filename (created if needed)
       --tracker-index N      Index of tracker to use for filename prefix (0-based, default: 0)
       --preset NAME          Apply named preset from presets.yaml
       --preset-file FILE     Load presets from specified file (default: searches ./presets.yaml, $XDG_CONFIG_HOME/torrent-builder/presets.yaml, ~/.config/torrent-builder/presets.yaml)
       --fail-on-season-warning  Fail if a TV season pack has missing episodes
```

> **Note:** `--verbose`, `--quiet`, and `--json` are ignored in interactive mode. In CLI mode, `--verbose` and `--quiet` are mutually exclusive, as are `--verbose` and `--json`. The `--json` flag implies `--quiet` and auto-declines any overwrite prompts.

### JSON Output Format

When using `--json`, the output is a single JSON object to stdout (errors go to stderr):

```json
{
  "name": "filename",
  "info_hash_v1": "...",
  "info_hash_v2": "...",
  "is_hybrid": false,
  "total_size": 12345678,
  "piece_length": 262144,
  "piece_count": 48,
  "files_count": 1,
  "is_private": false,
  "trackers": ["https://tracker.example/announce"],
  "web_seeds": [],
  "magnet_link": "...",
  "output_path": "/path/to/output.torrent"
}
```

Optional fields (`comment`, `creation_date`, `created_by`, `source`, `entropy`) appear only when present in the torrent metadata.

> **Note:** `--version` now shows the software version. For torrent format version, use `--torrent-version` or `-t`.

### Inspect Options

```
  ./torrent_builder inspect <torrent_file> [options]

  --json           Output in JSON format
  --files          Show detailed file tree only
  --verify         Verify files exist on disk
  --base-path DIR  Base path for file verification (default: current directory)
```

### Modify Options

```
  ./torrent_builder modify <torrent_file> [options]

  -h, --help              Show help
  -t, --tracker URL       Replace all trackers (exclusive with --add-tracker/--remove-tracker)
      --add-tracker URL   Add tracker URL (can be used multiple times)
      --remove-tracker URL Remove tracker URL (can be used multiple times)
      --private           Mark torrent as private
      --public            Mark torrent as public
      --source SOURCE     Set source field (empty string removes it)
      --comment COMMENT   Set comment (empty string removes it)
      --name NAME         Change torrent name
      --entropy           Randomize info hash by adding entropy field
  -o, --output OUTPUT     Output torrent file path (defaults to in-place)
      --dry-run           Preview changes without writing
```

> **Note:** `--tracker` is exclusive with `--add-tracker`/`--remove-tracker`. `--private` and `--public` are mutually exclusive. At least one modification option is required.

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

./torrent_builder --path /data/file --tracker "https://tracker.example.com/announce" --output-dir /torrents/output
# Auto-creates /torrents/output if it doesn't exist
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

Cross-seeding (unique info hash per tracker):
```bash
./torrent_builder --path /data/file --output ptp.torrent \
  --source "PTP" --tracker "https://ptp.example/announce"

./torrent_builder --path /data/file --output hdb.torrent \
  --source "HDB" --tracker "https://hdb.example/announce"

./torrent_builder --path /data/file --output unique.torrent \
  -e --tracker "https://tracker.example/announce"
```

Season pack detection:
```bash
# Fail if the directory is an incomplete season pack (missing episodes)
./torrent_builder --path /data/Show.Name.S01 --output season.torrent \
  --fail-on-season-warning
# Error: Season 1 pack has missing episodes (E03) in: Show.Name.S01

# Without the flag, the torrent is created normally
./torrent_builder --path /data/Show.Name.S01 --output season.torrent
```

Create a torrent with default trackers and custom trackers:
```bash
./torrent_builder --path /data/file --output file.torrent --default-trackers --tracker udp://mytracker.com:8080
```

Exclude unwanted files from a directory:
```bash
./torrent_builder --path /data/folder --output folder.torrent \
  --exclude "*.nfo" --exclude "*.txt"

./torrent_builder --path /data/folder --output folder.torrent \
  --exclude "subs/**"
```

Include only specific file types:
```bash
./torrent_builder --path /data/folder --output folder.torrent \
  --include "*.mkv" --include "*.mp4"
```

> **Note:** `--include` patterns take precedence over `--exclude` when both match. Glob syntax: `*` (any non-slash), `**/` (zero or more dirs), `**` (any path), `?` (single char). Matching is case-insensitive.

Verbose, quiet, and JSON output:
```bash
./torrent_builder --path /data/file --output file.torrent --verbose
# Shows extra detail: file count, piece size reasoning, tracker tiers

./torrent_builder --path /data/file --output file.torrent --quiet
# Suppresses progress bar and summary; errors still shown

./torrent_builder --path /data/file --output file.torrent --json
# Outputs torrent metadata as JSON to stdout (useful for scripting)
```

Inspect torrent metadata:
```bash
./torrent_builder inspect file.torrent
# Shows: name, info hash (v1/v2), size, piece size, trackers, web seeds, comment, magnet link

./torrent_builder inspect file.torrent --json
# Same info in JSON format (useful for scripting)

./torrent_builder inspect file.torrent --files
# Shows detailed file tree with sizes

./torrent_builder inspect file.torrent --verify --base-path /data
# Verifies all files exist on disk under /data
```

Modify torrent metadata:
```bash
./torrent_builder modify file.torrent --tracker "https://tracker.example/announce"
# Replaces all trackers with the specified one

./torrent_builder modify file.torrent --add-tracker "https://tracker2.example/announce"
# Adds a tracker to the existing list

./torrent_builder modify file.torrent --remove-tracker "https://old.example/announce"
# Removes a specific tracker

./torrent_builder modify file.torrent --private
# Marks torrent as private

./torrent_builder modify file.torrent --public
# Marks torrent as public (removes private flag)

./torrent_builder modify file.torrent --source "PTP"
# Sets source field for cross-seeding

./torrent_builder modify file.torrent --source ""
# Removes source field

./torrent_builder modify file.torrent --comment "Updated comment"
# Sets or updates comment

./torrent_builder modify file.torrent --comment ""
# Removes comment

./torrent_builder modify file.torrent --name "New Name" --entropy
# Changes torrent name and randomizes info hash

./torrent_builder modify file.torrent --dry-run --tracker "https://tracker.example/announce"
# Preview changes without writing to file

./torrent_builder modify file.torrent --output modified.torrent --entropy
# Write to a new file instead of modifying in-place
```

### Preset System

Presets save per-tracker settings in a YAML file. Use `--preset <name>` to apply a preset when creating a torrent.

**Preset file format** (`presets.yaml`):
```yaml
version: 1
default:
  private: true
  exclude_patterns: ["*.nfo", "*.txt"]

presets:
  ptp:
    source: "PTP"
    trackers:
      - "https://ptp.example/announce"
    exclude_patterns: ["*.nfo", "*.txt", "*.sfv"]

  public:
    private: false
    trackers:
      - "udp://tracker.example/announce"
```

**Preset file resolution**: `--preset-file` (if given, used directly). Otherwise, search order: `./presets.yaml` → `$XDG_CONFIG_HOME/torrent-builder/presets.yaml` → `~/.config/torrent-builder/presets.yaml`.

**Merge hierarchy**: CLI flags > preset values > `default:` section > built-in defaults.

**Usage:**
```bash
# Apply a preset
./torrent_builder --preset ptp --path /data/file -o file.torrent

# Specify a custom preset file
./torrent_builder --preset ptp --preset-file /config/presets.yaml --path /data/file -o file.torrent

# Preset values are overridden by CLI flags
./torrent_builder --preset ptp --path /data/file -o file.torrent --comment "override"
```

### Batch Mode

Create multiple torrents in parallel from a YAML config file.

**Batch file format** (`batch.yaml`):
```yaml
version: 1
workers: 2
preset_file: presets.yaml
output_dir: /torrents/output

jobs:
  - path: /data/movie1.mkv
    preset: ptp
  - path: /data/movie2.mkv
    output: custom_output.torrent
    trackers:
      - "https://tracker.example/announce"
  - path: /data/Show.Name.S01
    fail_on_season_warning: true
```

> **Note:** Output paths automatically receive a `.torrent` extension if not already present. `workers: 0` in the YAML config throws an error; `--workers 0` on the CLI is ignored with a warning.

**Usage:**
```bash
# Run a batch job
./torrent_builder batch batch.yaml

# Override worker count
./torrent_builder batch batch.yaml --workers 4
```

Each job can optionally reference a preset by name. Jobs run in parallel with `--workers` threads (default: 1). A summary showing success/failure per job is printed at the end.

## Troubleshooting

- **Compilation Error: 'contains' is not a member of 'std::ranges'**: This occurs with older compilers (e.g., GCC 12 on Debian 12). CMakeLists.txt requires GCC 13+ for C++23. Solutions:
  - Upgrade GCC: `sudo apt install g++-13` (via backports on Debian 12) or upgrade to Debian 13/Ubuntu 24.04.
  - Use Docker for testing: `docker run -v $(pwd):/app -w /app ubuntu:24.04 bash -c "apt update && apt install -y build-essential cmake libtorrent-rasterbar-dev pkg-config && mkdir build && cd build && cmake .. && make"`.
- **libtorrent Not Found or pkg-config Error**: Check your installation: `pkg-config --modversion libtorrent-rasterbar`. Reinstall if < 2.0.10 (e.g., `sudo apt install libtorrent-rasterbar-dev pkg-config`). pkg-config is required for dependency detection.
- **FetchContent/cxxopts Failure**: Make sure you have an internet connection (it downloads the library during the configure step).
- **Need More Help**: Open a [GitHub issue](https://github.com/cantalupo555/torrent-builder/issues) with logs (e.g., `cmake .. 2>&1 | tee cmake.log` and `make 2>&1 | tee make.log`).

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
