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

// Constructor for TorrentCreator
TorrentCreator::TorrentCreator(TorrentConfig config)
    : config_(std::move(config)), ses(lt::session_params{}) {
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

    // Monitor thread polls for 'q' keypress while workers block on I/O;
    // signals shutdown via cancel atomic so workers can exit cleanly.
    std::thread monitor([&guard, &cancel]() {
        while (!cancel.load()) {
            char c = 0;
            if (guard.check_key_press(c)) {
                if (c == 'q' || c == 'Q' || c == '\x03') {
                    cancel.store(true);
                    return;
                }
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

        // Update progress safely
        {
            std::lock_guard<std::mutex> lock(progress_mutex_);
            total_processed_ += bytes_read;
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            double speed = elapsed > 0 ? static_cast<double>(total_processed_) / elapsed : 0.0;
            double eta = speed > 0 ? (file_size - total_processed_) / speed : 0.0;
            
            print_progress_bar(static_cast<int>(total_processed_ / piece_size),
                             static_cast<int>(file_size / piece_size),
                             speed,
                             eta,
                             total_processed_, 
                             file_size);
        }

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
                print_progress_bar(static_cast<int>(bytes_processed / piece_size),
                                 static_cast<int>(total_bytes / piece_size),
                                 speed, eta, bytes_processed, total_bytes);
            }
        }

        // --- Check for timeout and user interruption ---
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);

        if (elapsed.count() > 30) { // 30s stall threshold: filesystem freeze or unresponsive I/O
            log_message("Hashing timeout after 30 seconds", LogLevel::ERR);
            throw UserInterrupt("Hashing timeout");
        }

        char c = 0;
        if (guard.check_key_press(c)) {
            if (c == 'q' || c == 'Q' || c == '\x03') {
                log_message("Process interrupted by user", LogLevel::WARNING);
                throw UserInterrupt("Process interrupted by user");
            }
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
    print_progress_bar(static_cast<int>(bytes_processed / piece_size),
                     static_cast<int>(total_bytes / piece_size),
                     speed, 0.0, bytes_processed, total_bytes);
}

// Displays a progress bar
void TorrentCreator::print_progress_bar(int progress, int total, double speed, double eta, int64_t processed, int64_t total_size) const {
    if (config_.silent) return;
    print_progress(progress, total, speed, eta, processed, total_size);
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
        print_verbose("Files added to storage: " + std::to_string(fs_.num_files()) + " file(s), total size: " + utils::format_size(fs_.total_size()) + "\n");
        log_message("Files in storage: " + std::to_string(fs_.num_files()) + ", total size: " + std::to_string(fs_.total_size()) + " bytes", LogLevel::INFO);

        int piece_size = config_.piece_size ? *config_.piece_size : utils::auto_piece_size(fs_.total_size());
        if (config_.piece_size) {
            print_verbose("Piece size: " + std::to_string(piece_size / 1024) + " KB (user-specified)\n");
            log_message("Piece size: " + std::to_string(piece_size / 1024) + " KB (user-specified)", LogLevel::INFO);
        } else {
            print_verbose("Piece size: " + std::to_string(piece_size / 1024) + " KB (auto-calculated for " + utils::format_size(fs_.total_size()) + " total)\n");
            log_message("Piece size: " + std::to_string(piece_size / 1024) + " KB (auto-calculated for " + std::to_string(fs_.total_size()) + " bytes)", LogLevel::INFO);
        }
        lt::create_flags_t flags = get_torrent_flags(config_.version);

        lt::create_torrent t(fs_, piece_size, flags);

        // Add trackers to create_torrent object
        int tier = 0;
        for (const auto& tracker : config_.trackers) {
            t.add_tracker(tracker, tier++);
            print_verbose("Tracker tier " + std::to_string(tier - 1) + ": " + tracker + "\n");
        }

        // Set piece hashes using streaming for large files
        print_info("Hashing pieces...\n");
        log_message("Starting hashing process for: " + config_.path.string(), LogLevel::INFO);
        int num_pieces = t.num_pieces();

        auto start_time = std::chrono::steady_clock::now();
        double speed = 0.0;
        double eta = 0.0;
        int64_t total_size = fs_.total_size(); // Total size in bytes
        int64_t piece_size_bytes = piece_size; // Piece size in bytes
        lt::add_torrent_params p;
        p.save_path = config_.output.parent_path().string(); // Where to save the torrent

        // Vector to store alerts
        std::vector<lt::alert*> alerts;
        std::string error_message; // To store error messages
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

            // Call print_progress_bar with all six arguments
            print_progress_bar(progress, num_pieces, speed, eta, progress * piece_size_bytes, total_size);
            ses.pop_alerts(&alerts);
            for (lt::alert const* a : alerts) {
                if (auto const* te = lt::alert_cast<lt::torrent_error_alert>(a)) {
                    error_message = "Torrent error: " + te->error.message();
                }
                if (auto const* fe = lt::alert_cast<lt::file_error_alert>(a)) {
                    error_message = "File error: " + fe->error.message() + " - " + fe->filename();
                }
                if (auto const* srdf = lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
                    error_message = "Save resume data failed: " + srdf->error.message();
                }
            }
            
            // Check for user interruption
            char c = 0;
            if (guard.check_key_press(c)) {
                if (c == 'q' || c == 'Q' || c == '\x03') {
                    log_message("Process interrupted by user", LogLevel::WARNING);
                    throw UserInterrupt("Process interrupted by user");
                }
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
            // Progress monitoring loop: polls libtorrent alerts and user keypress
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

                // Check for user interruption
                char c = 0;
                if (guard.check_key_press(c)) {
                    if (c == 'q' || c == 'Q' || c == '\x03') {
                        log_message("Process interrupted by user", LogLevel::WARNING);
                        throw UserInterrupt("Process interrupted by user");
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); // keypress polling interval

                if (progress >= num_pieces - 1) {
                    print_progress_bar(num_pieces, num_pieces, speed, 0.0, total_size, total_size);
                    print_info("\n");
                    break;
                }

                ses.pop_alerts(&alerts);
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // alert drain interval
            }
        } else {
            // For single files, hashing is already complete - just show final progress
            print_progress_bar(num_pieces, num_pieces, speed, 0.0, total_size, total_size);
            print_info("\n");
        }

        // If there was an error, throw an exception
        if (!error_message.empty()) {
            log_message("Error during torrent creation: " + error_message, LogLevel::ERR);
            throw std::runtime_error(error_message);
        }

        // Generate and save torrent file
        try {
            std::ofstream out(config_.output, std::ios_base::binary);
            if (!out) {
                throw std::runtime_error("Failed to open output file: " + config_.output.string());
            }

            lt::entry e = t.generate();

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
        print_error("\n");
        throw;
    } catch (const std::runtime_error& e) {
        print_error(std::string(e.what()) + "\n");
        log_message("Runtime error: " + std::string(e.what()), LogLevel::ERR);
        throw;
    } catch (const std::exception& e) {
        print_error(std::string("An unexpected error occurred: ") + e.what() + "\n");
        log_message("Unexpected error: " + std::string(e.what()), LogLevel::ERR);
        throw;
    }
}

// Prints a summary of the created torrent
void TorrentCreator::print_torrent_summary(int64_t total_size, int piece_size, int num_pieces) const {
    print_info("\n=== TORRENT CREATED SUCCESSFULLY ===\n");
    print_info("File: " + config_.output.string() + "\n");
    
    std::string version_str;
    switch(config_.version) {
        case TorrentVersion::V1: version_str = "v1"; break;
        case TorrentVersion::V2: version_str = "v2"; break;
        case TorrentVersion::HYBRID: version_str = "hybrid"; break;
    }
    print_info("Version: " + version_str + "\n");

    print_info("Total size: " + utils::format_size(total_size) + "\n");
    print_info("Pieces: " + std::to_string(num_pieces) + " of " + std::to_string(piece_size / 1024) + "KB\n");
    print_info("Trackers: " + std::to_string(config_.trackers.size()) + "\n");
    print_info("Web seeds: " + std::to_string(config_.web_seeds.size()) + "\n");
    print_info("Private: " + std::string(config_.is_private ? "Yes" : "No") + "\n");
    if (config_.source) {
        print_info("Source: " + *config_.source + "\n");
    }
    if (config_.entropy) {
        print_info("Entropy: Yes (randomized info hash)\n");
    }
}
