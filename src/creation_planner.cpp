#include "creation_planner.hpp"

#include "constants.hpp"
#include "logger.hpp"
#include "season_pack.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <regex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{

std::string allowed_piece_sizes_message()
{
    std::string message = "Invalid piece size. Must be one of: ";
    for (std::size_t i = 0; i < AllowedPieceSizes::values.size(); ++i)
    {
        message += std::to_string(AllowedPieceSizes::values[i]);
        if (i + 1 < AllowedPieceSizes::values.size())
        {
            message += ", ";
        }
    }
    message += " KB";
    return message;
}

bool is_whitespace_only(const std::string &value)
{
    return value.find_first_not_of(" \t\n\r") == std::string::npos;
}

bool has_effective_piece_size(const ConfigValues &values)
{
    return values.piece_size
           && std::ranges::find(AllowedPieceSizes::values, *values.piece_size)
                  != AllowedPieceSizes::values.end();
}

bool has_effective_target_count(const ConfigValues &values)
{
    return values.target_piece_count && *values.target_piece_count > 0;
}

void validate_piece_selection(const ConfigValues &values)
{
    if (has_effective_piece_size(values) && has_effective_target_count(values))
    {
        throw std::runtime_error(
            "piece size and target piece count are mutually exclusive");
    }
}

TorrentVersion to_version(int version)
{
    switch (version)
    {
        case 1:
            return TorrentVersion::V1;
        case 2:
            return TorrentVersion::V2;
        case 3:
            return TorrentVersion::HYBRID;
        default:
            throw std::runtime_error("Invalid torrent version: " + std::to_string(version)
                                     + " (expected 1, 2 or 3)");
    }
}

std::vector<std::regex> compile_patterns(const std::vector<std::string> &patterns,
                                         const char *kind)
{
    std::vector<std::regex> compiled;
    compiled.reserve(patterns.size());
    for (const auto &pattern : patterns)
    {
        try
        {
            compiled.push_back(utils::glob_to_regex(pattern));
        }
        catch (const std::regex_error &)
        {
            throw std::runtime_error(std::string("Invalid ") + kind + " pattern: " + pattern);
        }
    }
    return compiled;
}

/**
 * @brief Reject invalid values that came directly from a frontend.
 *
 * Runs before the preset merge so a bad CLI/GUI value fails exactly like today,
 * while a bad value coming only from a preset can be handled leniently later.
 */
void validate_request_values(const ConfigValues &values)
{
    if (values.trackers)
    {
        for (const auto &tracker : *values.trackers)
        {
            if (!utils::is_valid_url(tracker))
            {
                throw std::runtime_error("Invalid tracker URL: " + tracker);
            }
        }
    }

    if (values.web_seeds)
    {
        for (const auto &web_seed : *values.web_seeds)
        {
            if (!utils::is_valid_web_seed_url(web_seed))
            {
                throw std::runtime_error("Invalid web seed URL: " + web_seed);
            }
        }
    }

    if (values.piece_size
        && std::ranges::find(AllowedPieceSizes::values, *values.piece_size)
               == AllowedPieceSizes::values.end())
    {
        throw std::runtime_error(allowed_piece_sizes_message());
    }

    if (values.target_piece_count && *values.target_piece_count <= 0)
    {
        throw std::runtime_error("Invalid target piece count: must be a positive integer");
    }

    if (values.piece_size && values.target_piece_count)
    {
        validate_piece_selection(values);
    }

    if (values.torrent_version
        && (*values.torrent_version < 1 || *values.torrent_version > 3))
    {
        throw std::runtime_error("Invalid torrent version: "
                                 + std::to_string(*values.torrent_version)
                                 + " (expected 1, 2 or 3)");
    }

    if (values.name && is_whitespace_only(*values.name))
    {
        throw std::runtime_error("Torrent name cannot be empty or whitespace-only");
    }
}

std::vector<std::string> request_trackers(const CreationRequest &request)
{
    std::vector<std::string> trackers;
    if (request.use_default_trackers)
    {
        trackers = Defaults::trackers;
    }
    if (request.values.trackers)
    {
        trackers.insert(trackers.end(), request.values.trackers->begin(),
                        request.values.trackers->end());
    }
    return trackers;
}

void validate_duplicate_trackers(const std::vector<std::string> &trackers)
{
    for (std::size_t i = 0; i < trackers.size(); ++i)
    {
        if (std::ranges::find(trackers.begin(), trackers.begin() + i, trackers[i])
            != trackers.begin() + i)
        {
            throw std::runtime_error("Duplicate tracker URL: " + trackers[i]);
        }
    }
}


