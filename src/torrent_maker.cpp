#include "torrent_creator.hpp"
#include <iostream>
#include <vector>
#include <optional>

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
    std::cout << "Path to file or directory: ";
    std::getline(std::cin, path);
    if (path.empty()) {
        throw std::runtime_error("Input path cannot be empty");
    }

    // Get output path
    std::string output;
    while (true) {
        std::cout << "Path to save torrent: ";
        std::getline(std::cin, output);
        if (output.empty()) {
            throw std::runtime_error("Output path cannot be empty");
        }
        if (output.size() < 8 || output.substr(output.size() - 8) != ".torrent") {
            std::cout << "Error: Output path must end with '.torrent'\n";
            continue;
        }
        break;
    }

    // Get version
    std::string version;
    std::cout << "Torrent version (1-v1, 2-v2, 3-Hybrid) [1]: ";
    std::getline(std::cin, version);
    if (version.empty()) version = "1";
    
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

int main(int argc, char* argv[]) {
    try {
        if (argc > 1 && std::string(argv[1]) == "-i") {
            auto config = get_interactive_config();
            TorrentCreator creator(config);
            creator.create_torrent();
        } else {
            // TODO: Add command line argument parsing
            std::cerr << "Command line mode not implemented yet. Use -i for interactive mode.\n";
            return 1;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
