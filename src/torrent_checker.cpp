#include "torrent_checker.hpp"
#include "logger.hpp"
#include "utils.hpp"
#include "output.hpp"
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/info_hash.hpp>
#include <sstream>
#include <unordered_set>
#include <algorithm>
#include <chrono>

TorrentChecker::TorrentChecker(const fs::path &torrent_path) : torrent_path_(torrent_path)
{
    load_torrent();
}

TorrentChecker::~TorrentChecker()
{
    close_all_files();
}

void TorrentChecker::load_torrent()
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

        raw_buffer_ = std::move(buffer);

        torrent_info_ = std::make_unique<lt::torrent_info>(
            lt::span<const char>(raw_buffer_.data(), raw_buffer_.size()), lt::from_span);
    }
    catch (const lt::system_error &e)
    {
        throw std::runtime_error("Failed to parse torrent file: " + std::string(e.what()));
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("Error reading torrent file: " + std::string(e.what()));
    }
}

void TorrentChecker::close_all_files()
{
    for (auto &[path, stream] : open_files_)
    {
        if (stream.is_open())
            stream.close();
    }
    open_files_.clear();
}

int TorrentChecker::find_file_for_piece(int64_t piece_offset, int64_t piece_end) const
{
    const auto &files = torrent_info_->files();
    int lo = 0, hi = files.num_files() - 1;
    int result = -1;

    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        int64_t file_end = files.file_offset(mid) + files.file_size(mid);

        if (file_end <= piece_offset)
        {
            lo = mid + 1;
        }
        else
        {
            result = mid;
            hi = mid - 1;
        }
    }

    return result;
}

std::ifstream &TorrentChecker::get_file_stream(const fs::path &file_path)
{
    auto key = file_path.string();
    auto it = open_files_.find(key);
    if (it != open_files_.end())
    {
        it->second.clear();
        it->second.seekg(0);
        return it->second;
    }

    if (open_files_.size() >= 64)
    {
        auto oldest = open_files_.begin();
        if (oldest->second.is_open())
            oldest->second.close();
        open_files_.erase(oldest);
    }

    auto [inserted, ok] = open_files_.emplace(key, std::ifstream(file_path, std::ios::binary));
    return inserted->second;
}

std::vector<CheckResult::MissingFile> TorrentChecker::check_missing_files(
    const fs::path &base_path, std::vector<CheckResult::FileResult> &file_results)
{
    std::vector<CheckResult::MissingFile> missing;
    const auto &files = torrent_info_->files();

    for (int i = 0; i < files.num_files(); ++i)
    {
        if (files.pad_file_at(i))
        {
            continue;
        }

        CheckResult::FileResult fr;
        fr.path = files.file_path(i);
        fr.expected_size = files.file_size(i);

        fs::path file_path = base_path / fr.path;
        std::error_code ec;
        fr.exists = fs::exists(file_path, ec);

        if (fr.exists)
        {
            fr.actual_size = fs::file_size(file_path, ec);
            if (ec)
            {
                fr.actual_size = 0;
                fr.exists = false;
            }
            fr.size_matches = (fr.actual_size == fr.expected_size);
        }
        else
        {
            fr.actual_size = 0;
            fr.size_matches = false;
        }

        if (!fr.exists)
        {
            CheckResult::MissingFile mf;
            mf.path = fr.path;
            mf.expected_size = fr.expected_size;
            missing.push_back(mf);
            log_message("Missing file: " + file_path.string(), LogLevel::WARNING);
        }
        else if (!fr.size_matches)
        {
            log_message("File size mismatch: " + file_path.string()
                            + " (expected " + std::to_string(fr.expected_size)
                            + ", got " + std::to_string(fr.actual_size) + ")",
                        LogLevel::WARNING);
        }

        file_results.push_back(fr);
    }

    return missing;
}

