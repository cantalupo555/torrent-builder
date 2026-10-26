#ifndef GUI_FORMATTING_HPP
#define GUI_FORMATTING_HPP

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace gui {

/**
 * @brief Human-readable byte size ("823 B", "12.3 MB", "4.0 GB"), base 1024.
 *
 * Not a wrapper over utils::format_size on purpose: the GUI needs one decimal
 * digit and stable units, while utils targets CLI wording. Kept separate to
 * avoid touching the core library from this GUI-only scope. The sub-KB range
 * intentionally uses integer bytes with no decimal ("1023 B" → "1.0 KB") —
 * standard human-readable convention.
 */
[[nodiscard]] inline std::string format_bytes(uint64_t bytes) {
    constexpr double kKiB = 1024.0;
    if (bytes < 1024) return std::to_string(bytes) + " B";
    double value = static_cast<double>(bytes);
    const char* units[] = {"KB", "MB", "GB", "TB"};
    int unit = -1;
    do {
        value /= kKiB;
        ++unit;
    } while (value >= kKiB && unit < 3);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
    return buf;
}

/**
 * @brief Clock-style ETA ("--:--", "mm:ss" or "hh:mm:ss").
 *
 * Distinct format from utils::format_eta ("5m 30s"): the GUI renders a fixed-
 * width clock so the label does not resize while hashing progresses.
 */
[[nodiscard]] inline std::string format_eta(double seconds) {
    if (seconds < 0 || !std::isfinite(seconds)) return "--:--";
    auto total = static_cast<long long>(seconds + 0.5);
    auto h = total / 3600;
    auto m = (total % 3600) / 60;
    auto s = total % 60;
    char buf[32];
    if (h > 0)
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", h, m, s);
    else
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld", m, s);
    return buf;
}

/**
 * @brief Progress percentage clamped to [0,100]; 0 when total <= 0.
 */
[[nodiscard]] inline int progress_percent(long long done, long long total) {
    if (total <= 0) return 0;
    auto pct = static_cast<int>((static_cast<double>(done) / total) * 100.0);
    return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

} // namespace gui

#endif // GUI_FORMATTING_HPP
