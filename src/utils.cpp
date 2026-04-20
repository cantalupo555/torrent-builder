#include "utils.hpp"
#include "constants.hpp"
#include <regex>
#include <cctype>
#include <algorithm>
#include <format>
#include <sstream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <ctime>

namespace utils
{

int auto_piece_size(int64_t total_size)
{
    using namespace PieceSizes;

    if (total_size < 64LL * 1024 * 1024)
        return k16KB;
    if (total_size < 128LL * 1024 * 1024)
        return k32KB;
    if (total_size < 256LL * 1024 * 1024)
        return k64KB;
    if (total_size < 512LL * 1024 * 1024)
        return k128KB;
    if (total_size < 1LL * 1024 * 1024 * 1024)
        return k256KB;
    if (total_size < 2LL * 1024 * 1024 * 1024)
        return k512KB;
    if (total_size < 4LL * 1024 * 1024 * 1024)
        return k1024KB;
    if (total_size < 8LL * 1024 * 1024 * 1024)
        return k2048KB;
    if (total_size < 16LL * 1024 * 1024 * 1024)
        return k4096KB;
    if (total_size < 32LL * 1024 * 1024 * 1024)
        return k8192KB;
    if (total_size < 64LL * 1024 * 1024 * 1024)
        return k16384KB;
    return k32768KB;
}

bool is_valid_url(const std::string &url)
{
    static const std::regex url_regex(R"(^(http|https|udp)://.+$)", std::regex::icase);
    return std::regex_match(url, url_regex);
}

std::string sanitize_path(std::string input)
{
    auto is_space = [](unsigned char c) { return std::isspace(c); };
    auto ltrim = [&](std::string &s)
    { s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), is_space)); };
    auto rtrim = [&](std::string &s)
    { s.erase(std::find_if_not(s.rbegin(), s.rend(), is_space).base(), s.end()); };

    rtrim(input);
    ltrim(input);

    if (input.size() >= 2)
    {
        if ((input.front() == '"' && input.back() == '"') ||
            (input.front() == '\'' && input.back() == '\''))
        {
            input = input.substr(1, input.size() - 2);
            ltrim(input);
            rtrim(input);
        }
    }

    std::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] == '\\' && i + 1 < input.size())
        {
            result += input[i + 1];
            ++i;
        }
        else if (input[i] == '\\' && i + 1 == input.size())
        {
            break;
        }
        else
        {
            result += input[i];
        }
    }
    return result;
}

std::string format_size(int64_t bytes)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024 && unit < 4)
    {
        size /= 1024;
        unit++;
    }
    return std::format("{:.2f} {}", size, units[unit]);
}

std::string format_speed(double speed)
{
    double mb_per_sec = speed / (1024 * 1024);
    return std::format("{:.2f} MB/s", mb_per_sec);
}

std::string format_eta(double eta)
{
    int minutes = static_cast<int>(eta / 60);
    int seconds = static_cast<int>(eta) % 60;
    return std::format("{}m {}s", minutes, seconds);
}

std::string url_encode(const std::string &str)
{
    std::ostringstream encoded;
    encoded << std::hex << std::uppercase << std::setfill('0');

    for (unsigned char c : str)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded << c;
        }
        else if (c == ' ')
        {
            encoded << "%20";
        }
        else
        {
            encoded << '%' << std::setw(2) << static_cast<int>(c);
        }
    }

    return encoded.str();
}

std::string escape_json(const std::string &str)
{
    std::ostringstream escaped;

    for (char c : str)
    {
        switch (c)
        {
        case '"':
            escaped << "\\\"";
            break;
        case '\\':
            escaped << "\\\\";
            break;
        case '\b':
            escaped << "\\b";
            break;
        case '\f':
            escaped << "\\f";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c);
            }
            else
            {
                escaped << c;
            }
            break;
        }
    }

    return escaped.str();
}

std::string format_file_size(int64_t bytes)
{
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    int unit_index = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit_index < 5)
    {
        size /= 1024.0;
        unit_index++;
    }

    std::ostringstream formatted;
    formatted << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
    return formatted.str();
}

std::string format_timestamp(int64_t timestamp)
{
    std::time_t time = static_cast<std::time_t>(timestamp);
    std::tm *tm_info = std::gmtime(&time);

    if (tm_info == nullptr)
    {
        return "Invalid timestamp";
    }

    std::ostringstream formatted;
    formatted << std::put_time(tm_info, "%Y-%m-%d %H:%M:%S") << " UTC";

    return formatted.str();
}
std::string to_lower(const std::string &str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

bool starts_with(const std::string &str, const std::string &prefix)
{
    if (prefix.length() > str.length())
        return false;
    return str.compare(0, prefix.length(), prefix) == 0;
}

std::string join(const std::vector<std::string> &parts, const std::string &delimiter)
{
    if (parts.empty())
        return "";

    std::ostringstream result;
    result << parts[0];

    for (size_t i = 1; i < parts.size(); ++i)
    {
        result << delimiter << parts[i];
    }

    return result.str();
}

std::vector<std::string> split(const std::string &str, char delimiter)
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, delimiter))
    {
        if (!token.empty())
        {
            tokens.push_back(token);
        }
    }

    return tokens;
}

} // namespace utils
