#ifndef CREATE_TORRENT_HPP
#define CREATE_TORRENT_HPP

#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/error_code.hpp>

#include <string>
#include <vector>
#include <optional>
#include <iostream>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

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

    TorrentConfig(fs::path p, fs::path o, std::vector<std::string> t, 
                 TorrentVersion v, std::optional<std::string> c = std::nullopt,
                 bool priv = false, std::vector<std::string> ws = {})
        : path(p), output(o), trackers(t), version(v), 
          comment(c), is_private(priv), web_seeds(ws) 
    {
        if (!fs::exists(path)) {
            throw std::runtime_error("Invalid path: " + path.string());
        }
    }
};

class TorrentCreator {
public:
    explicit TorrentCreator(const TorrentConfig& config);
    void create_torrent();

private:
    TorrentConfig config_;
    lt::file_storage fs_;
    

    // Outras funções
    static int auto_piece_size(int64_t total_size);
    int get_torrent_flags() const;
    void add_files_to_storage();
    void print_torrent_summary(int64_t total_size, int piece_size, int num_pieces) const;
};

#endif // CREATE_TORRENT_HPP
