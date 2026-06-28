#include "torrent_creator.hpp"
#include "logger.hpp"
#include "constants.hpp"
#include "utils.hpp"
#include "terminal.hpp"
#include "version.hpp"
#include "preset.hpp"
#include "batch.hpp"
#include "tracker_rules.hpp"
#include <iostream>
#include <vector>
#include <optional>
#include <cxxopts.hpp>
#include <filesystem>
#include <cmath>
#include <random>
#include <chrono>
#include <ranges>
#include <stdexcept>
#include <cstdlib>
#include <yaml-cpp/exceptions.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include "torrent_inspector.hpp"
#include "torrent_modifier.hpp"
#include "torrent_checker.hpp"
#include "season_pack.hpp"
#include "output.hpp"
#include "updater.hpp"

namespace fs = std::filesystem;

// Public open trackers from the NewTrackon list. Provide redundancy for peer discovery when no private tracker is configured.
const std::vector<std::string> default_trackers = {
    "udp://open.stealth.si:80/announce",         "udp://tracker.opentrackr.org:1337/announce",
    "udp://tracker.torrent.eu.org:451/announce", "udp://explodie.org:6969/announce",
    "udp://tracker.cyberia.is:6969/announce",    "udp://retracker.hotplug.ru:2710/announce"};

// List of allowed piece sizes (in KB). These are powers of 2
const std::vector<int> allowed_piece_sizes(AllowedPieceSizes::values.begin(),
                                           AllowedPieceSizes::values.end());

namespace
{
enum class OverwriteDecision
{
    Proceed,
    Declined
};

// Prompts the user with a y/N question. Returns true for 'y'/'Y', false for 'n'/'N'/empty.
// Loops on invalid input until a valid response is given.
bool prompt_yes_no(const std::string &prompt_text)
{
    while (true)
    {
        std::string input;
        std::cout << prompt_text << " (y/N): ";
        std::getline(std::cin, input);
        if (input == "y" || input == "Y")
        {
            return true;
        }
        else if (input == "n" || input == "N" || input.empty())
        {
            return false;
        }
        std::cout << "Error: Invalid input. Please enter 'y' or 'n'.\n";
    }
}

// Prompts the user for an optional string. Returns std::nullopt if input is empty.
std::optional<std::string> prompt_optional_string(const std::string &prompt_text)
{
    std::string input;
    std::cout << prompt_text;
    std::getline(std::cin, input);
    if (input.empty())
    {
        return std::nullopt;
    }
    return input;
}

OverwriteDecision prompt_overwrite(const std::string &filepath)
{
    if (get_verbosity() == Verbosity::QUIET || is_json_mode())
    {
        return OverwriteDecision::Declined;
    }
    while (true)
    {
        std::string overwrite;
        std::cout << "File " << filepath << " already exists. Overwrite? (y/N): ";
        std::getline(std::cin, overwrite);
        if (overwrite == "y" || overwrite == "Y")
        {
            return OverwriteDecision::Proceed;
        }
        else if (overwrite == "n" || overwrite == "N" || overwrite.empty())
        {
            return OverwriteDecision::Declined;
        }
        std::cout << "Error: Invalid input. Please enter 'y' or 'n'.\n";
    }
}
} // namespace

