#include "torrent_creator.hpp"
#include "constants.hpp"
#include <fstream>
#include <iomanip>
#include <chrono>

// Simple logging function
void log_message(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ofstream logfile("torrent_builder.log", std::ios_base::app);
    logfile << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S") 
            << " - " << message << "\n";
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
    
    if (total_size < 64 * 1024 * 1024) return k16KB;  // Smaller files get 16KB pieces
    if (total_size < 512 * 1024 * 1024) return k32KB; // Medium files get 32KB pieces
    return k64KB; // Larger files get 64KB pieces
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
void TorrentCreator::hash_large_file(const fs::path& path, lt::create_torrent& t, int piece_size) {
    const size_t buffer_size = 16 * 1024 * 1024; // 16MB buffer
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
            }
        }
        
        // Update progress
        print_progress_bar(static_cast<int>(bytes_processed / piece_size), 
                         static_cast<int>(total_bytes / piece_size));
    }

    // Final piece if any
    if (bytes_in_current_piece > 0) {
        t.set_hash(piece_index, piece_hasher.final());
    }
}

// Displays a progress bar
void TorrentCreator::print_progress_bar(int progress, int total) const {
    const int bar_width = 50;
    float progress_percentage = static_cast<float>(progress) / total;
    int filled_width = static_cast<int>(round(bar_width * progress_percentage));

    std::cout << "[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < filled_width) {
            std::cout << "=";
        } else {
            std::cout << " ";
        }
    }
    std::cout << "] " << static_cast<int>(progress_percentage * 100) << "%\r";
    std::cout.flush();
}

// Creates the torrent file
void TorrentCreator::create_torrent() {
    try {
        log_message("Starting torrent creation for: " + config_.path.string());
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
        } catch (const fs::filesystem_error& e) {
            // Log the error but continue, as space check is not critical
            log_message("Warning: Could not verify disk space: " + std::string(e.what()));
        }

        // Add files to the file storage
        add_files_to_storage();

        // Use the specified piece_size or calculate automatically
        int piece_size = config_.piece_size ? *config_.piece_size : auto_piece_size(fs_.total_size());
        lt::create_flags_t flags = get_torrent_flags();

        // Simplify the torrent for debugging
        lt::create_torrent t(fs_, piece_size, flags);

        // Set piece hashes using streaming for large files
        std::cout << "Hashing pieces...\n";
        int num_pieces = t.num_pieces();
        
        if (fs::is_directory(config_.path)) {
            // For directories, use libtorrent's built-in hashing
            lt::set_piece_hashes(t, config_.path.parent_path().string(), 
                [this, num_pieces](lt::piece_index_t piece) {
                    print_progress_bar(static_cast<int>(piece), num_pieces);
                });
        } else {
            // For single large files, use our streaming hasher
            hash_large_file(config_.path, t, piece_size);
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

        // libtorrent session for alerts
        lt::add_torrent_params p;
        p.save_path = config_.output.parent_path().string(); // Where to save the torrent (and potentially resume data)

        // Create a vector to store alerts
        std::vector<lt::alert*> alerts;
        std::string error_message; // Store any error message

        // Use a lambda for the progress callback.  Correct parameter type here:
        auto progress_callback = [&](lt::piece_index_t piece) {
            print_progress_bar(static_cast<int>(piece), num_pieces);
            ses.pop_alerts(&alerts);
            for (lt::alert const* a : alerts) {
                // Check for errors
                if (auto const* te = lt::alert_cast<lt::torrent_error_alert>(a)) {
                    error_message = "Torrent error: " + te->error.message();
                }
                if (auto const* fe = lt::alert_cast<lt::file_error_alert>(a)) {
                    error_message = "File error: " + fe->error.message() + " - " + fe->filename();
                }
                if (auto const* srdf = lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
                    error_message = "Save resume data failed: " + srdf->error.message();
                }
                if (lt::alert_cast<lt::save_resume_data_alert>(a)) {
                    // This is just informational, no action needed
                }
            }
        };

        // Set the hashes with the progress callback and error code
        lt::error_code ec;
        lt::set_piece_hashes(t, config_.path.parent_path().string(), progress_callback, ec);
        if (ec) {
            throw std::runtime_error("Error setting piece hashes: " + ec.message());
        }

        // Check for user interruption
        for (int i = 0; i < 5; ++i) { // Check for 5 seconds max
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            ses.pop_alerts(&alerts);
            
            // Check for user interruption
            if (std::cin.peek() == 'q' || std::cin.peek() == 'Q') {
                throw std::runtime_error("Process interrupted by user");
            }
        }

        // If there was an error, throw an exception
        if (!error_message.empty()) {
            throw std::runtime_error(error_message);
        }

        // Generate and save torrent file
        std::ofstream out(config_.output, std::ios_base::binary);
        lt::entry e = t.generate();
        lt::bencode(std::ostream_iterator<char>(out), e);

        print_torrent_summary(fs_.total_size(), piece_size, t.num_pieces());
        log_message("Torrent created successfully: " + config_.output.string());

    } catch (const std::runtime_error& e) {
        std::cerr << "Runtime error: " << e.what() << std::endl;
        throw;
    } catch (const std::exception& e) { // Catch-all for other standard exceptions
        std::cerr << "An unexpected error occurred: " << e.what() << std::endl;
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
    std::cout << "Pieces: " << num_pieces << " of " << piece_size/1024 << "KB\n"; // Show piece_size in KB
    std::cout << "Trackers: " << config_.trackers.size() << "\n"; // Simplified tracker count
    std::cout << "Web seeds: " << config_.web_seeds.size() << "\n";
    std::cout << "Private: " << (config_.is_private ? "Yes" : "No") << "\n";
}
