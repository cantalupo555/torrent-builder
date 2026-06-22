#include "updater.hpp"
#include "utils.hpp"
#include "logger.hpp"
#include "output.hpp"
#include "version.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <array>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <vector>
#include <openssl/evp.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <unistd.h>
#include <sys/stat.h>
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace
{
Updater::CurlExecutor g_curl_executor;
Updater::FileDownloader g_file_downloader;
Updater::ExePathProvider g_exe_path_provider;
Updater::BinaryValidator g_binary_validator;
std::string g_current_version_override;
std::optional<std::string> g_curl_path_cache;

std::string escape_batch_path(const std::string &path)
{
    std::string escaped;
    escaped.reserve(path.size() * 2);
    for (char c : path)
    {
        if (c == '"')
            escaped += "\"\"";
        else if (c == '%')
            escaped += "%%";
        else
            escaped += c;
    }
    return escaped;
}

std::string escape_shell_arg(const std::string &arg)
{
    std::string escaped;
    escaped.reserve(arg.size() + 2);
#ifdef _WIN32
    escaped += "\"";
    for (char c : arg)
    {
        if (c == '"')
            escaped += "\"\"";
        else
            escaped += c;
    }
    escaped += "\"";
#else
    escaped += "'";
    for (char c : arg)
    {
        if (c == '\'')
            escaped += "'\\''";
        else
            escaped += c;
    }
    escaped += "'";
#endif
    return escaped;
}

std::string compute_sha256(const std::string &filepath)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file)
    {
        log_message("SHA-256: failed to open file: " + filepath, LogLevel::WARNING);
        return "";
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
    {
        log_message("SHA-256: failed to create EVP context", LogLevel::WARNING);
        return "";
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1)
    {
        EVP_MD_CTX_free(ctx);
        log_message("SHA-256: EVP_DigestInit_ex failed", LogLevel::WARNING);
        return "";
    }

    char buf[8192];
    while (file.read(buf, sizeof(buf)))
    {
        if (EVP_DigestUpdate(ctx, buf, static_cast<size_t>(file.gcount())) != 1)
        {
            EVP_MD_CTX_free(ctx);
            log_message("SHA-256: EVP_DigestUpdate failed", LogLevel::WARNING);
            return "";
        }
    }
    if (file.gcount() > 0)
    {
        if (EVP_DigestUpdate(ctx, buf, static_cast<size_t>(file.gcount())) != 1)
        {
            EVP_MD_CTX_free(ctx);
            log_message("SHA-256: EVP_DigestUpdate (tail) failed", LogLevel::WARNING);
            return "";
        }
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1)
    {
        EVP_MD_CTX_free(ctx);
        log_message("SHA-256: EVP_DigestFinal_ex failed", LogLevel::WARNING);
        return "";
    }
    EVP_MD_CTX_free(ctx);

    std::string hash;
    hash.reserve(digest_len * 2);
    for (unsigned int i = 0; i < digest_len; ++i)
    {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02x", digest[i]);
        hash += hex;
    }
    return hash;
}

nlohmann::json fetch_release_json()
{
    std::string url = "https://api.github.com/repos/cantalupo555/torrent-builder/releases/latest";

    std::string response;
    try
    {
        std::vector<std::string> args = {"-H", "Accept: application/vnd.github+json"};
        if (const char *tok = std::getenv("GITHUB_TOKEN"))
        {
            if (tok[0] != '\0')
            {
                args.push_back("-H");
                args.push_back(std::string("Authorization: Bearer ") + tok);
                log_message("Using GITHUB_TOKEN for authenticated API request", LogLevel::INFO);
            }
        }
        response = Updater::execute_curl(url, args);
    }
    catch (const std::exception &e)
    {
        if (std::string(e.what()).find("rate limit") != std::string::npos)
        {
            throw std::runtime_error("GitHub API rate limit exceeded. Try again later or set GITHUB_TOKEN env variable.");
        }
        throw std::runtime_error(std::string("Failed to fetch release info: ") + e.what());
    }

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(response);
    }
    catch (const nlohmann::json::parse_error &)
    {
        throw std::runtime_error("Failed to parse GitHub API response");
    }

    if (j.contains("message") && !j.contains("tag_name"))
    {
        std::string msg = j["message"].get<std::string>();
        if (msg.find("rate limit") != std::string::npos)
        {
            throw std::runtime_error("GitHub API rate limit exceeded. Try again later.");
        }
        throw std::runtime_error("GitHub API error: " + msg);
    }

    return j;
}