void TorrentChecker::read_piece_data(int piece_index, const int piece_length,
                                      const int64_t total_size,
                                      const lt::torrent_info &info,
                                      const fs::path &base_path)
{
    const auto &files = info.files();
    int64_t piece_start = static_cast<int64_t>(piece_index) * piece_length;
    int64_t piece_end = std::min(piece_start + piece_length, total_size);
    int64_t piece_size = piece_end - piece_start;

    piece_buffer_.assign(piece_size, '\0');

    int start_file = find_file_for_piece(piece_start, piece_end);
    if (start_file < 0)
        return;

    for (int i = start_file; i < files.num_files(); ++i)
    {
        int64_t file_start = files.file_offset(i);
        int64_t file_end = file_start + files.file_size(i);

        if (file_start >= piece_end)
            break;

        if (file_end <= piece_start)
            continue;

        if (files.pad_file_at(i))
            continue;

        int64_t read_start_in_file = std::max(file_start, piece_start) - file_start;
        int64_t read_end_in_file = std::min(file_end, piece_end) - file_start;
        int64_t read_size = read_end_in_file - read_start_in_file;
        int64_t buf_pos = std::max(file_start, piece_start) - piece_start;

        fs::path file_path = base_path / files.file_path(i);

        std::ifstream &f = get_file_stream(file_path);
        if (!f.is_open())
        {
            log_message("Cannot open file for piece " + std::to_string(piece_index)
                            + ": " + file_path.string(),
                        LogLevel::WARNING);
            continue;
        }

        f.clear();
        f.seekg(read_start_in_file);

        if (read_end_in_file > files.file_size(i))
        {
            int64_t available = files.file_size(i) - read_start_in_file;
            if (available > 0)
            {
                f.read(piece_buffer_.data() + buf_pos, available);
                auto got = f.gcount();
                if (got < available)
                {
                    log_message("Short read for piece " + std::to_string(piece_index)
                                    + " from file: " + file_path.string()
                                    + " (read " + std::to_string(got)
                                    + " of " + std::to_string(available) + " bytes)",
                                LogLevel::WARNING);
                }
            }
            continue;
        }

        f.read(piece_buffer_.data() + buf_pos, read_size);

        if (!f)
        {
            auto got = f.gcount();
            log_message("Short read for piece " + std::to_string(piece_index)
                            + " from file: " + file_path.string()
                            + " (read " + std::to_string(got)
                            + " of " + std::to_string(read_size) + " bytes)",
                        LogLevel::WARNING);
        }
    }
}

bool TorrentChecker::verify_piece_v1(int piece_index) const
{
    lt::hasher h;
    h.update(piece_buffer_.data(), static_cast<int>(piece_buffer_.size()));
    lt::sha1_hash computed = h.final();
    lt::sha1_hash expected = torrent_info_->hash_for_piece(lt::piece_index_t(piece_index));
    return computed == expected;
}

bool TorrentChecker::verify_piece_v2(int piece_index, const lt::torrent_info &info) const
{
    const auto &files = info.files();
    int piece_length = info.piece_length();
    int64_t piece_offset = static_cast<int64_t>(piece_index) * piece_length;
    int64_t piece_end = piece_offset + piece_length;

    int file_idx = find_file_for_piece(piece_offset, piece_end);
    if (file_idx < 0)
        return true;

    for (int i = file_idx; i < files.num_files(); ++i)
    {
        if (files.pad_file_at(i))
            continue;

        int64_t file_start = files.file_offset(i);
        int64_t file_size = files.file_size(i);
        int64_t file_end = file_start + file_size;

        if (file_start >= piece_end)
            break;

        if (!(piece_offset >= file_start && piece_offset < file_end))
            continue;

        auto layer = info.piece_layer(lt::file_index_t(i));
        if (layer.empty())
            return true;

        int64_t file_piece_offset = file_start / piece_length;
        int local_index = piece_index - static_cast<int>(file_piece_offset);

        if (local_index < 0 || local_index >= static_cast<int>(layer.size() / 32))
            return true;

        int64_t data_start = std::max(int64_t(0), file_start - piece_offset);
        int64_t data_end = std::min(static_cast<int64_t>(piece_length), file_end - piece_offset);
        int actual_size = static_cast<int>(data_end - data_start);

        lt::hasher256 h;
        h.update(piece_buffer_.data() + data_start, actual_size);
        lt::sha256_hash computed = h.final();

        lt::sha256_hash expected;
        std::memcpy(expected.data(), layer.data() + local_index * 32, 32);

        return computed == expected;
    }

    return true;
}

