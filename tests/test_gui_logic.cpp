// Tests for the GUI-side pure creation options and formatting helpers.
//
// These exercise gui/creation_options.hpp WITHOUT any Qt dependency: the
// struct/validate/mapping/formatting logic is plain C++ so it runs in the
// regular gtest binary (no QApplication needed).

#include <gtest/gtest.h>
#include "creation_options.hpp"
#include "formatting.hpp"
#include "test_temp_dir.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;

namespace {

class GuiLogicTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = test_support::unique_temp_dir("tb_gui_logic");
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    fs::path create_temp_file(const std::string& name) {
        auto path = temp_dir_ / name;
        std::ofstream out(path, std::ios::binary);
        out << "payload";
        return path;
    }

    fs::path temp_dir_;
};

// ── validate() ─────────────────────────────────────────────────

TEST_F(GuiLogicTest, ValidateAcceptsCompleteOptions) {
    auto input = create_temp_file("in.bin");
    gui::CreationOptions options{
        .input_path = input.string(),
        .output_path = (temp_dir_ / "out.torrent").string(),
        .trackers = {"https://tracker.example.com/announce"},
        .web_seeds = {"https://mirror.example.com/files/"},
        .version = TorrentVersion::HYBRID,
        .comment = "hello",
        .source = "SRC",
        .is_private = true,
        .entropy = true,
    };
    EXPECT_TRUE(options.validate().empty());
}

TEST_F(GuiLogicTest, ValidateRejectsEmptyPaths) {
    gui::CreationOptions options{};
    auto errors = options.validate();
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_NE(errors[0].find("input"), std::string::npos);
    EXPECT_NE(errors[1].find("output"), std::string::npos);
}

TEST_F(GuiLogicTest, ValidateRejectsTrackerWithoutScheme) {
    gui::CreationOptions options{
        .input_path = "x",
        .output_path = "y",
        .trackers = {"udp://ok.example/announce", "not-a-url"},
    };
    auto errors = options.validate();
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("tracker"), std::string::npos);
}

TEST_F(GuiLogicTest, ValidateRejectsWebSeedWithoutScheme) {
    gui::CreationOptions options{
        .input_path = "x",
        .output_path = "y",
        .web_seeds = {"also-not-a-url"},
    };
    auto errors = options.validate();
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("web seed"), std::string::npos);
}

TEST_F(GuiLogicTest, ValidateAcceptsSupportedSchemes) {
    gui::CreationOptions options{
        .input_path = "x",
        .output_path = "y",
        .trackers = {"udp://tracker.example:80/announce",
                     "wss://tracker.example/announce",
                     "HTTPS://Tracker.Example/announce",
                     "http://[::1]:8080/announce",
                     "http://host:65535/announce"},
        .web_seeds = {"http://mirror.example/files/",
                      "https://mirror.example/files/"},
    };
    EXPECT_TRUE(options.validate().empty());
}

TEST_F(GuiLogicTest, ValidateRejectsWeakOrUnsupportedSchemes) {
    gui::CreationOptions options{
        .input_path = "x",
        .output_path = "y",
        .trackers = {"://", "x://host", "ftp://host/announce",
                     "https://host/announce with space"},
        .web_seeds = {"udp://host/files/", "http:///files/"},
    };
    auto errors = options.validate();
    ASSERT_EQ(errors.size(), 6u);
    EXPECT_NE(errors[0].find("tracker"), std::string::npos);
    EXPECT_NE(errors[3].find("tracker"), std::string::npos);
    EXPECT_NE(errors[4].find("web seed"), std::string::npos);
    EXPECT_NE(errors[5].find("web seed"), std::string::npos);
}

TEST_F(GuiLogicTest, ValidateRejectsEmptyHostAuthorities) {
    gui::CreationOptions options{
        .input_path = "x",
        .output_path = "y",
        .trackers = {"http://:8080/announce", "http://user@:8080/announce"},
        .web_seeds = {"https://:443/files/"},
    };
    auto errors = options.validate();
    ASSERT_EQ(errors.size(), 3u);
}

TEST_F(GuiLogicTest, ValidateRejectsMalformedIpv6Authorities) {
    gui::CreationOptions options{
        .input_path = "x",
        .output_path = "y",
        .trackers = {"http://[::1]garbage", "http://[::1]:abc"},
    };
    auto errors = options.validate();
    ASSERT_EQ(errors.size(), 2u);
}

TEST_F(GuiLogicTest, ValidateRejectsMalformedPorts) {
    gui::CreationOptions options{
        .input_path = "x",
        .output_path = "y",
        .trackers = {"http://host:abc/announce", "http://host:/announce",
                     "http://host:99999/announce", "http://host:0/announce"},
    };
    auto errors = options.validate();
    ASSERT_EQ(errors.size(), 4u);
}

// ── to_torrent_config() ────────────────────────────────────────

