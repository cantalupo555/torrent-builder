#include "torrent_creator.hpp"
#include "logger.hpp"
#include "constants.hpp"
#include "utils.hpp"
#include "terminal.hpp"
#include "output.hpp"
#include <fstream>
#include <iomanip>
#include <chrono>
#include <format>
#include <libtorrent/version.hpp>
#include <cmath>
#include <ctime>
#include <thread>
#include <stdexcept>
#include <system_error>
#include <unordered_set>

// Constructor for TorrentCreator
TorrentCreator::TorrentCreator(TorrentConfig config,
                               OutputSink& sink,
                               std::function<bool()> should_cancel)
    : config_(std::move(config)),
      sink_(sink),
      should_cancel_(std::move(should_cancel)) {
}

void TorrentCreator::emit_progress(int pieces_done, int pieces_total, double speed, double eta,
                                   int64_t bytes_done, int64_t bytes_total,
                                   const std::string& current_file) const {
    if (config_.silent) return;
    try {
        sink_.progress(ProgressInfo{
            pieces_done, pieces_total, speed, eta, bytes_done, bytes_total, current_file
        });
    } catch (...) {
        // A misbehaving sink must not std::terminate a hashing worker thread.
    }
}

void TorrentCreator::end_progress() const {
    if (config_.silent) return;
    try {
        sink_.progress_line_end();
    } catch (...) {
        // Same rationale as emit_progress: never let a sink kill the worker.
    }
}

void TorrentCreator::report_error_noexcept(const std::string& msg) const noexcept {
    // A throwing sink must never mask the exception currently being handled
    // (or crash via a secondary throw while unwinding).
    try {
        sink_.error(msg);
    } catch (...) {
    }
}

std::string TorrentCreator::current_file_at_offset(int64_t offset) const {
    if (fs_.num_files() == 0 || offset < 0 || offset >= fs_.total_size()) {
        return {};
    }
    return fs_.file_path(fs_.file_index_at_offset(offset));
}

bool TorrentCreator::is_cancelled(TerminalGuard& guard) const noexcept {
    try {
        // GUI / programmatic cancellation: predicate injected at construction.
        if (should_cancel_ && should_cancel_()) {
            return true;
        }
        // CLI cancellation: TerminalGuard polls stdin (no-op when stdin is not a TTY).
        char c = 0;
        if (guard.check_key_press(c)) {
            return c == 'q' || c == 'Q' || c == '\x03';
        }
    } catch (...) {
        // Treat a throwing predicate/keyboard probe as a cancellation request
        // so hashing unwinds as UserInterrupt, not a foreign exception.
        return true;
    }
    return false;
}