std::vector<CheckResult::CorruptedPiece> TorrentChecker::verify_all_pieces(const fs::path &base_path,
                                                                            bool verbose)
{
    std::vector<CheckResult::CorruptedPiece> corrupted;
    int piece_length = torrent_info_->piece_length();
    int64_t total_size = torrent_info_->total_size();
    int num_pieces = torrent_info_->num_pieces();

    const auto &info_hash = torrent_info_->info_hashes();
    bool has_v1 = info_hash.has_v1();
    bool has_v2 = info_hash.has_v2();

    log_message("Starting verification for: " + torrent_path_.string()
                    + " (" + std::to_string(num_pieces) + " pieces)",
                LogLevel::INFO);

    auto start_time = std::chrono::steady_clock::now();
    int64_t bytes_processed = 0;

    for (int i = 0; i < num_pieces; ++i)
    {
        int64_t piece_start = static_cast<int64_t>(i) * piece_length;
        int64_t piece_size = std::min(static_cast<int64_t>(piece_length), total_size - piece_start);

        read_piece_data(i, piece_length, total_size, *torrent_info_, base_path);

        bool piece_ok = true;

        if (has_v1)
        {
            if (!verify_piece_v1(i))
                piece_ok = false;
        }

        if (piece_ok && has_v2)
        {
            if (!verify_piece_v2(i, *torrent_info_))
                piece_ok = false;
        }

        if (!piece_ok)
        {
            CheckResult::CorruptedPiece cp;
            cp.index = i;
            cp.offset = piece_start;
            corrupted.push_back(cp);
            log_message("Corrupted piece #" + std::to_string(i)
                            + " at offset " + std::to_string(piece_start),
                        LogLevel::WARNING);
        }

        bytes_processed += piece_size;

        if (verbose)
        {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start_time).count();
            double speed = elapsed > 0 ? bytes_processed / elapsed : 0;
            double remaining_bytes = total_size - bytes_processed;
            double eta = speed > 0 ? remaining_bytes / speed : 0;

            print_progress(i + 1, num_pieces, speed, eta, bytes_processed, total_size);
        }
    }

    log_message("Verification complete: "
                    + std::to_string(num_pieces - static_cast<int>(corrupted.size()))
                    + "/" + std::to_string(num_pieces) + " pieces OK, "
                    + std::to_string(corrupted.size()) + " corrupted",
                LogLevel::INFO);

    close_all_files();

    return corrupted;
}

std::vector<CheckResult::ExtraFile> TorrentChecker::find_extra_files(const fs::path &base_path)
{
    std::vector<CheckResult::ExtraFile> extra;
    const auto &files = torrent_info_->files();

    std::unordered_set<std::string> torrent_paths;
    for (int i = 0; i < files.num_files(); ++i)
    {
        if (files.pad_file_at(i))
            continue;

        fs::path p = fs::path(files.file_path(i));
        std::string normalized = p.generic_string();
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char c)
                       { return std::tolower(c); });
        torrent_paths.insert(normalized);
    }

    std::error_code ec;
    if (!fs::exists(base_path, ec) || !fs::is_directory(base_path, ec))
        return extra;

    for (const auto &entry : fs::recursive_directory_iterator(base_path, ec))
    {
        if (ec)
            break;

        if (entry.is_symlink())
            continue;
        if (!entry.is_regular_file())
            continue;

        fs::path rel = entry.path().lexically_relative(base_path);
        std::string normalized = rel.generic_string();
        std::string normalized_lower = normalized;
        std::transform(normalized_lower.begin(), normalized_lower.end(),
                       normalized_lower.begin(),
                       [](unsigned char c)
                       { return std::tolower(c); });

        if (torrent_paths.find(normalized_lower) == torrent_paths.end())
        {
            CheckResult::ExtraFile ef;
            ef.path = normalized;
            ef.size = entry.file_size(ec);
            if (ec)
                ef.size = 0;
            extra.push_back(ef);
            log_message("Extra file found: " + entry.path().string(), LogLevel::INFO);
        }
    }

    return extra;
}

CheckResult TorrentChecker::check(const fs::path &content_path, bool verbose)
{
    if (!torrent_info_)
    {
        log_message("Torrent info not loaded", LogLevel::ERR);
        throw std::runtime_error("Torrent info not loaded");
    }

    std::error_code ec;
    if (!fs::exists(content_path, ec))
    {
        log_message("Content path does not exist: " + content_path.string(), LogLevel::ERR);
        throw std::runtime_error("Content path does not exist: " + content_path.string());
    }

    CheckResult result;
    result.pieces_total = torrent_info_->num_pieces();
    result.total_size_expected = torrent_info_->total_size();

    result.missing_files = check_missing_files(content_path, result.file_results);

    result.corrupted_pieces = verify_all_pieces(content_path, verbose);

    result.extra_files = find_extra_files(content_path);

    result.pieces_corrupted = static_cast<int32_t>(result.corrupted_pieces.size());
    result.pieces_verified = result.pieces_total - result.pieces_corrupted;

    if (result.pieces_total > 0)
    {
        result.completion_percentage =
            (static_cast<double>(result.pieces_verified) / result.pieces_total) * 100.0;
    }

    result.total_size_verified = 0;
    for (const auto &fr : result.file_results)
    {
        if (fr.exists)
        {
            result.total_size_verified += fr.actual_size;
        }
    }

    result.passed = result.missing_files.empty() && result.corrupted_pieces.empty();

    return result;
}

