#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>

/**
 * Log severity levels used throughout torrent-builder.
 *
 * - INFO:     Normal operational messages (startup, success, user cancellation).
 * - WARNING:  Non-fatal issues that do not prevent operation (e.g., disk space check skipped).
 * - ERR:      Fatal errors that cause the program to exit with code 1.
 */
enum class LogLevel {
    INFO,
    WARNING,
    ERR
};

/**
 * @brief Append a timestamped, leveled message to the log file.
 *
 * Writes to "torrent_builder.log" in the current working directory (append mode).
 * Each entry format: "YYYY-MM-DD HH:MM:SS [LEVEL] - message".
 *
 * @param message  Human-readable description of the event.
 * @param level    Severity level (defaults to INFO).
 *
 * @note Not thread-safe. Callers in multi-threaded contexts must synchronize externally.
 */
void log_message(const std::string& message, LogLevel level = LogLevel::INFO);

#endif
