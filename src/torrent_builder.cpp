#include "torrent_creator.hpp"
#include "creation_planner.hpp"
#include "logger.hpp"
#include "constants.hpp"
#include "utils.hpp"
#include "terminal.hpp"
#include "version.hpp"
#include "batch.hpp"
#include <iostream>
#include <vector>
#include <memory>
#include <optional>
#include <cxxopts.hpp>
#include <filesystem>
#include <charconv>
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
#include "output.hpp"
#include "updater.hpp"

namespace fs = std::filesystem;

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

// Get torrent configuration from user input (interactive mode). Answers become
// a CreationRequest so the wizard shares resolution with the CLI and the GUI.
TorrentConfig get_interactive_config(const cxxopts::ParseResult &result)
{
    // Load an explicit rules file before prompting: a typo should not cost the
    // user the whole questionnaire. Default-path discovery stays CLI-only.
    std::shared_ptr<TrackerRulesDatabase> rules;
    if (result.count("rules-file"))
    {
        auto db = std::make_shared<TrackerRulesDatabase>();
        db->load(TrackerRulesDatabase::find_rules_file(
            fs::path(result["rules-file"].as<std::string>())));
        rules = std::move(db);
    }

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
    int version_number = 3;
    while (true)
    {
        print_info("Torrent version (1-v1, 2-v2, 3-Hybrid) [3]: ");
        std::getline(std::cin, version);
        if (version.empty() || version == "3")
        {
            version_number = 3;
            break;
        }
        else if (version == "1")
        {
            version_number = 1;
            break;
        }
        else if (version == "2")
        {
            version_number = 2;
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
        trackers.insert(trackers.end(), Defaults::trackers.begin(), Defaults::trackers.end());
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

        if (!utils::is_valid_web_seed_url(seed))
        {
            print_info("Error: Invalid web seed URL. Must start with http:// or https://\n");
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
            for (size_t i = 0; i < AllowedPieceSizes::values.size(); ++i)
            {
                valid_options += std::to_string(AllowedPieceSizes::values[i]);
                if (i < AllowedPieceSizes::values.size() - 1)
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
                if (std::ranges::contains(AllowedPieceSizes::values, ps))
                {
                    piece_size = ps; // KB; the planner converts to bytes
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
    // Reuse the size scanned for the preview so the planner does not walk the
    // content tree a second time.
    std::optional<int64_t> content_size_hint;
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
                        target_piece_count = tpc;
                        content_size_hint = total_size;
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

    // Creator defaults to "Torrent Builder" unless the user opts out.
    const bool omit_creator = prompt_yes_no("Omit creator from torrent?");

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

    // Get creation date (default: included, user can choose to omit)
    bool include_creation_date = !prompt_yes_no("Omit creation date from torrent?");

    // Get optional source string
    std::optional<std::string> source = prompt_optional_string("Source string for cross-seeding (leave blank to skip): ");

    // Get entropy flag
    bool entropy = prompt_yes_no("Add random entropy for unique info hash?");

    // Raw glob patterns; the planner compiles them and applies built-in excludes.
    std::vector<std::string> exclude_patterns;
    if (prompt_yes_no("Exclude files by pattern?"))
    {
        while (true)
        {
            std::string pattern;
            print_info("Exclude pattern (glob, leave blank to finish): ");
            std::getline(std::cin, pattern);
            if (pattern.empty())
                break;
            exclude_patterns.push_back(pattern);
        }
    }

    std::vector<std::string> include_patterns;
    if (prompt_yes_no("Include only matching files?"))
    {
        while (true)
        {
            std::string pattern;
            print_info("Include pattern (glob, leave blank to finish): ");
            std::getline(std::cin, pattern);
            if (pattern.empty())
                break;
            include_patterns.push_back(pattern);
        }
    }

    // Built-in system-file exclusions are on by default. prompt_yes_no returns
    // false on empty/n input, so the question is phrased to opt INTO inclusion:
    // answering "no" (the default) keeps system files excluded, matching the
    // documented on-by-default behavior of CLI and batch modes.
    bool include_system_files = prompt_yes_no("Include system files (.DS_Store, Thumbs.db, etc.)?");
    bool use_builtin_excludes = !include_system_files;
    if (use_builtin_excludes) {
        log_message("Applying built-in exclude patterns (interactive)", LogLevel::INFO);
    }

    CreationRequest request;
    request.input = fs::path(path);
    request.output = fs::path(output);
    request.fail_on_season_warning = result.count("fail-on-season-warning") > 0;

    request.values.trackers = trackers;
    request.values.web_seeds = web_seeds;
    request.values.torrent_version = version_number;
    request.values.comment =
        comment.empty() ? std::nullopt : std::optional<std::string>(comment);
    request.values.is_private = is_private;
    request.values.piece_size = piece_size;
    request.values.target_piece_count = target_piece_count;
    request.content_size_hint = content_size_hint;
    request.values.no_creator = omit_creator;
    request.values.name = torrent_name;
    request.values.no_date = !include_creation_date;
    request.values.source = source;
    request.values.entropy = entropy;
    request.values.exclude_patterns = exclude_patterns;
    request.values.include_patterns = include_patterns;
    request.values.builtin_excludes = use_builtin_excludes;

    return CreationPlanner(PlannerResources{nullptr, std::move(rules)}).plan(request).config;
}

namespace
{

int parse_torrent_version(const std::string &raw)
{
    int value = 0;
    const char *begin = raw.data();
    const char *end = begin + raw.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end || value < 1 || value > 3)
    {
        throw std::runtime_error("Invalid torrent version: " + raw + " (expected 1, 2 or 3)");
    }
    return value;
}

} // namespace

// Map command-line flags into a CreationRequest and let CreationPlanner resolve
// the configuration. Returns std::nullopt when the user declines to overwrite
// an existing output file.
std::optional<CreationPlan> get_commandline_config(const cxxopts::ParseResult &result,
                                                   std::string &declined_path)
{
    if (!result.count("path"))
    {
        throw std::runtime_error("Path is required");
    }

    CreationRequest request;
    request.input = fs::path(result["path"].as<std::string>());

    if (result.count("output"))
    {
        request.output = fs::path(result["output"].as<std::string>());
        if (request.output->empty())
        {
            throw std::runtime_error("--output cannot be empty");
        }
    }
    else
    {
        request.skip_prefix = result.count("skip-prefix") > 0;
        request.tracker_index = result["tracker-index"].as<int>();
        if (result.count("output-dir"))
        {
            request.output_dir = fs::path(result["output-dir"].as<std::string>());
        }
    }

    if (result.count("preset"))
    {
        request.preset = result["preset"].as<std::string>();
    }
    if (result.count("preset-file"))
    {
        request.preset_file = fs::path(result["preset-file"].as<std::string>());
    }
    if (result.count("rules-file"))
    {
        request.rules_file = fs::path(result["rules-file"].as<std::string>());
    }

    request.use_default_trackers = result.count("default-trackers") > 0;
    request.fail_on_season_warning = result.count("fail-on-season-warning") > 0;
    // The CLI keeps discovering ./rules.yaml and the XDG/home fallbacks.
    request.discover_rules = true;

    if (result.count("tracker"))
    {
        request.values.trackers = result["tracker"].as<std::vector<std::string>>();
    }
    if (result.count("webseed"))
    {
        request.values.web_seeds = result["webseed"].as<std::vector<std::string>>();
    }
    if (result.count("torrent-version"))
    {
        request.values.torrent_version =
            parse_torrent_version(result["torrent-version"].as<std::string>());
    }
    if (result.count("comment"))
    {
        request.values.comment = result["comment"].as<std::string>();
    }
    if (result.count("name"))
    {
        request.values.name = result["name"].as<std::string>();
    }
    if (result.count("source"))
    {
        request.values.source = result["source"].as<std::string>();
    }
    if (result.count("piece-size"))
    {
        request.values.piece_size = result["piece-size"].as<int>();
    }
    if (result.count("target-piece-count"))
    {
        request.values.target_piece_count = result["target-piece-count"].as<int>();
    }
    if (result.count("no-creator"))
    {
        request.values.no_creator = true;
    }
    if (result.count("no-date"))
    {
        request.values.no_date = true;
    }
    if (result.count("entropy"))
    {
        request.values.entropy = true;
    }
    if (result.count("exclude"))
    {
        request.values.exclude_patterns = result["exclude"].as<std::vector<std::string>>();
    }
    if (result.count("include"))
    {
        request.values.include_patterns = result["include"].as<std::vector<std::string>>();
    }
    if (result.count("no-builtin-excludes"))
    {
        request.values.builtin_excludes = false;
    }
    if (result.count("private"))
    {
        request.values.is_private = true;
    }

    // Resolve an explicit preset before prompting: a typo in --preset-file must
    // be reported before the overwrite question, matching the old order. The
    // loaded instance is injected so plan() does not read the file twice.
    PlannerResources resources;
    ConfigValues preset_values;
    if (request.preset)
    {
        auto presets = std::make_shared<PresetLoader>();
        presets->load(PresetLoader::find_preset_file(request.preset_file));
        // Resolve now so an unknown preset name is reported before the prompt.
        // plan() resolves again from the injected loader (a cheap map lookup).
        preset_values = presets->resolve(*request.preset);
        resources.presets = std::move(presets);
    }

    // Validate cheap request-level values before asking about overwrite, so an
    // invalid tracker/version/piece size is reported even when the output exists.
    // Preset-derived conflicts are checked here too, while the preset is already
    // loaded and no prompt has run yet.
    CreationPlanner::validate(request, preset_values);

    if (request.output && !request.output->empty() && fs::exists(*request.output))
    {
        if (prompt_overwrite(request.output->string()) == OverwriteDecision::Declined)
        {
            declined_path = request.output->string();
            return std::nullopt;
        }
    }

    return CreationPlanner(std::move(resources)).plan(request);
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
            cxxopts::value<int>(), "N")("no-creator", "Omit creator field from torrent metadata")(
            "d,no-date", "Omit creation date from torrent metadata")("p,path", "Path to file or directory",
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
            "no-builtin-excludes", "Disable built-in system-file exclusions (.DS_Store, Thumbs.db, etc.)")(
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
            auto config = get_interactive_config(result);
            TorrentCreator creator(config);
            creator.create_torrent();
        }
        else
        {
            std::string declined_path;
            auto plan_opt = get_commandline_config(result, declined_path);
            if (!plan_opt)
            {
                log_message("Overwrite declined: " + declined_path, LogLevel::INFO);
                if (is_json_mode()) {
                    print_error("Error: Output file already exists: " + declined_path + "\n");
                } else {
                    print_info("Operation cancelled.\n");
                }
                return 1;
            }
            // Best-effort update check — runs only after args are validated so
            // the notice never appears before an error message.
            maybe_check_for_updates_on_startup(result);
            TorrentCreator creator(plan_opt->config);
            creator.create_torrent();
            if (is_json_mode()) {
                try {
                    TorrentInspector inspector(plan_opt->config.output);
                    TorrentMetadata meta = inspector.inspect();
                    std::string json = TorrentInspector::format_metadata(meta, true);
                    std::string path_field = ",\n  \"output_path\": \"" + utils::escape_json(plan_opt->config.output.string()) + "\"\n";
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