// Get torrent configuration from user input (interactive mode)
TorrentConfig get_interactive_config()
{
    print_info("=== TORRENT CONFIGURATION ===\n");

    // Get input path
    std::string path;
    while (true)
    {
        print_info("Path to file or directory: ");
        std::getline(std::cin, path);
        path = utils::sanitize_path(path);
        if (path.empty())
        {
            print_info("Error: Input path cannot be empty\n");
            continue;
        }
        try
        {
            // Check if the path exists
            if (!fs::exists(path))
            {
                print_info("Error: The specified path does not exist. Please check the path and "
                             "try again.\n");
                continue;
            }
            // Check if we have read permissions
            fs::file_status status = fs::status(path);
            if ((status.permissions() & fs::perms::owner_read) == fs::perms::none)
            {
                print_info("Error: No read permissions for path\n");
                continue;
            }
            break;
        }
        catch (const fs::filesystem_error &e)
        {
            print_info(std::string("Filesystem error: ") + e.what() + "\n");
            continue;
        }
    }

    // Get output path
    std::string output;
    while (true)
    {
        print_info("Path to save torrent: ");
        std::getline(std::cin, output);
        output = utils::sanitize_path(output);
        if (output.empty())
        {
            print_info("Error: Output path cannot be empty\n");
            continue;
        }
        // 8 is the minimum length that can carry ".torrent" suffix
        if (output.size() < 8 || output.substr(output.size() - 8) != ".torrent")
        {
            print_info("Error: Output path must end with '.torrent'\n");
            continue;
        }

        // Check if file exists and prompt for overwrite
        if (fs::exists(output))
        {
            if (prompt_overwrite(output) == OverwriteDecision::Declined)
            {
                goto get_version;
            }
        }

        // Check if parent directory exists and is writable
        try
        {
            fs::path parent_dir = fs::path(output).parent_path();
            if (!parent_dir.empty() && !fs::exists(parent_dir))
            {
                print_info("Error: Parent directory does not exist\n");
                continue;
            }
            // Check write permissions
            fs::file_status status = fs::status(parent_dir);
            if ((status.permissions() & fs::perms::owner_write) == fs::perms::none)
            {
                print_info("Error: No write permissions for directory\n");
                continue;
            }
            break;
        }
        catch (const fs::filesystem_error &e)
        {
            print_info(std::string("Filesystem error: ") + e.what() + "\n");
            continue;
        }
    }

get_version: // Label to jump to if overwrite is 'n' or empty
    // Get torrent version
    std::string version;
    TorrentVersion tv = TorrentVersion::V1; // Initialize tv here
    while (true)
    {
        print_info("Torrent version (1-v1, 2-v2, 3-Hybrid) [3]: ");
        std::getline(std::cin, version);
        if (version.empty() || version == "3")
        {
            tv = TorrentVersion::HYBRID;
            break;
        }
        else if (version == "1")
        {
            tv = TorrentVersion::V1;
            break;
        }
        else if (version == "2")
        {
            tv = TorrentVersion::V2;
            break;
        }
        else
        {
            print_info("Error: Invalid input. Please enter '1', '2', or '3'.\n");
        }
    }

    // Get optional comment
    std::string comment;
    print_info("Comment (optional): ");
    std::getline(std::cin, comment);

    // Get private flag
    bool is_private = prompt_yes_no("Private torrent?");

    // Get trackers
    std::vector<std::string> trackers;
    if (prompt_yes_no("Use default trackers?"))
    {
        trackers.insert(trackers.end(), default_trackers.begin(), default_trackers.end());
    }

    if (prompt_yes_no("Add custom trackers?"))
    {
        while (true)
        {
            std::string tracker;
            print_info("Add tracker (leave blank to finish): ");
            std::getline(std::cin, tracker);
            if (tracker.empty())
                break;

            if (!utils::is_valid_url(tracker))
            {
                print_info("Error: Invalid tracker URL. Must start with http://, https://, "
                             "or udp://\n");
                continue;
            }

            if (std::ranges::contains(trackers, tracker))
            {
                print_info("Error: Tracker already added.\n");
                continue;
            }

            trackers.push_back(tracker);
        }
    }

    // Get web seeds
    std::vector<std::string> web_seeds;
    while (true)
    {
        std::string seed;
        print_info("Add web seed (leave blank to finish): ");
        std::getline(std::cin, seed);
        if (seed.empty())
            break;

        if (!utils::is_valid_url(seed))
        {
            print_info("Error: Invalid web seed URL. Must start with http://, https://, or udp://\n");
            continue;
        }

        web_seeds.push_back(seed);
    }

    // Get piece size
    std::optional<int> piece_size = std::nullopt;
    if (prompt_yes_no("Set custom piece size?"))
    {
        while (true)
        {
            std::string piece_size_str;
            print_info("Piece size in KB: \n");

            std::string valid_options = "Valid options: ";
            for (size_t i = 0; i < allowed_piece_sizes.size(); ++i)
            {
                valid_options += std::to_string(allowed_piece_sizes[i]);
                if (i < allowed_piece_sizes.size() - 1)
                {
                    valid_options += ", ";
                }
            }
            valid_options += "\n";
            print_info(valid_options);

            std::getline(std::cin, piece_size_str);
            if (piece_size_str.empty())
            {
                break;
            }

            try
            {
                int ps = std::stoi(piece_size_str);
                if (std::ranges::contains(allowed_piece_sizes, ps))
                {
                    piece_size = ps * 1024;
                    break;
                }
                else
                {
                    print_info("Error: Invalid piece size. Please enter a valid option.\n");
                }
            }
            catch (const std::invalid_argument &e)
            {
                print_info("Invalid input. Please enter a valid integer.\n");
                continue;
            }
            catch (const std::out_of_range &e)
            {
                print_info("Input out of range. Please enter a valid integer.\n");
                continue;
            }
        }
    }

    // Get target piece count (only if custom piece size was not set)
    std::optional<int> target_piece_count = std::nullopt;
    if (!piece_size && prompt_yes_no("Set target piece count?"))
    {
        while (true)
        {
            std::string input;
            print_info("Target number of pieces: ");
            std::getline(std::cin, input);
            if (input.empty())
            {
                break;
            }

            try
            {
                int tpc = std::stoi(input);
                if (tpc > 0)
                {
                    int64_t total_size = utils::compute_content_size(path);

                    if (total_size > 0) {
                        int resolved = utils::piece_size_for_target_count(total_size, tpc);
                        int64_t resulting_pieces = (total_size + resolved - 1) / resolved;
                        piece_size = resolved;
                        target_piece_count = tpc;
                        print_info("Target: " + std::to_string(tpc) + " pieces -> "
                            + std::to_string(resolved / 1024) + " KB piece size"
                            + " (yields ~" + std::to_string(resulting_pieces) + " pieces)\n");
                    } else {
                        print_info("WARNING: Content is empty — cannot compute piece size from target. Using automatic piece size.\n");
                    }
                    break;
                }
                else
                {
                    print_info("Error: Target piece count must be positive.\n");
                }
            }
            catch (const std::invalid_argument &)
            {
                print_info("Invalid input. Please enter a valid integer.\n");
                continue;
            }
            catch (const std::out_of_range &)
            {
                print_info("Input out of range. Please enter a valid integer.\n");
                continue;
            }
        }
    }

    // Get creator
    std::optional<std::string> creator_str = std::nullopt;
    if (prompt_yes_no("Set \"Torrent Builder\" as creator?"))
    {
        creator_str = "Torrent Builder";
    }

    // Get optional custom torrent name
    std::optional<std::string> torrent_name = std::nullopt;
    while (true) {
        auto input_name = prompt_optional_string("Custom torrent name (leave blank for default): ");
        if (!input_name.has_value()) {
            break;
        }
        if (input_name->find_first_not_of(" \t\n\r") == std::string::npos) {
            print_info("Error: Name cannot be whitespace-only\n");
            continue;
        }
        torrent_name = input_name;
        break;
    }

    // Get creation date
    bool include_creation_date = prompt_yes_no("Set creation date?");

    // Get optional source string
    std::optional<std::string> source = prompt_optional_string("Source string for cross-seeding (leave blank to skip): ");

    // Get entropy flag
    bool entropy = prompt_yes_no("Add random entropy for unique info hash?");

    // Compile glob patterns to regex once at config time. glob_to_regex() escapes
    // all metacharacters so regex_error is unreachable — the catch is defense-in-depth.
    std::vector<std::regex> exclude_regex_compiled;
    if (prompt_yes_no("Exclude files by pattern?"))
    {
        while (true)
        {
            std::string pattern;
            print_info("Exclude pattern (glob, leave blank to finish): ");
            std::getline(std::cin, pattern);
            if (pattern.empty())
                break;
            try
            {
                exclude_regex_compiled.push_back(utils::glob_to_regex(pattern));
            }
            catch (const std::regex_error &)
            {
                print_info(std::string("Error: Invalid glob pattern: ") + pattern + "\n");
                log_message("Invalid glob pattern entered: " + pattern, LogLevel::WARNING);
            }
        }
    }

    std::vector<std::regex> include_regex_compiled;
    if (prompt_yes_no("Include only matching files?"))
    {
        while (true)
        {
            std::string pattern;
            print_info("Include pattern (glob, leave blank to finish): ");
            std::getline(std::cin, pattern);
            if (pattern.empty())
                break;
            try
            {
                include_regex_compiled.push_back(utils::glob_to_regex(pattern));
            }
            catch (const std::regex_error &)
            {
                print_info(std::string("Error: Invalid glob pattern: ") + pattern + "\n");
                log_message("Invalid glob pattern entered: " + pattern, LogLevel::WARNING);
            }
        }
    }

    return TorrentConfig(path, output, trackers, tv,
                         comment.empty() ? std::nullopt : std::optional<std::string>(comment),
                         is_private, web_seeds, piece_size, creator_str, torrent_name,
                         include_creation_date, source, entropy,
                         std::move(exclude_regex_compiled), std::move(include_regex_compiled),
                         false, target_piece_count
    );
}

