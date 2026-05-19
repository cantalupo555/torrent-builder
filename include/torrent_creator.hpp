#ifndef CREATE_TORRENT_HPP
#define CREATE_TORRENT_HPP

#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/session.hpp>

#include "logger.hpp"
#include "terminal.hpp"
#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <vector>
#include <optional>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <regex>

namespace fs = std::filesystem;

extern const std::vector<std::string> default_trackers;

enum class TorrentVersion {
    V1,
    V2,
    HYBRID
};

struct TorrentConfig {
    fs::path path;
    fs::path output;
    std::vector<std::string> trackers;
    TorrentVersion version;
    std::optional<std::string> comment;
    bool is_private;
    std::vector<std::string> web_seeds;
    std::optional<int> piece_size;
    std::optional<std::string> creator;
    std::optional<std::string> name;              // Overrides default name inferred from path
    bool include_creation_date;
    std::optional<std::string> source;            // Cross-seeding identity (sets info.source)
    bool entropy;                                 // Randomize info hash per invocation
    std::vector<std::regex> exclude_regex;        // Pre-compiled exclude patterns
    std::vector<std::regex> include_regex;        // Pre-compiled include patterns (overrides exclude)

    /**
     * @brief Construct a torrent configuration with all creation parameters.
     * @param p Path to the file or directory to include.
     * @param o Output path for the generated .torrent file.
     * @param t List of tracker announce URLs.
     * @param v Torrent protocol version (V1, V2, or HYBRID).
     * @param c Optional comment string.
     * @param priv Whether the torrent is private (disables DHT/PEX).
     * @param ws List of web seed URLs.
     * @param ps Optional piece size in bytes (std::nullopt = auto-calculated).
     * @param creator Optional creator string.
     * @param name_val Optional custom torrent name (overrides name from path).
     * @param include_creation_date Whether to embed the current timestamp.
     * @param source Source string for cross-seeding (sets info.source).
     * @param entropy Add random entropy field for unique info hash.
     * @param exclude_re Pre-compiled regex patterns for file exclusion.
     * @param include_re Pre-compiled regex patterns for file inclusion (overrides exclude).
     * @throws std::filesystem::filesystem_error if the path does not exist.
     */
    TorrentConfig(fs::path p, fs::path o, std::vector<std::string> t,
                 TorrentVersion v, std::optional<std::string> c = std::nullopt,
                 bool priv = false, std::vector<std::string> ws = {}, std::optional<int> ps = std::nullopt,
                 std::optional<std::string> creator = std::nullopt,
                 std::optional<std::string> name_val = std::nullopt,
                 bool include_creation_date = false,
                 std::optional<std::string> source = std::nullopt,
                 bool entropy = false,
                 std::vector<std::regex> exclude_re = {},
                 std::vector<std::regex> include_re = {})
        : path(p), output(o), trackers(t), version(v),
          comment(c), is_private(priv), web_seeds(ws), piece_size(ps),
          creator(creator), name(name_val), include_creation_date(include_creation_date),
          source(source), entropy(entropy),
          exclude_regex(std::move(exclude_re)),
          include_regex(std::move(include_re))
    {
        // Validate that the provided path exists
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            throw std::filesystem::filesystem_error("The specified path does not exist", path, ec);
        }
    }
};

class TorrentCreator {
public:
    /**
     * @brief Construct a torrent creator with the given configuration.
     * @param config Fully populated TorrentConfig.
     */
    explicit TorrentCreator(TorrentConfig config);

    /**
     * @brief Create and save a .torrent file according to the configuration.
     *
     * Orchestrates file scanning, piece hashing (single-threaded or parallel
     * based on file size), metadata assembly, and disk write.
     *
     * @throws std::runtime_error on hashing, I/O, or configuration errors.
     * @throws UserInterrupt if the user presses 'q' or hashing times out.
     */
    void create_torrent();
    /**
     * @brief Get libtorrent creation flags for a given torrent version.
     * @param version V1 → v1_only, V2 → v2_only, HYBRID → no flags (both v1+v2).
     */
    static lt::create_flags_t get_torrent_flags(TorrentVersion version);

private:
    TorrentConfig config_;
    lt::file_storage fs_;
    lt::session ses;              // Needed for libtorrent async hashing alerts
    std::mutex progress_mutex_;   // Guards total_processed_ across worker threads
    int64_t total_processed_ = 0;

    void add_files_to_storage();
    void print_torrent_summary(int64_t total_size, int piece_size, int num_pieces) const;
    void print_progress_bar(int progress, int total, double speed, double eta, int64_t processed, int64_t total_size) const;
    void hash_large_file(const fs::path& path, lt::create_torrent& t, int piece_size, TerminalGuard& guard);
    void hash_large_file_parallel(const fs::path& path, lt::create_torrent& t, int piece_size, TerminalGuard& guard);
    void hash_block(const fs::path& path, lt::create_torrent& t, int piece_size, int64_t start_offset, int64_t end_offset, std::mutex& mutex, std::atomic<bool>& cancel);
};

#endif // CREATE_TORRENT_HPP