lt::create_flags_t TorrentCreator::get_torrent_flags(TorrentVersion version) {
    lt::create_flags_t flags = {};

    switch(version) {
        case TorrentVersion::V1:
            flags |= lt::create_torrent::v1_only;
            break;
        case TorrentVersion::V2:
            flags |= lt::create_torrent::v2_only;
            break;
        case TorrentVersion::HYBRID:
            break;
    }

    return flags;
}

    void TorrentCreator::add_files_to_storage() {
        if (fs::is_directory(config_.path)) {
            const auto &exclude_regex = config_.exclude_regex;
            const auto &include_regex = config_.include_regex;

            int file_count_before = fs_.num_files();

            if (exclude_regex.empty() && include_regex.empty()) {
                lt::add_files(fs_, config_.path.string(), [](std::string const&) { return true; });
            } else {
                fs::path base = fs::absolute(config_.path);
                int dirs_excluded = 0;
                int files_excluded = 0;

                // lt::add_files callback signature is (std::string const&) — only a path string,
                // no file type information. is_directory() is mandatory to distinguish files from
                // directories for pruning. There is no alternative without replacing lt::add_files.
                lt::add_files(fs_, config_.path.string(),
                    [&base, &exclude_regex, &include_regex, &dirs_excluded, &files_excluded](std::string const &file_path) {
                        std::error_code ec;
                        fs::path rel = fs::path(file_path).lexically_relative(base);

                        // Fail-open: if lexically_relative fails (different mount points, symlinks),
                        // include the entry rather than silently dropping it. This guard is
                        // defense-in-depth — lt::add_files always passes paths within the base tree.
                        if (rel.empty()) {
                            log_message("Could not compute relative path for: " + file_path, LogLevel::WARNING);
                            return true;
                        }
                        std::string rel_str = rel.generic_string();
                        if (fs::is_directory(file_path, ec)) {
                            // When include patterns exist, never prune directories — their files
                            // must be checked individually against include rules.
                            if (!include_regex.empty()) return true;
                            std::string dir_path = rel_str + "/";
                            for (const auto &re : exclude_regex) {
                                if (std::regex_match(dir_path, re) || std::regex_match(rel_str, re)) {
                                    ++dirs_excluded;
                                    return false;
                                }
                            }
                            return true;
                        }
                        if (!utils::should_include_file(rel_str, exclude_regex, include_regex)) {
                            ++files_excluded;
                            log_message("Excluded by pattern: " + rel_str, LogLevel::INFO);
                            return false;
                        }
                        return true;
                    });

                int files_added = fs_.num_files() - file_count_before;
                std::string filter_summary = "Pattern filter: " + std::to_string(files_added) + " file(s) included";
                if (files_excluded > 0) filter_summary += ", " + std::to_string(files_excluded) + " file(s) excluded";
                if (dirs_excluded > 0) filter_summary += ", " + std::to_string(dirs_excluded) + " directory(ies) excluded";
                log_message(filter_summary);
            }

            if (fs_.num_files() == 0) {
                throw std::runtime_error("No files matched the specified patterns ("
                    + std::to_string(config_.exclude_regex.size()) + " exclude, "
                    + std::to_string(config_.include_regex.size()) + " include). "
                    "Torrent would be empty.");
            }
        } else {
            fs_.add_file(config_.path.filename().string(), fs::file_size(config_.path));
        }
    }

void TorrentCreator::deduplicate_pad_paths(lt::entry& torrent) {
    auto info_it = torrent.dict().find("info");
    if (info_it == torrent.dict().end()) return;
    auto files_it = info_it->second.dict().find("files");
    if (files_it == info_it->second.dict().end()) return;
    auto& files = files_it->second.list();

    auto join_components = [](const lt::entry::list_type& components) {
        std::string joined;
        for (const auto& component : components) {
            if (!joined.empty()) joined += "/";
            joined += component.string();
        }
        return joined;
    };

    // Collect every path up front so a generated suffix cannot collide with a
    // file that appears later in the list.
    std::unordered_set<std::string> taken;
    for (const auto& file : files) {
        auto path = file.dict().find("path");
        if (path == file.dict().end()) continue;
        taken.insert(utils::to_lower(join_components(path->second.list())));
    }

    std::unordered_set<std::string> seen;
    int renamed = 0;
    for (auto& file : files) {
        auto path_it = file.dict().find("path");
        if (path_it == file.dict().end()) continue;
        auto& components = path_it->second.list();
        if (components.empty()) continue;

        const std::string path = join_components(components);
        if (seen.insert(utils::to_lower(path)).second) continue;

        auto attr_it = file.dict().find("attr");
        if (attr_it == file.dict().end()
            || attr_it->second.string().find('p') == std::string::npos) {
            continue;
        }

        const std::string leaf = components.back().string();
        for (int suffix = 1; ; ++suffix) {
            const std::string candidate_leaf =
                leaf + "." + std::to_string(suffix);
            std::string candidate_path = path;
            const auto slash = candidate_path.rfind('/');
            if (slash == std::string::npos) {
                candidate_path = candidate_leaf;
            } else {
                candidate_path.replace(slash + 1, std::string::npos,
                                       candidate_leaf);
            }

            const std::string key = utils::to_lower(candidate_path);
            if (!taken.insert(key).second) continue;
            components.back().string() = candidate_leaf;
            ++renamed;
            break;
        }
    }

    if (renamed > 0) {
        const std::string msg = "Renamed " + std::to_string(renamed)
            + " duplicate pad path(s) to keep torrent paths unique";
        sink_.verbose(msg + "\n");
        log_message(msg, LogLevel::INFO);
    }
}

