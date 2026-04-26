#include "utils.hpp"
#include "constants.hpp"
#include "logger.hpp"
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

    for (unsigned char c : str)
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
            if (c < 0x20)
            {
                escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c);
            }
            else if (c >= 0x80)
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

std::string extract_domain(const std::string &tracker_url)
{
    auto scheme_end = tracker_url.find("://");
    if (scheme_end == std::string::npos)
        return "";

    std::size_t authority_start = scheme_end + 3;
    if (authority_start >= tracker_url.size())
        return "";

    std::size_t authority_end = tracker_url.size();
    for (std::size_t i = authority_start; i < tracker_url.size(); ++i)
    {
        if (tracker_url[i] == '/')
        {
            authority_end = i;
            break;
        }
    }

    std::string authority = tracker_url.substr(authority_start, authority_end - authority_start);

    auto at_pos = authority.rfind('@');
    std::string host_port = (at_pos != std::string::npos) ? authority.substr(at_pos + 1) : authority;

    if (!host_port.empty() && host_port[0] == '[')
    {
        auto bracket_end = host_port.find(']');
        if (bracket_end != std::string::npos)
            return host_port.substr(1, bracket_end - 1);
        return "";
    }

    auto colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos)
        return host_port.substr(0, colon_pos);

    return host_port;
}

std::string sanitize_filename_part(const std::string &part)
{
    std::string result;
    result.reserve(part.size());
    bool prev_underscore = false;
    for (char c : part)
    {
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|'
            || c == '?' || c == '*' || c == '\\' || c == '/' || c == '_')
        {
            if (!prev_underscore)
                result += '_';
            prev_underscore = true;
        }
        else
        {
            result += c;
            prev_underscore = false;
        }
    }
    return result;
}

std::string truncate_filename(const std::string &filename, std::size_t max_bytes)
{
    if (filename.size() <= max_bytes)
        return filename;

    const std::string ext = ".torrent";
    if (filename.size() <= ext.size() || max_bytes <= ext.size())
        return filename;

    std::size_t available = max_bytes - ext.size();

    while (available > 0 && (static_cast<unsigned char>(filename[available - 1]) & 0xC0) == 0x80)
        --available;

    if (available == 0)
        available = 1;

    return filename.substr(0, available) + ext;
}

std::string resolve_collision(const std::filesystem::path &directory,
                              const std::string &base_filename)
{
    if (!std::filesystem::exists(directory / base_filename))
        return base_filename;

    std::string stem;
    std::string ext;
    auto dot_pos = base_filename.rfind('.');
    if (dot_pos != std::string::npos)
    {
        stem = base_filename.substr(0, dot_pos);
        ext = base_filename.substr(dot_pos);
    }
    else
    {
        stem = base_filename;
        ext = "";
    }

    for (int i = 1; i <= 1000; ++i)
    {
        std::string candidate = stem + "(" + std::to_string(i) + ")" + ext;
        if (!std::filesystem::exists(directory / candidate))
            return candidate;
    }

    throw std::runtime_error("Could not resolve filename collision for '" + base_filename +
                             "' in directory '" + directory.string() +
                             "' after 1000 attempts");
}

std::string generate_output_filename(const std::filesystem::path &content_path,
                                       const std::vector<std::string> &trackers,
                                       bool skip_prefix,
                                       int tracker_index)
{
    auto resolved = content_path;
    if (content_path.filename().empty())
        resolved = content_path.parent_path();

    std::string content_name = resolved.filename().string();
    if (content_name == "." || content_name == "..")
    {
        std::error_code canon_ec;
        auto canon = std::filesystem::canonical(resolved, canon_ec);
        if (!canon_ec)
            content_name = canon.filename().string();
        else
            content_name = std::filesystem::absolute(resolved).filename().string();
        if (content_name == "." || content_name == "..")
            content_name = std::filesystem::current_path().filename().string();
    }
    if (content_name.empty())
        content_name = "torrent";

    std::error_code ec;
    if (std::filesystem::is_regular_file(resolved, ec) && !ec)
    {
        auto stem = resolved.stem().string();
        if (!stem.empty())
            content_name = stem;
    }

    content_name = sanitize_filename_part(content_name);

    if (skip_prefix || trackers.empty())
        return content_name + ".torrent";

    if (tracker_index < 0 || tracker_index >= static_cast<int>(trackers.size()))
        tracker_index = 0;

    std::string domain = extract_domain(trackers[tracker_index]);
    if (domain.empty())
        return content_name + ".torrent";

    return sanitize_filename_part(domain) + "_" + content_name + ".torrent";
}

std::string generate_auto_output_path(const std::filesystem::path &content_path,
                                       const std::vector<std::string> &trackers,
                                       bool skip_prefix,
                                       int tracker_index,
                                       const std::filesystem::path &output_dir)
{
    std::string filename = generate_output_filename(content_path, trackers, skip_prefix, tracker_index);
    filename = truncate_filename(filename, 255 - 6);

    std::filesystem::path dir = output_dir.empty() ? std::filesystem::current_path() : output_dir;
    std::string resolved = resolve_collision(dir, filename);
    if (resolved != filename)
        log_message("Filename collision detected, resolved to: " + resolved, LogLevel::INFO);

    return (dir / resolved).string();
}

} // namespace utils
