#include <gtest/gtest.h>
#include "test_temp_dir.hpp"
#include <filesystem>

namespace fs = std::filesystem;

TEST(TestTempDir, UniquePathsDoNotExist)
{
    const auto first = test_support::unique_temp_path("tb_test_temp");
    const auto second = test_support::unique_temp_path("tb_test_temp");

    EXPECT_NE(first, second);
    EXPECT_FALSE(fs::exists(first));
    EXPECT_FALSE(fs::exists(second));
}

TEST(TestTempDir, UniqueDirIsCreated)
{
    const auto dir = test_support::unique_temp_dir("tb_test_temp");

    EXPECT_TRUE(fs::is_directory(dir));

    std::error_code ec;
    fs::remove_all(dir, ec);
    EXPECT_FALSE(fs::exists(dir));
}
