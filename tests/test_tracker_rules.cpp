#include "portable.hpp"
#include <gtest/gtest.h>
#include "tracker_rules.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class TrackerRulesTest : public ::testing::Test {
protected:
    fs::path temp_dir;

    void SetUp() override {
        temp_dir = fs::temp_directory_path() / ("tracker_rules_test_" + std::to_string(portable_getpid()));
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        fs::remove_all(temp_dir);
    }

    void write_file(const std::string& name, const std::string& content) {
        std::ofstream f(temp_dir / name);
        f << content;
    }
};

TEST_F(TrackerRulesTest, ParseBasicRules) {
    write_file("rules.yaml", R"(
version: 1
trackers:
  ptp:
    domain: "passthepopcorn.me"
    source: "PTP"
    max_piece_length: 16777216
    max_torrent_size: 524288
  hdb:
    domain: "hdbits.org"
    source: "HDBits"
    max_piece_length: 16777216
)");

    TrackerRulesDatabase db;
    db.load(temp_dir / "rules.yaml");

    EXPECT_EQ(db.trackers().size(), 2u);
    EXPECT_EQ(db.trackers()[0].name, "ptp");
    EXPECT_EQ(*db.trackers()[0].domain, "passthepopcorn.me");
    EXPECT_EQ(*db.trackers()[0].source, "PTP");
    EXPECT_EQ(*db.trackers()[0].max_piece_length, 16777216);
    EXPECT_EQ(*db.trackers()[0].max_torrent_size, 524288);
    EXPECT_EQ(db.trackers()[1].name, "hdb");
    EXPECT_FALSE(db.has_default_rules());
}

TEST_F(TrackerRulesTest, ParseWithDefault) {
    write_file("rules.yaml", R"(
version: 1
default:
  max_piece_length: 16777216
trackers:
  ptp:
    domain: "passthepopcorn.me"
    source: "PTP"
)");

    TrackerRulesDatabase db;
    db.load(temp_dir / "rules.yaml");

    EXPECT_TRUE(db.has_default_rules());
    EXPECT_EQ(*db.default_rules().max_piece_length, 16777216);
}

TEST_F(TrackerRulesTest, ParseWithOverrides) {
    write_file("rules.yaml", R"(
version: 1
trackers:
  ptp:
    domain: "passthepopcorn.me"
    source: "PTP"
    max_piece_length: 16777216
    piece_length_overrides:
      - size_below: 1073741824
        piece_length: 512
)");

    TrackerRulesDatabase db;
    db.load(temp_dir / "rules.yaml");

    ASSERT_EQ(db.trackers().size(), 1u);
    ASSERT_EQ(db.trackers()[0].piece_length_overrides.size(), 1u);
    EXPECT_EQ(db.trackers()[0].piece_length_overrides[0].size_below, 1073741824);
    EXPECT_EQ(db.trackers()[0].piece_length_overrides[0].piece_length_kb, 512);
}

TEST_F(TrackerRulesTest, ParseMissingVersionThrows) {
    write_file("rules.yaml", R"(
trackers:
  ptp:
    domain: "passthepopcorn.me"
)");

    TrackerRulesDatabase db;
    EXPECT_THROW(db.load(temp_dir / "rules.yaml"), std::runtime_error);
}

TEST_F(TrackerRulesTest, ParseInvalidVersionThrows) {
    write_file("rules.yaml", R"(
version: 2
trackers:
  ptp:
    domain: "passthepopcorn.me"
)");

    TrackerRulesDatabase db;
    EXPECT_THROW(db.load(temp_dir / "rules.yaml"), std::runtime_error);
}

TEST_F(TrackerRulesTest, ParseFileNotFoundThrows) {
    TrackerRulesDatabase db;
    EXPECT_THROW(db.load(temp_dir / "nonexistent.yaml"), std::runtime_error);
}

TEST_F(TrackerRulesTest, ParseFileTooLargeThrows) {
    write_file("rules.yaml", std::string(1024 * 1024 + 100, ' '));
    TrackerRulesDatabase db;
    EXPECT_THROW(db.load(temp_dir / "rules.yaml"), std::runtime_error);
}

