#ifndef CREATION_PLANNER_HPP
#define CREATION_PLANNER_HPP

#include "output_sink.hpp"
#include "preset.hpp"
#include "torrent_creator.hpp"
#include "tracker_rules.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace fs = std::filesystem;

/**
 * @brief Partial user intent for creating a torrent.
 *
 * Frontends (CLI flags, interactive wizard answers, GUI widgets) fill this
 * struct and hand it to CreationPlanner. The planner owns every default,
 * validation and resolution rule so the frontends never build a TorrentConfig
 * on their own.
 *
 * @note `values.path` and `values.output` are ignored. Input and output always
 *       come from `input`/`output` so a preset cannot redirect a single-file
 *       creation (batch keeps its own output handling).
 */
struct CreationRequest {
    fs::path input;                          ///< Content path (file or directory). Required.
    std::optional<fs::path> output;          ///< Explicit .torrent path; empty triggers auto-naming.
    ConfigValues values;                     ///< User-provided options (request wins over preset).
    std::optional<std::string> preset;       ///< Preset name to apply as the base layer.
    std::optional<fs::path> preset_file;     ///< Explicit preset file path.
    std::optional<fs::path> rules_file;      ///< Explicit tracker rules file path.
    bool use_default_trackers = false;       ///< Prepend Defaults::trackers when true.
    bool skip_prefix = false;                ///< Auto-naming: omit the tracker prefix.
    int tracker_index = 0;                   ///< Auto-naming: tracker used for the filename prefix.
    std::optional<fs::path> output_dir;      ///< Auto-naming: destination directory (created if missing).
    bool fail_on_season_warning = false;     ///< Fail when a TV season pack is incomplete.
    std::optional<int64_t> content_size_hint;///< Frontend-scanned size; avoids rescanning for target/rules.
    bool discover_rules = false;             ///< Search default rules paths when rules_file is absent.
    bool silent = false;                     ///< Pass-through to TorrentConfig (batch).
};

/**
 * @brief Result of resolving a CreationRequest.
 */
struct CreationPlan {
    TorrentConfig config;   ///< Fully resolved and validated configuration.
    bool auto_named = false;///< True when the output path was generated, not user-provided.
};

/**
 * @brief Preloaded resources a caller can share across planning calls.
 *
 * The CLI injects a loaded PresetLoader when `--preset` is used, and the
 * interactive wizard injects an explicit rules database. The GUI passes empty
 * resources. Batch is not migrated to the planner yet.
 *
 * Injected resources take precedence over `preset_file` and `rules_file` in
 * the request.
 */
struct PlannerResources {
    std::shared_ptr<const PresetLoader> presets;          ///< Preloaded presets; when set, `preset_file` is ignored.
    std::shared_ptr<const TrackerRulesDatabase> rules;   ///< Preloaded rules; when set, `rules_file` is ignored.
};

/**
 * @brief Resolve partial creation intent into a validated TorrentConfig.
 *
 * Single source of truth for creation defaults and validation shared by every
 * frontend. The planner performs no prompts and no direct console I/O: console
 * messages go through the injected OutputSink, and every message is also
 * written to the log file.
 */
class CreationPlanner {
  public:
    /**
     * @brief Construct a planner, optionally with preloaded preset/rules.
     * @param resources Shared resources; empty means load from the request paths.
     */
    explicit CreationPlanner(PlannerResources resources = {});

    /**
     * @brief Validate request-level values without touching the filesystem.
     *
     * Runs the checks that do not need presets, rules or content scanning:
     * required input, URLs, piece size, target count, version, name and
     * duplicate trackers. Frontends call it before prompting so an invalid
     * argument fails before any interactive question.
     *
     * @throws std::runtime_error on the first invalid value.
     */
    static void validate(const CreationRequest& request);

    /**
     * @brief Validate request values plus preset-derived conflicts.
     *
     * Frontends that resolve a preset before prompting call this overload so a
     * preset piece size combined with a request target count (or the reverse)
     * fails before any interactive question. Preset values that the resolver
     * would ignore are not treated as conflicts.
     *
     * @param request Partial user intent.
     * @param preset_values Resolved preset values; request values win per field.
     * @throws std::runtime_error on the first invalid value or conflict.
     */
    static void validate(const CreationRequest& request,
                         const ConfigValues& preset_values);

    /**
     * @brief Resolve and validate a creation request.
     *
     * @param request Partial user intent.
     * @param sink Output sink for informational and verbose messages.
     * @return Resolved config plus whether the output path was auto-generated.
     * @throws std::runtime_error on invalid options, missing presets/rules, or
     *         an incomplete season pack when fail_on_season_warning is set.
     * @throws std::filesystem::filesystem_error when the input path is missing.
     */
    CreationPlan plan(const CreationRequest& request,
                      OutputSink& sink = default_stdout_sink()) const;

  private:
    PlannerResources resources_;
};

#endif // CREATION_PLANNER_HPP
