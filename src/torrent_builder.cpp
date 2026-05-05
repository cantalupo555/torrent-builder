#include "torrent_creator.hpp"
#include "logger.hpp"
#include "constants.hpp"
#include "utils.hpp"
#include "terminal.hpp"
#include "version.hpp"
#include <iostream>
#include <vector>
#include <optional>
#include <cxxopts.hpp>
#include <filesystem>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include "torrent_inspector.hpp"

namespace fs = std::filesystem;

// Default trackers to be used if none are provided
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
    std::cout << "=== TORRENT CONFIGURATION ===" << std::endl;

    // Get input path
    std::string path;
    while (true)
    {
        std::cout << "Path to file or directory: ";
        std::getline(std::cin, path);
        path = utils::sanitize_path(path);
        if (path.empty())
        {
            std::cout << "Error: Input path cannot be empty\n";
            continue;
        }
        try
        {
            // Check if the path exists
            if (!fs::exists(path))
            {
                std::cout << "Error: The specified path does not exist. Please check the path and "
                             "try again.\n";
                continue;
            }
            // Check if we have read permissions
            fs::file_status status = fs::status(path);
            if ((status.permissions() & fs::perms::owner_read) == fs::perms::none)
            {
                std::cout << "Error: No read permissions for path\n";
                continue;
            }
            break;
        }
        catch (const fs::filesystem_error &e)
        {
            std::cout << "Filesystem error: " << e.what() << "\n";
            continue;
        }
    }

    // Get output path
    std::string output;
    while (true)
    {
        std::cout << "Path to save torrent: ";
        std::getline(std::cin, output);
        output = utils::sanitize_path(output);
        if (output.empty())
        {
            std::cout << "Error: Output path cannot be empty\n";
            continue;
        }
        if (output.size() < 8 || output.substr(output.size() - 8) != ".torrent")
        {
            std::cout << "Error: Output path must end with '.torrent'\n";
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
                std::cout << "Error: Parent directory does not exist\n";
                continue;
            }
            // Check write permissions
            fs::file_status status = fs::status(parent_dir);
            if ((status.permissions() & fs::perms::owner_write) == fs::perms::none)
            {
                std::cout << "Error: No write permissions for directory\n";
                continue;
            }
            break;
        }
        catch (const fs::filesystem_error &e)
        {
            std::cout << "Filesystem error: " << e.what() << "\n";
            continue;
        }
    }