struct OutputResolution
{
    fs::path path;
    bool auto_named = false;
};

struct MetadataResolution
{
    TorrentVersion version = TorrentVersion::V1;
    std::optional<std::string> comment;
    std::optional<std::string> name;
    std::optional<std::string> source;
    std::optional<std::string> creator;
    bool include_creation_date = true;
    std::optional<int> piece_size; // bytes
    std::optional<int> target_piece_count;
    std::vector<std::regex> exclude_regex;
    std::vector<std::regex> include_regex;
    bool is_private = false;
    bool entropy = false;
};

struct RulesResolution
{
    std::optional<int> piece_size; // bytes
    std::optional<int> target_piece_count;
    std::optional<std::string> source;
};

OutputResolution resolve_output(const CreationRequest &request,
                                const std::vector<std::string> &trackers)
{
    OutputResolution resolution;
    if (request.output && !request.output->empty())
    {
        resolution.path = *request.output;
        return resolution;
    }

    resolution.auto_named = true;

    if (request.output_dir && !request.output_dir->empty())
    {
        std::error_code ec;
        if (fs::exists(*request.output_dir, ec))
        {
            if (!fs::is_directory(*request.output_dir))
            {
                throw std::runtime_error("Output directory is not a directory: "
                                         + request.output_dir->string());
            }
        }
        else
        {
            fs::create_directories(*request.output_dir, ec);
            if (!ec)
            {
                log_message("Created output directory: " + request.output_dir->string(),
                            LogLevel::INFO);
            }
            if (ec)
            {
                throw std::runtime_error("Failed to create output directory: "
                                         + request.output_dir->string() + " (" + ec.message()
                                         + ")");
            }
        }
    }

    int tracker_index = request.tracker_index;
    if (!trackers.empty()
        && (tracker_index < 0 || tracker_index >= static_cast<int>(trackers.size())))
    {
        log_message("Tracker index " + std::to_string(tracker_index) + " out of range (0-"
                        + std::to_string(static_cast<int>(trackers.size()) - 1)
                        + "), using first tracker",
                    LogLevel::WARNING);
        tracker_index = 0;
    }

    resolution.path = utils::generate_auto_output_path(request.input, trackers,
                                                       request.skip_prefix, tracker_index,
                                                       request.output_dir.value_or(fs::path{}));
    log_message("Auto-generated output path: " + resolution.path.string(), LogLevel::INFO);
    return resolution;
}

MetadataResolution resolve_metadata(const CreationRequest &request, const ConfigValues &merged)
{
    MetadataResolution metadata;
    metadata.version = to_version(merged.torrent_version.value_or(3));
    metadata.comment = merged.comment;
    metadata.name = merged.name;

    metadata.source = merged.source;
    // An explicit empty --source clears a preset-provided source.
    if (request.values.source && request.values.source->empty())
    {
        metadata.source = std::nullopt;
    }

    metadata.creator = std::string(Defaults::creator);
    if (merged.no_creator && *merged.no_creator)
    {
        metadata.creator = std::nullopt;
    }
    else if (merged.creator)
    {
        metadata.creator = merged.creator;
    }

    if (merged.no_date && *merged.no_date)
    {
        metadata.include_creation_date = false;
    }
    else if (merged.creation_date)
    {
        metadata.include_creation_date = *merged.creation_date;
    }

    // Piece size arrives in KB and is stored in bytes. Request-level values were
    // validated above; an invalid value coming only from a preset is ignored.
    if (merged.piece_size)
    {
        if (std::ranges::find(AllowedPieceSizes::values, *merged.piece_size)
            != AllowedPieceSizes::values.end())
        {
            metadata.piece_size = *merged.piece_size * 1024;
        }
        else
        {
            log_message("Preset piece_size " + std::to_string(*merged.piece_size)
                            + " KB is not in the allowed list, ignoring it",
                        LogLevel::WARNING);
        }
    }

    if (merged.target_piece_count)
    {
        if (*merged.target_piece_count > 0)
        {
            metadata.target_piece_count = merged.target_piece_count;
        }
        else
        {
            log_message("Preset target_piece_count must be positive, ignoring it",
                        LogLevel::WARNING);
        }
    }

    if (metadata.piece_size && metadata.target_piece_count)
    {
        throw std::runtime_error("piece size and target piece count are mutually exclusive");
    }

    const bool use_builtin_excludes = merged.builtin_excludes.value_or(true);
    metadata.exclude_regex = compile_patterns(
        merged.exclude_patterns.value_or(std::vector<std::string>{}), "exclude");
    metadata.include_regex = compile_patterns(
        merged.include_patterns.value_or(std::vector<std::string>{}), "include");
    metadata.exclude_regex = utils::apply_builtin_excludes(metadata.exclude_regex,
                                                           use_builtin_excludes);

    metadata.is_private = merged.is_private.value_or(false);
    metadata.entropy = merged.entropy.value_or(false);
    return metadata;
}

