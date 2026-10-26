#ifndef GUI_CREATION_OPTIONS_HPP
#define GUI_CREATION_OPTIONS_HPP

#include "torrent_creator.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gui {

namespace detail {

/// Returns true when the given string is a valid decimal TCP/UDP port.
[[nodiscard]] inline bool is_numeric_port(std::string_view port) {
    if (port.empty()) return false;
    unsigned int value = 0;
    const auto* end = port.data() + port.size();
    const auto [ptr, ec] = std::from_chars(port.data(), end, value);
    return ec == std::errc{} && ptr == end && value > 0 && value <= 65535;
}

/// Validates a URL as `<scheme>://<host>...` with no whitespace and one of the
/// accepted schemes. Host must be non-empty; path/query are not inspected.
[[nodiscard]] inline bool has_allowed_scheme(
    const std::string& url,
    std::initializer_list<std::string_view> schemes) {
    if (url.empty()) return false;
    for (unsigned char c : url) {
        if (std::isspace(c)) return false;
    }
    const auto sep = url.find("://");
    if (sep == std::string::npos || sep == 0) return false;

    std::string scheme = url.substr(0, sep);
    for (auto& c : scheme) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (std::find(schemes.begin(), schemes.end(), scheme) == schemes.end()) {
        return false;
    }

    const auto host_start = sep + 3;
    if (host_start >= url.size()) return false;

    const auto host_end = url.find_first_of("/?#", host_start);
    std::string_view authority(
        url.data() + host_start,
        (host_end == std::string::npos ? url.size() : host_end) - host_start);
    if (const auto at = authority.rfind('@'); at != std::string_view::npos) {
        authority.remove_prefix(at + 1);
    }
    if (authority.empty() || authority.front() == ':') return false;
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos || close == 1) return false;
        const auto suffix = authority.substr(close + 1);
        if (suffix.empty()) return true;
        if (suffix.front() != ':') return false;
        return is_numeric_port(suffix.substr(1));
    }

    const auto colon = authority.find(':');
    if (colon == std::string_view::npos) return true;
    if (colon == 0) return false;
    return is_numeric_port(authority.substr(colon + 1));
}

} // namespace detail

/**
 * @brief Named-field creation options collected from the GUI form.
 *
 * Bridges the Qt form and the 17-positional-parameter TorrentConfig ctor in a
 * single, testable place. Deliberately free of Qt types so it can be unit
 * tested in the plain gtest binary (no QApplication required).
 */
struct CreationOptions {
    fs::path input_path;
    fs::path output_path;
    std::vector<std::string> trackers;
    std::vector<std::string> web_seeds;
    TorrentVersion version = TorrentVersion::V1;
    std::string comment;
    std::string source;
    bool is_private = false;
    bool entropy = false;

    /**
     * @brief Pure string-level validation (no filesystem access).
     * @return One human-readable message per problem; empty when valid.
     */
    [[nodiscard]] std::vector<std::string> validate() const {
        std::vector<std::string> errors;
        if (input_path.empty()) errors.push_back("input path is empty");
        if (output_path.empty()) errors.push_back("output path is empty");
        for (const auto& url : trackers) {
            if (!detail::has_allowed_scheme(url, {"http", "https", "udp", "ws", "wss"}))
                errors.push_back("invalid tracker URL: " + url);
        }
        for (const auto& url : web_seeds) {
            if (!detail::has_allowed_scheme(url, {"http", "https"}))
                errors.push_back("invalid web seed URL: " + url);
        }
        return errors;
    }

    /**
     * @brief Map to the core TorrentConfig.
     *
     * Fields outside the current GUI scope get explicit defaults:
     * piece_size=nullopt, creator="Torrent Builder GUI", name=nullopt,
     * include_creation_date=true, exclude/include_regex={}, silent=false,
     * target_piece_count=nullopt.
     *
     * @throws std::filesystem::filesystem_error if input_path does not exist
     *         (inherited from TorrentConfig's own validation). Call validate()
     *         and check existence first for user-friendly handling.
     */
    [[nodiscard]] TorrentConfig to_torrent_config() const {
        return TorrentConfig(
            input_path,
            output_path,
            trackers,
            version,
            comment.empty() ? std::nullopt : std::optional<std::string>(comment),
            is_private,
            web_seeds,
            std::nullopt,
            std::optional<std::string>("Torrent Builder GUI"),
            std::nullopt,
            true,
            source.empty() ? std::nullopt : std::optional<std::string>(source),
            entropy);
    }
};

} // namespace gui
#endif // GUI_CREATION_OPTIONS_HPP
