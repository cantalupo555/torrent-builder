#include "season_pack.hpp"
#include "logger.hpp"
#include <regex>
#include <algorithm>
#include <set>
#include <cctype>

namespace fs = std::filesystem;

namespace
{

int safe_stoi(const std::string &s)
{
    try { return std::stoi(s); }
    catch (const std::exception &)
    {
        log_message("safe_stoi: non-numeric string '" + s + "'", LogLevel::INFO);
        return 0;
    }
}

const std::vector<std::regex> &season_patterns()
{
    static const std::vector<std::regex> patterns = {
        std::regex(R"(\.S(\d{1,2})(?:\.|-|_|\s)Complete)", std::regex::icase),
        std::regex(R"(\.Season\.(\d{1,2})\.)", std::regex::icase),
        std::regex(R"(\.S(\d{1,2})(?:\.|-|_|\s)*$)", std::regex::icase),
        std::regex(R"([-_\s]S(\d{1,2})[-_\s])", std::regex::icase),
        std::regex(R"([/\\]Season\s*(\d{1,2})[/\\])", std::regex::icase),
        std::regex(R"([/\\]S(\d{1,2})[/\\])", std::regex::icase),
        std::regex(R"(\.S(\d{1,2})\.(?:\d+p|Complete|COMPLETE))", std::regex::icase),
        std::regex(R"(Season\s*(\d{1,2})(?:[/\\]|$))", std::regex::icase),
        std::regex(R"(\.S(\d{1,2})$)", std::regex::icase),
    };
    return patterns;
}

const std::regex &episode_pattern()
{
    static const std::regex re(R"(S\d{1,2}E(\d{1,3}))", std::regex::icase);
    return re;
}

const std::regex &multi_episode_pattern()
{
    static const std::regex re(R"(S\d{1,2}E(\d{1,3})(?:-E?|E)(\d{1,3}))", std::regex::icase);
    return re;
}

const std::regex &season_in_filename_pattern()
{
    static const std::regex re(R"(S(\d{1,2}))", std::regex::icase);
    return re;
}

const std::regex &alt_episode_pattern()
{
    static const std::regex re(R"((?:^|[.\-\s_])(\d{1,2})x(\d{1,3})(?:[.\-\s_]|$))", std::regex::icase);
    return re;
}

const std::set<std::string> &video_extensions()
{
    static const std::set<std::string> exts = {
        ".mkv", ".mp4", ".avi", ".ts", ".m2ts",
        ".webm", ".flv", ".wmv", ".mov", ".m4v",
        ".mpg", ".mpeg", ".ogv",
    };
    return exts;
}

std::string to_lower(const std::string &s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

}

namespace season_pack
{

bool is_video_file(const std::string &path)
{
    fs::path p(path);
    std::string ext = to_lower(p.extension().string());
    return video_extensions().count(ext) > 0;
}

int detect_season_number(const std::string &path)
{
    for (const auto &pattern : season_patterns())
    {
        std::smatch matches;
        if (std::regex_search(path, matches, pattern) && matches.size() > 1)
        {
            int season = safe_stoi(matches[1].str());
            if (season > 0)
                return season;
        }
    }
    return 0;
}

std::pair<int, int> extract_season_episode(const std::string &filename)
{
    int season = 0;
    int episode = 0;

    std::smatch ep_matches;
    if (std::regex_search(filename, ep_matches, episode_pattern()) && ep_matches.size() > 1)
    {
        episode = safe_stoi(ep_matches[1].str());
    }

    if (episode == 0)
    {
        std::smatch alt_matches;
        if (std::regex_search(filename, alt_matches, alt_episode_pattern()) && alt_matches.size() > 2)
        {
            season = safe_stoi(alt_matches[1].str());
            episode = safe_stoi(alt_matches[2].str());
            return {season, episode};
        }
    }

    std::smatch s_matches;
    if (std::regex_search(filename, s_matches, season_in_filename_pattern()) && s_matches.size() > 1)
    {
        season = safe_stoi(s_matches[1].str());
    }

    return {season, episode};
}

std::vector<int> extract_multi_episodes(const std::string &filename)
{
    std::vector<int> episodes;

    std::smatch matches;
    if (std::regex_search(filename, matches, multi_episode_pattern()) && matches.size() > 2)
    {
        int start = safe_stoi(matches[1].str());
        int end = safe_stoi(matches[2].str());
        if (end >= start && (end - start) < 100)
        {
            for (int i = start; i <= end; ++i)
            {
                episodes.push_back(i);
            }
        }
    }

    return episodes;
}

SeasonPackInfo analyze(const fs::path &input_path)
{
    SeasonPackInfo info;

    std::error_code ec;
    if (!fs::exists(input_path, ec) || !fs::is_directory(input_path, ec))
    {
        return info;
    }

    std::vector<fs::path> files;
    bool iteration_error = false;
    for (const auto &entry : fs::recursive_directory_iterator(input_path, ec))
    {
        if (ec)
        {
            log_message("Directory iteration error: " + ec.message(), LogLevel::WARNING);
            iteration_error = true;
            break;
        }
        if (entry.is_regular_file(ec))
        {
            if (ec)
            {
                log_message("File access error: " + ec.message(), LogLevel::WARNING);
                ec.clear();
                continue;
            }
            files.push_back(entry.path());
        }
    }

    if (iteration_error && files.empty())
    {
        return info;
    }

    if (files.empty())
    {
        return info;
    }

    int season = detect_season_number(input_path.filename().string());
    if (season == 0)
    {
        season = detect_season_number(input_path.string());
    }

    if (season == 0 && files.size() > 1)
    {
        size_t limit = std::min(files.size(), static_cast<size_t>(5));
        for (size_t i = 0; i < limit; ++i)
        {
            auto [s, e] = extract_season_episode(files[i].filename().string());
            if (s > 0)
            {
                season = s;
                break;
            }
        }
    }

    if (season == 0)
    {
        return info;
    }

    info.season = season;

    std::set<int> episode_set;
    for (const auto &file : files)
    {
        std::string ext = to_lower(file.extension().string());
        if (!video_extensions().count(ext))
        {
            continue;
        }

        info.video_file_count++;

        std::string basename = file.filename().string();

        auto multi_eps = extract_multi_episodes(basename);
        if (!multi_eps.empty())
        {
            for (int ep : multi_eps)
            {
                if (ep > 0)
                {
                    episode_set.insert(ep);
                    if (ep > info.max_episode)
                        info.max_episode = ep;
                }
            }
        }
        else
        {
            auto [s, ep] = extract_season_episode(basename);
            if (ep > 0)
            {
                episode_set.insert(ep);
                if (ep > info.max_episode)
                    info.max_episode = ep;
            }
        }
    }

    for (int ep : episode_set)
    {
        info.episodes.push_back(ep);
    }
    std::sort(info.episodes.begin(), info.episodes.end());

    if (info.episodes.size() > 1)
    {
        info.is_season_pack = true;
    }

    if (info.max_episode > 0 && info.is_season_pack)
    {
        for (int i = 1; i <= info.max_episode; ++i)
        {
            if (episode_set.find(i) == episode_set.end())
            {
                info.missing_episodes.push_back(i);
            }
        }

        if (static_cast<int>(info.episodes.size()) < info.max_episode)
        {
            info.is_suspicious = true;
        }
    }

    return info;
}

std::string format_missing_episodes(const std::vector<int> &missing)
{
    std::string result;
    for (size_t i = 0; i < missing.size(); ++i)
    {
        if (i > 0) result += ", ";
        result += "E" + std::string(missing[i] < 10 ? "0" : "") + std::to_string(missing[i]);
    }
    return result;
}

std::optional<std::string> evaluate_season_warning(
    const fs::path &input_path,
    bool fail_on_warning,
    int job_index)
{
    std::error_code ec;
    bool is_dir = fs::is_directory(input_path, ec);
    if (!fail_on_warning || !is_dir)
    {
        if (fail_on_warning && !is_dir)
        {
            log_message("Season pack check skipped (not a directory): " + input_path.string(), LogLevel::INFO);
        }
        return std::nullopt;
    }

    SeasonPackInfo sp_info = analyze(input_path);

    std::string prefix;
    if (job_index >= 0)
    {
        prefix = "Job " + std::to_string(job_index + 1) + ": ";
    }

    if (sp_info.is_season_pack && sp_info.is_suspicious)
    {
        std::string missing_str = format_missing_episodes(sp_info.missing_episodes);
        std::string msg = prefix + "Season pack detected: Season " +
                          std::to_string(sp_info.season) + ", " +
                          std::to_string(sp_info.episodes.size()) + " episode(s) found, " +
                          std::to_string(sp_info.missing_episodes.size()) + " missing (" +
                          missing_str + ")";
        log_message(msg, LogLevel::WARNING);
        return "Season " + std::to_string(sp_info.season) +
               " pack has missing episodes (" + missing_str + ") in: " +
               input_path.filename().string();
    }
    else if (sp_info.is_season_pack)
    {
        log_message(prefix + "Season " + std::to_string(sp_info.season) +
                    " pack complete (" + std::to_string(sp_info.episodes.size()) + " episodes)",
                    LogLevel::INFO);
    }

    return std::nullopt;
}

}
