#include <gtest/gtest.h>
#include "torrent_creator.hpp"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

class TorrentConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = fs::temp_directory_path() / "torrent_builder_test";
        fs::create_directories(temp_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    fs::path create_temp_file(const std::string& name) {
        auto path = temp_dir_ / name;
        std::ofstream(path) << "test content";
        return path;
    }

    fs::path temp_dir_;
};

TEST_F(TorrentConfigTest, ValidFilePath) {
    auto file = create_temp_file("test.txt");
    EXPECT_NO_THROW(TorrentConfig(
        file, temp_dir_ / "out.torrent", {},
        TorrentVersion::HYBRID
    ));
}

TEST_F(TorrentConfigTest, ValidDirectoryPath) {
    EXPECT_NO_THROW(TorrentConfig(
        temp_dir_, temp_dir_ / "out.torrent", {},
        TorrentVersion::HYBRID
    ));
}

TEST_F(TorrentConfigTest, InvalidPathThrows) {
    EXPECT_THROW(TorrentConfig(
        fs::path("/nonexistent/path/that/does/not/exist"),
        temp_dir_ / "out.torrent", {},
        TorrentVersion::HYBRID
    ), std::filesystem::filesystem_error);
}

TEST_F(TorrentConfigTest, DefaultValues) {
    auto file = create_temp_file("defaults.txt");
    TorrentConfig config(
        file, temp_dir_ / "out.torrent", {},
        TorrentVersion::HYBRID
    );

    EXPECT_FALSE(config.is_private);
    EXPECT_EQ(config.comment, std::nullopt);
    EXPECT_EQ(config.piece_size, std::nullopt);
    EXPECT_EQ(config.creator, std::nullopt);
    EXPECT_FALSE(config.include_creation_date);
    EXPECT_TRUE(config.web_seeds.empty());
    EXPECT_TRUE(config.trackers.empty());
}

TEST(GetTorrentFlags, V1) {
    auto flags = TorrentCreator::get_torrent_flags(TorrentVersion::V1);
    EXPECT_TRUE(flags & lt::create_torrent::v1_only);
}

TEST(GetTorrentFlags, V2) {
    auto flags = TorrentCreator::get_torrent_flags(TorrentVersion::V2);
    EXPECT_TRUE(flags & lt::create_torrent::v2_only);
}

TEST(GetTorrentFlags, Hybrid) {
    auto flags = TorrentCreator::get_torrent_flags(TorrentVersion::HYBRID);
    EXPECT_FALSE(flags & lt::create_torrent::v1_only);
    EXPECT_FALSE(flags & lt::create_torrent::v2_only);
}