// Parse command-line arguments into a TorrentConfig.
// Returns std::nullopt if the user declines to overwrite an existing output file.
std::optional<TorrentConfig> get_commandline_config(const cxxopts::ParseResult &result, std::string &declined_path)
{
    if (!result.count("path"))
    {
        throw std::runtime_error("Path is required");
    }

    std::string input_path = result["path"].as<std::string>();

    ConfigValues preset_values;
    if (result.count("preset"))
    {
        std::optional<fs::path> preset_file;
        if (result.count("preset-file")) {
            preset_file = result["preset-file"].as<std::string>();
        }
        PresetLoader loader;
        auto path = PresetLoader::find_preset_file(preset_file);
        loader.load(path);
        preset_values = loader.resolve(result["preset"].as<std::string>());
        log_message("Applied preset values for: " + result["preset"].as<std::string>(), LogLevel::INFO);
    }

    // Get trackers (before output, needed for auto-naming)
    std::vector<std::string> trackers;
    if (!result.count("tracker") && !result.count("default-trackers") && preset_values.trackers) {
        trackers = *preset_values.trackers;
    } else {
        bool use_default = result.count("default-trackers") > 0;
        if (use_default)
        {
            trackers.insert(trackers.end(), default_trackers.begin(), default_trackers.end());
        }
        if (result.count("tracker"))
        {
            std::vector<std::string> custom_trackers = result["tracker"].as<std::vector<std::string>>();
            for (const auto &tracker : custom_trackers)
            {
                if (!utils::is_valid_url(tracker))
                {
                    throw std::runtime_error("Invalid tracker URL: " + tracker);
                }
                if (std::ranges::contains(trackers, tracker))
                {
                    throw std::runtime_error("Duplicate tracker URL: " + tracker);
                }
            }
            trackers.insert(trackers.end(), custom_trackers.begin(), custom_trackers.end());
        }
    }

    // Resolve output path (optional — auto-generate if not provided)
    std::string output_path;
    bool auto_named = false;
    if (result.count("output"))
    {
        output_path = result["output"].as<std::string>();
    }
    else
    {
        auto_named = true;
        bool skip_prefix = result.count("skip-prefix") > 0;
        int tracker_index = result["tracker-index"].as<int>();
        fs::path output_dir;
        if (result.count("output-dir"))
        {
            output_dir = result["output-dir"].as<std::string>();
            if (!output_dir.empty())
            {
                std::error_code ec;
                if (fs::exists(output_dir, ec))
                {
                    if (!fs::is_directory(output_dir))
                        throw std::runtime_error("Output directory is not a directory: " + output_dir.string());
                }
                else
                {
                    fs::create_directories(output_dir, ec);
                    if (!ec)
                        log_message("Created output directory: " + output_dir.string(), LogLevel::INFO);
                    if (ec)
                        throw std::runtime_error("Failed to create output directory: " + output_dir.string()
                                                 + " (" + ec.message() + ")");
                }
            }
        }

        if (!trackers.empty() && (tracker_index < 0 || tracker_index >= static_cast<int>(trackers.size())))
        {
            log_message("Tracker index " + std::to_string(tracker_index)
                        + " out of range (0-" + std::to_string(static_cast<int>(trackers.size()) - 1)
                        + "), using first tracker", LogLevel::WARNING);
            tracker_index = 0;
        }

        output_path = utils::generate_auto_output_path(input_path, trackers, skip_prefix,
                                                        tracker_index, output_dir);
        log_message("Auto-generated output path: " + output_path, LogLevel::INFO);
    }

    if (!auto_named && fs::exists(output_path))
    {
        if (prompt_overwrite(output_path) == OverwriteDecision::Declined)
        {
            declined_path = output_path;
            return std::nullopt;
        }
    }

    // Get torrent version
    std::string version_str = "3";
    if (result.count("torrent-version")) {
        version_str = result["torrent-version"].as<std::string>();
    } else if (preset_values.torrent_version) {
        version_str = std::to_string(*preset_values.torrent_version);
    }
    TorrentVersion tv = TorrentVersion::V1;
    if (version_str == "2")
        tv = TorrentVersion::V2;
    else if (version_str == "3")
        tv = TorrentVersion::HYBRID;

    // Get optional comment
    std::optional<std::string> comment = std::nullopt;
    if (result.count("comment"))
    {
        comment = result["comment"].as<std::string>();
    }
    else if (preset_values.comment)
    {
        comment = preset_values.comment;
    }

    // Get optional torrent name
    std::optional<std::string> torrent_name = std::nullopt;
    if (result.count("name")) {
        torrent_name = result["name"].as<std::string>();
        if (torrent_name->find_first_not_of(" \t\n\r") == std::string::npos) {
            throw std::runtime_error("Torrent name cannot be empty or whitespace-only");
        }
    }
    else if (preset_values.name)
    {
        torrent_name = preset_values.name;
    }

    // Get web seeds
    std::vector<std::string> web_seeds;
    if (result.count("webseed"))
    {
        web_seeds = result["webseed"].as<std::vector<std::string>>();
        for (const auto &webseed : web_seeds)
        {
            if (!utils::is_valid_url(webseed))
            {
                throw std::runtime_error("Invalid web seed URL: " + webseed);
            }
        }
    }
    else if (preset_values.web_seeds)
    {
        web_seeds = *preset_values.web_seeds;
    }

    // Get and validate piece size
    std::optional<int> piece_size = std::nullopt;
    if (result.count("piece-size"))
    {
        int ps = result["piece-size"].as<int>();
        // Validate using std::ranges::contains
        if (std::ranges::contains(allowed_piece_sizes, ps))
        {
            piece_size = ps * 1024; // Store in bytes
        }
        else
        {
            std::string piece_sizes_str = "Error: Invalid piece size. Must be one of: ";
            for (size_t i = 0; i < allowed_piece_sizes.size(); ++i)
            {
                piece_sizes_str += std::to_string(allowed_piece_sizes[i]);
                if (i < allowed_piece_sizes.size() - 1)
                {
                    piece_sizes_str += ", ";
                }
            }
            piece_sizes_str += " KB\n";
            print_error(piece_sizes_str);
            throw std::runtime_error("Invalid piece size");
        }
    }
    else if (preset_values.piece_size && *preset_values.piece_size > 0)
    {
        int ps = *preset_values.piece_size;
        if (std::ranges::contains(allowed_piece_sizes, ps))
        {
            piece_size = ps * 1024;
        }
    }

    // Get and validate target piece count
    std::optional<int> target_piece_count = std::nullopt;
    if (result.count("target-piece-count"))
    {
        target_piece_count = result["target-piece-count"].as<int>();
        if (*target_piece_count <= 0)
        {
            print_error("Error: --target-piece-count must be a positive integer\n");
            throw std::runtime_error("Invalid target piece count");
        }
    }
    else if (preset_values.target_piece_count && *preset_values.target_piece_count > 0)
    {
        target_piece_count = preset_values.target_piece_count;
    }

    // Mutual exclusivity: --piece-size and --target-piece-count
    if (piece_size && target_piece_count)
    {
        print_error("Error: --piece-size and --target-piece-count are mutually exclusive\n");
        throw std::runtime_error("Conflicting piece size options");
    }

    // Get creator string
    std::optional<std::string> creator_str = std::nullopt;
    if (result.count("creator"))
    {
        creator_str = "Torrent Builder";
    }
    else if (preset_values.creator)
    {
        creator_str = preset_values.creator;
    }

    // Get creation date flag
    bool include_creation_date = result.count("creation-date") > 0;
    if (!include_creation_date && preset_values.creation_date) {
        include_creation_date = *preset_values.creation_date;
    }

    // Get optional source string
    std::optional<std::string> source = std::nullopt;
    if (result.count("source"))
    {
        std::string source_str = result["source"].as<std::string>();
        if (!source_str.empty())
        {
            source = source_str;
        }
    }
    else if (preset_values.source)
    {
        source = preset_values.source;
    }

    // Get entropy flag
    bool entropy = result.count("entropy") > 0;
    if (!entropy && preset_values.entropy) {
        entropy = *preset_values.entropy;
    }

    // Compile glob patterns to regex once at config time. glob_to_regex() escapes
    // all metacharacters so regex_error is unreachable — the catch is defense-in-depth.
    std::vector<std::regex> exclude_regex_compiled;
    if (result.count("exclude"))
    {
        auto patterns = result["exclude"].as<std::vector<std::string>>();
        for (const auto &p : patterns)
        {
            try { exclude_regex_compiled.push_back(utils::glob_to_regex(p)); }
            catch (const std::regex_error &) { throw std::runtime_error("Invalid exclude pattern: " + p); }
        }
    }
    else if (preset_values.exclude_patterns)
    {
        for (const auto &p : *preset_values.exclude_patterns)
        {
            try { exclude_regex_compiled.push_back(utils::glob_to_regex(p)); }
            catch (const std::regex_error &) { throw std::runtime_error("Invalid exclude pattern: " + p); }
        }
    }

    std::vector<std::regex> include_regex_compiled;
    if (result.count("include"))
    {
        auto patterns = result["include"].as<std::vector<std::string>>();
        for (const auto &p : patterns)
        {
            try { include_regex_compiled.push_back(utils::glob_to_regex(p)); }
            catch (const std::regex_error &) { throw std::runtime_error("Invalid include pattern: " + p); }
        }
    }
    else if (preset_values.include_patterns)
    {
        for (const auto &p : *preset_values.include_patterns)
        {
            try { include_regex_compiled.push_back(utils::glob_to_regex(p)); }
            catch (const std::regex_error &) { throw std::runtime_error("Invalid include pattern: " + p); }
        }
    }

    try
    {
        bool is_private = result.count("private") > 0;
        if (!is_private && preset_values.is_private) {
            is_private = *preset_values.is_private;
        }

        TrackerRulesDatabase rules_db;
        std::optional<fs::path> rules_file;
        if (result.count("rules-file")) {
            rules_file = result["rules-file"].as<std::string>();
        }

        bool rules_loaded = false;
        try {
            auto rules_path = TrackerRulesDatabase::find_rules_file(rules_file);
            rules_db.load(rules_path);
            rules_loaded = true;
        } catch (const std::runtime_error& e) {
            if (rules_file) {
                throw;
            }
            log_message("No tracker rules file found, skipping rules enforcement", LogLevel::INFO);
        }

        // Compute content size once for target resolution and/or tracker rule enforcement
        bool need_content_size = (target_piece_count && !piece_size)
            || (rules_loaded && !trackers.empty());
        int64_t content_size = need_content_size
            ? utils::compute_content_size(input_path) : 0;

        // Resolve target_piece_count → piece_size before tracker rule enforcement
        // so that rules can naturally cap/adjust the resolved piece size.
        if (target_piece_count && !piece_size)
        {
            if (content_size > 0) {
                int resolved = utils::piece_size_for_target_count(content_size, *target_piece_count);
                piece_size = resolved;
                int64_t resulting_pieces = (content_size + resolved - 1) / resolved;
                print_verbose("Target: " + std::to_string(*target_piece_count)
                    + " pieces -> " + std::to_string(resolved / 1024) + " KB piece size"
                    + " (yields ~" + std::to_string(resulting_pieces) + " pieces)\n");
                log_message("Target piece count " + std::to_string(*target_piece_count)
                    + " resolved to " + std::to_string(resolved / 1024) + " KB ("
                    + std::to_string(resulting_pieces) + " pieces)", LogLevel::INFO);
            } else {
                print_info("WARNING: target_piece_count cannot be resolved (content is empty); falling back to automatic piece size\n");
                log_message("target_piece_count " + std::to_string(*target_piece_count)
                    + " ignored: content size is 0", LogLevel::WARNING);
                target_piece_count = std::nullopt;
            }
        }

        if (rules_loaded && !trackers.empty()) {
            auto matched_rule = rules_db.find_matching_rule(trackers);
            if (matched_rule) {
                if (matched_rule->source && !source) {
                    source = *matched_rule->source;
                    log_message("Tracker rule '" + matched_rule->name + "': auto-set source to '" + *source + "'", LogLevel::INFO);
                }

                if (matched_rule->max_piece_length || matched_rule->max_torrent_size || !matched_rule->piece_length_overrides.empty()) {

                    std::optional<int> current_kb;
                    if (piece_size) current_kb = *piece_size / 1024;

                    auto enforcement = rules_db.enforce(*matched_rule, content_size, current_kb);

                    if (enforcement.adjusted && enforcement.adjusted_piece_length) {
                        if (current_kb) {
                            std::string limit_info;
                            if (matched_rule->max_piece_length) {
                                limit_info = "max_piece_length (" + std::to_string(*matched_rule->max_piece_length / 1024) + " KB)";
                            } else {
                                limit_info = "rule constraint";
                            }
                            log_message("Tracker rule '" + matched_rule->name + "': user-specified piece size ("
                                + std::to_string(*current_kb) + " KB) adjusted by " + limit_info, LogLevel::WARNING);
                        } else {
                            piece_size = *enforcement.adjusted_piece_length * 1024;
                            print_verbose("Tracker rule '" + matched_rule->name + "': piece size adjusted to "
                                + std::to_string(*enforcement.adjusted_piece_length) + " KB\n");
                        }
                    }

                    if (enforcement.constraint_violation) {
                        print_info("WARNING: " + enforcement.violation_message + "\n");
                        log_message(enforcement.violation_message, LogLevel::WARNING);
                }
            } else {
                log_message("No matching tracker rule found for provided trackers", LogLevel::INFO);
            }
            }
        }

        return TorrentConfig(input_path, output_path, trackers, tv,
                             comment, is_private, web_seeds, piece_size,
                             creator_str, torrent_name, include_creation_date,
                             source, entropy,
                             std::move(exclude_regex_compiled), std::move(include_regex_compiled),
                             false, target_piece_count
        );
    }
    catch (const fs::filesystem_error &e)
    {
        // Preserve the specific path and system error for better diagnostics
        throw std::runtime_error("Error accessing path '" + e.path1().string() + "': " + e.what());
    }
}

