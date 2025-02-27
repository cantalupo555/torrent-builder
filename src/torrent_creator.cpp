#include "torrent_creator.hpp"
#include "constants.hpp"
#include <fstream>
#include <iomanip>
#include <chrono>
#include <format>

// Logging function with levels
void log_message(const std::string& message, LogLevel level = LogLevel::INFO) {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ofstream logfile("torrent_builder.log", std::ios_base::app);
    
    // Get level string
    std::string level_str;
    switch(level) {
        case LogLevel::INFO: level_str = "INFO"; break;
        case LogLevel::WARNING: level_str = "WARNING"; break;
        case LogLevel::ERROR: level_str = "ERROR"; break;
    }
    
    logfile << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S") 
            << " [" << level_str << "] - " << message << "\n";
}
#include <libtorrent/version.hpp>
#include <iomanip>
#include <cmath>
#include <fstream>
#include <ctime>
#include <thread>
#include <format>
#include <stdexcept> // For std::runtime_error
#include <system_error> // For std::error_code

// Constructor for TorrentCreator
TorrentCreator::TorrentCreator(const TorrentConfig& config)
    : config_(config), ses(lt::session_params{}) { // Initialize session with default parameters
}

// Automatically determines a suitable piece size based on the total size
int TorrentCreator::auto_piece_size(int64_t total_size) {
    using namespace PieceSizes;

    if (total_size < 64LL * 1024 * 1024) return k16KB;        // < 64MB: 16KB
    if (total_size < 128LL * 1024 * 1024) return k32KB;       // < 128MB: 32KB
    if (total_size < 256LL * 1024 * 1024) return k64KB;       // < 256MB: 64KB
    if (total_size < 512LL * 1024 * 1024) return k128KB;      // < 512MB: 128KB
    if (total_size < 1LL * 1024 * 1024 * 1024) return k256KB;   // < 1GB: 256KB
    if (total_size < 2LL * 1024 * 1024 * 1024) return k512KB;   // < 2GB: 512KB
    if (total_size < 4LL * 1024 * 1024 * 1024) return k1024KB;  // < 4GB: 1MB
    if (total_size < 8LL * 1024 * 1024 * 1024) return k2048KB;  // < 8GB: 2MB
    if (total_size < 16LL * 1024 * 1024 * 1024) return k4096KB; // < 16GB: 4MB
    if (total_size < 32LL * 1024 * 1024 * 1024) return k8192KB; // < 32GB: 8MB
    if (total_size < 64LL * 1024 * 1024 * 1024) return k16384KB; // < 64GB: 16MB
    return k32768KB; // >= 64GB: 32MB
}

// Returns flags for torrent creation based on the specified version
lt::create_flags_t TorrentCreator::get_torrent_flags() const {
    lt::create_flags_t flags = {};

    switch(config_.version) {
        case TorrentVersion::V1:
            flags |= lt::create_torrent::v1_only; // Set v1_only flag for V1 torrents
            break;
        case TorrentVersion::V2:
            flags |= lt::create_torrent::v2_only; // Set v2_only flag for V2 torrents
            break;
        case TorrentVersion::HYBRID:
            // No flags for hybrid, it will create both v1 and v2 merkle trees
            break;
    }

    return flags;
}

// Adds files to the libtorrent file_storage
void TorrentCreator::add_files_to_storage() {
    if (fs::is_directory(config_.path)) {
        // If the path is a directory, add all files in the directory
        lt::add_files(fs_, config_.path.string(), [](std::string const&) { return true; });
    } else {
        // If the path is a file, add the single file
        fs_.add_file(config_.path.filename().string(), fs::file_size(config_.path));
    }
}

// Hashing with streaming for large files
void TorrentCreator::hash_large_file_parallel(const fs::path& path, lt::create_torrent& t, int piece_size) {
    const int64_t file_size = fs::file_size(path);
    const int num_threads = std::thread::hardware_concurrency(); // Number of available threads
    const int64_t block_size = (file_size / num_threads) + 1; // Size of each block

    std::vector<std::thread> threads;
    std::mutex mutex; // To synchronize access to object `t`

    for (int i = 0; i < num_threads; ++i) {
        int64_t start_offset = i * block_size;
        int64_t end_offset = std::min(start_offset + block_size, file_size);

        threads.emplace_back(&TorrentCreator::hash_block, this, path, std::ref(t), piece_size, start_offset, end_offset, std::ref(mutex));
    }

    // Wait for all threads to finish
    for (auto& thread : threads) {
        thread.join();
    }
}

