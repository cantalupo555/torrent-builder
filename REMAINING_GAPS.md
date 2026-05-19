# Remaining Gaps — Round 6 Code Review

## Minor (Test Coverage)

- [ ] PresetLoader::load() file size limit untested (batch equivalent tested via ParseFileTooLargeThrows)
  - src/preset.cpp:127-130 (1MB file_size check before LoadFile)
  - Add PresetTest.LoadFileTooLargeThrows test

- [ ] CWD preset search success path untested
  - src/preset.cpp:find_preset_file() checks ./presets.yaml before XDG/HOME
  - XDG path tested (FindPresetFileXDGConfigHome), CWD path is not
  - Add test that creates ./presets.yaml and verifies find_preset_file() returns it

- [ ] Worker capping logic untested
  - src/batch.cpp:293-297 (min(config.workers, jobs.size(), hardware_concurrency * 2))
  - No test deliberately triggers the cap (e.g. workers > jobs or workers > hw_concurrency * 2)
  - Add test with workers: 100 and verify actual_workers is capped

- [ ] 9 of 13 CLI preset fallback fields without direct CLI-level regression test
  - src/torrent_builder.cpp:get_commandline_config() — name, web_seeds, piece_size, creator, creation_date, entropy, torrent_version, exclude_patterns, include_patterns
  - Unit tests cover parsing and merge; CLI integration tests cover comment, source, private, trackers
  - Add CLI tests verifying these fields propagate from preset to created torrent

- [ ] Batch CLI exit code with job failure unverified
  - src/torrent_builder.cpp:handle_batch_command returns 1 if any job fails
  - No CLI test creates a batch with a failing job and checks exit code != 0
  - Add test with invalid path in batch job, verify exit code 1

- [ ] FindPresetFileNoFileThrows lacks env isolation
  - tests/test_preset.cpp:FindPresetFileNoFileThrows may pass unexpectedly if ~/.config/torrent-builder/presets.yaml exists
  - Wrap with XDG_CONFIG_HOME save/restore like FindPresetFileXDGConfigHome

## Minor (Plan Deviation)

- [ ] yaml-cpp version mismatch: planned 0.8.0, implemented find_package(0.6.0) + FetchContent(0.9.0)
  - CMakeLists.txt — compatible, 0.9.0 is newer stable release
  - Low risk: only basic YAML features used (scalars, sequences, maps)

## Minor (Observability)

- [ ] handle_batch_command missing specialized catch blocks for filesystem_error and YAML::Exception
  - src/torrent_builder.cpp:1108-1120 — only catches runtime_error and std::exception
  - Main handler has dedicated catch blocks per type; batch handler lumps them together
  - Not a correctness issue (filesystem_error and YAML::Exception both inherit runtime_error)

## Previously Resolved (Rounds 1-5)

- [x] optional<bool> merge bug — FIXED with .has_value() (preset.cpp)
- [x] ANSI escape injection — FIXED with sanitize_for_terminal() (batch.cpp)
- [x] Batch output .torrent extension — FIXED (batch.cpp)
- [x] --workers 0 warning — FIXED with print_error + log_message (torrent_builder.cpp)
- [x] Batch wall-clock time — FIXED (torrent_builder.cpp)
- [x] fork-join terminology — FIXED to worker-pool (batch.hpp)
- [x] Data race on results vector — FIXED with explicit joins (batch.cpp)
- [x] Logger thread safety — FIXED with static mutex + localtime_r (logger.cpp, logger.hpp)
- [x] YAML file size limit — FIXED with 1MB cap (preset.cpp, batch.cpp)
- [x] Case-insensitive .torrent extension — FIXED with utils::to_lower (batch.cpp)
- [x] Troubleshooting heading missing — FIXED (README.md)
- [x] .torrent auto-append undocumented — FIXED (README.md)
- [x] Batch subcommand missing from usage — FIXED (README.md)
- [x] workers: 0 behavior undocumented — FIXED (README.md)
- [x] --preset-file in batch CLI example (nonexistent flag) — FIXED (README.md)
- [x] Job started log entry — FIXED (batch.cpp)
- [x] Batch failure count at ERR level — FIXED (batch.cpp)
- [x] log_mutex removed from batch.cpp — FIXED (logger now thread-safe)
- [x] .torrent extension tests — FIXED (3 tests in test_batch.cpp)
- [x] --workers 0 CLI test — FIXED (WorkersZeroShowsWarning in test_cli.cpp)
- [x] YAML file size test — FIXED (ParseFileTooLargeThrows in test_batch.cpp)
- [x] optional<bool> merge test — FIXED (MergeExplicitFalseOverridesTrue in test_preset.cpp)
- [x] Preset tracker CLI tests — FIXED (PresetCLI.* in test_cli.cpp)
- [x] Batch unknown preset test — FIXED (RunWithUnknownPresetFails)
- [x] CLI --workers override test — FIXED (WorkersOverride)
- [x] Missing preset file warning test — FIXED (RunWithoutPresetFileContinues)
- [x] Invalid preset piece_size test — FIXED (RunWithInvalidPresetPieceSizeFails)
- [x] TorrentCreator pass-by-value + move — FIXED
- [x] Workers upper bound cap — FIXED (hardware_concurrency * 2)
- [x] URL validation in preset/batch — FIXED
- [x] Silent preset load failure — FIXED with WARNING log
- [x] Missing CLI integration tests — ADDED
- [x] Missing Doxygen comments — ADDED
- [x] parse_yaml_config() forward declaration — MOVED to preset.hpp
- [x] Unnecessary results_mutex — REMOVED
- [x] Silent create_directories error — FIXED with ec check + throw
- [x] Missing batch start log — ADDED
- [x] Missing per-job success log — ADDED