RulesResolution apply_rules(const CreationRequest &request, const PlannerResources &resources,
                            const std::vector<std::string> &trackers,
                            std::optional<int> piece_size,
                            std::optional<int> target_piece_count,
                            std::optional<std::string> source, OutputSink &sink)
{
    // Tracker rules: injected instances are shared read-only; otherwise try the
    // default search path and only fail when the user named a file explicitly.
    std::unique_ptr<TrackerRulesDatabase> owned_rules;
    const TrackerRulesDatabase *rules_db = resources.rules.get();
    if (!rules_db && (request.rules_file || request.discover_rules))
    {
        std::optional<fs::path> rules_path;
        try
        {
            rules_path = TrackerRulesDatabase::find_rules_file(request.rules_file);
        }
        catch (const std::runtime_error &)
        {
            if (request.rules_file)
            {
                throw;
            }
            log_message("No tracker rules file found, skipping rules enforcement",
                        LogLevel::INFO);
        }

        if (rules_path)
        {
            owned_rules = std::make_unique<TrackerRulesDatabase>();
            try
            {
                owned_rules->load(*rules_path);
                rules_db = owned_rules.get();
            }
            catch (const std::runtime_error &e)
            {
                if (request.rules_file)
                {
                    throw;
                }
                // A discovered file exists but is invalid: report it instead of
                // pretending no rules file was found.
                log_message("Failed to load tracker rules from " + rules_path->string()
                                + ": " + e.what() + " (skipping)",
                            LogLevel::WARNING);
            }
        }
    }

    const bool need_content_size = (target_piece_count && !piece_size)
                                   || (rules_db != nullptr && !trackers.empty());
    bool piece_size_from_target = false;
    int64_t content_size = 0;
    if (request.content_size_hint)
    {
        // A frontend already walked the tree (the wizard's target preview), so
        // reuse that result instead of scanning the content a second time.
        log_message("Using precomputed content size: "
                        + std::to_string(*request.content_size_hint) + " bytes",
                    LogLevel::INFO);
        content_size = *request.content_size_hint;
    }
    else if (need_content_size)
    {
        content_size = utils::compute_content_size(request.input);
    }

    if (target_piece_count && !piece_size)
    {
        if (content_size > 0)
        {
            const int resolved = utils::piece_size_for_target_count(content_size,
                                                                    *target_piece_count);
            piece_size = resolved;
            piece_size_from_target = true;
            const int64_t resulting_pieces = (content_size + resolved - 1) / resolved;
            sink.verbose("Target: " + std::to_string(*target_piece_count) + " pieces -> "
                         + std::to_string(resolved / 1024) + " KB piece size"
                         + " (yields ~" + std::to_string(resulting_pieces) + " pieces)\n");
            log_message("Target piece count " + std::to_string(*target_piece_count)
                            + " resolved to " + std::to_string(resolved / 1024) + " KB ("
                            + std::to_string(resulting_pieces) + " pieces)",
                        LogLevel::INFO);
        }
        else
        {
            sink.info("WARNING: target_piece_count cannot be resolved (content is empty); "
                      "falling back to automatic piece size\n");
            log_message("target_piece_count " + std::to_string(*target_piece_count)
                            + " ignored: content size is 0",
                        LogLevel::WARNING);
            target_piece_count = std::nullopt;
        }
    }

    if (rules_db && !trackers.empty())
    {
        const auto matched_rule = rules_db->find_matching_rule(trackers);
        if (matched_rule)
        {
            if (matched_rule->source && !source)
            {
                source = *matched_rule->source;
                log_message("Tracker rule '" + matched_rule->name + "': auto-set source to '"
                                + *source + "'",
                            LogLevel::INFO);
            }

            if (matched_rule->max_piece_length || matched_rule->max_torrent_size
                || !matched_rule->piece_length_overrides.empty())
            {
                std::optional<int> current_kb;
                if (piece_size)
                {
                    current_kb = *piece_size / 1024;
                }

                const auto enforcement =
                    rules_db->enforce(*matched_rule, content_size, current_kb);

                if (enforcement.adjusted && enforcement.adjusted_piece_length)
                {
                    if (current_kb && !piece_size_from_target)
                    {
                        // User-specified sizes are never overridden, only reported.
                        log_message("Tracker rule '" + matched_rule->name
                                        + "': keeping user-specified piece size ("
                                        + std::to_string(*current_kb) + " KB); recommended "
                                        + std::to_string(*enforcement.adjusted_piece_length)
                                        + " KB. The torrent may violate the rule.",
                                    LogLevel::WARNING);
                    }
                    else
                    {
                        piece_size = *enforcement.adjusted_piece_length * 1024;
                        sink.verbose("Tracker rule '" + matched_rule->name
                                     + "': piece size adjusted to "
                                     + std::to_string(*enforcement.adjusted_piece_length) + " KB\n");
                    }
                }

                if (enforcement.constraint_violation)
                {
                    sink.info("WARNING: " + enforcement.violation_message + "\n");
                    log_message(enforcement.violation_message, LogLevel::WARNING);
                }
            }
        }
        else
        {
            log_message("No matching tracker rule found for provided trackers", LogLevel::INFO);
        }
    }

    return RulesResolution{std::move(piece_size), std::move(target_piece_count),
                           std::move(source)};
}

} // namespace