bool ends_with(const std::string &str, const std::string &suffix)
{
    if (suffix.size() > str.size())
        return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}

void Updater::set_curl_executor_for_testing(CurlExecutor executor)
{
    g_curl_executor = std::move(executor);
}

void Updater::set_file_downloader_for_testing(FileDownloader downloader)
{
    g_file_downloader = std::move(downloader);
}

void Updater::set_exe_path_provider_for_testing(ExePathProvider provider)
{
    g_exe_path_provider = std::move(provider);
}

void Updater::set_current_version_for_testing(const std::string &version)
{
    g_current_version_override = version;
}

void Updater::set_binary_validator_for_testing(BinaryValidator validator)
{
    g_binary_validator = std::move(validator);
}

void Updater::reset_test_overrides()
{
    g_curl_executor = nullptr;
    g_file_downloader = nullptr;
    g_exe_path_provider = nullptr;
    g_binary_validator = nullptr;
    g_current_version_override.clear();
    g_curl_path_cache.reset();
}

PlatformInfo Updater::get_platform_info()
{
    PlatformInfo info;
#if defined(_WIN32)
    info.os = "windows";
#elif defined(__APPLE__)
    info.os = "macos";
#elif defined(__linux__)
    info.os = "linux";
#else
    info.os = "unknown";
#endif

#if defined(__x86_64__) || defined(_M_X64)
    info.arch = "amd64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    info.arch = "arm64";
#else
    info.arch = "unknown";
#endif
    return info;
}

std::string Updater::get_current_version()
{
    if (!g_current_version_override.empty())
        return g_current_version_override;
    return TORRENT_BUILDER_VERSION;
}

std::string Updater::get_exe_path()
{
    if (g_exe_path_provider)
        return g_exe_path_provider();

    try
    {
#ifdef _WIN32
        std::array<char, 4096> buf;
        DWORD len = GetModuleFileNameA(NULL, buf.data(), static_cast<DWORD>(buf.size()));
        if (len > 0 && len < buf.size())
        {
            return std::string(buf.data(), len);
        }
#elif defined(__linux__)
        return fs::canonical("/proc/self/exe").string();
#elif defined(__APPLE__)
        std::array<char, 4096> buf;
        uint32_t size = static_cast<uint32_t>(buf.size());
        std::string result;
        if (_NSGetExecutablePath(buf.data(), &size) == 0)
        {
            result = std::string(buf.data());
        }
        else if (size > static_cast<uint32_t>(buf.size()))
        {
            std::vector<char> big_buf(size);
            if (_NSGetExecutablePath(big_buf.data(), &size) == 0)
            {
                result = std::string(big_buf.data());
            }
        }
        if (!result.empty())
        {
            std::error_code ec;
            fs::path canonical_path = fs::canonical(result, ec);
            if (!ec)
                return canonical_path.string();
            return result;
        }
#endif
    }
    catch (const fs::filesystem_error &)
    {
    }

#ifdef __linux__
    std::array<char, 4096> buf;
    ssize_t len = readlink("/proc/self/exe", buf.data(), buf.size());
    if (len > 0)
        return std::string(buf.data(), static_cast<size_t>(len));
#endif

    return "torrent_builder";
}

