// Tests for the GUI-side pure creation options and formatting helpers.
//
// These exercise gui/creation_options.hpp WITHOUT any Qt dependency: the
// struct/validate/request-mapping/formatting logic is plain C++ so it runs in
// the regular gtest binary (no QApplication needed).

#include <gtest/gtest.h>
#include "creation_options.hpp"
#include "formatting.hpp"
#include "test_temp_dir.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

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

    /// Same hermetic setup as the planner tests: no rules file discovery.
    CreationPlanner planner() const {
        return CreationPlanner(
            PlannerResources{nullptr, std::make_shared<TrackerRulesDatabase>()});
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
        .trackers = {"not-a-url"},
    };
    auto errors = options.validate();
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("tracker"), std::string::npos);
}

TEST_F(GuiLogicTest, ValidateRejectsWebSocketTracker) {
    gui::CreationOptions options{
        .input_path = "x",
        .output_path = "y",
        .trackers = {"wss://tracker.example/announce"},
    };
    auto errors = options.validate();
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("tracker"), std::string::npos);
}

TEST_F(GuiLogicTest, ValidateAcceptsCliUrlSchemes) {
    gui::CreationOptions options{
        .input_path = "x",
        .output_path = "y",
        .trackers = {"udp://tracker.example:80/announce",
                     "https://tracker.example/announce"},
        .web_seeds = {"http://mirror.example/files/",
                      "https://mirror.example/files/"},
    };
    EXPECT_TRUE(options.validate().empty());
}

TEST_F(GuiLogicTest, ValidateRejectsUdpWebSeed) {
    gui::CreationOptions options{
        .input_path = "x",
        .output_path = "y",
        .web_seeds = {"udp://mirror.example:80/files/"},
    };
    auto errors = options.validate();
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("web seed"), std::string::npos);
}

// ── planner integration ────────────────────────────────────────

TEST_F(GuiLogicTest, PlannerRejectsTrackerWithoutScheme) {
    auto input = create_temp_file("bad_tracker.bin");
    gui::CreationOptions options{
        .input_path = input.string(),
        .output_path = (temp_dir_ / "out.torrent").string(),
        .trackers = {"not-a-url"},
    };

    EXPECT_THROW(planner().plan(options.to_creation_request()), std::runtime_error);
}

TEST_F(GuiLogicTest, PlannerRejectsWebSocketTracker) {
    auto input = create_temp_file("ws_tracker.bin");
    gui::CreationOptions options{
        .input_path = input.string(),
        .output_path = (temp_dir_ / "out.torrent").string(),
        .trackers = {"wss://tracker.example/announce"},
    };

    EXPECT_THROW(planner().plan(options.to_creation_request()), std::runtime_error);
}

TEST_F(GuiLogicTest, PlannerRejectsInvalidWebSeed) {
    auto input = create_temp_file("bad_seed.bin");
    gui::CreationOptions options{
        .input_path = input.string(),
        .output_path = (temp_dir_ / "out.torrent").string(),
        .web_seeds = {"not-a-url"},
    };

    EXPECT_THROW(planner().plan(options.to_creation_request()), std::runtime_error);
}

TEST_F(GuiLogicTest, PlannerAcceptsCliUrlSchemes) {
    auto input = create_temp_file("schemes.bin");
    gui::CreationOptions options{
        .input_path = input.string(),
        .output_path = (temp_dir_ / "out.torrent").string(),
        .trackers = {"udp://tracker.example:80/announce",
                     "https://tracker.example/announce"},
        .web_seeds = {"http://mirror.example/files/",
                      "https://mirror.example/files/"},
    };

    EXPECT_NO_THROW(planner().plan(options.to_creation_request()));
}

TEST_F(GuiLogicTest, PlannerRejectsUdpWebSeed) {
    auto input = create_temp_file("udp_seed.bin");
    gui::CreationOptions options{
        .input_path = input.string(),
        .output_path = (temp_dir_ / "out.torrent").string(),
        .web_seeds = {"udp://mirror.example:80/files/"},
    };

    EXPECT_THROW(planner().plan(options.to_creation_request()), std::runtime_error);
}

// ── to_creation_request() ──────────────────────────────────────