static std::string format_result_json(const CheckResult &result)
{
    std::stringstream json;
    json << "{\n";
    json << "  \"status\": \"" << (result.passed ? "PASS" : "FAIL") << "\",\n";
    json << "  \"completion_percentage\": "
         << std::fixed << std::setprecision(1) << result.completion_percentage << ",\n";
    json << "  \"pieces\": {\n";
    json << "    \"total\": " << result.pieces_total << ",\n";
    json << "    \"verified\": " << result.pieces_verified << ",\n";
    json << "    \"corrupted\": " << result.pieces_corrupted << ",\n";
    json << "    \"corrupted_pieces\": [\n";
    for (size_t i = 0; i < result.corrupted_pieces.size(); ++i)
    {
        const auto &cp = result.corrupted_pieces[i];
        json << "      {\"index\": " << cp.index << ", \"offset\": " << cp.offset << "}";
        if (i < result.corrupted_pieces.size() - 1)
            json << ",";
        json << "\n";
    }
    json << "    ]\n";
    json << "  },\n";
    json << "  \"size\": {\n";
    json << "    \"expected\": " << result.total_size_expected << ",\n";
    json << "    \"verified\": " << result.total_size_verified << ",\n";
    json << "    \"expected_formatted\": \""
         << utils::format_file_size(result.total_size_expected) << "\",\n";
    json << "    \"verified_formatted\": \""
         << utils::format_file_size(result.total_size_verified) << "\"\n";
    json << "  },\n";
    json << "  \"missing_files\": [\n";
    for (size_t i = 0; i < result.missing_files.size(); ++i)
    {
        const auto &mf = result.missing_files[i];
        json << "    {\"path\": \"" << utils::escape_json(mf.path)
             << "\", \"expected_size\": " << mf.expected_size
             << ", \"expected_size_formatted\": \""
             << utils::format_file_size(mf.expected_size) << "\"}";
        if (i < result.missing_files.size() - 1)
            json << ",";
        json << "\n";
    }
    json << "  ],\n";
    json << "  \"extra_files\": [\n";
    for (size_t i = 0; i < result.extra_files.size(); ++i)
    {
        const auto &ef = result.extra_files[i];
        json << "    {\"path\": \"" << utils::escape_json(ef.path)
             << "\", \"size\": " << ef.size
             << ", \"size_formatted\": \"" << utils::format_file_size(ef.size)
             << "\"}";
        if (i < result.extra_files.size() - 1)
            json << ",";
        json << "\n";
    }
    json << "  ]\n";
    json << "}\n";
    return json.str();
}

static std::string format_result_text(const CheckResult &result)
{
    std::stringstream out;
    out << "Results:\n";
    out << "  Status: " << (result.passed ? "PASS" : "FAIL") << "\n";
    out << "  Completion: " << std::fixed << std::setprecision(1)
        << result.completion_percentage << "%\n";
    out << "  Pieces: " << result.pieces_verified << " / " << result.pieces_total
        << " verified";
    if (result.pieces_corrupted > 0)
        out << " (" << result.pieces_corrupted << " corrupted)";
    out << "\n";
    out << "  Size: " << utils::format_file_size(result.total_size_verified)
        << " / " << utils::format_file_size(result.total_size_expected) << "\n";

    if (!result.missing_files.empty())
    {
        out << "\nMissing files:\n";
        for (const auto &mf : result.missing_files)
        {
            out << "  - " << mf.path << " (expected "
                << utils::format_file_size(mf.expected_size) << ")\n";
        }
    }

    if (!result.corrupted_pieces.empty())
    {
        out << "\nCorrupted pieces:\n";
        for (const auto &cp : result.corrupted_pieces)
        {
            out << "  - Piece #" << cp.index << " at offset "
                << utils::format_file_size(cp.offset) << "\n";
        }
    }

    if (!result.extra_files.empty())
    {
        out << "\nExtra files:\n";
        for (const auto &ef : result.extra_files)
        {
            out << "  - " << ef.path << " (" << utils::format_file_size(ef.size) << ")\n";
        }
    }

    return out.str();
}

std::string TorrentChecker::format_result(const CheckResult &result, bool json_format)
{
    if (json_format)
        return format_result_json(result);
    return format_result_text(result);
}
