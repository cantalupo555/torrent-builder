#include "torrent_inspector.hpp"
#include "logger.hpp"
#include "utils.hpp"
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/info_hash.hpp>
#include <libtorrent/file_storage.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <map>

TorrentInspector::TorrentInspector(const fs::path &torrent_path) : torrent_path_(torrent_path)
{
    parse_torrent_file();
}

TorrentInspector::~TorrentInspector() = default;

void TorrentInspector::parse_torrent_file()
{
    try
    {
        if (!fs::exists(torrent_path_))
        {
            throw std::runtime_error("Torrent file does not exist: " + torrent_path_.string());
        }

        std::ifstream file(torrent_path_, std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("Cannot open torrent file: " + torrent_path_.string());
        }

        std::vector<char> buffer((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
        file.close();

        torrent_info_ = std::make_unique<libtorrent::torrent_info>(
            libtorrent::span<const char>(buffer.data(), buffer.size()), libtorrent::from_span);
    }
    catch (const libtorrent::system_error &e)
    {
        throw std::runtime_error("Failed to parse torrent file: " + std::string(e.what()));
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("Error reading torrent file: " + std::string(e.what()));
    }
}

std::string TorrentInspector::compute_info_hash(const libtorrent::info_hash_t &hash) const
{
    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    if (hash.has_v1())
    {
        for (unsigned char byte : hash.v1)
        {
            ss << std::setw(2) << static_cast<int>(byte);
        }
    }
    else if (hash.has_v2())
    {
        for (unsigned char byte : hash.v2)
        {
            ss << std::setw(2) << static_cast<int>(byte);
        }
    }

    return ss.str();
}

void TorrentInspector::generate_magnet_link(TorrentMetadata &meta)
{
    std::stringstream magnet;
    magnet << "magnet:?xt=urn:btih:" << meta.info_hash_v1;

    if (meta.is_hybrid && !meta.info_hash_v2.empty())
    {
        magnet << "&xt=urn:btmh:" << meta.info_hash_v2;
    }

    magnet << "&dn=" << utils::url_encode(meta.name);

    for (const auto &tracker : flatten_tracker_list())
    {
        magnet << "&tr=" << utils::url_encode(tracker);
    }

    for (const auto &webseed : meta.web_seeds)
    {
        magnet << "&ws=" << utils::url_encode(webseed);
    }

    meta.magnet_link = magnet.str();
}

std::vector<std::string> TorrentInspector::flatten_tracker_list() const
{
    std::vector<std::string> all_trackers;
    if (torrent_info_)
    {
        const auto &trackers = torrent_info_->trackers();
        for (const auto &entry : trackers)
        {
            all_trackers.push_back(entry.url);
        }
    }
    return all_trackers;
}

TorrentMetadata TorrentInspector::inspect()
{
    TorrentMetadata meta;

    if (!torrent_info_)
    {
        throw std::runtime_error("Torrent info not loaded");
    }

    meta.name = torrent_info_->name();
    meta.piece_length = torrent_info_->piece_length();
    meta.piece_count = torrent_info_->num_pieces();
    meta.total_size = torrent_info_->total_size();

    const auto &info_hash = torrent_info_->info_hashes();
    meta.info_hash_v1 = compute_info_hash(info_hash);
    meta.info_hash_v2 = info_hash.has_v2() ? compute_info_hash(info_hash) : "";
    meta.is_hybrid = info_hash.has_v1() && info_hash.has_v2();

    const auto &files = torrent_info_->files();
    for (int i = 0; i < files.num_files(); ++i)
    {
        TorrentMetadata::FileInfo file_info;

        file_info.path = files.file_path(i);
        file_info.size = files.file_size(i);

        if (files.file_flags(i) & libtorrent::file_storage::flag_symlink)
        {
            file_info.symlink_path = files.symlink(i);
        }

        meta.files.push_back(file_info);
    }

    const auto &trackers = torrent_info_->trackers();
    for (const auto &entry : trackers)
    {
        meta.trackers.push_back(entry.url);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    for (const auto &url_seed : torrent_info_->url_seeds())
    {
        meta.web_seeds.push_back(url_seed);
    }
    for (const auto &http_seed : torrent_info_->http_seeds())
    {
        meta.web_seeds.push_back(http_seed);
    }
#pragma GCC diagnostic pop

    meta.is_private = torrent_info_->priv();

    auto comment = torrent_info_->comment();
    if (!comment.empty())
    {
        meta.comment = comment;
    }

    auto creator = torrent_info_->creator();
    if (!creator.empty())
    {
        meta.created_by = creator;
    }

    if (torrent_info_->creation_date() != 0)
    {
        meta.creation_date = torrent_info_->creation_date();
    }

    generate_magnet_link(meta);

    return meta;
}

bool TorrentInspector::verify_files(const fs::path &base_path) const
{
    if (!torrent_info_)
    {
        log_message("Torrent info not loaded", LogLevel::ERR);
        return false;
    }

    try
    {
        const auto &files = torrent_info_->files();
        for (int i = 0; i < files.num_files(); ++i)
        {
            fs::path file_path = base_path / files.file_path(i);

            if (!fs::exists(file_path))
            {
                log_message("Missing file: " + file_path.string(), LogLevel::WARNING);
                return false;
            }

            if (fs::file_size(file_path) != files.file_size(i))
            {
                log_message("File size mismatch: " + file_path.string(), LogLevel::WARNING);
                return false;
            }
        }
        return true;
    }
    catch (const std::exception &e)
    {
        log_message("Verification failed: " + std::string(e.what()), LogLevel::ERR);
        return false;
    }
}

std::string TorrentInspector::format_metadata(const TorrentMetadata &meta, bool json_format)
{
    if (json_format)
    {
        std::stringstream json;
        json << "{\n";
        json << "  \"name\": \"" << utils::escape_json(meta.name) << "\",\n";
        json << "  \"info_hash_v1\": \"" << meta.info_hash_v1 << "\",\n";
        json << "  \"info_hash_v2\": \"" << meta.info_hash_v2 << "\",\n";
        json << "  \"is_hybrid\": " << (meta.is_hybrid ? "true" : "false") << ",\n";
        json << "  \"total_size\": " << meta.total_size << ",\n";
        json << "  \"piece_length\": " << meta.piece_length << ",\n";
        json << "  \"piece_count\": " << meta.piece_count << ",\n";
        json << "  \"files_count\": " << meta.files.size() << ",\n";
        json << "  \"is_private\": " << (meta.is_private ? "true" : "false") << ",\n";

        if (meta.comment)
        {
            json << "  \"comment\": \"" << utils::escape_json(*meta.comment) << "\",\n";
        }
        if (meta.creation_date)
        {
            json << "  \"creation_date\": " << *meta.creation_date << ",\n";
        }
        if (meta.created_by)
        {
            json << "  \"created_by\": \"" << utils::escape_json(*meta.created_by) << "\",\n";
        }

        json << "  \"trackers\": [\n";
        for (size_t i = 0; i < meta.trackers.size(); ++i)
        {
            json << "    \"" << utils::escape_json(meta.trackers[i]) << "\"";
            if (i < meta.trackers.size() - 1)
                json << ",";
            json << "\n";
        }
        json << "  ],\n";

        json << "  \"web_seeds\": [\n";
        for (size_t i = 0; i < meta.web_seeds.size(); ++i)
        {
            json << "    \"" << utils::escape_json(meta.web_seeds[i]) << "\"";
            if (i < meta.web_seeds.size() - 1)
                json << ",";
            json << "\n";
        }
        json << "  ],\n";

        json << "  \"magnet_link\": \"" << utils::escape_json(meta.magnet_link) << "\"\n";
        json << "}\n";

        return json.str();
    }
    else
    {
        std::stringstream output;
        output << "=== TORRENT INFORMATION ===\n\n";

        output << "Name: " << meta.name << "\n";
        output << "Info Hash (v1): " << meta.info_hash_v1 << "\n";
        if (!meta.info_hash_v2.empty())
        {
            output << "Info Hash (v2): " << meta.info_hash_v2 << "\n";
        }
        output << "Type: "
               << (meta.is_hybrid ? "Hybrid (v1+v2)" : (meta.info_hash_v2.empty() ? "v1" : "v2"))
               << "\n\n";

        output << "Total Size: " << utils::format_file_size(meta.total_size) << " ("
               << meta.total_size << " bytes)\n";
        output << "Piece Size: " << utils::format_file_size(meta.piece_length) << " ("
               << meta.piece_length << " bytes)\n";
        output << "Piece Count: " << meta.piece_count << "\n";
        output << "File Count: " << meta.files.size() << "\n\n";

        if (meta.is_private)
        {
            output << "Private: Yes\n";
        }
        else
        {
            output << "Private: No\n";
        }

        if (meta.comment)
        {
            output << "Comment: " << *meta.comment << "\n";
        }
        if (meta.creation_date)
        {
            output << "Creation Date: " << utils::format_timestamp(*meta.creation_date) << "\n";
        }
        if (meta.created_by)
        {
            output << "Created By: " << *meta.created_by << "\n";
        }

        output << "\n";

        if (!meta.trackers.empty())
        {
            output << "Trackers:\n";
            for (const auto &tracker : meta.trackers)
            {
                output << "  " << tracker << "\n";
            }
            output << "\n";
        }

        if (!meta.web_seeds.empty())
        {
            output << "Web Seeds:\n";
            for (const auto &seed : meta.web_seeds)
            {
                output << "  " << seed << "\n";
            }
            output << "\n";
        }

        output << "Magnet Link:\n";
        output << meta.magnet_link << "\n";

        return output.str();
    }
}

std::string TorrentInspector::format_file_tree(const TorrentMetadata &meta, bool json_format)
{
    if (json_format)
    {
        std::stringstream json;
        json << "{\n  \"files\": [\n";
        for (size_t i = 0; i < meta.files.size(); ++i)
        {
            const auto &file = meta.files[i];
            json << "    {\n";
            json << "      \"path\": \"" << utils::escape_json(file.path) << "\",\n";
            json << "      \"size\": " << file.size << ",\n";
            json << "      \"size_formatted\": \"" << utils::format_file_size(file.size) << "\"\n";
            if (file.symlink_path)
            {
                json << "      \"symlink\": \"" << utils::escape_json(*file.symlink_path) << "\"\n";
            }
            json << "    }";
            if (i < meta.files.size() - 1)
                json << ",";
            json << "\n";
        }
        json << "  ]\n}\n";
        return json.str();
    }
    else
    {
        std::stringstream output;
        output << "=== FILE TREE ===\n\n";

        for (const auto &file : meta.files)
        {
            output << "  " << file.path << " (" << utils::format_file_size(file.size) << ")\n";
        }
        output << "\n";

        return output.str();
    }
}
