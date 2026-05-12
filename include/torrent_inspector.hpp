#ifndef TORRENT_INSPECTOR_HPP
#define TORRENT_INSPECTOR_HPP

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <filesystem>

namespace fs = std::filesystem;

// Forward declarations for libtorrent
namespace libtorrent
{
class torrent_info;
class info_hash_t;
} // namespace libtorrent

// Alias for convenience
namespace lt = libtorrent;

/**
 * @brief Aggregated metadata extracted from a .torrent file.
 */
struct TorrentMetadata
{
    std::string name;
    std::string info_hash_v1;                   // SHA-1 hash
    std::string info_hash_v2;                   // SHA-256 hash
    bool is_hybrid = false;

    int64_t total_size = 0;
    int64_t piece_length = 0;
    int32_t piece_count = 0;

    /**
     * @brief Information about a single file within the torrent.
     */
    struct FileInfo
    {
        std::string path;                       // Relative path within the torrent
        int64_t size;
        std::optional<std::string> symlink_path; // Target if the file is a symbolic link
    };
    std::vector<FileInfo> files;                // Flat list; see file_tree for hierarchy
    std::vector<std::vector<std::string>> file_tree;

    std::vector<std::string> trackers;          // Flattened across tiers
    std::vector<std::vector<std::string>> tracker_tiers;

    std::vector<std::string> web_seeds;         // HTTP/FTP web seed URLs

    bool is_private = false;                    // Disables DHT/PEX

    std::optional<std::string> source;          // Cross-seeding identity
    std::optional<std::string> entropy;         // Random hex string for unique info hash per invocation
    std::optional<std::string> comment;
    std::optional<int64_t> creation_date;       // Unix timestamp
    std::optional<std::string> created_by;

    std::string magnet_link;
};

class TorrentInspector
{
  public:
    /**
     * @brief Construct an inspector and parse the given .torrent file.
     * @param torrent_path Path to a valid .torrent file.
     * @throws std::runtime_error if the file cannot be read or parsed.
     */
    explicit TorrentInspector(const fs::path &torrent_path);

    /**
     * @brief Destructor. Releases the loaded torrent_info handle.
     */
    ~TorrentInspector();

    /**
     * @brief Extract all metadata from the loaded torrent.
     * @return Populated TorrentMetadata struct.
     * @throws std::runtime_error if torrent info is not loaded.
     */
    TorrentMetadata inspect();

    /**
     * @brief Check that every file in the torrent exists on disk with the expected size.
     * @param base_path Root directory to resolve relative file paths against (defaults to CWD).
     * @return true if all files exist and match expected sizes, false otherwise.
     */
    bool verify_files(const fs::path &base_path = fs::current_path()) const;

    /**
     * @brief Format torrent metadata as a human-readable or JSON string.
     * @param meta The metadata to format.
     * @param json_format If true, output JSON; otherwise plain text.
     * @return Formatted metadata string.
     */
    static std::string format_metadata(const TorrentMetadata &meta, bool json_format = false);

    /**
     * @brief Format the file list as a human-readable or JSON string.
     * @param meta The metadata whose files to list.
     * @param json_format If true, output JSON; otherwise plain text.
     * @return Formatted file tree string.
     */
    static std::string format_file_tree(const TorrentMetadata &meta, bool json_format = false);

  private:
    fs::path torrent_path_;
    std::unique_ptr<libtorrent::torrent_info> torrent_info_;
    std::vector<char> raw_buffer_; // stored during parse for manual bencode extraction of custom fields

    void parse_torrent_file();
    std::string compute_info_hash_v1(const libtorrent::info_hash_t &hash) const;
    std::string compute_info_hash_v2(const libtorrent::info_hash_t &hash) const;
    void generate_magnet_link(TorrentMetadata &meta);
    std::vector<std::string> flatten_tracker_list() const;
};

#endif // TORRENT_INSPECTOR_HPP