// Hashing with streaming for large files
void TorrentCreator::hash_large_file_parallel(const fs::path& path, lt::create_torrent& t, int piece_size, TerminalGuard& guard) {
    const int64_t file_size = fs::file_size(path);
    // Hashing is CPU-bound SHA-1, so matching thread count to physical cores
    // maximizes throughput without oversubscribing the CPU.
    const int num_threads = std::thread::hardware_concurrency();
    const int64_t block_size = (file_size / num_threads) + 1; // +1 prevents dropping last block on integer truncation

    std::vector<std::thread> threads;
    std::mutex mutex;
    std::atomic<bool> cancel{false};

    for (int i = 0; i < num_threads; ++i) {
        int64_t start_offset = i * block_size;
        int64_t end_offset = std::min(start_offset + block_size, file_size);

        threads.emplace_back(&TorrentCreator::hash_block, this, path, std::ref(t), piece_size, start_offset, end_offset, std::ref(mutex), std::ref(cancel));
    }

    // Monitor thread polls for 'q' keypress (or programmatic cancel) while
    // workers block on I/O; signals shutdown via cancel atomic so workers
    // can exit cleanly. is_cancelled() honors both TerminalGuard (CLI) and
    // the should_cancel_ predicate (GUI / programmatic).
    //
    // A throwing user predicate must not escape this thread — an uncaught
    // exception in a std::thread calls std::terminate. Treat a throw as a
    // cancel request so workers unwind through the normal UserInterrupt path.
    std::thread monitor([&guard, &cancel, this]() {
        while (!cancel.load()) {
            bool stop = false;
            try {
                stop = is_cancelled(guard);
            } catch (...) {
                stop = true;
            }
            if (stop) {
                cancel.store(true);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    for (auto& thread : threads) {
        thread.join();
    }

    bool interrupted = cancel.exchange(true);
    monitor.join();

    if (interrupted) {
        log_message("Process interrupted by user", LogLevel::WARNING);
        throw UserInterrupt("Process interrupted by user");
    }
}

void TorrentCreator::hash_block(const fs::path& path, lt::create_torrent& t, int piece_size, int64_t start_offset, int64_t end_offset, std::mutex& mutex, std::atomic<bool>& cancel) {
    const size_t buffer_size = PieceSizes::k16384KB; // Largest supported piece size; minimizes read syscalls
    std::vector<char> buffer(buffer_size);
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }

    file.seekg(start_offset); // Position the file at the start of the block

    int64_t bytes_processed = 0;
    lt::piece_index_t piece_index(static_cast<int>(start_offset / piece_size));
    lt::hasher piece_hasher;
    int bytes_in_current_piece = 0;
    
    int64_t file_size = fs::file_size(path);
    auto start_time = std::chrono::steady_clock::now();

    while (file && (start_offset + bytes_processed) < end_offset) {
        size_t bytes_to_read = std::min(buffer_size, static_cast<size_t>(end_offset - (start_offset + bytes_processed)));
        file.read(buffer.data(), bytes_to_read);
        size_t bytes_read = file.gcount();

        // Process the buffer
        size_t remaining = bytes_read;
        size_t offset = 0;

        while (remaining > 0) {
            size_t chunk = std::min(remaining, static_cast<size_t>(piece_size - bytes_in_current_piece));
            piece_hasher.update(buffer.data() + offset, chunk);
            offset += chunk;
            bytes_processed += chunk;
            bytes_in_current_piece += chunk;

            if (bytes_in_current_piece == piece_size) {
                std::lock_guard<std::mutex> lock(mutex); // Synchronize access to object `t`
                t.set_hash(piece_index, piece_hasher.final());
                piece_index = lt::piece_index_t(static_cast<int>(piece_index) + 1);
                piece_hasher.reset();
                bytes_in_current_piece = 0;
            }
        }

        // Update progress safely: mutate shared state under the lock, then
        // emit OUTSIDE it — a sink that synchronizes on a lock also taken by
        // this thread's monitor must never be invoked while holding
        // progress_mutex_ (deadlock hazard).
        int pieces_done_now = 0;
        int pieces_total_now = 0;
        int64_t bytes_done_now = 0;
        double speed = 0.0;
        double eta = 0.0;
        {
            std::lock_guard<std::mutex> lock(progress_mutex_);
            total_processed_ += bytes_read;
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            speed = elapsed > 0 ? static_cast<double>(total_processed_) / elapsed : 0.0;
            eta = speed > 0 ? (file_size - total_processed_) / speed : 0.0;
            pieces_done_now = static_cast<int>(total_processed_ / piece_size);
            pieces_total_now = static_cast<int>(file_size / piece_size);
            // Snapshot under the lock: other workers mutate total_processed_
            // concurrently, so the unsynchronized emit below must not read it.
            bytes_done_now = total_processed_;
        }

        emit_progress(pieces_done_now, pieces_total_now, speed, eta,
                      bytes_done_now, file_size,
                      config_.path.filename().string());

        if (cancel.load()) {
            return;
        }
    }

    // Finalize the last piece if necessary
    if (bytes_in_current_piece > 0) {
        std::lock_guard<std::mutex> lock(mutex);
        t.set_hash(piece_index, piece_hasher.final());
    }
}

void TorrentCreator::hash_large_file(const fs::path& path, lt::create_torrent& t, int piece_size, TerminalGuard& guard) {
    const size_t buffer_size = PieceSizes::k16384KB; // Largest supported piece size; minimizes read syscalls
    std::vector<char> buffer(buffer_size);
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }

    int64_t total_bytes = fs::file_size(path);
    int64_t bytes_processed = 0;
    lt::piece_index_t piece_index(0);
    lt::hasher piece_hasher;
    int bytes_in_current_piece = 0;

    auto start_time = std::chrono::steady_clock::now();
    double speed = 0.0;
    double eta = 0.0;

    while (file) {
        file.read(buffer.data(), buffer.size());
        size_t bytes_read = file.gcount();

        // Process buffer
        size_t remaining = bytes_read;
        size_t offset = 0;

        while (remaining > 0) {
            size_t chunk = std::min(remaining, static_cast<size_t>(piece_size - bytes_in_current_piece));
            piece_hasher.update(buffer.data() + offset, chunk);
            offset += chunk;
            remaining -= chunk;
            bytes_processed += chunk;
            bytes_in_current_piece += chunk;

            if (bytes_in_current_piece == piece_size) {
                t.set_hash(piece_index, piece_hasher.final());
                piece_index = lt::piece_index_t(static_cast<int>(piece_index) + 1);
                piece_hasher.reset();
                bytes_in_current_piece = 0;

                // Calculate speed and ETA
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                if (elapsed > 0) {
                    speed = static_cast<double>(bytes_processed) / elapsed; // bytes per second
                    double remaining_bytes = total_bytes - bytes_processed;
                    eta = remaining_bytes / speed; // in seconds
                }

                // Update progress bar
                emit_progress(static_cast<int>(bytes_processed / piece_size),
                                 static_cast<int>(total_bytes / piece_size),
                                 speed, eta, bytes_processed, total_bytes,
                                 config_.path.filename().string());
            }
        }

        // --- Check for timeout and user interruption ---
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);

        if (elapsed.count() > 30) { // 30s stall threshold: filesystem freeze or unresponsive I/O
            log_message("Hashing timeout after 30 seconds", LogLevel::ERR);
            throw UserInterrupt("Hashing timeout");
        }

        if (is_cancelled(guard)) {
            log_message("Process interrupted by user", LogLevel::WARNING);
            throw UserInterrupt("Process interrupted by user");
        }
        // --- End of timeout and user interruption check ---
    }

    // Final piece if any
    if (bytes_in_current_piece > 0) {
        t.set_hash(piece_index, piece_hasher.final());
    }

    // Update final progress bar
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
    if (elapsed > 0) {
        speed = static_cast<double>(bytes_processed) / elapsed;
    }
    emit_progress(static_cast<int>(bytes_processed / piece_size),
                     static_cast<int>(total_bytes / piece_size),
                     speed, 0.0, bytes_processed, total_bytes,
                     config_.path.filename().string());
}

// Creates the torrent file
void TorrentCreator::create_torrent() {
    TerminalGuard guard;
    
    try {
        log_message("Starting torrent creation for: " + config_.path.string(), LogLevel::INFO);
        // Check available disk space
        fs::path output_dir = config_.output.parent_path();
        if (output_dir.empty()) {
            output_dir = fs::current_path(); // Use current directory if no parent path
        }

        try {
            fs::space_info si = fs::space(output_dir);
            int64_t required_space = 0;
            if (fs::is_directory(config_.path)) {
                for (const auto& entry : fs::recursive_directory_iterator(config_.path)) {
                    if (entry.is_regular_file()) {
                        required_space += entry.file_size();
                    }
                }
            } else {
                required_space = fs::file_size(config_.path);
            }
            
            if (si.available < required_space * 1.1) { // 10% buffer for filesystem metadata overhead
                throw std::runtime_error("Not enough disk space. Required: " + 
                    std::to_string(required_space) + " bytes, Available: " + 
                    std::to_string(si.available) + " bytes");
            }
            log_message("Disk space check passed. Required: " + std::to_string(required_space) + " bytes, Available: " + std::to_string(si.available) + " bytes", LogLevel::INFO);
        } catch (const fs::filesystem_error& e) {
            log_message("Could not verify disk space: " + std::string(e.what()), LogLevel::WARNING);
        } catch (const std::exception& e) {
            log_message("Could not verify disk space: " + std::string(e.what()), LogLevel::WARNING);
        }

        // Add files to the file storage
        add_files_to_storage();
        sink_.verbose("Files added to storage: " + std::to_string(fs_.num_files()) + " file(s), total size: " + utils::format_size(fs_.total_size()) + "\n");
        log_message("Files in storage: " + std::to_string(fs_.num_files()) + ", total size: " + std::to_string(fs_.total_size()) + " bytes", LogLevel::INFO);

        int piece_size = config_.piece_size ? *config_.piece_size : utils::auto_piece_size(fs_.total_size());
        if (config_.piece_size) {
            sink_.verbose("Piece size: " + std::to_string(piece_size / 1024) + " KB (user-specified)\n");
            log_message("Piece size: " + std::to_string(piece_size / 1024) + " KB (user-specified)", LogLevel::INFO);
        } else {
            sink_.verbose("Piece size: " + std::to_string(piece_size / 1024) + " KB (auto-calculated for " + utils::format_size(fs_.total_size()) + " total)\n");
            log_message("Piece size: " + std::to_string(piece_size / 1024) + " KB (auto-calculated for " + std::to_string(fs_.total_size()) + " bytes)", LogLevel::INFO);
        }
        lt::create_flags_t flags = get_torrent_flags(config_.version);

        lt::create_torrent t(fs_, piece_size, flags);

        // Add trackers to create_torrent object
        int tier = 0;
        for (const auto& tracker : config_.trackers) {
            t.add_tracker(tracker, tier++);
            sink_.verbose("Tracker tier " + std::to_string(tier - 1) + ": " + tracker + "\n");
        }

        // Set piece hashes using streaming for large files
        sink_.info("Hashing pieces...\n");
        log_message("Starting hashing process for: " + config_.path.string(), LogLevel::INFO);
        int num_pieces = t.num_pieces();

        auto start_time = std::chrono::steady_clock::now();
        double speed = 0.0;
        double eta = 0.0;
        int64_t total_size = fs_.total_size(); // Total size in bytes
        int64_t piece_size_bytes = piece_size; // Piece size in bytes

        int progress = 0; // Progress variable

        // Define the progress_callback with additional calculations
        auto progress_callback = [&](lt::piece_index_t piece) mutable {
            progress = static_cast<int>(piece); // Update progress
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            
            if (elapsed > 0) {
                // Calculate speed in pieces per second
                double pieces_per_second = static_cast<double>(progress) / elapsed;
                // Estimate bytes processed
                int64_t processed = progress * piece_size_bytes;
                // Calculate speed in bytes per second
                speed = static_cast<double>(processed) / elapsed;
                // Estimate ETA in seconds
                int remaining_pieces = num_pieces - progress;
                eta = (pieces_per_second > 0) ? (remaining_pieces / pieces_per_second) : 0.0;
            } else {
                speed = 0.0;
                eta = 0.0;
            }

            // Call emit_progress with all six arguments
            const int64_t bytes_done = static_cast<int64_t>(progress) * piece_size_bytes;
            emit_progress(progress, num_pieces, speed, eta, bytes_done, total_size,
                          current_file_at_offset(bytes_done));

            // Check for user interruption (keypress or programmatic cancel)
            if (is_cancelled(guard)) {
                log_message("Process interrupted by user", LogLevel::WARNING);
                throw UserInterrupt("Process interrupted by user");
            }
        };

        // Set the hashes with the progress callback and error code
        lt::error_code ec;

        if (fs::is_directory(config_.path) || config_.version == TorrentVersion::HYBRID) {
            // Use libtorrent's native hashing to guarantee hybrid spec compliance for:
            // 1. Directory inputs (always require both v1 and v2 hashes)
            // 2. Explicitly requested hybrid torrents (even with single file inputs)
            lt::set_piece_hashes(t, config_.path.parent_path().string(), progress_callback, ec);

        } else {
            // For single large files, use our streaming hasher
            if (fs::file_size(config_.path) > 1 * 1024 * 1024 * 1024) { // Below 1GB single-threaded is cheaper than thread coordination
                hash_large_file_parallel(config_.path, t, piece_size, guard);
            } else {
                hash_large_file(config_.path, t, piece_size, guard);
            }
        }

        if (ec) {
            log_message("Error setting piece hashes: " + ec.message(), LogLevel::ERR);
            throw std::filesystem::filesystem_error("Error setting piece hashes: " + ec.message(), ec);
        }

        // Set creation date if requested
        if (config_.include_creation_date) {
            t.set_creation_date(std::time(nullptr)); // Set to current time
        } else {
            t.set_creation_date(0); // Remove creation date
        }

        // Set creator if requested
        if (config_.creator) {
            t.set_creator(config_.creator->c_str()); // Set creator string
        }
        
        // Check for user interruption and timeout
        auto loop_start_time = std::chrono::steady_clock::now();
        bool timeout_thrown = false;

        // Only run progress loop for directory hashing (libtorrent async)
        if (fs::is_directory(config_.path)) {
            // Progress monitoring loop: polls hashing progress and user keypress
            // until hashing completes or times out.
            while (true) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - loop_start_time);

                // Reset: progress→0 each iteration so timeout detects stalls;
                // timer refreshes only on meaningful progress.
                if (progress > 0 && progress < num_pieces - 1) {
                    loop_start_time = std::chrono::steady_clock::now();
                    progress = 0;
                }
                
                // progress == 0 means no piece completed since the last reset → stalled
                if (elapsed.count() > 30 && !timeout_thrown && progress == 0) { // 30s stall threshold
                    timeout_thrown = true;
                    log_message("Hashing timeout after 30 seconds", LogLevel::ERR);
                    throw UserInterrupt("Hashing timeout");
                }

                // Check for user interruption (keypress or programmatic cancel)
                if (is_cancelled(guard)) {
                    log_message("Process interrupted by user", LogLevel::WARNING);
                    throw UserInterrupt("Process interrupted by user");
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10)); // keypress polling interval

                if (progress >= num_pieces - 1) {
                    const int64_t final_offset =
                        num_pieces > 0
                            ? static_cast<int64_t>(num_pieces - 1) * piece_size_bytes
                            : 0;
                    emit_progress(num_pieces, num_pieces, speed, 0.0, total_size, total_size,
                                  current_file_at_offset(final_offset));
                    end_progress();
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // progress poll interval
            }
        } else {
            // For single files, hashing is already complete - just show final progress
            emit_progress(num_pieces, num_pieces, speed, 0.0, total_size, total_size,
                          config_.path.filename().string());
            end_progress();
        }

        // Generate and save torrent file
        try {
            std::ofstream out(config_.output, std::ios_base::binary);
            if (!out) {
                throw std::runtime_error("Failed to open output file: " + config_.output.string());
            }

            lt::entry e = t.generate();

            // libtorrent names hybrid pad files by size alone, so equal-sized
            // pads collide in the v1 file list. Strict clients reject the
            // resulting duplicate paths; rename them before bencoding.
            deduplicate_pad_paths(e);

            if (config_.name) {
                e["info"]["name"] = *config_.name;
            }

            if (config_.source) {
                e["info"]["source"] = *config_.source;
            }

            if (config_.entropy) {
                try {
                    e["info"]["entropy"] = utils::generate_entropy_hex();
                } catch (const std::exception& ex) {
                    log_message("Failed to generate entropy: " + std::string(ex.what()), LogLevel::ERR);
                    throw std::runtime_error("Failed to generate entropy: " + std::string(ex.what()));
                }
            }

            lt::bencode(std::ostream_iterator<char>(out), e);

            if (!out) {
                throw std::runtime_error("Failed to write torrent file: " + config_.output.string());
            }

            print_torrent_summary(fs_.total_size(), piece_size, t.num_pieces());
            log_message("Torrent created successfully: " + config_.output.string(), LogLevel::INFO);
            log_message("Torrent size: " + std::to_string(fs::file_size(config_.output)) + " bytes", LogLevel::INFO);
        } catch (const std::exception& e) {
            log_message("Error saving torrent file: " + std::string(e.what()), LogLevel::ERR);
            throw;
        }

    } catch (const UserInterrupt&) {
        report_error_noexcept("\n");
        throw;
    } catch (const std::runtime_error& e) {
        report_error_noexcept(std::string(e.what()) + "\n");
        log_message("Runtime error: " + std::string(e.what()), LogLevel::ERR);
        throw;
    } catch (const std::exception& e) {
        report_error_noexcept(std::string("An unexpected error occurred: ") + e.what() + "\n");
        log_message("Unexpected error: " + std::string(e.what()), LogLevel::ERR);
        throw;
    }
}

