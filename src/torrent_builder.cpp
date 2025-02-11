#include "torrent_creator.hpp"
#include <iostream>
#include <vector>
#include <optional>
#include <cxxopts.hpp>
#include <filesystem>
#include <cmath>
#include <regex>
#include <ranges> // Adicione este include

namespace fs = std::filesystem;

// Default trackers to be used if none are provided
std::vector<std::string> default_trackers = {
    "udp://open.stealth.si:80/announce",
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://tracker.torrent.eu.org:451/announce",
    "udp://explodie.org:6969/announce",
    "udp://tracker.cyberia.is:6969/announce",
    "udp://retracker.hotplug.ru:2710/announce"
};

// List of allowed piece sizes (in KB). These are powers of 2
const std::vector<int> allowed_piece_sizes = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};

// Function to validate URLs. Checks if the URL starts with http://, https://, or udp://
bool is_valid_url(const std::string& url) {
    // Simple regular expression to validate URLs.
    static const std::regex url_regex(R"(^(http|https|udp)://.+$)", std::regex::icase);
    return std::regex_match(url, url_regex);
}

// Get torrent configuration from user input (interactive mode)
TorrentConfig get_interactive_config() {
    std::cout << "=== TORRENT CONFIGURATION ===" << std::endl;

    // Get input path
    std::string path;
    while (true) {
        std::cout << "Path to file or directory: ";
        std::getline(std::cin, path);
        if (path.empty()) {
            std::cout << "Error: Input path cannot be empty\n";
            continue;
        }
        try {
            // Check if the path exists
            if (!fs::exists(path)) {
                std::cout << "Error: Path does not exist\n";
                continue;
            }
            // Check if we have read permissions
            fs::file_status status = fs::status(path);
            if ((status.permissions() & fs::perms::owner_read) == fs::perms::none) {
                std::cout << "Error: No read permissions for path\n";
                continue;
            }
            break;
        } catch (const fs::filesystem_error& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }

    // Get output path
    std::string output;
    while (true) {
        std::cout << "Path to save torrent: ";
        std::getline(std::cin, output);
        if (output.empty()) {
            std::cout << "Error: Output path cannot be empty\n";
            continue;
        }
        if (output.size() < 8 || output.substr(output.size() - 8) != ".torrent") {
            std::cout << "Error: Output path must end with '.torrent'\n";
            continue;
        }

        // Check if file exists and prompt for overwrite
        if (fs::exists(output)) {
            while(true) { // Loop for overwrite validation
                std::string overwrite;
                std::cout << "File " << output << " already exists. Overwrite? (y/N): ";
                std::getline(std::cin, overwrite);
                if (overwrite == "y" || overwrite == "Y") {
                    break; // Exit overwrite loop
                } else if (overwrite == "n" || overwrite == "N" || overwrite.empty()) {
                    goto get_version; // Exit the inner loop, continue to next question
                } else {
                    std::cout << "Error: Invalid input. Please enter 'y' or 'n'.\n";
                }
            }
        }

        // Check if parent directory exists and is writable
        try {
            fs::path parent_dir = fs::path(output).parent_path();
            if (!parent_dir.empty() && !fs::exists(parent_dir)) {
                std::cout << "Error: Parent directory does not exist\n";
                continue;
            }
            // Check write permissions
            fs::file_status status = fs::status(parent_dir);
            if ((status.permissions() & fs::perms::owner_write) == fs::perms::none) {
                std::cout << "Error: No write permissions for directory\n";
                continue;
            }
            break;
        } catch (const fs::filesystem_error& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }

    get_version: // Label to jump to if overwrite is 'n' or empty
    // Get torrent version
    std::string version;
    TorrentVersion tv = TorrentVersion::V1; // Initialize tv here
    while(true){
        std::cout << "Torrent version (1-v1, 2-v2, 3-Hybrid) [3]: ";
        std::getline(std::cin, version);
        if (version.empty() || version == "3") {
            tv = TorrentVersion::HYBRID;
            break;
        } else if (version == "1") {
            tv = TorrentVersion::V1;
            break;
        } else if (version == "2") {
            tv = TorrentVersion::V2;
            break;
        } else{
            std::cout << "Error: Invalid input. Please enter '1', '2', or '3'.\n";
        }
    }

    // Get optional comment
    std::string comment;
    std::cout << "Comment (optional): ";
    std::getline(std::cin, comment);

    // Get private flag
    bool is_private = false;
    while (true) { // Loop for private flag validation
        std::string priv;
        std::cout << "Private torrent? (y/N): ";
        std::getline(std::cin, priv);
        if (priv == "y" || priv == "Y") {
            is_private = true;
            break;
        } else if (priv == "n" || priv == "N" || priv.empty()) {
            break;
        } else {
            std::cout << "Error: Invalid input. Please enter 'y' or 'n'.\n";
        }
    }

    // Get trackers
    std::vector<std::string> trackers;
    while(true){ // Loop for Use default trackers
        std::string use_default;
        std::cout << "Use default trackers? (y/N): ";
        std::getline(std::cin, use_default);
        if (use_default == "y" || use_default == "Y") {
            trackers.insert(trackers.end(), default_trackers.begin(), default_trackers.end());
            break;
        } else if (use_default == "n" || use_default == "N" || use_default.empty()) {
            break;
        }
        else {
            std::cout << "Error: Invalid input. Please enter 'y' or 'n'.\n";
        }
    }

    while(true){ // Loop for Add custom trackers
        std::string add_trackers;
        std::cout << "Add custom trackers? (y/N): ";
        std::getline(std::cin, add_trackers);
        if (add_trackers == "y" || add_trackers == "Y") {
            while (true) {
                std::string tracker;
                std::cout << "Add tracker (leave blank to finish): ";
                std::getline(std::cin, tracker);
                if (tracker.empty()) break;

                if (!is_valid_url(tracker)) {
                    std::cout << "Error: Invalid tracker URL. Must start with http://, https://, or udp://\n";
                    continue;
                }

                // Check if the tracker already exists using std::ranges::contains
                if (std::ranges::contains(trackers, tracker)) {
                    std::cout << "Error: Tracker already added.\n";
                    continue;
                }

                trackers.push_back(tracker);
            }
            break;
        } else if (add_trackers == "n" || add_trackers == "N" || add_trackers.empty()) {
            break;
        }
        else{
            std::cout << "Error: Invalid input. Please enter 'y' or 'n'.\n";
        }
    }

    // Get web seeds
    std::vector<std::string> web_seeds;
    while (true) {
        std::string seed;
        std::cout << "Add web seed (leave blank to finish): ";
        std::getline(std::cin, seed);
        if (seed.empty()) break;

        if (!is_valid_url(seed)) {
            std::cout << "Error: Invalid web seed URL. Must start with http://, https://, or udp://\n";
            continue;
        }

        web_seeds.push_back(seed);
    }

    // Get piece size
    std::optional<int> piece_size = std::nullopt;
    while(true){ // Loop for Set custom piece size
        std::string set_piece_size;
        std::cout << "Set custom piece size? (y/N): ";
        std::getline(std::cin, set_piece_size);
        if (set_piece_size == "y" || set_piece_size == "Y") {
            while (true) { // Loop to repeat the question
                std::string piece_size_str;
                std::cout << "Piece size in KB: \n";

                // Show valid options
                std::cout << "Valid options: ";
                for (size_t i = 0; i < allowed_piece_sizes.size(); ++i) {
                    std::cout << allowed_piece_sizes[i];
                    if (i < allowed_piece_sizes.size() - 1) {
                        std::cout << ", ";
                    }
                }
                std::cout << "\n";

                std::getline(std::cin, piece_size_str);
                if (piece_size_str.empty()) {
                    break; // Exit the inner loop if input is empty
                }

                try {
                    int ps = std::stoi(piece_size_str);
                    // Validate using std::ranges::contains
                    if (std::ranges::contains(allowed_piece_sizes, ps)) {
                        piece_size = ps * 1024; // Store in bytes
                        break; // Exit the loop if input is valid
                    } else {
                        std::cout << "Error: Invalid piece size. Please enter a valid option.\n";
                    }
                } catch (const std::exception& e) {
                    std::cout << "Error: Invalid input. Please enter a number.\n";
                }
            }
            break; // Exit the outer piece_size loop
        } else if (set_piece_size == "n" || set_piece_size == "N" || set_piece_size.empty()) {
            break;
        }
        else{
            std::cout << "Error: Invalid input. Please enter 'y' or 'n'.\n";
        }
    }

    // Get creator
    std::string set_creator;
    std::optional<std::string> creator_str = std::nullopt;
    while (true) { // Loop to repeat the question
        std::cout << "Set \"Torrent Builder\" as creator? (y/N): ";
        std::getline(std::cin, set_creator);
        if (set_creator == "y" || set_creator == "Y") {
            creator_str = "Torrent Builder";
            break;
        } else if (set_creator == "n" || set_creator == "N" || set_creator.empty()) {
            break;
        } else {
            std::cout << "Error: Invalid input. Please enter 'y' or 'n'.\n";
        }
    }

    // Get creation date
    bool include_creation_date = false;
    while (true) { // Loop to repeat the question
        std::string set_creation_date;
        std::cout << "Set creation date? (y/N): ";
        std::getline(std::cin, set_creation_date);
        if (set_creation_date == "y" || set_creation_date == "Y") {
            include_creation_date = true;
            break;
        } else if (set_creation_date == "n" || set_creation_date == "N" || set_creation_date.empty()) {
             break;
        } else{
            std::cout << "Error: Invalid input. Please enter 'y' or 'n'.\n";
        }
    }

    return TorrentConfig(
        path,
        output,
        trackers,
        tv,
        comment.empty() ? std::nullopt : std::optional<std::string>(comment),
        is_private,
        web_seeds,
        piece_size,
        creator_str,
        include_creation_date

    );
}

// Get torrent configuration from command line arguments
TorrentConfig get_commandline_config(const cxxopts::ParseResult& result) {
    if (!result.count("path")) {
        throw std::runtime_error("Path is required");
    }
    if (!result.count("output")) {
        throw std::runtime_error("Output path is required");
    }

    // --- Start of modification: Overwrite check ---
    std::string output_path = result["output"].as<std::string>();
    if (fs::exists(output_path)) {
        while (true) {
            std::string overwrite;
            std::cout << "File " << output_path << " already exists. Overwrite? (y/N): ";
            std::getline(std::cin, overwrite);
            if (overwrite == "y" || overwrite == "Y") {
                break; // Allow overwrite
            } else if (overwrite == "n" || overwrite == "N" || overwrite.empty()) {
                // Do not allow overwrite: throw exception
                throw std::runtime_error("Output file already exists. User chose not to overwrite.");
            } else {
                std::cout << "Error: Invalid input. Please enter 'y' or 'n'.\n";
            }
        }
    }
    // --- End of modification ---

    // Get torrent version
    std::string version = result["version"].as<std::string>();
    TorrentVersion tv = TorrentVersion::V1;
    if (version == "2") tv = TorrentVersion::V2;
    else if (version == "3") tv = TorrentVersion::HYBRID;

    // Get optional comment
    std::optional<std::string> comment = std::nullopt;
    if (result.count("comment")) {
        comment = result["comment"].as<std::string>();
    }

   // Get trackers
    std::vector<std::string> trackers;
    bool use_default = result.count("default-trackers") > 0;

    if (use_default) {
        trackers.insert(trackers.end(), default_trackers.begin(), default_trackers.end());
    }
    if (result.count("tracker")) {
        std::vector<std::string> custom_trackers = result["tracker"].as<std::vector<std::string>>();
        for (const auto& tracker : custom_trackers) {
            if (!is_valid_url(tracker)) {
                throw std::runtime_error("Invalid tracker URL: " + tracker);
            }
            // Check for duplicates using std::ranges::contains
            if (std::ranges::contains(trackers, tracker)) {
                throw std::runtime_error("Duplicate tracker URL: " + tracker);
            }
        }
        trackers.insert(trackers.end(), custom_trackers.begin(), custom_trackers.end());
    }

    // Get web seeds
    std::vector<std::string> web_seeds;
    if (result.count("webseed")) {
        web_seeds = result["webseed"].as<std::vector<std::string>>();
        for (const auto& webseed : web_seeds) {
            if (!is_valid_url(webseed)) {
                throw std::runtime_error("Invalid web seed URL: " + webseed);
            }
        }
    }

    // Get and validate piece size
    std::optional<int> piece_size = std::nullopt;
    if (result.count("piece-size")) {
        int ps = result["piece-size"].as<int>();
        // Validate using std::ranges::contains
        if (std::ranges::contains(allowed_piece_sizes, ps)) {
            piece_size = ps * 1024; // Store in bytes
        } else {
            std::cerr << "Error: Invalid piece size. Must be one of: ";
            for (size_t i = 0; i < allowed_piece_sizes.size(); ++i) {
                std::cerr << allowed_piece_sizes[i];
                if (i < allowed_piece_sizes.size() - 1) {
                    std::cerr << ", ";
                }
            }
            std::cerr << " KB\n";
            throw std::runtime_error("Invalid piece size");
        }
    }

    // Get creator string
    std::optional<std::string> creator_str = std::nullopt;
    if (result.count("creator")) {
        creator_str = "Torrent Builder";
    }

     // Get creation date flag
    bool include_creation_date = result.count("creation-date") > 0;


    return TorrentConfig(
         result["path"].as<std::string>(),
        output_path, // Use the validated output path
        trackers, // Use user-provided trackers
        tv,
        comment,
        result.count("private"),
        web_seeds,
        piece_size,
        creator_str, // Pass creator string
        include_creation_date // Pass creation date flag
    );
}

// Main function
int main(int argc, char* argv[]) {
    try {
        // Define command-line options
        cxxopts::Options options("torrent_builder", "Create torrent files");
        options.add_options()
            ("h,help", "Show help")
            ("i,interactive", "Run in interactive mode")
            ("path", "Path to file or directory", cxxopts::value<std::string>(), "PATH")
            ("output", "Output torrent file path", cxxopts::value<std::string>(), "OUTPUT")
            ("version", "Torrent version (1=v1, 2=v2, 3=hybrid)", cxxopts::value<std::string>()->default_value("3"), "{1,2,3}")
            ("comment", "Torrent comment", cxxopts::value<std::string>(), "COMMENT")
            ("private", "Make torrent private")
            ("default-trackers", "Use default trackers")
            ("tracker", "Add tracker URL", cxxopts::value<std::vector<std::string>>(), "URL")
            ("webseed", "Add web seed URL", cxxopts::value<std::vector<std::string>>(), "URL")
            ("piece-size", "Piece size in KB", cxxopts::value<int>(), "SIZE")
            ("creator", "Set \"Torrent Builder\" as creator")
            ("creation-date", "Set creation date")
            ;

        options.positional_help("PATH OUTPUT");
        options.parse_positional({"path", "output"});
        auto result = options.parse(argc, argv);

        // Help and version
        if (result.count("help") || argc == 1) {
            std::cout << options.help() << "\n";
            std::cout << "Examples:\n";
            // Reordered examples here:
            std::cout << "  ./torrent_builder -i\n";
            std::cout << "  ./torrent_builder --path /data/file --output file.torrent\n";
            std::cout << "  ./torrent_builder --path /data/file --output file.torrent --default-trackers\n";
            std::cout << "  ./torrent_builder --path /data/folder --output folder.torrent --version 2 --private\n";
            std::cout << "  ./torrent_builder --path /data/file --output file.torrent --piece-size 1024\n";
            return 0;
        }

        // Run in interactive or command-line mode based on arguments
        if (result.count("interactive")) {
            auto config = get_interactive_config();
            TorrentCreator creator(config);
            creator.create_torrent();
        } else {
            auto config = get_commandline_config(result);
            TorrentCreator creator(config);
            creator.create_torrent();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
