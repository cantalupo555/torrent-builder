#pragma once

#include <atomic>
#include <filesystem>
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
    return path;
}

inline std::filesystem::path unique_temp_dir(std::string_view prefix)
{
    auto path = unique_temp_path(prefix);
    std::filesystem::create_directories(path);
    return path;
}
} // namespace test_support
