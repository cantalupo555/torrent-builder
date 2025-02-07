#ifndef CREATE_TORRENT_HPP
#define CREATE_TORRENT_HPP

#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/error_code.hpp>

#include <string>
#include <vector>
#include <optional>
#include <iostream>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// Enum for supported torrent versions
enum class TorrentVersion {
    V1,
    V2,
    HYBRID
};

// Struct to hold the configuration for creating a torrent
struct TorrentConfig {
    fs::path path;                   // Path to the file or directory to be included in the torrent
    fs::path output;                 // Path to save the generated .torrent file
    std::vector<std::string> trackers; // List of tracker URLs
    TorrentVersion version;          // Torrent version (V1, V2, or HYBRID)
    std::optional<std::string> comment; // Optional comment to be included in the torrent
    bool is_private;                 // Flag to indicate if the torrent is private
    std::vector<std::string> web_seeds; // List of web seed URLs
    std::optional<int> piece_size;    // Optional piece size in bytes

    // Constructor for TorrentConfig
    TorrentConfig(fs::path p, fs::path o, std::vector<std::string> t,
                 TorrentVersion v, std::optional<std::string> c = std::nullopt,
                 bool priv = false, std::vector<std::string> ws = {}, std::optional<int> ps = std::nullopt)
        : path(p), output(o), trackers(t), version(v),
          comment(c), is_private(priv), web_seeds(ws), piece_size(ps) // Initialize piece_size
    {
        // Validate that the provided path exists. Throws an exception if not
        if (!fs::exists(path)) {
            throw std::runtime_error("Invalid path: " + path.string());
        }
    }
};

class TorrentCreator {
public:
    // Constructor
    explicit TorrentCreator(const TorrentConfig& config);

    // Creates the torrent file
    void create_torrent();

private:
    TorrentConfig config_;       // Configuration for the torrent
    lt::file_storage fs_;        // libtorrent file storage object. Stores information about the files in the torrent

    // Calculates a suitable piece size automatically based on total file size
    static int auto_piece_size(int64_t total_size);
    // Gets the torrent flags based on the configured version
    int get_torrent_flags() const;
    // Adds files to the file storage
    void add_files_to_storage();
    // Prints a summary of the created torrent
    void print_torrent_summary(int64_t total_size, int piece_size, int num_pieces) const;
};

#endif // CREATE_TORRENT_HPP
