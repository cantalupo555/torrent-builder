#ifndef TORRENT_CHECKER_HPP
#define TORRENT_CHECKER_HPP

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <filesystem>
#include <unordered_map>
#include <fstream>

namespace fs = std::filesystem;

namespace libtorrent
{
class torrent_info;
} // namespace libtorrent

namespace lt = libtorrent;

/**
 * @brief Result of verifying local files against a .torrent file.
 *
 * Contains counts of verified/corrupted pieces, lists of missing, extra,
 * and corrupted items, per-file verification details, and an overall pass/fail status.
 */
struct CheckResult
{
    bool passed = false;                          ///< true if all pieces verified and no files missing
    double completion_percentage = 0.0;           ///< pieces_verified / pieces_total * 100
    int64_t total_size_expected = 0;              ///< Sum of all file sizes in the torrent
    int64_t total_size_verified = 0;              ///< Sum of actual sizes of files that exist on disk
    int32_t pieces_total = 0;                     ///< Total number of pieces in the torrent
    int32_t pieces_verified = 0;                  ///< Pieces that passed hash verification
    int32_t pieces_corrupted = 0;                 ///< Pieces that failed hash verification

    /**
     * @brief A file expected by the torrent but not found on disk.
     */
    struct MissingFile
    {
        std::string path;                         ///< Relative path within the torrent
        int64_t expected_size;                    ///< Expected file size in bytes
    };
    std::vector<MissingFile> missing_files;

    /**
     * @brief A piece whose computed hash does not match the expected hash.
     */
    struct CorruptedPiece
    {
        int32_t index;                            ///< Zero-based piece index
        int64_t offset;                           ///< Byte offset from start of torrent data
    };
    std::vector<CorruptedPiece> corrupted_pieces;

    /**
     * @brief A file on disk not referenced by the torrent.
     */
    struct ExtraFile
    {
        std::string path;                         ///< Relative path from the content root
        int64_t size;                             ///< File size in bytes
    };
    std::vector<ExtraFile> extra_files;

    /**
     * @brief Per-file verification details.
     */
    struct FileResult
    {
        std::string path;                         ///< Relative path within the torrent
        int64_t expected_size;                    ///< Size declared in the torrent
        int64_t actual_size;                      ///< Size on disk (0 if missing)
        bool exists;                              ///< Whether the file exists on disk
        bool size_matches;                        ///< Whether actual_size equals expected_size
    };
    std::vector<FileResult> file_results;
};

/**
 * @brief Verify local files against a .torrent file.
 *
 * Loads a .torrent file, checks for missing files, verifies piece hashes
 * (v1 SHA-1, v2 SHA-256, or both for hybrid torrents), and detects extra
 * files not referenced by the torrent.
 */
class TorrentChecker
{
  public:
    /**
     * @brief Construct a checker and parse the given .torrent file.
     * @param torrent_path Path to a valid .torrent file.
     * @throws std::runtime_error if the file cannot be read or parsed.
     */
    explicit TorrentChecker(const fs::path &torrent_path);

    /**
     * @brief Destructor. Releases the loaded torrent_info handle.
     */
    ~TorrentChecker();

    /**
     * @brief Run a full verification of local content against the loaded torrent.
     * @param content_path Root directory containing the local files.
     * @param verbose If true, print per-piece progress to stdout.
     * @return Populated CheckResult with all verification findings.
     * @throws std::runtime_error if the torrent info is not loaded or the content path does not exist.
     */
    CheckResult check(const fs::path &content_path, bool verbose = false);

    /**
     * @brief Format verification results as human-readable text or JSON.
     * @param result The verification result to format.
     * @param json_format If true, output JSON; otherwise plain text.
     * @return Formatted result string.
     */
    static std::string format_result(const CheckResult &result, bool json_format = false);

  private:
    fs::path torrent_path_;
    std::vector<char> raw_buffer_;
    std::unique_ptr<lt::torrent_info> torrent_info_;
    std::vector<char> piece_buffer_;
    std::unordered_map<std::string, std::ifstream> open_files_;

    void load_torrent();
    void close_all_files();
    std::ifstream &get_file_stream(const fs::path &file_path);

    int find_file_for_piece(int64_t piece_offset, int64_t piece_end) const;

    void read_piece_data(int piece_index, const int piece_length,
                         const int64_t total_size,
                         const lt::torrent_info &info,
                         const fs::path &base_path);

    bool verify_piece_v1(int piece_index) const;
    bool verify_piece_v2(int piece_index, const lt::torrent_info &info) const;

    std::vector<CheckResult::CorruptedPiece> verify_all_pieces(const fs::path &base_path,
                                                                bool verbose);

    std::vector<CheckResult::ExtraFile> find_extra_files(const fs::path &base_path);
    std::vector<CheckResult::MissingFile> check_missing_files(const fs::path &base_path,
                                                               std::vector<CheckResult::FileResult> &file_results);
};

#endif // TORRENT_CHECKER_HPP
