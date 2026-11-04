#ifndef CONSTANTS_HPP
#define CONSTANTS_HPP

#include <array>
#include <string>
#include <string_view>
#include <vector>

/**
 * Standard torrent protocol piece sizes in bytes (16 KB to 32 MB, powers of 2).
 */
namespace PieceSizes {
    constexpr int k16KB = 16 * 1024;
    constexpr int k32KB = 32 * 1024;
    constexpr int k64KB = 64 * 1024;
    constexpr int k128KB = 128 * 1024;
    constexpr int k256KB = 256 * 1024;
    constexpr int k512KB = 512 * 1024;
    constexpr int k1024KB = 1024 * 1024;
    constexpr int k2048KB = 2048 * 1024;
    constexpr int k4096KB = 4096 * 1024;
    constexpr int k8192KB = 8192 * 1024;
    constexpr int k16384KB = 16384 * 1024;
    constexpr int k32768KB = 32768 * 1024;
}

/**
 * Valid piece sizes for command-line input validation (powers of 2, in KB).
 */
namespace AllowedPieceSizes {
    constexpr std::array<int, 12> values = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
}

/**
 * Defaults shared by every creation frontend.
 */
namespace Defaults {
    /// Public open trackers from the NewTrackon list, used when default trackers are requested.
    inline const std::vector<std::string> trackers = {
        "udp://open.stealth.si:80/announce",         "udp://tracker.opentrackr.org:1337/announce",
        "udp://tracker.torrent.eu.org:451/announce", "udp://explodie.org:6969/announce",
        "udp://tracker.cyberia.is:6969/announce",    "udp://retracker.hotplug.ru:2710/announce"};

    /// Default value for the torrent's "created by" field.
    inline constexpr std::string_view creator = "Torrent Builder";
}

#endif