TEST_F(GuiLogicTest, ToTorrentConfigMapsAllFields) {
    auto input = create_temp_file("map.bin");
    auto output = temp_dir_ / "mapped.torrent";

    gui::CreationOptions options{
        .input_path = input.string(),
        .output_path = output.string(),
        .trackers = {"https://t1.example/ann", "https://t2.example/ann"},
        .web_seeds = {"https://ws.example/f/"},
        .version = TorrentVersion::V2,
        .comment = "a comment",
        .source = "TRK",
        .is_private = true,
        .entropy = true,
    };

    TorrentConfig config = options.to_torrent_config();

    // The GUI form fields must reach the core config verbatim.
    EXPECT_EQ(config.path, input);
    EXPECT_EQ(config.output, output);
    ASSERT_EQ(config.trackers.size(), 2u);
    EXPECT_EQ(config.trackers[0], "https://t1.example/ann");
    ASSERT_EQ(config.web_seeds.size(), 1u);
    EXPECT_EQ(config.version, TorrentVersion::V2);
    ASSERT_TRUE(config.comment.has_value());
    EXPECT_EQ(*config.comment, "a comment");
    EXPECT_TRUE(config.is_private);
    ASSERT_TRUE(config.source.has_value());
    EXPECT_EQ(*config.source, "TRK");
    EXPECT_TRUE(config.entropy);

    // Documented defaults for fields outside the current GUI scope.
    ASSERT_TRUE(config.creator.has_value());
    EXPECT_EQ(*config.creator, "Torrent Builder GUI");
    EXPECT_FALSE(config.name.has_value());
    EXPECT_FALSE(config.piece_size.has_value());
    EXPECT_TRUE(config.include_creation_date);
    EXPECT_FALSE(config.silent);
}

TEST_F(GuiLogicTest, ToTorrentConfigEmptyOptionalTextBecomesNullopt) {
    auto input = create_temp_file("opt.bin");
    gui::CreationOptions options{
        .input_path = input.string(),
        .output_path = (temp_dir_ / "opt.torrent").string(),
    };
    TorrentConfig config = options.to_torrent_config();
    EXPECT_FALSE(config.comment.has_value());
    EXPECT_FALSE(config.source.has_value());
    EXPECT_EQ(config.trackers.size(), 0u);
    EXPECT_EQ(config.web_seeds.size(), 0u);
}

TEST_F(GuiLogicTest, ToTorrentConfigThrowsOnMissingInputPath) {
    gui::CreationOptions options{
        .input_path = (temp_dir_ / "does_not_exist.bin").string(),
        .output_path = (temp_dir_ / "x.torrent").string(),
    };
    EXPECT_THROW(options.to_torrent_config(), std::filesystem::filesystem_error);
}

// ── format_bytes() ─────────────────────────────────────────────

TEST(GuiFormatTest, FormatBytesUnits) {
    EXPECT_EQ(gui::format_bytes(0), "0 B");
    EXPECT_EQ(gui::format_bytes(823), "823 B");
    EXPECT_EQ(gui::format_bytes(1024), "1.0 KB");
    EXPECT_EQ(gui::format_bytes(1536), "1.5 KB");
    EXPECT_EQ(gui::format_bytes(1024ull * 1024), "1.0 MB");
    EXPECT_EQ(gui::format_bytes(12ull * 1024 * 1024), "12.0 MB");
    EXPECT_EQ(gui::format_bytes(4ull * 1024 * 1024 * 1024), "4.0 GB");
}

// ── format_eta() ───────────────────────────────────────────────

TEST(GuiFormatTest, FormatEtaClockStyle) {
    EXPECT_EQ(gui::format_eta(-1.0), "--:--");
    EXPECT_EQ(gui::format_eta(0.0), "00:00");
    EXPECT_EQ(gui::format_eta(59.4), "00:59");   // rounds down at 59.4
    EXPECT_EQ(gui::format_eta(61.0), "01:01");
    EXPECT_EQ(gui::format_eta(3661.0), "01:01:01");
    EXPECT_EQ(gui::format_eta(7325.0), "02:02:05");
}

TEST(GuiFormatTest, FormatEtaNonFiniteIsPlaceholder) {
    EXPECT_EQ(gui::format_eta(std::numeric_limits<double>::quiet_NaN()), "--:--");
    EXPECT_EQ(gui::format_eta(std::numeric_limits<double>::infinity()), "--:--");
}

// ── progress_percent() ─────────────────────────────────────────

TEST(GuiFormatTest, ProgressPercentGuardsAndClamps) {
    EXPECT_EQ(gui::progress_percent(0, 0), 0);       // div-by-zero guard
    EXPECT_EQ(gui::progress_percent(10, 0), 0);      // degenerate total
    EXPECT_EQ(gui::progress_percent(0, 100), 0);
    EXPECT_EQ(gui::progress_percent(3, 4), 75);
    EXPECT_EQ(gui::progress_percent(100, 100), 100);
    EXPECT_EQ(gui::progress_percent(200, 100), 100); // clamp high
    EXPECT_EQ(gui::progress_percent(-5, 100), 0);    // clamp low
}

} // namespace
