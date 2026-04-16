#include "utils.hpp"
#include "constants.hpp"
#include <regex>
#include <cctype>
#include <algorithm>
#include <format>

namespace utils {

int auto_piece_size(int64_t total_size) {
    using namespace PieceSizes;

    if (total_size < 64LL * 1024 * 1024) return k16KB;
    if (total_size < 128LL * 1024 * 1024) return k32KB;
    if (total_size < 256LL * 1024 * 1024) return k64KB;
    if (total_size < 512LL * 1024 * 1024) return k128KB;
    if (total_size < 1LL * 1024 * 1024 * 1024) return k256KB;
    if (total_size < 2LL * 1024 * 1024 * 1024) return k512KB;
    if (total_size < 4LL * 1024 * 1024 * 1024) return k1024KB;
    if (total_size < 8LL * 1024 * 1024 * 1024) return k2048KB;
    if (total_size < 16LL * 1024 * 1024 * 1024) return k4096KB;
    if (total_size < 32LL * 1024 * 1024 * 1024) return k8192KB;
    if (total_size < 64LL * 1024 * 1024 * 1024) return k16384KB;
    return k32768KB;
}

bool is_valid_url(const std::string& url) {
    static const std::regex url_regex(R"(^(http|https|udp)://.+$)", std::regex::icase);
    return std::regex_match(url, url_regex);
}

std::string sanitize_path(std::string input) {
    auto is_space = [](unsigned char c) { return std::isspace(c); };
    auto ltrim = [&](std::string& s) { s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), is_space)); };
    auto rtrim = [&](std::string& s) { s.erase(std::find_if_not(s.rbegin(), s.rend(), is_space).base(), s.end()); };

    rtrim(input);
    ltrim(input);

    if (input.size() >= 2) {
        if ((input.front() == '"' && input.back() == '"') ||
            (input.front() == '\'' && input.back() == '\'')) {
            input = input.substr(1, input.size() - 2);
            ltrim(input);
            rtrim(input);
        }
    }

    std::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            result += input[i + 1];
            ++i;
        } else if (input[i] == '\\' && i + 1 == input.size()) {
            break;
        } else {
            result += input[i];
        }
    }
    return result;
}

std::string format_size(int64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }
    return std::format("{:.2f} {}", size, units[unit]);
}

std::string format_speed(double speed) {
    double mb_per_sec = speed / (1024 * 1024);
    return std::format("{:.2f} MB/s", mb_per_sec);
}

std::string format_eta(double eta) {
    int minutes = static_cast<int>(eta / 60);
    int seconds = static_cast<int>(eta) % 60;
    return std::format("{}m {}s", minutes, seconds);
}

}