void TorrentCreator::hash_block(const fs::path& path, lt::create_torrent& t, int piece_size, int64_t start_offset, int64_t end_offset, std::mutex& mutex) {
    const size_t buffer_size = PieceSizes::k16384KB; // 16MB buffer
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
    
    // Add these variables
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
    }

    // Finalize the last piece if necessary
    if (bytes_in_current_piece > 0) {
        std::lock_guard<std::mutex> lock(mutex);
        t.set_hash(piece_index, piece_hasher.final());
    }
}

void TorrentCreator::hash_large_file(const fs::path& path, lt::create_torrent& t, int piece_size) {
    const size_t buffer_size = PieceSizes::k16384KB; // 16MB buffer
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

    auto start_time = std::chrono::steady_clock::now(); // Start time
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

        if (elapsed.count() > 30) {
            log_message("Hanging piece detection triggered - No progress for 30 seconds", LogLevel::WARNING);
            std::cerr << "\nRuntime error: Hanging piece detection activated. Check disk performance and file integrity\n";
            std::cerr.flush();
            throw std::runtime_error("Hashing timeout");
        }

        if (std::cin.peek() == 'q' || std::cin.peek() == 'Q' || std::cin.peek() == '\x03') {
            log_message("Process interrupted by user", LogLevel::WARNING);
            std::cerr << "\nProcess interrupted by user\n";
            std::cerr.flush();
            throw std::runtime_error("Process interrupted by user");
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
    const int bar_width = 50;
    float progress_percentage = static_cast<float>(progress) / total;
    int filled_width = static_cast<int>(std::round(bar_width * progress_percentage));

    std::cout << "[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < filled_width) {
            std::cout << "=";
        } else {
            std::cout << " ";
        }
    }
    std::cout << "] " << static_cast<int>(progress_percentage * 100) << "% ";

    // Display processed data / total
    std::cout << format_size(processed) << " / " << format_size(total_size) << " ";

    // Display speed
    std::cout << "Speed: " << format_speed(speed) << " ";

    // Display ETA
    std::cout << "ETA: " << format_eta(eta) << "\r";
    std::cout.flush();
}

std::string TorrentCreator::format_size(int64_t bytes) const {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }
    return std::format("{:.2f} {}", size, units[unit]);
}

std::string TorrentCreator::format_speed(double speed) const {
    double mb_per_sec = speed / (1024 * 1024);
    return std::format("{:.2f} MB/s", mb_per_sec);
}

std::string TorrentCreator::format_eta(double eta) const {
    int minutes = static_cast<int>(eta / 60);
    int seconds = static_cast<int>(eta) % 60;
    return std::format("{}m {}s", minutes, seconds);
}

