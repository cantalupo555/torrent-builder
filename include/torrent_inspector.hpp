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

struct TorrentMetadata
{
    // Basic info
    std::string name;
    std::string info_hash_v1;
    std::string info_hash_v2;
    bool is_hybrid = false;

    // Size info
    int64_t total_size = 0;
    int64_t piece_length = 0;
    int32_t piece_count = 0;

    // Files
    struct FileInfo
    {
        std::string path;
        int64_t size;
        std::optional<std::string> symlink_path;
    };
    std::vector<FileInfo> files;
    std::vector<std::vector<std::string>> file_tree;

    // Trackers
    std::vector<std::string> trackers;
    std::vector<std::vector<std::string>> tracker_tiers;

    // Web seeds
    std::vector<std::string> web_seeds;

    // Flags
    bool is_private = false;

    // Optional fields
    std::optional<std::string> source;
    std::optional<std::string> comment;
    std::optional<int64_t> creation_date;
    std::optional<std::string> created_by;

    // Magnet link
    std::string magnet_link;
};

class TorrentInspector
{
  public:
    explicit TorrentInspector(const fs::path &torrent_path);
    ~TorrentInspector();

    TorrentMetadata inspect();
    bool verify_files(const fs::path &base_path = fs::current_path()) const;

    static std::string format_metadata(const TorrentMetadata &meta, bool json_format = false);
    static std::string format_file_tree(const TorrentMetadata &meta, bool json_format = false);

  private:
    fs::path torrent_path_;
    std::unique_ptr<libtorrent::torrent_info> torrent_info_;

    void parse_torrent_file();
    std::string compute_info_hash(const libtorrent::info_hash_t &hash, bool v2) const;
    void generate_magnet_link(TorrentMetadata &meta);
    std::vector<std::string> flatten_tracker_list() const;
};

#endif // TORRENT_INSPECTOR_HPP
