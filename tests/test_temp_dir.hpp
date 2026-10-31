#pragma once

#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "portable.hpp"

namespace test_support
{
inline std::filesystem::path unique_temp_path(std::string_view prefix)
{
    static std::atomic<unsigned long long> counter{0};
    const auto id = std::to_string(portable_getpid()) + "_" +
                    std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
    auto path = std::filesystem::temp_directory_path() /
                (std::string(prefix) + "_" + id);

    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec)
    {
        throw std::filesystem::filesystem_error(
            "could not clear stale temp path", path, ec);
    }
    if (std::filesystem::exists(path))
    {
        throw std::runtime_error("stale temp path still exists: " + path.string());
    }
    return path;
}

inline std::filesystem::path unique_temp_dir(std::string_view prefix)
{
    auto path = unique_temp_path(prefix);

    std::error_code ec;
    const bool created = std::filesystem::create_directories(path, ec);
    if (ec)
    {
        throw std::filesystem::filesystem_error(
            "could not create temp directory", path, ec);
    }
    if (!created)
    {
        throw std::runtime_error("temp path already exists: " + path.string());
    }
    return path;
}
} // namespace test_support