TEST_F(GuiLogicTest, ToCreationRequestMapsAllFields) {
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

    TorrentConfig config = planner().plan(options.to_creation_request()).config;

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

    // Defaults come from the shared planner, not from GUI-local code.
    ASSERT_TRUE(config.creator.has_value());
    EXPECT_EQ(*config.creator, "Torrent Builder");
    EXPECT_FALSE(config.name.has_value());
    EXPECT_FALSE(config.piece_size.has_value());
    EXPECT_TRUE(config.include_creation_date);
    EXPECT_FALSE(config.silent);
}

TEST_F(GuiLogicTest, EmptyOptionalTextBecomesNullopt) {
    auto input = create_temp_file("opt.bin");
    gui::CreationOptions options{
        .input_path = input.string(),
        .output_path = (temp_dir_ / "opt.torrent").string(),
    };
    TorrentConfig config = planner().plan(options.to_creation_request()).config;
    EXPECT_FALSE(config.comment.has_value());
    EXPECT_FALSE(config.source.has_value());
    EXPECT_EQ(config.trackers.size(), 0u);
    EXPECT_EQ(config.web_seeds.size(), 0u);
}

TEST_F(GuiLogicTest, VersionMappingCoversAllEnumerators) {
    gui::CreationOptions v1{.version = TorrentVersion::V1};
    gui::CreationOptions v2{.version = TorrentVersion::V2};
    gui::CreationOptions hybrid{.version = TorrentVersion::HYBRID};
    EXPECT_EQ(v1.to_creation_request().values.torrent_version, 1);
    EXPECT_EQ(v2.to_creation_request().values.torrent_version, 2);
    EXPECT_EQ(hybrid.to_creation_request().values.torrent_version, 3);
}

TEST_F(GuiLogicTest, DefaultVersionIsHybrid) {
    gui::CreationOptions options{};
    EXPECT_EQ(options.version, TorrentVersion::HYBRID);
    EXPECT_EQ(options.to_creation_request().values.torrent_version, 3);
}

TEST_F(GuiLogicTest, PlannerThrowsOnMissingInputPath) {
    gui::CreationOptions options{
        .input_path = (temp_dir_ / "does_not_exist.bin").string(),
        .output_path = (temp_dir_ / "x.torrent").string(),
    };
    EXPECT_THROW(planner().plan(options.to_creation_request()),
                 std::filesystem::filesystem_error);
}

TEST_F(GuiLogicTest, GuiRequestMatchesEquivalentPlannerRequest) {
    // CLI flag parsing itself is covered by the integration tests in
    // tests/test_cli.cpp; this pins the widget-to-request mapping.
    auto input = create_temp_file("parity.bin");
    const auto output = temp_dir_ / "parity.torrent";

    gui::CreationOptions options{
        .input_path = input.string(),
        .output_path = output.string(),
        .trackers = {"https://tracker.example/announce"},
        .version = TorrentVersion::HYBRID,
        .comment = "same",
        .is_private = true,
    };
    const TorrentConfig gui_config = planner().plan(options.to_creation_request()).config;

    CreationRequest cli_request;
    cli_request.input = input;
    cli_request.output = output;
    cli_request.values.trackers =
        std::vector<std::string>{"https://tracker.example/announce"};
    cli_request.values.torrent_version = 3;
    cli_request.values.comment = "same";
    cli_request.values.is_private = true;
    // The GUI keeps built-in excludes off until the toggle lands.
    cli_request.values.builtin_excludes = false;
    const TorrentConfig cli_config = planner().plan(cli_request).config;

    EXPECT_EQ(gui_config.version, cli_config.version);
    EXPECT_EQ(gui_config.creator, cli_config.creator);
    EXPECT_EQ(gui_config.trackers, cli_config.trackers);
    EXPECT_EQ(gui_config.comment, cli_config.comment);
    EXPECT_EQ(gui_config.is_private, cli_config.is_private);
    EXPECT_EQ(gui_config.include_creation_date, cli_config.include_creation_date);
    EXPECT_EQ(gui_config.piece_size, cli_config.piece_size);
    EXPECT_EQ(gui_config.exclude_regex.size(), cli_config.exclude_regex.size());
    EXPECT_EQ(gui_config.output, cli_config.output);
}

TEST_F(GuiLogicTest, BuiltinExcludesStayOffUntilToggleExists) {
    auto input = create_temp_file("excludes.bin");
    gui::CreationOptions options{
        .input_path = input.string(),
        .output_path = (temp_dir_ / "excludes.torrent").string(),
    };

    const TorrentConfig config = planner().plan(options.to_creation_request()).config;
    EXPECT_TRUE(config.exclude_regex.empty());
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