TEST_F(TrackerRulesTest, MatchByExactDomain) {
    write_file("rules.yaml", R"(
version: 1
trackers:
  ptp:
    domain: "passthepopcorn.me"
    source: "PTP"
)");

    TrackerRulesDatabase db;
    db.load(temp_dir / "rules.yaml");

    auto rule = db.find_matching_rule({"https://passthepopcorn.me/announce"});
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->name, "ptp");
    EXPECT_EQ(*rule->source, "PTP");
}

TEST_F(TrackerRulesTest, MatchBySubdomain) {
    write_file("rules.yaml", R"(
version: 1
trackers:
  ptp:
    domain: "passthepopcorn.me"
    source: "PTP"
)");

    TrackerRulesDatabase db;
    db.load(temp_dir / "rules.yaml");

    auto rule = db.find_matching_rule({"https://tracker.passthepopcorn.me/announce"});
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->name, "ptp");
}

TEST_F(TrackerRulesTest, NoMatchReturnsDefault) {
    write_file("rules.yaml", R"(
version: 1
default:
  max_piece_length: 16777216
trackers:
  ptp:
    domain: "passthepopcorn.me"
    source: "PTP"
)");

    TrackerRulesDatabase db;
    db.load(temp_dir / "rules.yaml");

    auto rule = db.find_matching_rule({"https://unknown-tracker.example/announce"});
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->name, "default");
}

TEST_F(TrackerRulesTest, NoMatchNoDefaultReturnsNullopt) {
    write_file("rules.yaml", R"(
version: 1
trackers:
  ptp:
    domain: "passthepopcorn.me"
    source: "PTP"
)");

    TrackerRulesDatabase db;
    db.load(temp_dir / "rules.yaml");

    auto rule = db.find_matching_rule({"https://unknown-tracker.example/announce"});
    EXPECT_FALSE(rule.has_value());
}

TEST_F(TrackerRulesTest, MatchEmptyTrackersReturnsNullopt) {
    write_file("rules.yaml", R"(
version: 1
trackers:
  ptp:
    domain: "passthepopcorn.me"
    source: "PTP"
)");

    TrackerRulesDatabase db;
    db.load(temp_dir / "rules.yaml");

    auto rule = db.find_matching_rule({});
    EXPECT_FALSE(rule.has_value());
}

TEST_F(TrackerRulesTest, EnforceSourceApplied) {
    TrackerRule rule;
    rule.name = "ptp";
    rule.source = "PTP";

    TrackerRulesDatabase db;
    auto result = db.enforce(rule, 1024 * 1024 * 1024, std::nullopt);

    EXPECT_TRUE(result.source_applied);
    EXPECT_EQ(result.source_value, "PTP");
    EXPECT_FALSE(result.adjusted);
}

TEST_F(TrackerRulesTest, EnforceMaxPieceLengthCapsDown) {
    TrackerRule rule;
    rule.name = "ptp";
    rule.max_piece_length = 16777216;

    TrackerRulesDatabase db;
    auto result = db.enforce(rule, 10LL * 1024 * 1024 * 1024, 32768);

    EXPECT_TRUE(result.adjusted);
    EXPECT_EQ(result.original_piece_length, 32768);
    ASSERT_TRUE(result.adjusted_piece_length.has_value());
    EXPECT_LE(*result.adjusted_piece_length * 1024, 16777216);
}

TEST_F(TrackerRulesTest, EnforceWithinLimitNoAdjust) {
    TrackerRule rule;
    rule.name = "ptp";
    rule.max_piece_length = 16777216;

    TrackerRulesDatabase db;
    auto result = db.enforce(rule, 1024 * 1024 * 1024, 512);

    EXPECT_FALSE(result.adjusted);
}

TEST_F(TrackerRulesTest, EnforceOverridesSmallFile) {
    TrackerRule rule;
    rule.name = "ptp";
    rule.max_piece_length = 16777216;
    PieceLengthOverride ov;
    ov.size_below = 1073741824;
    ov.piece_length_kb = 512;
    rule.piece_length_overrides.push_back(ov);

    TrackerRulesDatabase db;
    auto result = db.enforce(rule, 500 * 1024 * 1024, 1024);

    EXPECT_TRUE(result.adjusted);
    ASSERT_TRUE(result.adjusted_piece_length.has_value());
    EXPECT_EQ(*result.adjusted_piece_length, 512);
}

