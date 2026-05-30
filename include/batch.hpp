#ifndef BATCH_HPP
#define BATCH_HPP

#include "preset.hpp"
#include "tracker_rules.hpp"
#include "torrent_creator.hpp"
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;

/** @brief A single job within a batch configuration. */
struct BatchJob {
    std::string path;                      ///< Input file or directory
    std::optional<std::string> output;     ///< Output .torrent path (auto-generated if empty)
    std::optional<std::string> preset;     ///< Preset name to apply
    bool fail_on_season_warning = false;   ///< Fail if TV season pack has missing episodes
    ConfigValues values;                   ///< Per-job config overrides
};

/** @brief Parsed batch configuration from a YAML file. */
struct BatchConfig {
    int workers = 1;                       ///< Number of parallel workers (default: 1)
    std::optional<fs::path> preset_file;   ///< Shared preset file for all jobs
    std::optional<fs::path> rules_file;    ///< Shared tracker rules file for all jobs
    std::optional<fs::path> output_dir;    ///< Default output directory
    std::vector<BatchJob> jobs;            ///< Ordered list of jobs to execute
};

/** @brief Result of executing a single batch job. */
struct BatchResult {
    int job_index;                         ///< Index into the jobs vector
    std::string job_name;                  ///< Display name (output path or input path)
    bool success;                          ///< Whether the torrent was created successfully
    std::string error_message;             ///< Error details if success is false
    double elapsed_seconds;                ///< Wall-clock time for this job
};

/** @brief Parses batch YAML files and executes jobs in parallel.
 *
 * Uses a worker-pool pattern with std::thread. Each worker pulls the next
 * available job index via an atomic counter, executes it, and stores the result.
 *
 * Thread oversubscription note: each worker internally spawns hashing threads
 * via libtorrent. Total threads ≈ workers × hashing_threads. Consider capping
 * workers to available cores for CPU-bound workloads.
 */
class BatchProcessor {
public:
    /** @brief Construct a processor with the given batch configuration. */
    explicit BatchProcessor(BatchConfig config);

    /** @brief Parse a batch YAML file into a BatchConfig.
     * @throws std::runtime_error on missing file, bad version, or parse errors.
     */
    static BatchConfig parse(const fs::path& yaml_path);

    /** @brief Execute all jobs and return results in order. */
    std::vector<BatchResult> run();

    /** @brief Print a human-readable summary to stdout. */
    static void print_summary(const std::vector<BatchResult>& results);

private:
    BatchConfig config_;

    BatchResult execute_job(int job_index, const PresetLoader& presets, const TrackerRulesDatabase& rules);
};

#endif
