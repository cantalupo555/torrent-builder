/**
 * @file logger.cpp
 * @brief File-based logging implementation for torrent-builder.
 *
 * Writes timestamped log entries to "torrent_builder.log" in append mode.
 * The file is opened and closed on each call to ensure entries are flushed
 * even if the process terminates unexpectedly (e.g., std::exit).
 *
 * Thread safety: Not thread-safe. External synchronization is required
 * when called from concurrent threads.
 */

#include "logger.hpp"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <ctime>

void log_message(const std::string& message, LogLevel level) {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);

    std::ofstream logfile("torrent_builder.log", std::ios_base::app);

    std::string level_str;
    switch(level) {
        case LogLevel::INFO: level_str = "INFO"; break;
        case LogLevel::WARNING: level_str = "WARNING"; break;
        case LogLevel::ERR: level_str = "ERROR"; break;
    }

    logfile << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S")
            << " [" << level_str << "] - " << message << "\n";
}
