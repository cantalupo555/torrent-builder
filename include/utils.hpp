#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <cstdint>

namespace utils {

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
bool is_valid_url(const std::string& url);

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

}

#endif
