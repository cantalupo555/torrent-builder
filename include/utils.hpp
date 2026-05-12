#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <cstdint>
#include <filesystem>
#include <vector>
#include <regex>

namespace utils
{

/**
 * @brief Percent-encode a string for safe embedding in URLs (RFC 3986).
 * @param str The raw string to encode.
 * @return URL-safe string with non-alphanumeric characters percent-encoded.
 */
std::string url_encode(const std::string &str);

/**
 * @brief Escape a string for safe embedding in JSON string literals.
 * @param str The raw string to escape.
 * @return JSON-safe string with control characters and special chars backslash-escaped.
 */
std::string escape_json(const std::string &str);

/**
 * @brief Format a byte count as a human-readable string using binary prefixes (KiB, MiB, etc.).
 * @param bytes Size in bytes.
 * @return Formatted string with 2 decimal places, e.g. "1.50 GiB".
 */
std::string format_file_size(int64_t bytes);

/**
 * @brief Format a Unix timestamp as a UTC date-time string.
 * @param timestamp Unix epoch time in seconds.
 * @return Formatted string "YYYY-MM-DD HH:MM:SS UTC", or "Invalid timestamp" for non-positive values.
 */
std::string format_timestamp(int64_t timestamp);
/**
 * @brief Calculate the recommended piece size based on total content size.
 * @param total_size Total size of content in bytes.
 * @return Piece size in bytes (power-of-2, from 16KB to 32MB).
 */
int auto_piece_size(int64_t total_size);

/**
 * @brief Check if a URL has a valid scheme (http, https, or udp).
 * @param url The URL string to validate.
 * @return true if the URL matches ^(http|https|udp)://.+$ (case-insensitive).
 */
bool is_valid_url(const std::string &url);

/**
 * @brief Trim whitespace, strip surrounding quotes, and remove escape backslashes.
 * @param input The raw path string.
 * @return Sanitized path with no surrounding whitespace/quotes and unescaped backslashes.
 */
std::string sanitize_path(std::string input);

/**
 * @brief Format a byte count as a human-readable string (B, KB, MB, GB, TB).
 * @param bytes Size in bytes.
 * @return Formatted string with 2 decimal places, e.g. "1.50 GB".
 */
std::string format_size(int64_t bytes);

/**
 * @brief Format a speed value in bytes/sec as MB/s.
 * @param speed Speed in bytes per second.
 * @return Formatted string, e.g. "2.50 MB/s".
 */
std::string format_speed(double speed);

/**
 * @brief Format an ETA in seconds as "Mm Ss".
 * @param eta Time remaining in seconds.
 * @return Formatted string, e.g. "5m 30s".
 */
std::string format_eta(double eta);

/**
 * @brief Convert a string to lowercase.
 * @param str The input string.
 * @return Lowercase copy of the input string.
 */
std::string to_lower(const std::string &str);

/**
 * @brief Check if a string starts with a given prefix.
 * @param str The string to check.
 * @param prefix The prefix to look for.
 * @return true if str begins with prefix.
 */
bool starts_with(const std::string &str, const std::string &prefix);

/**
 * @brief Join a vector of strings with a delimiter.
 * @param parts The string parts to join.
 * @param delimiter The separator between parts.
 * @return Joined string, or empty string if parts is empty.
 */
std::string join(const std::vector<std::string> &parts, const std::string &delimiter);

/**
 * @brief Split a string by a delimiter into a vector.
 * @param str The string to split.
 * @param delimiter The character to split on.
 * @return Vector of non-empty tokens.
 */
std::vector<std::string> split(const std::string &str, char delimiter);

/**
 * @brief Extract the domain (host) from a tracker URL.
 * @param tracker_url Full tracker URL (e.g., "https://tracker.example.com:8080/announce").
 * @return Domain string (e.g., "tracker.example.com"), or empty string if URL is invalid.
 */
std::string extract_domain(const std::string &tracker_url);

/**
 * @brief Sanitize a string for safe use as a filename component.
 *
 * Replaces Windows-invalid characters (: < > " | ? * \\ /) with underscores,
 * then collapses consecutive underscores into a single underscore.
 *
 * @param part The raw string to sanitize.
 * @return Sanitized string safe for use in filenames.
 */
std::string sanitize_filename_part(const std::string &part);

/**
 * @brief Truncate a filename to fit within a maximum byte limit.
 *
 * Detects and preserves the file extension (everything after the last dot).
 * A leading dot (dotfile) is treated as part of the stem, not an extension
 * separator, so dotfiles are truncated normally.
 * Truncates the stem portion, respecting UTF-8 multi-byte boundaries.
 * If truncation would leave an incomplete multi-byte character, the entire
 * character is either preserved (if it fits) or removed completely. Returns
 * the input unchanged if it already fits or if max_bytes is too small to hold
 * the extension.
 *
 * @param filename The filename to potentially truncate.
 * @param max_bytes Maximum allowed filename size in bytes (default 255).
 * @return Filename truncated to at most max_bytes bytes, unless max_bytes is
 *         too small to hold the file extension, in which case the input
 *         is returned unchanged (may exceed max_bytes).
 */
std::string truncate_filename(const std::string &filename, std::size_t max_bytes = 255);

/**
 * @brief Resolve filename collisions by appending (1), (2), etc.
 *
 * Note: TOCTOU race between exists() and actual file creation is acceptable
 * for a single-user CLI tool.
 *
 * @param directory The directory where the file would be created.
 * @param base_filename The desired filename.
 * @param max_bytes Maximum allowed filename size in bytes (0 = no limit).
 *                  When set, the stem is truncated to fit the collision suffix
 *                  within this limit.
 * @return A filename that does not exist in the directory.
 * @throws std::runtime_error if no unique name found after 1000 attempts.
 * @throws std::filesystem::filesystem_error if the directory cannot be accessed.
 */
std::string resolve_collision(const std::filesystem::path &directory,
                               const std::string &base_filename,
                               std::size_t max_bytes = 0);

/**
 * @brief Generate the output .torrent filename based on content and trackers.
 * @param content_path Path to the file or directory being torrented.
 * @param trackers List of tracker URLs.
 * @param skip_prefix If true, omit tracker domain prefix from filename.
 * @param tracker_index Index of tracker to use for domain prefix (0-based).
 *                      Negative or out-of-range values default to 0.
 * @return Generated filename (e.g., "tracker.example.com_ContentName.torrent").
 */
std::string generate_output_filename(const std::filesystem::path &content_path,
                                      const std::vector<std::string> &trackers,
                                      bool skip_prefix,
                                      int tracker_index = 0);

/**
 * @brief Generate the full auto-named output path with collision resolution.
 *
 * Orchestrates filename generation, truncation, and collision resolution.
 * The final filename fits within 255 bytes.
 *
 * @param content_path Path to the file or directory being torrented.
 * @param trackers List of tracker URLs.
 * @param skip_prefix If true, omit tracker domain prefix from filename.
 * @param tracker_index Index of tracker to use for domain prefix.
 * @param output_dir Directory for the output file (uses current path if empty).
 * @return Full output path with collision-resolved filename.
 * @throws std::runtime_error if a unique filename cannot be resolved after 1000 attempts.
 * @throws std::filesystem::filesystem_error if the working directory cannot be determined
 *         or the target directory is inaccessible.
 */
std::string generate_auto_output_path(const std::filesystem::path &content_path,
                                       const std::vector<std::string> &trackers,
                                       bool skip_prefix,
                                       int tracker_index,
                                       const std::filesystem::path &output_dir);

/**
 * @brief Convert a glob pattern to a compiled std::regex for case-insensitive matching.
 *
 * Supports the following glob tokens:
 *   - Asterisk (*) matches any sequence of characters except forward slash
 *   - Double-asterisk-slash (**) followed by slash matches zero or more directory
 *     levels; the slash itself is optional in the matched path
 *   - Double-asterisk (**) without trailing slash matches any sequence including slashes
 *   - Question mark (?) matches exactly one character
 *
 * All other characters are treated as literal matches.
 * Regex metacharacters are automatically escaped.
 *
 * @param pattern Glob pattern string.
 * @return Compiled std::regex with ECMAScript syntax and case-insensitive flag.
 * @throws std::regex_error if the resulting regex is invalid.
 */
std::regex glob_to_regex(const std::string &pattern);

/**
 * @brief Determine whether a file should be included based on exclude/include patterns.
 *
 * Decision logic (evaluated in order):
 *   1. If both lists are empty, include the file.
 *   2. If include patterns exist and the file matches any, include.
 *   3. If include patterns exist and the file matches none, exclude.
 *   4. If only exclude patterns exist and the file matches any, exclude.
 *   5. Otherwise, include.
 *
 * Include patterns take precedence over exclude patterns: a file matching both
 * an exclude and an include pattern is included.
 *
 * @param relative_path File path relative to the torrent root (using forward slashes).
 * @param exclude_regex Pre-compiled regex patterns for exclusion.
 * @param include_regex Pre-compiled regex patterns for inclusion (takes precedence).
 * @return true if the file should be included in the torrent, false to exclude it.
 */
bool should_include_file(const std::string &relative_path,
                          const std::vector<std::regex> &exclude_regex,
                          const std::vector<std::regex> &include_regex);

} // namespace utils

#endif