get_version: // Label to jump to if overwrite is 'n' or empty
    // Get torrent version
    std::string version;
    TorrentVersion tv = TorrentVersion::V1; // Initialize tv here
    while (true)
    {
        std::cout << "Torrent version (1-v1, 2-v2, 3-Hybrid) [3]: ";
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
            std::cout << "Error: Invalid input. Please enter '1', '2', or '3'.\n";
        }
    }

    // Get optional comment
    std::string comment;
    std::cout << "Comment (optional): ";
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
            std::cout << "Add tracker (leave blank to finish): ";
            std::getline(std::cin, tracker);
            if (tracker.empty())
                break;

            if (!utils::is_valid_url(tracker))
            {
                std::cout << "Error: Invalid tracker URL. Must start with http://, https://, "
                             "or udp://\n";
                continue;
            }

            if (std::ranges::contains(trackers, tracker))
            {
                std::cout << "Error: Tracker already added.\n";
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
        std::cout << "Add web seed (leave blank to finish): ";
        std::getline(std::cin, seed);
        if (seed.empty())
            break;

        if (!utils::is_valid_url(seed))
        {
            std::cout
                << "Error: Invalid web seed URL. Must start with http://, https://, or udp://\n";
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
            std::cout << "Piece size in KB: \n";

            std::cout << "Valid options: ";
            for (size_t i = 0; i < allowed_piece_sizes.size(); ++i)
            {
                std::cout << allowed_piece_sizes[i];
                if (i < allowed_piece_sizes.size() - 1)
                {
                    std::cout << ", ";
                }
            }
            std::cout << "\n";

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
                    std::cout << "Error: Invalid piece size. Please enter a valid option.\n";
                }
            }
            catch (const std::invalid_argument &e)
            {
                std::cout << "Invalid input. Please enter a valid integer.\n";
                continue;
            }
            catch (const std::out_of_range &e)
            {
                std::cout << "Input out of range. Please enter a valid integer.\n";
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
            std::cout << "Error: Name cannot be whitespace-only\n";
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

    return TorrentConfig(path, output, trackers, tv,
                         comment.empty() ? std::nullopt : std::optional<std::string>(comment),
                         is_private, web_seeds, piece_size, creator_str, torrent_name,
                         include_creation_date, source, entropy
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

    // Get trackers (before output, needed for auto-naming)
    std::vector<std::string> trackers;
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
    std::string version = result["torrent-version"].as<std::string>();
    TorrentVersion tv = TorrentVersion::V1;
    if (version == "2")
        tv = TorrentVersion::V2;
    else if (version == "3")
        tv = TorrentVersion::HYBRID;

    // Get optional comment
    std::optional<std::string> comment = std::nullopt;
    if (result.count("comment"))
    {
        comment = result["comment"].as<std::string>();
    }

    // Get optional torrent name
    std::optional<std::string> torrent_name = std::nullopt;
    if (result.count("name")) {
        torrent_name = result["name"].as<std::string>();
        if (torrent_name->find_first_not_of(" \t\n\r") == std::string::npos) {
            throw std::runtime_error("Torrent name cannot be empty or whitespace-only");
        }
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
            std::cerr << "Error: Invalid piece size. Must be one of: ";
            for (size_t i = 0; i < allowed_piece_sizes.size(); ++i)
            {
                std::cerr << allowed_piece_sizes[i];
                if (i < allowed_piece_sizes.size() - 1)
                {
                    std::cerr << ", ";
                }
            }
            std::cerr << " KB\n";
            throw std::runtime_error("Invalid piece size");
        }
    }

    // Get creator string
    std::optional<std::string> creator_str = std::nullopt;
    if (result.count("creator"))
    {
        creator_str = "Torrent Builder";
    }

    // Get creation date flag
    bool include_creation_date = result.count("creation-date") > 0;

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

    // Get entropy flag
    bool entropy = result.count("entropy") > 0;

    try
    {
        return TorrentConfig(input_path, output_path, trackers, tv,
                             comment, result.count("private") > 0, web_seeds, piece_size,
                             creator_str, torrent_name, include_creation_date,
                             source, entropy
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
            std::cout << inspect_options.help() << "\n";
            std::cout << "\nExamples:\n";
            std::cout << "  torrent-builder inspect file.torrent\n";
            std::cout << "  torrent-builder inspect file.torrent --json\n";
            std::cout << "  torrent-builder inspect file.torrent --files\n";
            std::cout << "  torrent-builder inspect file.torrent --verify --base-path /data\n";
            return 0;
        }

        std::string torrent_path = result["path"].as<std::string>();
        TorrentInspector inspector(torrent_path);
        TorrentMetadata metadata = inspector.inspect();

        if (result.count("files"))
        {
            bool json_output = result.count("json") > 0;
            std::cout << TorrentInspector::format_file_tree(metadata, json_output);
        }
        else if (result.count("verify"))
        {
            std::string base_path = result["base-path"].as<std::string>();
            bool verified = inspector.verify_files(base_path);
            if (verified)
            {
                std::cout << "✓ All files verified successfully\n";
                return 0;
            }
            else
            {
                std::cerr << "✗ Verification failed - missing or corrupt files\n";
                return 1;
            }
        }
        else
        {
            bool json_output = result.count("json") > 0;
            std::cout << TorrentInspector::format_metadata(metadata, json_output);
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
// Main function
int main(int argc, char *argv[])
{
    // Check for subcommands
    if (argc >= 2 && std::string(argv[1]) == "inspect")
    {
        std::vector<std::string> args;
        for (int i = 2; i < argc; ++i)
        {
            args.push_back(argv[i]);
        }
        return handle_inspect_command(args);
    }

    try
    {
        // Define command-line options
        cxxopts::Options options("torrent_builder", "Create torrent files");
        options.add_options()("h,help", "Show help")("v,version", "Show version")(
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
                   cxxopts::value<int>(), "SIZE")("creator", "Set \"Torrent Builder\" as creator")(
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
            "e,entropy", "Randomize info hash by adding entropy field");

        options.positional_help("PATH [OUTPUT]");
        options.parse_positional({"path", "output"});
        auto result = options.parse(argc, argv);

        // Help and version
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
            std::cout << "\nNote: Allowed piece sizes (in KB): 16, 32, 64, 128, 256, 512, 1024, "
                         "2048, 4096, 8192, 16384, 32768\n";
            return 0;
        }

        // Run in interactive or command-line mode based on arguments
        if (result.count("interactive"))
        {
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
                log_message("User declined to overwrite: " + declined_path, LogLevel::INFO);
                std::cout << "Operation cancelled.\n";
                return 0;
            }
            TorrentCreator creator(*config_opt);
            creator.create_torrent();
        }
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        log_message(std::string("Filesystem error: ") + e.what(), LogLevel::ERR);
        std::cerr << e.what() << std::endl;
        return 1;
    }
    catch (const std::invalid_argument &e)
    {
        log_message(std::string("Invalid argument: ") + e.what(), LogLevel::ERR);
        std::cerr << e.what() << std::endl;
        return 1;
    }
    catch (const UserInterrupt &e)
    {
        log_message(std::string(e.what()), LogLevel::WARNING);
        std::cerr << e.what() << std::endl;
        return 1;
    }
    catch (const std::runtime_error &e)
    {
        log_message(std::string("Runtime error: ") + e.what(), LogLevel::ERR);
        std::cerr << e.what() << std::endl;
        return 1;
    }
    catch (const std::exception &e)
    {
        log_message(std::string("Unexpected error: ") + e.what(), LogLevel::ERR);
        std::cerr << "An unexpected error occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