TEST_F(TrackerRulesTest, EnforceNoViolationWhenWithinMaxTorrentSize) {
    TrackerRule rule;
    rule.name = "ptp";
    rule.max_torrent_size = 1048576;

    TrackerRulesDatabase db;
    auto result = db.enforce(rule, 1024 * 1024, 16);

    EXPECT_FALSE(result.constraint_violation);
}

TEST_F(TrackerRulesTest, EnforceMaxTorrentSizeAdjustsPieceLength) {
    TrackerRule rule;
    rule.name = "ptp";
    rule.max_torrent_size = 512000;

    TrackerRulesDatabase db;
    auto result = db.enforce(rule, 1073741824, 16);

    EXPECT_TRUE(result.adjusted);
    ASSERT_TRUE(result.adjusted_piece_length.has_value());
    EXPECT_GT(*result.adjusted_piece_length, 16);
}

TEST_F(TrackerRulesTest, EnforceMaxTorrentSizeConstraintViolation) {
    TrackerRule rule;
    rule.name = "ptp";
    rule.max_piece_length = 16384;
    rule.max_torrent_size = 100;

    TrackerRulesDatabase db;
    auto result = db.enforce(rule, 10LL * 1024 * 1024 * 1024, 16);

    EXPECT_TRUE(result.constraint_violation);
    EXPECT_NE(result.violation_message.find("ptp"), std::string::npos);
}

TEST_F(TrackerRulesTest, FindRulesFileExplicit) {
    write_file("rules.yaml", R"(version: 1
trackers: {}
)");

    auto path = TrackerRulesDatabase::find_rules_file(temp_dir / "rules.yaml");
    EXPECT_EQ(path, temp_dir / "rules.yaml");
}

TEST_F(TrackerRulesTest, FindRulesFileExplicitNotFound) {
    EXPECT_THROW(TrackerRulesDatabase::find_rules_file(temp_dir / "nonexistent.yaml"), std::runtime_error);
}

TEST_F(TrackerRulesTest, CaseInsensitiveDomainMatch) {
    write_file("rules.yaml", R"(
version: 1
trackers:
  ptp:
    domain: "PassThePopcorn.me"
    source: "PTP"
)");

    TrackerRulesDatabase db;
    db.load(temp_dir / "rules.yaml");

    auto rule = db.find_matching_rule({"https://passthepopcorn.me/announce"});
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->name, "ptp");
}

TEST_F(TrackerRulesTest, MultipleOverridesSortedBySize) {
    write_file("rules.yaml", R"(
version: 1
trackers:
  ptp:
    domain: "passthepopcorn.me"
    piece_length_overrides:
      - size_below: 1073741824
        piece_length: 512
      - size_below: 536870912
        piece_length: 256
)");

    TrackerRulesDatabase db;
    db.load(temp_dir / "rules.yaml");

    ASSERT_EQ(db.trackers().size(), 1u);
    ASSERT_EQ(db.trackers()[0].piece_length_overrides.size(), 2u);
    EXPECT_GT(db.trackers()[0].piece_length_overrides[0].size_below,
              db.trackers()[0].piece_length_overrides[1].size_below);
}

TEST_F(TrackerRulesTest, MatchFirstTrackerWins) {
    write_file("rules.yaml", R"(
version: 1
trackers:
  ptp:
    domain: "passthepopcorn.me"
    source: "PTP"
  hdb:
    domain: "hdbits.org"
    source: "HDBits"
)");

    TrackerRulesDatabase db;
    db.load(temp_dir / "rules.yaml");

    auto rule = db.find_matching_rule({
        "https://passthepopcorn.me/announce",
        "https://hdbits.org/announce"
    });
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->name, "ptp");
}

TEST_F(TrackerRulesTest, UdpTrackerMatch) {
    write_file("rules.yaml", R"(
version: 1
trackers:
  ptp:
    domain: "passthepopcorn.me"
    source: "PTP"
)");

    TrackerRulesDatabase db;
    db.load(temp_dir / "rules.yaml");

    auto rule = db.find_matching_rule({"udp://passthepopcorn.me:1337/announce"});
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->name, "ptp");
}
