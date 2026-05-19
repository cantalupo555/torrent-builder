---
created: 2026-05-16
branch: feat/preset-batch-mode
type: feat
issue: 27
---

## Objective

Implement the preset system (#25) and batch mode (#27) together. Both features share the yaml-cpp dependency and batch mode depends on the preset system for the `preset:` field in batch jobs.

## Preset System (#25)

Add YAML-based preset configuration that saves per-tracker settings and applies them automatically. Users can reference presets by name via `--preset <name>`.

### Preset YAML Format

```yaml
version: 1
default:
  private: true
  exclude_patterns: ["*.nfo"]
  piece_length: 0

presets:
  ptp:
    source: "PTP"
    trackers:
      - "https://ptp.example/announce"
    exclude_patterns: ["*.nfo", "*.txt"]
    include_patterns: ["*.mkv", "*.mp4"]
  public:
    private: false
    trackers:
      - "udp://tracker.opentrackr.org:1337/announce"
```

### Preset File Search Order

1. `./presets.yaml` (current working directory)
2. `$XDG_CONFIG_HOME/torrent-builder/presets.yaml` (falls back to `~/.config/` when unset)
3. `--preset-file <path>` (explicit override)

### Merge Hierarchy

CLI flags > preset values > `default:` section > TorrentConfig built-in defaults

Uses an intermediate `ConfigValues` struct (all-optional fields) as the merge layer. CLI flags are captured via `result.count()` into `ConfigValues`, presets are parsed into `ConfigValues`, then merged before constructing `TorrentConfig`. This ensures correct "was this explicitly set?" tracking.

## Batch Mode (#27)

Add batch processing via a YAML config file that processes multiple torrent creation jobs, optionally in parallel.

### Batch YAML Format

```yaml
version: 1
workers: 2
preset_file: "/path/to/presets.yaml"
output_dir: "/path/to/output"

jobs:
  - path: "/path/to/movie"
    output: "Movie.2026.torrent"
    trackers: ["https://tracker.example/announce"]
    private: true
    source: "SRC"
    exclude_patterns: ["*.nfo"]

  - path: "/path/to/show"
    output: "Show.S01.torrent"
    preset: ptp

  - path: "/path/to/album"
    preset: public
```

### Batch Features

- Parallel processing with configurable worker count (default: 1)
- Summary report: success/failure per job with elapsed time
- Each job supports all create flags + preset references
- Failed jobs logged but don't block remaining queue
- Batch mode suppresses interactive prompts (always overwrites)
- Auto-output generation when `output` field is omitted

## Implementation Plan

### Phase 1: Add yaml-cpp Dependency

- [ ] Modify `CMakeLists.txt` to add `yaml-cpp` 0.8.0 (find_package fallback, then FetchContent)
- [ ] Link `yaml-cpp::yaml-cpp` to a new `torrent_builder_config` static library
- [ ] Verify build succeeds on all platforms

### Phase 2: Preset System

- [ ] Create `include/preset.hpp` with `ConfigValues` struct (all-optional, used as intermediate merge layer) and `PresetLoader` class
- [ ] Create `src/preset.cpp` with YAML parsing, XDG-aware file search, preset resolution logic
- [ ] Add `src/preset.cpp` to `torrent_builder_config` library in CMakeLists.txt
- [ ] Modify `src/torrent_builder.cpp`: add `--preset` and `--preset-file` flags to create subcommand
- [ ] Refactor `get_commandline_config()` to produce `ConfigValues` first, merge with preset, then construct `TorrentConfig`
- [ ] Create `tests/test_preset.cpp` with unit tests for parsing, merge, and precedence

### Phase 3: Batch Mode

- [ ] Create `include/batch.hpp` with `BatchJob`, `BatchConfig`, `BatchResult` structs and `BatchProcessor` class
- [ ] Create `src/batch.cpp` with batch YAML parsing, parallel execution (atomic counter), summary report
- [ ] Add `src/batch.cpp` to `torrent_builder_config` library in CMakeLists.txt
- [ ] Modify `src/torrent_builder.cpp`: add `batch` subcommand handler (`handle_batch_command()`)
- [ ] Implement parallel execution with `std::jthread` + `std::atomic<int>` counter (fork-join pattern)
- [ ] Implement summary report printing
- [ ] Create `tests/test_batch.cpp` with unit tests

### Phase 4: CLI Integration Tests

- [ ] Add batch subcommand tests to `tests/test_cli.cpp` (valid batch, invalid YAML, missing file)
- [ ] Add preset flag tests to `tests/test_cli.cpp` (--preset, --preset-file, unknown preset)

## Key Design Decisions

### ConfigValues Struct (Intermediate Merge Layer)

All-optional fields mirroring TorrentConfig. Serves as the single intermediate for both CLI flag capture (via `result.count()`) and YAML preset data. Enables correct 3-way merge: `default → named_preset → cli_or_job`.

```cpp
struct ConfigValues {
    std::optional<std::string> path;
    std::optional<std::string> output;
    std::optional<std::vector<std::string>> trackers;
    std::optional<std::vector<std::string>> web_seeds;
    std::optional<bool> is_private;
    std::optional<std::string> source;
    std::optional<int> piece_size;             // KB, nullopt = auto
    std::optional<std::string> comment;
    std::optional<std::string> creator;
    std::optional<std::string> name;
    std::optional<bool> creation_date;
    std::optional<int> torrent_version;        // 1/2/3
    std::optional<bool> entropy;
    std::optional<std::vector<std::string>> exclude_patterns;
    std::optional<std::vector<std::string>> include_patterns;
};
```

### Batch Parallelism

- Fork-join pattern using `std::jthread` + `std::atomic<int>` counter (no queue/mutex/cv needed)
- Each worker atomically increments counter to claim next job index
- Each job gets its own `TorrentCreator` instance (no shared mutable state)
- Results vector pre-allocated by job index for ordered output

### Batch Output Handling

- When `output` is omitted: use `utils::generate_auto_output_path()` with job's trackers
- Batch-level `output_dir` applies as default directory for auto-generated outputs
- Skip overwrite prompts in batch mode (always overwrite)

## Files to Create/Modify

| File | Action |
|------|--------|
| `CMakeLists.txt` | Modify — yaml-cpp (find_package + FetchContent), new `torrent_builder_config` library |
| `include/preset.hpp` | New — ConfigValues struct, PresetLoader class |
| `src/preset.cpp` | New — YAML parsing, XDG-aware file search, preset resolution |
| `include/batch.hpp` | New — BatchJob, BatchConfig, BatchResult, BatchProcessor |
| `src/batch.cpp` | New — batch YAML parsing, parallel execution, summary report |
| `src/torrent_builder.cpp` | Modify — batch subcommand, --preset/--preset-file flags |
| `tests/test_preset.cpp` | New — preset unit tests |
| `tests/test_batch.cpp` | New — batch unit tests |
| `tests/test_cli.cpp` | Modify — add batch/preset CLI integration tests |

## Acceptance Criteria

### Preset System (#25)

- [ ] Parse YAML preset files with hierarchical merge (default → preset → CLI)
- [ ] Search for presets in standard locations (cwd, XDG config dir, custom path)
- [ ] `--preset <name>` flag loads and applies a named preset
- [ ] CLI flags override preset values
- [ ] Unknown preset name produces clear error
- [ ] Unit tests for YAML parsing and merge logic
- [ ] Integration with `--source`, `--exclude`/`--include`, and tracker flags

### Batch Mode (#27)

- [ ] `batch` subcommand reads YAML job list
- [ ] Parallel processing with configurable worker count
- [ ] Each job supports all create flags
- [ ] Jobs can reference presets by name
- [ ] Summary report with per-job status
- [ ] Failed jobs logged but don't block remaining queue
- [ ] Unit tests for job parsing and parallel execution

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| yaml-cpp FetchContent build fails on some platforms | Test on all CI platforms (Linux/Windows/macOS). Pin stable version 0.8.0. |
| Thread safety in parallel batch mode | Each job gets its own TorrentCreator instance. No shared mutable state between workers. Results vector pre-allocated by job index. |
| Memory pressure: N parallel jobs each running multi-threaded hashing | Default worker count is 1. Document tuning guidance. Users control via config. |
| Preset + CLI flag merge complexity | Track "was specified?" per field explicitly. Unit test merge logic thoroughly. |
| Batch mode interactive prompts | Auto-set QUIET verbosity in batch mode. Skip overwrite prompts. |

## Validation Strategy

1. Build check: `cmake --build build` compiles clean
2. Unit tests: `ctest --output-on-failure` — all existing + new tests pass
3. Manual smoke test: create presets.yaml + batch.yaml with 2-3 small test jobs
4. CI: existing GitHub Actions covers Linux/Windows/macOS
