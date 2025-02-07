#include "torrent_creator.hpp"
#include <libtorrent/version.hpp>
#include <iomanip>
#include <cmath>
#include <fstream>
#include <sstream>

// Constructor for TorrentCreator
TorrentCreator::TorrentCreator(const TorrentConfig& config)
    : config_(config) {}

// Automatically determines a suitable piece size based on the total size
int TorrentCreator::auto_piece_size(int64_t total_size) {
    if (total_size < 64 * 1024 * 1024) return 16 * 1024;  // Smaller files get 16KB pieces
    if (total_size < 512 * 1024 * 1024) return 32 * 1024; // Medium files get 32KB pieces
    return 64 * 1024; // Larger files get 64KB pieces
}

// Returns flags for torrent creation based on the specified version
int TorrentCreator::get_torrent_flags() const {
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

    return static_cast<int>(flags);
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

// Creates the torrent file
void TorrentCreator::create_torrent() {
    try {
        // Add files to the file storage
        add_files_to_storage();

        // Use the specified piece_size or calculate automatically
        int piece_size = config_.piece_size ? *config_.piece_size : auto_piece_size(fs_.total_size());
        int flags = get_torrent_flags();

        lt::create_torrent t(fs_, piece_size, lt::create_flags_t(flags));

        // Set private flag if needed
        if (config_.is_private) {
            t.set_priv(true);
        }

        // Add trackers
        for (const auto& tracker : config_.trackers) {
            t.add_tracker(tracker);
        }

        // Add web seeds
        for (const auto& web_seed : config_.web_seeds) {
            t.add_url_seed(web_seed);
        }

        // Set comment if provided
        if (config_.comment) {
            t.set_comment(config_.comment->c_str());
        }

        // Set piece hashes. This is the computationally intensive part
        lt::set_piece_hashes(t, config_.path.parent_path().string());

        // Generate and save torrent file
        std::ofstream out(config_.output, std::ios_base::binary);
        lt::entry e = t.generate();
        lt::bencode(std::ostream_iterator<char>(out), e);

        print_torrent_summary(fs_.total_size(), piece_size, t.num_pieces());
    }
    catch (const std::exception& e) {
        // Catch and re-throw exceptions, after printing an error message
        std::cerr << "Error creating torrent: " << e.what() << std::endl;
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

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
        return oss.str();
    };

    // Print torrent summary details: total size, number and size of pieces,
    // number of trackers, number of web seeds, and whether the torrent is private
    std::cout << "Total size: " << format_size(total_size) << "\n";
    std::cout << "Pieces: " << num_pieces << " of " << piece_size/1024 << "KB\n"; // Show piece_size in KB
    std::cout << "Trackers: " << config_.trackers.size() << "\n";
    std::cout << "Web seeds: " << config_.web_seeds.size() << "\n";
    std::cout << "Private: " << (config_.is_private ? "Yes" : "No") << "\n";
}