std::string Updater::find_curl_executable()
{
    if (g_curl_path_cache.has_value())
        return *g_curl_path_cache;

    auto check = [](const std::string &path) -> bool
    {
        return fs::exists(path);
    };

#ifdef _WIN32
    std::vector<std::string> candidates = {
        "C:\\Windows\\System32\\curl.exe",
        "C:\\Windows\\SysWOW64\\curl.exe"};
    const char *path_env = std::getenv("PATH");
    if (path_env)
    {
        std::istringstream iss(path_env);
        std::string dir;
        while (std::getline(iss, dir, ';'))
        {
            candidates.push_back(dir + "\\curl.exe");
        }
    }
#else
    std::vector<std::string> candidates = {
        "/usr/bin/curl",
        "/usr/local/bin/curl",
        "/opt/homebrew/bin/curl"};
    const char *path_env = std::getenv("PATH");
    if (path_env)
    {
        std::istringstream iss(path_env);
        std::string dir;
        while (std::getline(iss, dir, ':'))
        {
            candidates.push_back(dir + "/curl");
        }
    }
#endif

    for (const auto &c : candidates)
    {
        if (check(c))
        {
            g_curl_path_cache = c;
            return c;
        }
    }

    log_message("curl not found in common paths, falling back to PATH lookup", LogLevel::WARNING);
    g_curl_path_cache = "curl";
    return "curl";
}

bool Updater::is_safe_url(const std::string &url)
{
    if (url.size() < 9 || url.substr(0, 8) != "https://")
        return false;

    static const std::string safe_chars =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789"
        ":/.-_~?=%+@&";
    if (url.find_first_not_of(safe_chars) != std::string::npos)
        return false;

    for (size_t i = 0; i < url.size(); ++i)
    {
        if (url[i] != '%')
            continue;
        if (i + 2 >= url.size())
            return false;
        if (!std::isxdigit(static_cast<unsigned char>(url[i + 1])) ||
            !std::isxdigit(static_cast<unsigned char>(url[i + 2])))
            return false;
        i += 2;
    }

    return true;
}

