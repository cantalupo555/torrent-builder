#ifndef CREATE_TORRENT_HPP
#define CREATE_TORRENT_HPP

#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/error_code.hpp>

#include "logger.hpp"
#include "output_sink.hpp"
#include "terminal.hpp"
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
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
    std::optional<int> target_piece_count;         ///< Original target (for display only; piece_size holds resolved value)
    std::optional<std::string> creator;
    std::optional<std::string> name;              // Overrides default name inferred from path
    bool include_creation_date = true;
    std::optional<std::string> source;            // Cross-seeding identity (sets info.source)
    bool entropy;                                 // Randomize info hash per invocation
    std::vector<std::regex> exclude_regex;        // Pre-compiled exclude patterns
    std::vector<std::regex> include_regex;        // Pre-compiled include patterns (overrides exclude)
    bool silent;                                   // Suppress progress output (batch mode)

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
     * @param tpc Optional target piece count (for display; piece_size holds resolved value).
     * @throws std::filesystem::filesystem_error if the path does not exist.
     */
    TorrentConfig(fs::path p, fs::path o, std::vector<std::string> t,
                 TorrentVersion v, std::optional<std::string> c = std::nullopt,
                 bool priv = false, std::vector<std::string> ws = {}, std::optional<int> ps = std::nullopt,
                 std::optional<std::string> creator = "Torrent Builder",
                 std::optional<std::string> name_val = std::nullopt,
                 bool include_creation_date = true,
                 std::optional<std::string> source = std::nullopt,
                 bool entropy = false,
                 std::vector<std::regex> exclude_re = {},
                 std::vector<std::regex> include_re = {},
                 bool silent = false,
                 std::optional<int> tpc = std::nullopt)
        : path(p), output(o), trackers(t), version(v),
          comment(c), is_private(priv), web_seeds(ws), piece_size(ps),
          target_piece_count(tpc),
          creator(creator), name(name_val), include_creation_date(include_creation_date),
          source(source), entropy(entropy),
          exclude_regex(std::move(exclude_re)),
          include_regex(std::move(include_re)),
          silent(silent)
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
     * @param sink Output sink for progress and status messages. Defaults to
     *             the process-wide stdout sink (CLI behavior). Inject a
     *             custom sink (e.g. QtOutputSink) for GUI usage.
     * @param should_cancel Optional cancellation predicate. When provided and
     *                      returning true, create_torrent() aborts hashing and
     *                      throws UserInterrupt — same as pressing 'q' on the
     *                      CLI. Lets a GUI without a TTY cancel a long hash.
     *                      If empty (default), only the CLI TerminalGuard can
     *                      interrupt.
     */
    explicit TorrentCreator(TorrentConfig config,
                            OutputSink& sink = default_stdout_sink(),
                            std::function<bool()> should_cancel = {});

    /**
     * @brief Create and save a .torrent file according to the configuration.
     *
     * Orchestrates file scanning, piece hashing (single-threaded or parallel
     * based on file size), metadata assembly, and disk write.
     *
     * @throws std::runtime_error on hashing, I/O, or configuration errors.
     * @throws UserInterrupt if the user presses 'q', cancels via the
     *         should_cancel predicate, or hashing times out.
     */
    void create_torrent();
    /**
     * @brief Get libtorrent creation flags for a given torrent version.
     * @param version V1 → v1_only, V2 → v2_only, HYBRID → no flags (both v1+v2).
     */
    static lt::create_flags_t get_torrent_flags(TorrentVersion version);

private:
    TorrentConfig config_;
    OutputSink& sink_;
    std::function<bool()> should_cancel_;
    lt::file_storage fs_;
    std::mutex progress_mutex_;   // Guards total_processed_ across worker threads
    int64_t total_processed_ = 0;

    /// Emits a structured progress update via the injected sink (no-op when silent).
    void emit_progress(int pieces_done, int pieces_total, double speed, double eta,
                       int64_t bytes_done, int64_t bytes_total,
                       const std::string& current_file = {}) const;

    /// Signals the end of the transient progress line (no-op when silent).
    void end_progress() const;

    /**
     * @brief Report an error through the sink without ever throwing.
     *
     * Used inside catch handlers: a custom sink that throws from error() must
     * not mask the original exception (or replace it with a secondary one).
     */
    void report_error_noexcept(const std::string& msg) const noexcept;

    /// Resolves which file contains a byte offset, for progress display.
    /// Returns an empty string when the offset is out of range.
    std::string current_file_at_offset(int64_t offset) const;

    /// Returns true if cancellation has been requested (predicate or keypress).
    /// A throwing predicate is treated as a cancellation request.
    bool is_cancelled(TerminalGuard& guard) const noexcept;

    void add_files_to_storage();
    void print_torrent_summary(int64_t total_size, int piece_size, int num_pieces) const;
    void hash_large_file(const fs::path& path, lt::create_torrent& t, int piece_size, TerminalGuard& guard);
    void hash_large_file_parallel(const fs::path& path, lt::create_torrent& t, int piece_size, TerminalGuard& guard);
    void hash_block(const fs::path& path, lt::create_torrent& t, int piece_size, int64_t start_offset, int64_t end_offset, std::mutex& mutex, std::atomic<bool>& cancel);
};

#endif // CREATE_TORRENT_HPP
