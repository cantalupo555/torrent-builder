#ifndef CREATE_TORRENT_HPP
#define CREATE_TORRENT_HPP

#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/error_code.hpp>
// Add the following includes:
#include <libtorrent/alert_types.hpp>
#include <libtorrent/session.hpp>

// Enum for log levels
enum class LogLevel {
    INFO,
    WARNING,
    ERROR
};

#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <vector>
#include <optional>
#include <iostream>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// Default trackers to be used if none are provided
extern std::vector<std::string> default_trackers;

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
    std::optional<std::string> creator; // Optional creator string
    bool include_creation_date;       // Flag to include creation date



    // Constructor for TorrentConfig
    TorrentConfig(fs::path p, fs::path o, std::vector<std::string> t,
                 TorrentVersion v, std::optional<std::string> c = std::nullopt,
                 bool priv = false, std::vector<std::string> ws = {}, std::optional<int> ps = std::nullopt,
                 std::optional<std::string> creator = std::nullopt, bool include_creation_date = false) // Add new parameters with default values
        : path(p), output(o), trackers(t), version(v),
          comment(c), is_private(priv), web_seeds(ws), piece_size(ps),
          creator(creator), include_creation_date(include_creation_date) // Initialize new members
    {
        // Validate that the provided path exists. Throws an exception if not
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            throw std::filesystem::filesystem_error("Error: The specified path does not exist. Please check the path and try again.", ec);
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
    lt::session ses; // Add a session object
    std::mutex progress_mutex_; // To synchronize progress between threads
    int64_t total_processed_ = 0; // Total bytes processed

    // Calculates a suitable piece size automatically based on total file size
    static int auto_piece_size(int64_t total_size);
    // Gets the torrent flags based on the configured version
    lt::create_flags_t get_torrent_flags() const;
    // Adds files to the file storage
    void add_files_to_storage();
    // Prints a summary of the created torrent
    void print_torrent_summary(int64_t total_size, int piece_size, int num_pieces) const;
    // Displays a progress bar
    void print_progress_bar(int progress, int total, double speed, double eta, int64_t processed, int64_t total_size) const;
    std::string format_size(int64_t bytes) const;
    std::string format_speed(double speed) const;
    std::string format_eta(double eta) const;
    // Hashes large files using streaming
    void hash_large_file(const fs::path& path, lt::create_torrent& t, int piece_size);
    // Parallel hashing
    void hash_large_file_parallel(const fs::path& path, lt::create_torrent& t, int piece_size);
    void hash_block(const fs::path& path, lt::create_torrent& t, int piece_size, int64_t start_offset, int64_t end_offset, std::mutex& mutex);
};

#endif // CREATE_TORRENT_HPP
