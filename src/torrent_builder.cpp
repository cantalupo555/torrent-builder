#include "torrent_creator.hpp"
#include <iostream>
#include <vector>
#include <optional>
#include <cxxopts.hpp>
#include <filesystem>

namespace fs = std::filesystem;

std::vector<std::string> default_trackers = {
    "udp://open.stealth.si:80/announce",
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://tracker.torrent.eu.org:451/announce",
    "udp://explodie.org:6969/announce",
    "udp://tracker.cyberia.is:6969/announce",
    "udp://retracker.hotplug.ru:2710/announce"
};

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
            std::string overwrite;
            std::cout << "File " << output << " already exists. Overwrite? (y/N): ";
            std::getline(std::cin, overwrite);
            if (overwrite != "y" && overwrite != "Y") {
                continue;
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

    // Get version
    std::string version;
    std::cout << "Torrent version (1-v1, 2-v2, 3-Hybrid) [3]: ";
    std::getline(std::cin, version);
    if (version.empty()) version = "3";
    
    TorrentVersion tv = TorrentVersion::V1;
    if (version == "2") tv = TorrentVersion::V2;
    else if (version == "3") tv = TorrentVersion::HYBRID;

    // Get comment
    std::string comment;
    std::cout << "Comment (optional): ";
    std::getline(std::cin, comment);

    // Get private flag
    std::string priv;
    std::cout << "Private torrent? (y/N): ";
    std::getline(std::cin, priv);
    bool is_private = (priv == "y" || priv == "Y");

    // Get web seeds
    std::vector<std::string> web_seeds;
    while (true) {
        std::string seed;
        std::cout << "Add web seed (leave blank to finish): ";
        std::getline(std::cin, seed);
        if (seed.empty()) break;
        web_seeds.push_back(seed);
    }

    return TorrentConfig(
        path,
        output,
        default_trackers,
        tv,
        comment.empty() ? std::nullopt : std::optional<std::string>(comment),
        is_private,
        web_seeds
    );
}

TorrentConfig get_commandline_config(const cxxopts::ParseResult& result) {
    if (!result.count("path")) {
        throw std::runtime_error("Path is required");
    }
    if (!result.count("output")) {
        throw std::runtime_error("Output path is required");
    }

    std::string version = result["version"].as<std::string>();
    TorrentVersion tv = TorrentVersion::V1;
    if (version == "2") tv = TorrentVersion::V2;
    else if (version == "3") tv = TorrentVersion::HYBRID;

    std::optional<std::string> comment = std::nullopt;
    if (result.count("comment")) {
        comment = result["comment"].as<std::string>();
    }

    std::vector<std::string> web_seeds;
    if (result.count("webseed")) {
        web_seeds = result["webseed"].as<std::vector<std::string>>();
    }

    return TorrentConfig(
        result["path"].as<std::string>(),
        result["output"].as<std::string>(),
        default_trackers,
        tv,
        comment,
        result.count("private"),
        web_seeds
    );
}

int main(int argc, char* argv[]) {
    try {
        cxxopts::Options options("torrent_builder", "Create torrent files");
        options.add_options()
            ("h,help", "Show help")
            ("i,interactive", "Run in interactive mode")
            ("path", "Path to file or directory", cxxopts::value<std::string>(), "PATH")
            ("output", "Output torrent file path", cxxopts::value<std::string>(), "OUTPUT")
            ("version", "Torrent version (1=v1, 2=v2, 3=hybrid)", cxxopts::value<std::string>()->default_value("3"), "{1,2,3}")
            ("comment", "Torrent comment", cxxopts::value<std::string>(), "COMMENT")
            ("private", "Make torrent private")
            ("webseed", "Add web seed URL", cxxopts::value<std::vector<std::string>>(), "URL")
        ;

        options.positional_help("PATH OUTPUT");
        options.parse_positional({"path", "output"});
        auto result = options.parse(argc, argv);

        if (result.count("help") || argc == 1) {
            std::cout << options.help() << "\n";
            std::cout << "Examples:\n";
            std::cout << "  ./torrent_builder --path /data/file --output file.torrent\n";
            std::cout << "  ./torrent_builder --path /data/folder --output folder.torrent --version 2 --private\n";
            std::cout << "  ./torrent_builder -i\n";
            return 0;
        }

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