// Creates the torrent file
void TorrentCreator::create_torrent() {
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
            
            if (si.available < required_space * 1.1) { // 10% buffer
                throw std::runtime_error("Not enough disk space. Required: " + 
                    std::to_string(required_space) + " bytes, Available: " + 
                    std::to_string(si.available) + " bytes");
            }
            log_message("Disk space check passed. Required: " + std::to_string(required_space) + " bytes, Available: " + std::to_string(si.available) + " bytes", LogLevel::INFO);
        } catch (const fs::filesystem_error& e) {
            log_message("Could not verify disk space: " + std::string(e.what()), LogLevel::WARNING);
        } catch (const std::exception& e) {
            log_message("Warning: Could not verify disk space: " + std::string(e.what()));
        }

        // Add files to the file storage
        add_files_to_storage();

        // Use the specified piece_size or calculate automatically
        int piece_size = config_.piece_size ? *config_.piece_size : auto_piece_size(fs_.total_size());
        lt::create_flags_t flags = get_torrent_flags();

        // Simplify the torrent for debugging
        lt::create_torrent t(fs_, piece_size, flags);

        // Add trackers to create_torrent object
        int tier = 0;
        for (const auto& tracker : config_.trackers) {
            t.add_tracker(tracker, tier++);
        }

        // Set piece hashes using streaming for large files
        std::cout << "Hashing pieces...\n";
        log_message("Starting hashing process for: " + config_.path.string(), LogLevel::INFO);
        int num_pieces = t.num_pieces();

        // Declaração de variáveis para rastrear o progresso e tempo
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
        };

        // Set the hashes with the progress callback and error code
        lt::error_code ec;

        if (fs::is_directory(config_.path)) {
            // For directories, use libtorrent's built-in hashing, and the progress callback
            lt::set_piece_hashes(t, config_.path.parent_path().string(), progress_callback, ec);

        } else {
            // For single large files, use our streaming hasher
            if (fs::file_size(config_.path) > 1 * 1024 * 1024 * 1024) { // Use parallel hashing for files larger than 1GB
                hash_large_file_parallel(config_.path, t, piece_size);
            } else {
                hash_large_file(config_.path, t, piece_size);
            }
        }

        if (ec) {
            log_message("Error setting piece hashes: " + ec.message(), LogLevel::ERROR);
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

        while (true) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - loop_start_time);

            // Timeout after 30 seconds of no progress
            if (elapsed.count() > 30 && !timeout_thrown) {
                timeout_thrown = true;
                log_message("Hashing timeout after 30 seconds", LogLevel::ERROR);
                std::cout << "\n"; // New line before error message
                std::cerr << "Runtime error: Hashing timeout" << std::endl;
                std::cerr.flush();
                std::exit(1); // Exit immediately with error code
            }

            // Check for user interruption without blocking
            if (std::cin.rdbuf()->in_avail() > 0) {
                char c = std::cin.get();
                if (c == 'q' || c == 'Q' || c == '\x03') { // Ctrl+C
                    log_message("Process interrupted by user", LogLevel::WARNING);
                    std::cerr << "\nProcess interrupted by user" << std::endl;
                    std::cerr.flush();
                    std::exit(1);
                }
            }

            // Check if hashing is complete (handle potential off-by-one)
            if (progress >= num_pieces - 1) {
                break;
            }

            // Only sleep if we're not done yet
            if (progress < num_pieces - 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                ses.pop_alerts(&alerts);
            }
        }

        // If there was an error, throw an exception
        if (!error_message.empty()) {
            log_message("Error during torrent creation: " + error_message, LogLevel::ERROR);
            throw std::runtime_error(error_message);
        }

        // Generate and save torrent file
        try {
            std::ofstream out(config_.output, std::ios_base::binary);
            if (!out) {
                throw std::runtime_error("Failed to open output file: " + config_.output.string());
            }

            lt::entry e = t.generate();
            lt::bencode(std::ostream_iterator<char>(out), e);

            if (!out) {
                throw std::runtime_error("Failed to write torrent file: " + config_.output.string());
            }

            print_torrent_summary(fs_.total_size(), piece_size, t.num_pieces());
            log_message("Torrent created successfully: " + config_.output.string(), LogLevel::INFO);
            log_message("Torrent size: " + std::to_string(fs::file_size(config_.output)) + " bytes", LogLevel::INFO);
        } catch (const std::exception& e) {
            log_message("Error saving torrent file: " + std::string(e.what()), LogLevel::ERROR);
            throw;
        }

    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << std::endl;
        log_message("Runtime error: " + std::string(e.what()), LogLevel::ERROR);
        throw;
    } catch (const std::exception& e) { // Catch-all for other standard exceptions
        std::cerr << "An unexpected error occurred: " << e.what() << std::endl;
        log_message("Unexpected error: " + std::string(e.what()), LogLevel::ERROR);
        throw;
    }
}

// Prints a summary of the created torrent
void TorrentCreator::print_torrent_summary(int64_t total_size, int piece_size, int num_pieces) const {
    std::cout << "\n=== TORRENT CREATED SUCCESSFULLY ===\n";
    std::cout << "File: " << config_.output << "\n";

    // Lambda function to format file sizes with appropriate units (B, KB, MB, GB, TB).
    auto format_size = [](int64_t bytes) -> std::string {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        int unit = 0;
        double size = bytes;

        while (size >= 1024 && unit < 4) {
            size /= 1024;
            unit++;
        }

        // Use std::format instead of stringstream
        return std::format("{:.2f} {}", size, units[unit]);
    };

    // Print torrent summary details: total size, number and size of pieces,
    // number of trackers, number of web seeds, and whether the torrent is private
    std::cout << "Total size: " << format_size(total_size) << "\n";
    std::cout << "Pieces: " << num_pieces << " of " << piece_size / 1024 << "KB\n"; // Show piece_size in KB
    std::cout << "Trackers: " << config_.trackers.size() << "\n"; // Simplified tracker count
    std::cout << "Web seeds: " << config_.web_seeds.size() << "\n";
    std::cout << "Private: " << (config_.is_private ? "Yes" : "No") << "\n";
}