// Prints a summary of the created torrent
void TorrentCreator::print_torrent_summary(int64_t total_size, int piece_size, int num_pieces) const {
    sink_.info("\n=== TORRENT CREATED SUCCESSFULLY ===\n");
    sink_.info("File: " + config_.output.string() + "\n");

    std::string version_str;
    switch(config_.version) {
        case TorrentVersion::V1: version_str = "v1"; break;
        case TorrentVersion::V2: version_str = "v2"; break;
        case TorrentVersion::HYBRID: version_str = "hybrid"; break;
    }
    sink_.info("Version: " + version_str + "\n");

    sink_.info("Total size: " + utils::format_size(total_size) + "\n");
    if (config_.target_piece_count) {
        sink_.info("Target pieces: " + std::to_string(*config_.target_piece_count) + "\n");
    }
    sink_.info("Pieces: " + std::to_string(num_pieces) + " of " + std::to_string(piece_size / 1024) + "KB\n");
    sink_.info("Trackers: " + std::to_string(config_.trackers.size()) + "\n");
    sink_.info("Web seeds: " + std::to_string(config_.web_seeds.size()) + "\n");
    sink_.info("Private: " + std::string(config_.is_private ? "Yes" : "No") + "\n");
    if (config_.source) {
        sink_.info("Source: " + *config_.source + "\n");
    }
    if (config_.entropy) {
        sink_.info("Entropy: Yes (randomized info hash)\n");
    }
}