bool Updater::is_valid_binary(const std::string &filepath)
{
    if (g_binary_validator)
        return g_binary_validator(filepath);

    std::ifstream file(filepath, std::ios::binary);
    if (!file)
        return false;

    char magic[4] = {};
    if (!file.read(magic, 4))
        return false;

    if (magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F')
        return true;

    if (magic[0] == 'M' && magic[1] == 'Z')
        return true;

#ifdef __APPLE__
    static constexpr unsigned char ZIP_MAGIC[] = {0x50, 0x4b, 0x03, 0x04};
    if (memcmp(magic, ZIP_MAGIC, 4) == 0)
        return true;
#endif

    uint32_t m = static_cast<unsigned char>(magic[0])
        | (static_cast<unsigned char>(magic[1]) << 8)
        | (static_cast<unsigned char>(magic[2]) << 16)
        | (static_cast<unsigned char>(magic[3]) << 24);
    if (m == 0xfeedface || m == 0xcefaedfe
        || m == 0xfeedfacf || m == 0xcffaedfe)
        return true;

    return false;
}

std::string Updater::build_script_launch_cmd()
{
    return "cmd /c start \"\" /b \"%TB_LAUNCH_SCRIPT%\"";
}

std::string Updater::execute_curl(const std::string &url, const std::vector<std::string> &extra_args)
{
    if (g_curl_executor)
        return g_curl_executor(url, extra_args);

    if (!is_safe_url(url))
    {
        log_message("Rejected unsafe URL: " + url, LogLevel::ERR);
        throw std::runtime_error("Unsafe URL rejected: " + url);
    }

    std::string curl_path = find_curl_executable();
    std::string cmd = escape_shell_arg(curl_path) + " -s -S -L --connect-timeout 30 --max-time 300 --proto =https";

    for (const auto &arg : extra_args)
    {
        cmd += " " + escape_shell_arg(arg);
    }

    cmd += " " + escape_shell_arg(url);

#ifdef _WIN32
    cmd += " 2>NUL";
#endif

    log_message("Executing curl request to: " + url, LogLevel::INFO);

    std::array<char, 4096> buffer;
    std::string output;
    FILE *pipe =
#ifdef _WIN32
        _popen(cmd.c_str(), "r");
#else
        popen(cmd.c_str(), "r");
#endif

    if (!pipe)
    {
        log_message("Failed to execute curl for URL: " + url, LogLevel::ERR);
        throw std::runtime_error("Failed to execute curl");
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    {
        output += buffer.data();
    }

#ifdef _WIN32
    int status = _pclose(pipe);
#else
    int status = pclose(pipe);
#endif

    if (status != 0)
    {
        log_message("curl request failed with status " + std::to_string(status) + " for URL: " + url, LogLevel::ERR);
        throw std::runtime_error("curl request failed with status " + std::to_string(status) + " for URL: " + url);
    }

    return output;
}

UpdateInfo Updater::fetch_latest_release()
{
    nlohmann::json j = fetch_release_json();

    UpdateInfo info;
    info.version = j.value("tag_name", "");
    if (!info.version.empty() && (info.version[0] == 'v' || info.version[0] == 'V'))
    {
        info.version = info.version.substr(1);
    }

    info.changelog = j.value("body", "");
    info.release_json = j;

    return info;
}

std::optional<std::string> Updater::find_matching_asset(const std::string &release_json, const PlatformInfo &platform)
{
    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(release_json);
    }
    catch (const nlohmann::json::parse_error &)
    {
        return std::nullopt;
    }

    return find_matching_asset_from_json(j, platform);
}

std::optional<std::string> Updater::find_matching_asset_from_json(const nlohmann::json &j, const PlatformInfo &platform)
{
    if (!j.contains("assets") || !j["assets"].is_array())
    {
        return std::nullopt;
    }

    std::string suffix = "_" + platform.os + "_" + platform.arch;
    if (platform.os == "windows")
        suffix += ".exe";
    else if (platform.os == "macos")
        suffix += ".zip";

    for (const auto &asset : j["assets"])
    {
        std::string name = asset.value("name", "");
        if (ends_with(name, suffix))
        {
            std::string url = asset.value("browser_download_url", "");
            if (!url.empty())
                return url;
        }
    }

    return std::nullopt;
}

int64_t Updater::get_asset_size(const nlohmann::json &release_json, const std::string &asset_url)
{
    if (!release_json.contains("assets") || !release_json["assets"].is_array())
        return 0;

    for (const auto &asset : release_json["assets"])
    {
        if (asset.value("browser_download_url", "") == asset_url)
        {
            return asset.value("size", int64_t(0));
        }
    }
    return 0;
}

std::optional<std::string> Updater::fetch_checksums()
{
    nlohmann::json j = fetch_release_json();
    return fetch_checksums(j);
}

std::optional<std::string> Updater::fetch_checksums(const nlohmann::json &release_json)
{
    if (!release_json.contains("assets") || !release_json["assets"].is_array())
    {
        return std::nullopt;
    }

    for (const auto &asset : release_json["assets"])
    {
        std::string name = asset.value("name", "");
        if (name == "checksums.txt")
        {
            std::string download_url = asset.value("browser_download_url", "");
            if (download_url.empty())
                return std::nullopt;

            try
            {
                return execute_curl(download_url);
            }
            catch (const std::exception &e)
            {
                log_message(std::string("Failed to fetch checksums: ") + e.what(), LogLevel::WARNING);
                return std::nullopt;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::string> Updater::lookup_checksum(const std::string &checksums_content, const std::string &asset_name)
{
    std::istringstream stream(checksums_content);
    std::string line;
    while (std::getline(stream, line))
    {
        auto space_pos = line.find(' ');
        if (space_pos == std::string::npos)
            continue;

        std::string hash = line.substr(0, space_pos);
        std::string name = line.substr(space_pos + 1);
        if (!name.empty() && name[0] == '*')
            name.erase(0, 1);
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t\r\n") + 1);

        if (name == asset_name)
        {
            std::transform(hash.begin(), hash.end(), hash.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return hash;
        }
    }
    return std::nullopt;
}

bool Updater::download_asset(const std::string &url, const std::string &dest_path, int64_t expected_size)
{
    if (g_file_downloader)
        return g_file_downloader(url, dest_path, expected_size);

    if (!is_safe_url(url))
    {
        log_message("Rejected unsafe download URL: " + url, LogLevel::ERR);
        return false;
    }

    std::string curl_path = find_curl_executable();
    std::string cmd = escape_shell_arg(curl_path) + " -s -S -L --fail --connect-timeout 30 --max-time 300 --proto =https -o " + escape_shell_arg(dest_path);

    if (expected_size > 0)
    {
        cmd += " --max-filesize " + std::to_string(expected_size + 1024 * 1024);
    }

    cmd += " " + escape_shell_arg(url);

#ifdef _WIN32
    cmd += " 2>NUL";
#endif

    log_message("Downloading asset from: " + url + " to: " + dest_path, LogLevel::INFO);

    int status = std::system(cmd.c_str());
    if (status != 0)
    {
        log_message("Download failed with status " + std::to_string(status) + " for URL: " + url, LogLevel::ERR);
        return false;
    }

    return verify_downloaded_file(dest_path, expected_size);
}

bool Updater::verify_downloaded_file(const std::string &path, int64_t expected_size)
{
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec)
    {
        log_message("Downloaded file is missing or inaccessible: " + path + " (" + ec.message() + ")", LogLevel::ERR);
        return false;
    }
    if (size == 0)
    {
        log_message("Downloaded file is empty: " + path, LogLevel::ERR);
        return false;
    }

    if (expected_size > 0 && static_cast<int64_t>(size) != expected_size)
    {
        log_message("Downloaded file size mismatch: expected " + std::to_string(expected_size) +
                        " got " + std::to_string(size),
                    LogLevel::ERR);
        return false;
    }

    return true;
}

bool Updater::verify_checksum(const std::string &filepath, const std::string &expected_hash)
{
    std::string actual = compute_sha256(filepath);
    if (actual.empty())
    {
        log_message("Could not compute SHA-256 for: " + filepath, LogLevel::WARNING);
        return false;
    }

    std::string expected = expected_hash;
    std::transform(expected.begin(), expected.end(), expected.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (actual != expected)
    {
        log_message("Checksum mismatch for " + filepath + ": expected " + expected + " got " + actual, LogLevel::ERR);
        return false;
    }

    log_message("Checksum verified for: " + filepath, LogLevel::INFO);
    return true;
}

std::string Updater::get_old_binary_path(const std::string &current_path)
{
    return current_path + ".old";
}

bool Updater::make_executable(const std::string &path)
{
#ifndef _WIN32
    try
    {
        fs::permissions(path, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec, fs::perm_options::replace);
        return true;
    }
    catch (const fs::filesystem_error &)
    {
        return false;
    }
#else
    return true;
#endif
}

bool Updater::extract_macos_zip(const std::string &zip_path, const std::string &dest_dir)
{
    std::string cmd = "unzip -o " + escape_shell_arg(zip_path) + " -d " + escape_shell_arg(dest_dir) + " 2>/dev/null";
    int status = std::system(cmd.c_str());
    if (status != 0)
    {
        return false;
    }

    fs::path extracted = fs::path(dest_dir) / "torrent_builder";
    if (!fs::exists(extracted))
    {
        return false;
    }

    return make_executable(extracted.string());
}

UpdateResult Updater::perform_update(const std::string &new_binary_path, const std::string &current_binary_path)
{
    log_message("Starting update: replacing " + current_binary_path + " with " + new_binary_path, LogLevel::INFO);
    std::string old_path = get_old_binary_path(current_binary_path);
    std::string new_path = new_binary_path;

    if (!fs::exists(new_path))
    {
        throw std::runtime_error("New binary not found: " + new_path);
    }

    try
    {
        fs::remove(old_path);
    }
    catch (const fs::filesystem_error &)
    {
    }

#ifdef __APPLE__
    if (new_path.size() >= 4 && new_path.compare(new_path.size() - 4, 4, ".zip") == 0)
    {
        fs::path temp_dir = fs::path(current_binary_path).parent_path() / ".torrent_builder_update";
        fs::create_directories(temp_dir);

        if (!extract_macos_zip(new_path, temp_dir.string()))
        {
            fs::remove_all(temp_dir);
            throw std::runtime_error("Failed to extract macOS zip archive");
        }

        new_path = (temp_dir / "torrent_builder").string();
    }
#endif

    auto cleanup_macos_temp = [&]() {
#ifdef __APPLE__
        try
        {
            fs::path td = fs::path(current_binary_path).parent_path() / ".torrent_builder_update";
            if (fs::exists(td))
                fs::remove_all(td);
        }
        catch (const fs::filesystem_error &)
        {
        }
#endif
    };

    if (!make_executable(new_path))
    {
        cleanup_macos_temp();
        throw std::runtime_error("Failed to set executable permissions on new binary");
    }

#ifdef _WIN32
    try
    {
        fs::rename(current_binary_path, old_path);
        fs::rename(new_path, current_binary_path);
    }
    catch (const fs::filesystem_error &e)
    {
        log_message("Direct rename failed on Windows, falling back to batch script: " + std::string(e.what()), LogLevel::WARNING);
        std::string esc_current = escape_batch_path(current_binary_path);
        std::string esc_old = escape_batch_path(old_path);
        std::string esc_new = escape_batch_path(new_path);

        bool step1_done = fs::exists(old_path) && !fs::exists(current_binary_path);

        std::string script_path = current_binary_path + ".update.bat";
        {
            std::ofstream script(script_path);
            if (!script)
            {
                throw std::runtime_error("Failed to create update script");
            }
            script << "@echo off\n";
            script << "set tries=0\n";
            script << "timeout /t 2 /nobreak >nul\n";
            if (!step1_done)
            {
                script << ":retry_old\n";
                script << "move /y \"" << esc_current << "\" \"" << esc_old << "\" >nul 2>&1\n";
                script << "if not errorlevel 1 goto step2\n";
                script << "set /a tries+=1\n";
                script << "if %tries% geq 30 goto done\n";
                script << "timeout /t 1 /nobreak >nul\n";
                script << "goto retry_old\n";
            }
            script << ":step2\n";
            script << "set tries=0\n";
            script << ":retry_new\n";
            script << "move /y \"" << esc_new << "\" \"" << esc_current << "\" >nul 2>&1\n";
            script << "if not errorlevel 1 goto done\n";
            script << "set /a tries+=1\n";
            script << "if %tries% geq 30 goto done\n";
            script << "timeout /t 1 /nobreak >nul\n";
            script << "goto retry_new\n";
            script << ":done\n";
            script << "del \"%~f0\"\n";
        }

        SetEnvironmentVariableA("TB_LAUNCH_SCRIPT", script_path.c_str());
        std::string cmd = build_script_launch_cmd();
        int status = std::system(cmd.c_str());
        SetEnvironmentVariableA("TB_LAUNCH_SCRIPT", nullptr);
        if (status != 0)
        {
            fs::remove(script_path);
            throw std::runtime_error("Failed to launch update script");
        }
        log_message("Update scheduled via batch script: " + script_path, LogLevel::INFO);
        return UpdateResult::PendingRestart;
    }
#else
    try
    {
        fs::rename(current_binary_path, old_path);
    }
    catch (const fs::filesystem_error &e)
    {
        cleanup_macos_temp();
        throw std::runtime_error("Failed to backup current binary: " + std::string(e.what()));
    }

    try
    {
        std::error_code rename_ec;
        fs::rename(new_path, current_binary_path, rename_ec);
        if (rename_ec)
        {
            if (rename_ec == std::errc::cross_device_link)
            {
                fs::copy_file(new_path, current_binary_path, fs::copy_options::overwrite_existing);
                if (!make_executable(current_binary_path))
                {
                    throw fs::filesystem_error("failed to set executable permissions after cross-device copy",
                                               current_binary_path, new_path,
                                               std::make_error_code(std::errc::operation_not_permitted));
                }
                std::error_code remove_ec;
                fs::remove(new_path, remove_ec);
            }
            else
            {
                throw fs::filesystem_error("rename failed", new_path, current_binary_path, rename_ec);
            }
        }
    }
    catch (const fs::filesystem_error &e)
    {
        cleanup_macos_temp();
        try
        {
            fs::rename(old_path, current_binary_path);
        }
        catch (const fs::filesystem_error &)
        {
            throw std::runtime_error(
                "Failed to replace binary and rollback also failed. "
                "The binary may be in an inconsistent state. "
                "Original error: " + std::string(e.what()));
        }
        throw std::runtime_error("Failed to replace binary (rolled back): " + std::string(e.what()));
    }
#endif

    cleanup_macos_temp();

    return UpdateResult::Success;
}

UpdateResult Updater::perform_rollback(const std::string &current_binary_path)
{
    log_message("Starting rollback for: " + current_binary_path, LogLevel::INFO);
    std::string old_path = get_old_binary_path(current_binary_path);

    if (!fs::exists(old_path))
    {
        throw std::runtime_error("No previous version found to rollback to (" + old_path + " does not exist)");
    }

#ifdef _WIN32
    try
    {
        fs::rename(old_path, current_binary_path);
        return UpdateResult::Success;
    }
    catch (const fs::filesystem_error &e)
    {
        log_message("Direct rename failed on Windows rollback, falling back to batch script: " + std::string(e.what()), LogLevel::WARNING);
    }

    std::string esc_current = escape_batch_path(current_binary_path);
    std::string esc_old = escape_batch_path(old_path);

    std::string script_path = current_binary_path + ".rollback.bat";
    {
        std::ofstream script(script_path);
        if (!script)
        {
            throw std::runtime_error("Failed to create rollback script");
        }
        script << "@echo off\n";
        script << "set tries=0\n";
        script << "timeout /t 2 /nobreak >nul\n";
        script << ":retry\n";
        script << "move /y \"" << esc_old << "\" \"" << esc_current << "\" >nul 2>&1\n";
        script << "if not errorlevel 1 goto done\n";
        script << "set /a tries+=1\n";
        script << "if %tries% geq 30 goto done\n";
        script << "timeout /t 1 /nobreak >nul\n";
        script << "goto retry\n";
        script << ":done\n";
        script << "del \"%~f0\"\n";
    }

    SetEnvironmentVariableA("TB_LAUNCH_SCRIPT", script_path.c_str());
    std::string cmd = build_script_launch_cmd();
    int status = std::system(cmd.c_str());
    SetEnvironmentVariableA("TB_LAUNCH_SCRIPT", nullptr);
    if (status != 0)
    {
        fs::remove(script_path);
        throw std::runtime_error("Failed to launch rollback script");
    }
    log_message("Rollback scheduled via batch script: " + script_path, LogLevel::INFO);
    return UpdateResult::PendingRestart;
#else
    std::string temp_path = current_binary_path + ".rbtmp";
    try
    {
        fs::rename(current_binary_path, temp_path);
    }
    catch (const fs::filesystem_error &e)
    {
        throw std::runtime_error("Rollback failed: " + std::string(e.what()));
    }

    try
    {
        fs::rename(old_path, current_binary_path);
    }
    catch (const fs::filesystem_error &e)
    {
        try { fs::rename(temp_path, current_binary_path); }
        catch (const fs::filesystem_error &restore_err)
        {
            log_message("Rollback restore also failed: " + std::string(restore_err.what()) +
                        " — binary may be missing at " + current_binary_path + ", backup at " + temp_path, LogLevel::ERR);
        }
        throw std::runtime_error("Rollback failed: " + std::string(e.what()));
    }

    try
    {
        fs::remove(temp_path);
    }
    catch (const fs::filesystem_error &)
    {
    }
#endif

    return UpdateResult::Success;
}
