#ifndef OUTPUT_HPP
#define OUTPUT_HPP

#include <string>
#include <cstdint>

/**
 * Console output verbosity levels.
 *
 * - QUIET:   Only errors (stderr). Used for scripts/CI and JSON mode.
 * - NORMAL:  Default: progress bar + summary + info messages.
 * - VERBOSE: Extra detail: piece size reasoning, file discovery, tracker tiers.
 */
enum class Verbosity {
    QUIET,
    NORMAL,
    VERBOSE
};

/**
 * @brief Set the global console verbosity level.
 * @param level QUIET, NORMAL, or VERBOSE.
 */
void set_verbosity(Verbosity level);

/**
 * @brief Get the current global verbosity level.
 * @return Current Verbosity level.
 */
Verbosity get_verbosity();

/**
 * @brief Enable or disable JSON output mode.
 *
 * When enabled, suppresses all human-readable console output so that
 * only the final JSON object is written to stdout.
 *
 * @param enabled true to enable JSON mode.
 */
void set_json_mode(bool enabled);

/**
 * @brief Check whether JSON output mode is active.
 * @return true if JSON mode is enabled.
 */
bool is_json_mode();

/**
 * @brief Print an informational message to stdout.
 *
 * Suppressed in QUIET and JSON modes.
 *
 * @param msg The message to print.
 */
void print_info(const std::string& msg);

/**
 * @brief Print a verbose diagnostic message to stdout.
 *
 * Only printed when verbosity is VERBOSE.
 *
 * @param msg The message to print.
 */
void print_verbose(const std::string& msg);

/**
 * @brief Print an error message to stderr.
 *
 * Always printed regardless of verbosity or JSON mode.
 *
 * @param msg The error message to print.
 */
void print_error(const std::string& msg);

/**
 * @brief Print a progress bar to stdout.
 *
 * Displays an ASCII progress bar with percentage, processed/total size,
 * hashing speed, and ETA. Suppressed in QUIET and JSON modes.
 *
 * @param progress    Number of pieces completed.
 * @param total       Total number of pieces.
 * @param speed       Hashing speed in bytes per second.
 * @param eta         Estimated time remaining in seconds.
 * @param processed   Bytes processed so far.
 * @param total_size  Total bytes to process.
 */
void print_progress(int progress, int total, double speed, double eta,
                    int64_t processed, int64_t total_size);

#endif
