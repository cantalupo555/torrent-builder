#ifndef SEASON_PACK_HPP
#define SEASON_PACK_HPP

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <utility>

/**
 * @brief Result of season pack analysis for a directory.
 *
 * Contains detected episode numbers, missing episodes, season metadata,
 * and flags indicating whether the content appears to be a TV season pack
 * with potential gaps.
 */
struct SeasonPackInfo {
    std::vector<int> episodes;          ///< Detected episode numbers, sorted ascending
    std::vector<int> missing_episodes;  ///< Episodes missing from 1..max_episode
    int season;                         ///< Detected season number (0 if none found)
    int max_episode;                    ///< Highest episode number found
    int video_file_count;              ///< Number of video files scanned
    bool is_season_pack;               ///< True if 2+ distinct episodes detected
    bool is_suspicious;                ///< True if episodes.size() < max_episode

    SeasonPackInfo()
        : season(0), max_episode(0), video_file_count(0),
          is_season_pack(false), is_suspicious(false) {}
};

namespace season_pack
{

/**
 * @brief Analyze a directory for TV season pack completeness.
 *
 * Recursively scans video files, detects season/episode numbers from filenames,
 * and identifies gaps in the episode sequence.
 *
 * @param input_path Path to a directory (single files return is_season_pack=false).
 * @return SeasonPackInfo with detection results.
 */
SeasonPackInfo analyze(const std::filesystem::path &input_path);

/**
 * @brief Extract season number from a path string using naming conventions.
 *
 * Matches patterns like .S01, .Season.1, /Season 1/, .S01.Complete, etc.
 *
 * @param path Directory or file path string to analyze.
 * @return Season number (1-based), or 0 if no season pattern found.
 */
int detect_season_number(const std::string &path);

/**
 * @brief Extract season and episode numbers from a filename.
 *
 * Supports S01E01, s01e01, and 1x01 naming conventions.
 *
 * @param filename Filename string (not full path).
 * @return Pair of {season, episode}. Either or both may be 0 if not detected.
 */
std::pair<int, int> extract_season_episode(const std::string &filename);

/**
 * @brief Extract episode numbers from a multi-episode filename.
 *
 * Matches patterns like S01E01-E03 or S01E01E03, returning all episodes
 * in the range [start, end].
 *
 * @param filename Filename string.
 * @return Vector of episode numbers, or empty if no multi-episode pattern matched.
 */
std::vector<int> extract_multi_episodes(const std::string &filename);

/**
 * @brief Check if a file path has a recognized video extension.
 *
 * Supports: .mkv, .mp4, .avi, .ts, .m2ts, .webm, .flv, .wmv,
 * .mov, .m4v, .mpg, .mpeg, .ogv (case-insensitive).
 *
 * @param path File path string.
 * @return true if the file extension is a recognized video format.
 */
bool is_video_file(const std::string &path);

/**
 * @brief Format missing episode numbers as a human-readable string.
 *
 * Produces zero-padded episode codes joined by commas, e.g. "E03, E05, E09".
 *
 * @param missing Episode numbers to format.
 * @return Formatted string.
 */
std::string format_missing_episodes(const std::vector<int> &missing);

/**
 * @brief Evaluate whether a season pack warning should be raised.
 *
 * Analyzes the input path for season pack completeness. If the pack is
 * suspicious (has missing episodes), logs a warning and returns an error
 * message. If the pack is complete, logs an info message.
 *
 * @param input_path Path to analyze.
 * @param fail_on_warning Whether the feature is enabled.
 * @param job_index Batch job index for log prefix (-1 = no prefix, for CLI).
 * @return std::nullopt if safe to proceed, or an error message string if the
 *         season pack has missing episodes and should be rejected.
 */
std::optional<std::string> evaluate_season_warning(
    const std::filesystem::path &input_path,
    bool fail_on_warning,
    int job_index = -1);

}

#endif