CreationPlanner::CreationPlanner(PlannerResources resources)
    : resources_(std::move(resources))
{
}

void CreationPlanner::validate(const CreationRequest &request)
{
    if (request.input.empty())
    {
        throw std::runtime_error("Path is required");
    }

    validate_request_values(request.values);

    if (request.values.trackers.has_value() || request.use_default_trackers)
    {
        // Tracker lists are small; an O(n^2) duplicate check keeps the error
        // identical to the one the resolver used to emit before prompting.
        validate_duplicate_trackers(request_trackers(request));
    }
}

void CreationPlanner::validate(const CreationRequest &request,
                               const ConfigValues &preset_values)
{
    validate(request);
    validate_piece_selection(merge_config_values(preset_values, request.values));
}

CreationPlan CreationPlanner::plan(const CreationRequest &request, OutputSink &sink) const
{
    validate(request);

    // Preset is the base layer; request values win per field.
    ConfigValues preset_values;
    if (request.preset)
    {
        if (resources_.presets)
        {
            preset_values = resources_.presets->resolve(*request.preset);
        }
        else
        {
            PresetLoader loader;
            loader.load(PresetLoader::find_preset_file(request.preset_file));
            preset_values = loader.resolve(*request.preset);
        }
        log_message("Applied preset values for: " + *request.preset, LogLevel::INFO);
    }

    const ConfigValues merged = merge_config_values(preset_values, request.values);

    // Trackers: any request-level tracker intent discards the preset list, which
    // is what the CLI has always done. Without request intent the preset list is
    // used as-is (it was URL-validated when the preset file was parsed).
    const bool request_tracker_intent =
        request.values.trackers.has_value() || request.use_default_trackers;
    std::vector<std::string> trackers;
    if (request_tracker_intent)
    {
        // Validated by validate() before any I/O.
        trackers = request_trackers(request);
    }
    else if (merged.trackers)
    {
        trackers = *merged.trackers;
    }

    const std::vector<std::string> web_seeds = merged.web_seeds.value_or(std::vector<std::string>{});

    const OutputResolution output = resolve_output(request, trackers);
    MetadataResolution metadata = resolve_metadata(request, merged);
    RulesResolution rules = apply_rules(request, resources_, trackers,
                                         std::move(metadata.piece_size),
                                         std::move(metadata.target_piece_count),
                                         std::move(metadata.source), sink);

    if (const auto season_error = season_pack::evaluate_season_warning(
            request.input, request.fail_on_season_warning))
    {
        throw std::runtime_error(*season_error);
    }

    TorrentConfig config(request.input, output.path, std::move(trackers), metadata.version,
                         std::move(metadata.comment), metadata.is_private, web_seeds,
                         rules.piece_size, std::move(metadata.creator), std::move(metadata.name),
                         metadata.include_creation_date, std::move(rules.source), metadata.entropy,
                         std::move(metadata.exclude_regex), std::move(metadata.include_regex),
                         request.silent, rules.target_piece_count);

    return CreationPlan{std::move(config), output.auto_named};
}