int handle_inspect_command(const std::vector<std::string> &args)
{
    try
    {
        // Create array from strings
        int argc = static_cast<int>(args.size()) + 1;
        std::vector<const char *> argv;
        argv.push_back("torrent-builder");
        for (const auto &arg : args)
        {
            argv.push_back(arg.c_str());
        }

        cxxopts::Options inspect_options("torrent-builder inspect", "Inspect torrent file");
        inspect_options.add_options()("h,help", "Show help")("json", "Output in JSON format")(
            "files", "Show detailed file tree only")("verify", "Verify files exist on disk")(
            "path", "Path to torrent file",
            cxxopts::value<std::string>())("base-path", "Base path for verification",
                                           cxxopts::value<std::string>()->default_value("."));

        inspect_options.parse_positional({"path"});
        auto result = inspect_options.parse(argc, argv.data());

        if (result.count("help") || !result.count("path"))
        {
            print_info(inspect_options.help() + "\n");
            print_info("\nExamples:\n");
            print_info("  torrent-builder inspect file.torrent\n");
            print_info("  torrent-builder inspect file.torrent --json\n");
            print_info("  torrent-builder inspect file.torrent --files\n");
            print_info("  torrent-builder inspect file.torrent --verify --base-path /data\n");
            return 0;
        }

        std::string torrent_path = result["path"].as<std::string>();
        TorrentInspector inspector(torrent_path);
        TorrentMetadata metadata = inspector.inspect();

        if (result.count("files"))
        {
            bool json_output = result.count("json") > 0;
            print_info(TorrentInspector::format_file_tree(metadata, json_output));
        }
        else if (result.count("verify"))
        {
            std::string base_path = result["base-path"].as<std::string>();
            bool verified = inspector.verify_files(base_path);
            if (verified)
            {
                print_info("✓ All files verified successfully\n");
                return 0;
            }
            else
            {
                print_error("✗ Verification failed - missing or corrupt files\n");
                return 1;
            }
        }
        else
        {
            bool json_output = result.count("json") > 0;
            print_info(TorrentInspector::format_metadata(metadata, json_output));
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        log_message("Inspect error: " + std::string(e.what()), LogLevel::ERR);
        print_error(std::string("Error: ") + e.what() + "\n");
        return 1;
    }
}

int handle_modify_command(const std::vector<std::string> &args)
{
    try
    {
        int argc = static_cast<int>(args.size()) + 1;
        std::vector<const char *> argv;
        argv.push_back("torrent-builder");
        for (const auto &arg : args)
        {
            argv.push_back(arg.c_str());
        }

        cxxopts::Options modify_options("torrent-builder modify", "Modify torrent metadata");
        modify_options.add_options()(
            "h,help", "Show help")(
            "t,tracker", "Replace all trackers (exclusive with --add-tracker/--remove-tracker)",
            cxxopts::value<std::vector<std::string>>(), "URL")(
            "add-tracker", "Add tracker URL",
            cxxopts::value<std::vector<std::string>>(), "URL")(
            "remove-tracker", "Remove tracker URL",
            cxxopts::value<std::vector<std::string>>(), "URL")(
            "private", "Mark torrent as private")(
            "public", "Mark torrent as public")(
            "source", "Set source field (empty string removes it)",
            cxxopts::value<std::string>(), "SOURCE")(
            "comment", "Set comment (empty string removes it)",
            cxxopts::value<std::string>(), "COMMENT")(
            "name", "Change torrent name",
            cxxopts::value<std::string>(), "NAME")(
            "entropy", "Randomize info hash by adding entropy field")(
            "o,output", "Output torrent file path (defaults to in-place)",
            cxxopts::value<std::string>(), "OUTPUT")(
            "dry-run", "Preview changes without writing")(
            "input", "Input torrent file",
            cxxopts::value<std::string>());

        modify_options.parse_positional({"input"});
        auto result = modify_options.parse(argc, argv.data());

        if (result.count("help") || !result.count("input"))
        {
            print_info(modify_options.help() + "\n");
            print_info("\nExamples:\n");
            print_info("  torrent-builder modify file.torrent --tracker \"https://tracker.example/announce\"\n");
            print_info("  torrent-builder modify file.torrent --add-tracker \"https://tracker2.example/announce\"\n");
            print_info("  torrent-builder modify file.torrent --remove-tracker \"https://old.example/announce\"\n");
            print_info("  torrent-builder modify file.torrent --private\n");
            print_info("  torrent-builder modify file.torrent --public\n");
            print_info("  torrent-builder modify file.torrent --source \"PTP\"\n");
            print_info("  torrent-builder modify file.torrent --source \"\" --comment \"Updated\"\n");
            print_info("  torrent-builder modify file.torrent --name \"New Name\" --entropy\n");
            print_info("  torrent-builder modify file.torrent --dry-run --tracker \"https://tracker.example/announce\"\n");
            print_info("  torrent-builder modify file.torrent --output modified.torrent --entropy\n");
            return 0;
        }

        if (result.count("tracker") && (result.count("add-tracker") || result.count("remove-tracker")))
        {
            print_error("Error: --tracker is exclusive with --add-tracker/--remove-tracker\n");
            return 1;
        }

        if (result.count("private") && result.count("public"))
        {
            print_error("Error: --private and --public are mutually exclusive\n");
            return 1;
        }

        bool has_modification = result.count("tracker") || result.count("add-tracker") ||
                                result.count("remove-tracker") || result.count("private") ||
                                result.count("public") || result.count("source") ||
                                result.count("comment") || result.count("name") ||
                                result.count("entropy");

        if (!has_modification && !result.count("dry-run"))
        {
            print_error("Error: at least one modification option is required\n");
            return 1;
        }

        ModifyConfig config;
        config.input = result["input"].as<std::string>();

        if (result.count("output"))
        {
            config.output = result["output"].as<std::string>();
        }

        if (result.count("tracker"))
        {
            auto urls = result["tracker"].as<std::vector<std::string>>();
            for (const auto &url : urls)
            {
                if (!utils::is_valid_url(url))
                    throw std::runtime_error("Invalid tracker URL: " + url);
            }
            config.trackers = std::move(urls);
        }

        if (result.count("add-tracker"))
        {
            auto urls = result["add-tracker"].as<std::vector<std::string>>();
            for (const auto &url : urls)
            {
                if (!utils::is_valid_url(url))
                    throw std::runtime_error("Invalid tracker URL: " + url);
            }
            config.add_trackers = std::move(urls);
        }

        if (result.count("remove-tracker"))
        {
            auto urls = result["remove-tracker"].as<std::vector<std::string>>();
            for (const auto &url : urls)
            {
                if (!utils::is_valid_url(url))
                    throw std::runtime_error("Invalid tracker URL: " + url);
            }
            config.remove_trackers = std::move(urls);
        }

        if (result.count("private"))
        {
            config.is_private = true;
        }
        else if (result.count("public"))
        {
            config.is_private = false;
        }

        if (result.count("source"))
        {
            config.source = result["source"].as<std::string>();
        }

        if (result.count("comment"))
        {
            config.comment = result["comment"].as<std::string>();
        }

        if (result.count("name"))
        {
            config.name = result["name"].as<std::string>();
        }

        config.entropy = result.count("entropy") > 0;
        config.dry_run = result.count("dry-run") > 0;

        TorrentModifier modifier(config);
        modifier.modify();

        return 0;
    }
    catch (const std::exception &e)
    {
        log_message("Modify error: " + std::string(e.what()), LogLevel::ERR);
        print_error(std::string("Error: ") + e.what() + "\n");
        return 1;
    }
}

int handle_check_command(const std::vector<std::string> &args)
{
    try
    {
        int argc = static_cast<int>(args.size()) + 1;
        std::vector<const char *> argv;
        argv.push_back("torrent-builder");
        for (const auto &arg : args)
        {
            argv.push_back(arg.c_str());
        }

        cxxopts::Options check_options("torrent-builder check", "Verify local files against a .torrent file");
        check_options.add_options()(
            "h,help", "Show help")(
            "verbose", "Show per-piece verification progress")(
            "json", "Output results as JSON")(
            "path", "Content directory (defaults to torrent file directory)",
            cxxopts::value<std::string>(), "DIR")(
            "torrent", "Path to .torrent file",
            cxxopts::value<std::string>());

        check_options.parse_positional({"torrent"});
        auto result = check_options.parse(argc, argv.data());

        if (result.count("help") || !result.count("torrent"))
        {
            print_info(check_options.help() + "\n");
            print_info("\nExamples:\n");
            print_info("  torrent-builder check file.torrent\n");
            print_info("  torrent-builder check file.torrent --path /data/downloads\n");
            print_info("  torrent-builder check file.torrent --verbose\n");
            print_info("  torrent-builder check file.torrent --json\n");
            return 0;
        }

        if (result.count("verbose") && result.count("json"))
        {
            print_error("Error: --verbose and --json are mutually exclusive\n");
            return 1;
        }

        if (result.count("json"))
        {
            set_json_mode(true);
            set_verbosity(Verbosity::QUIET);
        }
        else if (result.count("verbose"))
        {
            set_verbosity(Verbosity::VERBOSE);
        }

        std::string torrent_path = result["torrent"].as<std::string>();

        fs::path content_path;
        if (result.count("path"))
        {
            content_path = result["path"].as<std::string>();
        }
        else
        {
            content_path = fs::path(torrent_path).parent_path();
        }

        if (!fs::exists(content_path))
        {
            log_message("Content path does not exist: " + content_path.string(), LogLevel::ERR);
            print_error("Error: Content path does not exist: " + content_path.string() + "\n");
            return 1;
        }

        TorrentChecker checker(torrent_path);
        CheckResult check_result = checker.check(content_path, result.count("verbose") > 0);

        if (is_json_mode())
        {
            std::cout << TorrentChecker::format_result(check_result, true);
        }
        else
        {
            print_info(TorrentChecker::format_result(check_result, false));
        }

        return check_result.passed ? 0 : 1;
    }
    catch (const std::exception &e)
    {
        log_message("Check error: " + std::string(e.what()), LogLevel::ERR);
        print_error(std::string("Error: ") + e.what() + "\n");
        return 1;
    }
}

int handle_batch_command(const std::vector<std::string> &args)
{
    try
    {
        int argc = static_cast<int>(args.size()) + 1;
        std::vector<const char *> argv;
        argv.push_back("torrent-builder");
        for (const auto &arg : args)
        {
            argv.push_back(arg.c_str());
        }

        cxxopts::Options batch_options("torrent-builder batch", "Batch create torrents from YAML config");
        batch_options.add_options()
            ("h,help", "Show help")
            ("w,workers", "Number of parallel workers", cxxopts::value<int>()->default_value("1"), "N")
            ("path", "Batch YAML file", cxxopts::value<std::string>(), "FILE");

        batch_options.parse_positional({"path"});
        auto result = batch_options.parse(argc, argv.data());

        if (result.count("help") || !result.count("path"))
        {
            print_info(batch_options.help() + "\n");
            print_info("Examples:\n");
            print_info("  torrent-builder batch batch.yaml\n");
            print_info("  torrent-builder batch batch.yaml --workers 4\n");
            return 0;
        }

        set_verbosity(Verbosity::QUIET);

        BatchConfig config = BatchProcessor::parse(result["path"].as<std::string>());

        if (result.count("workers")) {
            int w = result["workers"].as<int>();
            if (w >= 1) {
                config.workers = w;
            } else {
                print_error("Warning: Ignored --workers value: must be >= 1, using " + std::to_string(config.workers));
                log_message("Ignored --workers value: must be >= 1, using " + std::to_string(config.workers), LogLevel::WARNING);
            }
        }

        BatchProcessor processor(std::move(config));
        auto batch_start = std::chrono::steady_clock::now();
        auto results = processor.run();
        auto batch_end = std::chrono::steady_clock::now();
        double batch_elapsed = std::chrono::duration<double>(batch_end - batch_start).count();
        BatchProcessor::print_summary(results);
        log_message("Batch completed in " + std::to_string(batch_elapsed) + "s", LogLevel::INFO);

        for (const auto &r : results)
        {
            if (!r.success) return 1;
        }
        return 0;
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        log_message(std::string("Batch filesystem error: ") + e.what(), LogLevel::ERR);
        print_error(std::string("Filesystem error: ") + e.what() + "\n");
        return 1;
    }
    catch (const YAML::Exception &e)
    {
        log_message(std::string("Batch YAML error: ") + e.what(), LogLevel::ERR);
        print_error(std::string("YAML parsing error: ") + e.what() + "\n");
        return 1;
    }
    catch (const std::runtime_error &e)
    {
        log_message(std::string("Batch error: ") + e.what(), LogLevel::ERR);
        print_error(std::string(e.what()) + "\n");
        return 1;
    }
    catch (const std::exception &e)
    {
        log_message(std::string("Unexpected batch error: ") + e.what(), LogLevel::ERR);
        print_error(std::string("An unexpected error occurred: ") + e.what() + "\n");
        return 1;
    }
}

/**
 * @brief Handle the 'update' subcommand — check for, download, and install newer versions.
 *
 * Flags:
 *   -h,--help   Show help and usage.
 *   --check     Only check for available updates (no download).
 *   --yes       Skip confirmation prompt.
 *   --rollback  Restore previous version from .old backup.
 *
 * Returns 0 on success or user cancellation, 1 on error.
 */
int handle_update_command(const std::vector<std::string> &args)
{
    std::string download_path;
    try
    {
        int argc = static_cast<int>(args.size()) + 1;
        std::vector<const char *> argv;
        argv.push_back("torrent-builder");
        for (const auto &arg : args)
        {
            argv.push_back(arg.c_str());
        }

        cxxopts::Options update_options("torrent-builder update", "Update to the latest version");
        update_options.add_options()
            ("h,help", "Show help")
            ("y,yes", "Skip confirmation prompt")
            ("check", "Only check for updates (do not download)")
            ("rollback", "Restore previous version");

        auto result = update_options.parse(argc, argv.data());

        if (result.count("help"))
        {
            print_info(update_options.help() + "\n");
            print_info("\nExamples:\n");
            print_info("  torrent-builder update\n");
            print_info("  torrent-builder update --check\n");
            print_info("  torrent-builder update --yes\n");
            print_info("  torrent-builder update --rollback\n");
            return 0;
        }

        if (result.count("rollback"))
        {
            std::string exe_path = Updater::get_exe_path();

            print_info("Rolling back to previous version...\n");
            log_message("User initiated rollback", LogLevel::INFO);

            if (!result.count("yes") && !prompt_yes_no("Restore previous version?"))
            {
                print_info("Rollback cancelled.\n");
                log_message("Rollback cancelled by user", LogLevel::INFO);
                return 0;
            }

            UpdateResult rollback_result = Updater::perform_rollback(exe_path);
            if (rollback_result == UpdateResult::Success)
            {
                print_info("Successfully rolled back to previous version.\n");
                log_message("Rollback completed successfully", LogLevel::INFO);
            }
            else
            {
                print_info("Rollback scheduled: previous version will be restored when this command exits.\n");
                log_message("Rollback scheduled via async helper", LogLevel::INFO);
            }
            return 0;
        }

        print_info("Checking for updates...\n");
        log_message("Checking for updates (current: " + std::string(Updater::get_current_version()) + ")", LogLevel::INFO);

        auto latest = Updater::fetch_latest_release();

        if (latest.version.empty())
        {
            print_error("Error: Could not determine the latest release version.\n");
            log_message("Latest release has empty version tag", LogLevel::ERR);
            return 1;
        }

        std::string current = Updater::get_current_version();
        int cmp = utils::compare_versions(current, latest.version);

        bool is_dev = current.find("dev") != std::string::npos;

        if (cmp >= 0 && !is_dev)
        {
            print_info("Already up to date: " + current + "\n");
            log_message("Already up to date: " + current, LogLevel::INFO);
            return 0;
        }

        if (cmp > 0 && is_dev)
        {
            print_info("Current dev version (" + current + ") is newer than latest release (" + latest.version + ").\n");
            log_message("Dev build ahead of latest release: " + current + " > " + latest.version, LogLevel::INFO);
            return 0;
        }

        log_message("Update available: " + current + " -> " + latest.version, LogLevel::INFO);

        print_info("Current version:  " + current + "\n");
        print_info("Latest version:   " + latest.version + "\n");

        if (!latest.changelog.empty())
        {
            std::string changelog = latest.changelog;
            std::string sanitized;
            sanitized.reserve(changelog.size());
            for (size_t i = 0; i < changelog.size(); ++i)
            {
                unsigned char c = static_cast<unsigned char>(changelog[i]);
                if (c == '\x1b')
                {
                    if (i + 1 >= changelog.size())
                        continue;
                    char next = changelog[i + 1];
                    if (next == '[')
                    {
                        size_t j = i + 2;
                        while (j < changelog.size() &&
                               (std::isdigit(static_cast<unsigned char>(changelog[j])) || changelog[j] == ';'))
                            j++;
                        if (j < changelog.size() && changelog[j] >= 0x40 && changelog[j] <= 0x7E)
                        {
                            i = j;
                            continue;
                        }
                    }
                    else if (next == ']')
                    {
                        size_t j = i + 2;
                        while (j < changelog.size())
                        {
                            if (changelog[j] == '\x07') { i = j; break; }
                            if (changelog[j] == '\x1b' && j + 1 < changelog.size() && changelog[j + 1] == '\\') { i = j + 1; break; }
                            j++;
                        }
                        if (j >= changelog.size())
                            i = changelog.size() - 1;
                        continue;
                    }
                    else
                    {
                        ++i;
                        continue;
                    }
                }
                if (c < 0x20 && c != '\t' && c != '\n')
                    continue;
                sanitized += static_cast<char>(c);
            }
            changelog = sanitized;
            if (changelog.size() > 500)
            {
                size_t cut = 500;
                while (cut > 0 && (static_cast<unsigned char>(changelog[cut]) & 0xC0) == 0x80)
                    --cut;
                changelog = changelog.substr(0, cut) + "...";
            }
            print_info("\nChangelog:\n" + changelog + "\n\n");
        }

        if (result.count("check"))
        {
            print_info("Update available. Run 'torrent-builder update' to install.\n");
            log_message("Check-only mode: update available (" + latest.version + ")", LogLevel::INFO);
            return 0;
        }

        auto platform = Updater::get_platform_info();
        print_info("Platform: " + platform.os + "/" + platform.arch + "\n");

        auto asset_url = Updater::find_matching_asset_from_json(latest.release_json, platform);
        if (!asset_url)
        {
            log_message("No matching asset for " + platform.os + "/" + platform.arch, LogLevel::ERR);
            print_error("Error: No matching binary found for " + platform.os + "/" + platform.arch + "\n");
            return 1;
        }

        latest.asset_size = Updater::get_asset_size(latest.release_json, *asset_url);

        if (!result.count("yes"))
        {
            if (!prompt_yes_no("Download and install version " + latest.version + "?"))
            {
                print_info("Update cancelled.\n");
                log_message("Update cancelled by user for version " + latest.version, LogLevel::INFO);
                return 0;
            }
        }

        std::string exe_path = Updater::get_exe_path();

        fs::path temp_dir = fs::temp_directory_path();
        std::string ext = "";
#ifdef _WIN32
        ext = ".exe";
#elif defined(__APPLE__)
        ext = ".zip";
#endif
        std::random_device rd;
        download_path = (temp_dir / ("tb_update_" + std::to_string(rd()) + ext)).string();

        print_info("Downloading " + latest.version + "...\n");
        log_message("Downloading update " + latest.version + " from: " + *asset_url, LogLevel::INFO);

        if (!Updater::download_asset(*asset_url, download_path, latest.asset_size))
        {
            log_message("Download failed for: " + *asset_url, LogLevel::ERR);
            print_error("Error: Failed to download update.\n");
            try { fs::remove(download_path); } catch (...) {}
            return 1;
        }

        auto checksums = Updater::fetch_checksums(latest.release_json);
        bool has_checksum = false;
        if (checksums)
        {
            std::string asset_name = fs::path(*asset_url).filename().string();
            auto expected_hash = Updater::lookup_checksum(*checksums, asset_name);
            if (expected_hash)
            {
                has_checksum = true;
                print_info("Verifying checksum...\n");
                log_message("Verifying checksum for: " + download_path, LogLevel::INFO);
                if (!Updater::verify_checksum(download_path, *expected_hash))
                {
                    log_message("Checksum verification failed for: " + download_path, LogLevel::ERR);
                    print_error("Error: Checksum verification failed. Download may be corrupted.\n");
                    try { fs::remove(download_path); } catch (...) {}
                    return 1;
                }
                print_info("Checksum verified.\n");
            }
            else
            {
                log_message("No checksum found for asset: " + asset_name, LogLevel::WARNING);
            }
        }
        else
        {
            log_message("No checksums.txt found in release, skipping verification", LogLevel::WARNING);
        }

        if (!has_checksum)
        {
            if (!Updater::is_valid_binary(download_path))
            {
                log_message("Downloaded file is not a valid binary: " + download_path, LogLevel::ERR);
                print_error("Error: Downloaded file is not a valid binary. The download may be corrupted.\n");
                try { fs::remove(download_path); } catch (...) {}
                return 1;
            }
            log_message("No checksum available, validated binary format instead", LogLevel::WARNING);
        }

        print_info("Installing update...\n");
        log_message("Installing update from: " + download_path + " to: " + exe_path, LogLevel::INFO);

        UpdateResult update_result = Updater::perform_update(download_path, exe_path);

        if (update_result == UpdateResult::Success)
        {
            try { fs::remove(download_path); } catch (...) {}
            print_info("Successfully updated to version " + latest.version + "!\n");
            log_message("Successfully updated to version " + latest.version, LogLevel::INFO);
        }
        else
        {
            print_info("Update scheduled: version " + latest.version +
                           " will be installed when this command exits.\n");
            log_message("Update scheduled via async helper for version " + latest.version, LogLevel::INFO);
        }
        return 0;
    }
    catch (const std::exception &e)
    {
        log_message("Update error: " + std::string(e.what()), LogLevel::ERR);
        print_error(std::string("Error: ") + e.what() + "\n");
        try { fs::remove(download_path); } catch (...) {}
        return 1;
    }
}

#ifndef TORRENT_BUILDER_EXCLUDE_MAIN

/**
 * @brief Best-effort update check run once on startup (default flow only).
 *
 * Honors the disable switches (TB_NO_UPDATE_CHECK env var / --no-update-check
 * flag), the 24h throttle, and is suppressed under --quiet/--json. Prints an
 * update notification to stderr (never stdout, so --json output is safe) when
 * a newer version exists. Never throws and never crashes on network errors.
 */
static void maybe_check_for_updates_on_startup(const cxxopts::ParseResult &result)
{
    try
    {
        if (result.count("no-update-check"))
            return;

        if (const char *e = std::getenv("TB_NO_UPDATE_CHECK"))
        {
            if (e[0] == '1' || e[0] == 't' || e[0] == 'T')
                return;
        }

        if (result.count("quiet") || result.count("json"))
            return;

        if (!Updater::should_check_for_updates(24))
            return;

        auto check = Updater::check_for_update();
        // Persist the throttle timestamp ONLY when the network round-trip
        // actually completed — including when the build is already up-to-date
        // (the common case), so the throttle works for it. When the fetch
        // failed (offline / curl missing / rate limit) we do NOT record, so the
        // next run retries sooner instead of waiting out the full interval.
        if (check.fetch_succeeded)
            Updater::record_update_check();

        if (!check.info)
            return;

        std::string current = Updater::get_current_version();
        bool is_dev = (current == "dev" ||
                       (current.size() >= 5 && current.compare(0, 5, "dev (") == 0));
        if (is_dev)
        {
            // Informational only: do NOT suggest `update`, which would replace
            // this locally-compiled binary with a prebuilt release artifact.
            std::cerr << "\n*** Newer release available: v" << check.info->version
                      << " (you are running a dev build: " << current << ") ***\n";
        }
        else
        {
            std::cerr << "\n*** Update available: v" << check.info->version
                      << " — run `torrent_builder update` ***\n";
        }
    }
    catch (const std::exception &e)
    {
        log_message(std::string("Startup update check error: ") + e.what(), LogLevel::WARNING);
    }
}

int main(int argc, char *argv[])
{
    // Check for subcommands
    if (argc >= 2 && std::string(argv[1]) == "modify")
    {
        std::vector<std::string> args;
        for (int i = 2; i < argc; ++i)
        {
            args.push_back(argv[i]);
        }
        return handle_modify_command(args);
    }

    if (argc >= 2 && std::string(argv[1]) == "inspect")
    {
        std::vector<std::string> args;
        for (int i = 2; i < argc; ++i)
        {
            args.push_back(argv[i]);
        }
        return handle_inspect_command(args);
    }

    if (argc >= 2 && std::string(argv[1]) == "check")
    {
        std::vector<std::string> args;
        for (int i = 2; i < argc; ++i)
        {
            args.push_back(argv[i]);
        }
        return handle_check_command(args);
    }

    if (argc >= 2 && std::string(argv[1]) == "batch")
    {
        std::vector<std::string> args;
        for (int i = 2; i < argc; ++i)
        {
            args.push_back(argv[i]);
        }
        return handle_batch_command(args);
    }

    if (argc >= 2 && std::string(argv[1]) == "update")
    {
        std::vector<std::string> args;
        for (int i = 2; i < argc; ++i)
        {
            args.push_back(argv[i]);
        }
        return handle_update_command(args);
    }

    try
    {
        // Define command-line options
        cxxopts::Options options("torrent_builder", "Create torrent files");
        options.add_options()("h,help", "Show help")("v,version", "Show version")(
            "verbose", "Enable verbose output (conflicts with --quiet, --json)")("q,quiet", "Suppress non-essential output, auto-decline prompts (conflicts with --verbose)")(
            "json", "Output torrent metadata as JSON (implies --quiet, conflicts with --verbose)")(
            "i,interactive", "Run in interactive mode")(
            "t,torrent-version", "Torrent version (1=v1, 2=v2, 3=hybrid)",
            cxxopts::value<std::string>()->default_value("3"),
            "{1,2,3}")("c,comment", "Torrent comment", cxxopts::value<std::string>(), "COMMENT")(
            "n,name", "Set custom torrent name", cxxopts::value<std::string>(), "NAME")(
            "private", "Make torrent private")("default-trackers", "Use default trackers")(
            "T,tracker", "Add tracker URL", cxxopts::value<std::vector<std::string>>(), "URL")(
            "w,webseed", "Add web seed URL", cxxopts::value<std::vector<std::string>>(),
            "URL")("s,piece-size",
                   "Piece size in KB (allowed: 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, "
                   "16384, 32768)",
                   cxxopts::value<int>(), "SIZE")(
            "target-piece-count", "Target number of pieces (calculates optimal piece size)",
            cxxopts::value<int>(), "N")("creator", "Set \"Torrent Builder\" as creator")(
            "creation-date", "Set creation date")("p,path", "Path to file or directory",
                                                  cxxopts::value<std::string>(), "PATH")(
            "o,output", "Output torrent file path (optional, auto-generated if omitted)", cxxopts::value<std::string>(), "OUTPUT")(
            "skip-prefix", "Omit tracker domain from auto-generated output filename")(
            "output-dir", "Directory for auto-generated output filename (created if needed)",
            cxxopts::value<std::string>(), "DIR")(
            "tracker-index", "Index of tracker to use for filename prefix (0-based, defaults to 0 on out-of-range)",
            cxxopts::value<int>()->default_value("0"), "N")(
            "source", "Add source string to torrent info for cross-seeding",
            cxxopts::value<std::string>(), "SOURCE")(
            "e,entropy", "Randomize info hash by adding entropy field")(
            "x,exclude", "Exclude files matching glob pattern (supports *, **, ?)",
             cxxopts::value<std::vector<std::string>>(), "PATTERN")(
            "I,include", "Include only files matching glob pattern (supports *, **, ?)",
             cxxopts::value<std::vector<std::string>>(), "PATTERN")(
            "preset", "Apply named preset from presets.yaml", cxxopts::value<std::string>(), "NAME")(
            "preset-file", "Load presets from specified file", cxxopts::value<std::string>(), "FILE")(
            "rules-file", "Load tracker rules from specified file", cxxopts::value<std::string>(), "FILE")(
            "fail-on-season-warning", "Fail if a TV season pack has missing episodes")(
            "no-update-check", "Skip automatic update check on startup");

        options.positional_help("PATH [OUTPUT]");
        options.parse_positional({"path", "output"});
        auto result = options.parse(argc, argv);

        if (result.count("verbose") && result.count("quiet")) {
            print_error("Error: --verbose and --quiet are mutually exclusive\n");
            return 1;
        }
        if (result.count("verbose") && result.count("json")) {
            print_error("Error: --verbose and --json are mutually exclusive\n");
            return 1;
        }

        // Help and version — always print regardless of verbosity
        if (result.count("version"))
        {
            std::cout << "torrent_builder " << TORRENT_BUILDER_VERSION << std::endl;
            return 0;
        }

        if (result.count("help") || argc == 1)
        {
            std::cout << options.help() << "\n";
            std::cout << "Examples:\n";
            std::cout << "  ./torrent_builder -i\n";
            std::cout << "  ./torrent_builder -p /data/file -o file.torrent\n";
            std::cout << "  ./torrent_builder --path /data/file --output file.torrent "
                         "--default-trackers\n";
            std::cout << "  ./torrent_builder --path /data/folder --output folder.torrent "
                         "--torrent-version 2 --private\n";
            std::cout << "  ./torrent_builder --path /data/file --output file.torrent --piece-size "
                         "1024\n";
            std::cout << "  ./torrent_builder --path /data/file --tracker "
                         "\"https://tracker.example/announce\"\n";
            std::cout << "  ./torrent_builder --path /data/file --tracker "
                         "\"https://tracker.example/announce\" --skip-prefix\n";
            std::cout << "  ./torrent_builder --path /data/file --tracker "
                         "\"https://tracker.example/announce\" --output-dir /torrents\n";
            std::cout << "  ./torrent_builder --path /data/file --default-trackers "
                         "--tracker-index 2\n";
            std::cout << "  ./torrent_builder --path /data/folder --exclude \"*.nfo\" "
                         "--exclude \"*.txt\"\n";
            std::cout << "  ./torrent_builder --path /data/folder --include \"*.mkv\" "
                         "--include \"*.mp4\"\n";
            std::cout << "  ./torrent_builder --path /data/file --verbose\n";
            std::cout << "  ./torrent_builder --path /data/file --quiet\n";
            std::cout << "  ./torrent_builder --path /data/file --json\n";
            std::cout << "\nNote: Allowed piece sizes (in KB): 16, 32, 64, 128, 256, 512, 1024, "
                         "2048, 4096, 8192, 16384, 32768\n";
            std::cout << "Glob patterns: * (any non-slash), **/ (zero or more dirs), "
                         "** (any path), ? (single char). Case-insensitive.\n";
            std::cout << "Note: --include patterns take precedence over --exclude when both match.\n";
            return 0;
        }

        // Set verbosity only when NOT in interactive mode
        // (interactive mode needs print_info for prompts)
        if (!result.count("interactive")) {
            if (result.count("json")) {
                set_json_mode(true);
                set_verbosity(Verbosity::QUIET);
            } else if (result.count("quiet")) {
                set_verbosity(Verbosity::QUIET);
            } else if (result.count("verbose")) {
                set_verbosity(Verbosity::VERBOSE);
            }
        }

        // Run in interactive or command-line mode based on arguments
        if (result.count("interactive"))
        {
            // Best-effort update check (default flow only; subcommands skip main()).
            maybe_check_for_updates_on_startup(result);
            auto config = get_interactive_config();
            TorrentCreator creator(config);
            creator.create_torrent();
        }
        else
        {
            std::string declined_path;
            auto config_opt = get_commandline_config(result, declined_path);
            if (!config_opt)
            {
                log_message("Overwrite declined: " + declined_path, LogLevel::INFO);
                if (is_json_mode()) {
                    print_error("Error: Output file already exists: " + declined_path + "\n");
                } else {
                    print_info("Operation cancelled.\n");
                }
                return 1;
            }
            auto season_error = season_pack::evaluate_season_warning(
                config_opt->path, result.count("fail-on-season-warning") > 0);
            if (season_error)
            {
                throw std::runtime_error(*season_error);
            }
            // Best-effort update check — runs only after args are validated so
            // the notice never appears before an error message.
            maybe_check_for_updates_on_startup(result);
            TorrentCreator creator(*config_opt);
            creator.create_torrent();
            if (is_json_mode()) {
                try {
                    TorrentInspector inspector(config_opt->output);
                    TorrentMetadata meta = inspector.inspect();
                    std::string json = TorrentInspector::format_metadata(meta, true);
                    std::string path_field = ",\n  \"output_path\": \"" + utils::escape_json(config_opt->output.string()) + "\"\n";
                    json.insert(json.rfind('}'), path_field);
                    std::cout << json;
                } catch (const std::exception& e) {
                    log_message("JSON output generation error: " + std::string(e.what()), LogLevel::ERR);
                    print_error(std::string("Error generating JSON output: ") + e.what() + "\n");
                    return 1;
                }
            }
        }
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        log_message(std::string("Filesystem error: ") + e.what(), LogLevel::ERR);
        print_error(std::string(e.what()) + "\n");
        return 1;
    }
    catch (const std::invalid_argument &e)
    {
        log_message(std::string("Invalid argument: ") + e.what(), LogLevel::ERR);
        print_error(std::string(e.what()) + "\n");
        return 1;
    }
    catch (const UserInterrupt &e)
    {
        log_message(std::string(e.what()), LogLevel::WARNING);
        print_error(std::string(e.what()) + "\n");
        return 1;
    }
    catch (const std::runtime_error &e)
    {
        log_message(std::string("Runtime error: ") + e.what(), LogLevel::ERR);
        print_error(std::string(e.what()) + "\n");
        return 1;
    }
    catch (const std::exception &e)
    {
        log_message(std::string("Unexpected error: ") + e.what(), LogLevel::ERR);
        print_error(std::string("An unexpected error occurred: ") + e.what() + "\n");
        return 1;
    }

    return 0;
}
#endif
