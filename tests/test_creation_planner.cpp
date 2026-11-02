#include "creation_planner.hpp"

#include "constants.hpp"
#include "test_temp_dir.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{

class CwdGuard
{
  public:
    explicit CwdGuard(fs::path original) : original_(std::move(original)) {}
    ~CwdGuard()
    {
        std::error_code ec;
        fs::current_path(original_, ec);
    }

  private:
    fs::path original_;
};

class CapturingSink : public OutputSink
{
  public:
    void info(const std::string &msg) override { infos.push_back(msg); }
    void verbose(const std::string &msg) override { verboses.push_back(msg); }
    void error(const std::string &msg) override { errors.push_back(msg); }
    void progress(const ProgressInfo &) override {}

    std::vector<std::string> infos;
    std::vector<std::string> verboses;
    std::vector<std::string> errors;
};

std::string thrown_message(const std::function<void()> &fn)
{
    try
    {
        fn();
    }
    catch (const std::exception &e)
    {
        return e.what();
    }
    return {};
}

class CreationPlannerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        temp_dir_ = test_support::unique_temp_dir("tb_planner");
        input_ = temp_dir_ / "content.bin";
        write_file(input_, std::string(64 * 1024, 'X'));
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    static void write_file(const fs::path &path, const std::string &content)
    {
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    CreationRequest base_request() const
    {
        CreationRequest request;
        request.input = input_;
        request.output = temp_dir_ / "out.torrent";
        return request;
    }

    /// Empty rules database keeps the unit tests hermetic: no ./rules.yaml or
    /// ~/.config discovery can leak into the assertions.
    CreationPlanner planner() const
    {
        return CreationPlanner(
            PlannerResources{nullptr, std::make_shared<TrackerRulesDatabase>()});
    }

    CreationPlanner planner_with_rules(const fs::path &rules_path) const
    {
        auto rules = std::make_shared<TrackerRulesDatabase>();
        rules->load(rules_path);
        return CreationPlanner(PlannerResources{nullptr, rules});
    }

    fs::path write_presets(const std::string &yaml)
    {
        const auto path = temp_dir_ / "presets.yaml";
        write_file(path, yaml);
        return path;
    }

    fs::path write_rules(const std::string &yaml)
    {
        const auto path = temp_dir_ / "rules.yaml";
        write_file(path, yaml);
        return path;
    }

    fs::path temp_dir_;
    fs::path input_;
};

TEST_F(CreationPlannerTest, DefaultsAreHybridWithCreatorAndDate)
{
    CapturingSink sink;
    const CreationPlan plan = planner().plan(base_request(), sink);

    EXPECT_FALSE(plan.auto_named);
    EXPECT_EQ(plan.config.path, input_);
    EXPECT_EQ(plan.config.output, temp_dir_ / "out.torrent");
    EXPECT_EQ(plan.config.version, TorrentVersion::HYBRID);
    ASSERT_TRUE(plan.config.creator.has_value());
    EXPECT_EQ(*plan.config.creator, std::string(Defaults::creator));
    EXPECT_TRUE(plan.config.include_creation_date);
    EXPECT_FALSE(plan.config.is_private);
    EXPECT_FALSE(plan.config.entropy);
    EXPECT_FALSE(plan.config.silent);
    EXPECT_FALSE(plan.config.piece_size.has_value());
    EXPECT_FALSE(plan.config.target_piece_count.has_value());
    EXPECT_TRUE(plan.config.trackers.empty());
    EXPECT_TRUE(plan.config.web_seeds.empty());
    EXPECT_FALSE(plan.config.comment.has_value());
    EXPECT_FALSE(plan.config.name.has_value());
    EXPECT_FALSE(plan.config.source.has_value());
    EXPECT_TRUE(sink.verboses.empty());
}

TEST_F(CreationPlannerTest, MissingInputThrows)
{
    CreationRequest request = base_request();
    request.input.clear();

    const std::string message = thrown_message([&] { planner().plan(request); });
    EXPECT_NE(message.find("Path is required"), std::string::npos) << message;
}

TEST_F(CreationPlannerTest, NonExistentInputThrowsFilesystemError)
{
    CreationRequest request = base_request();
    request.input = temp_dir_ / "missing.bin";

    EXPECT_THROW(planner().plan(request), std::filesystem::filesystem_error);
}

TEST_F(CreationPlannerTest, AutoNamingUsesOutputDirectory)
{
    const auto output_dir = temp_dir_ / "generated";
    CreationRequest request = base_request();
    request.output = std::nullopt;
    request.output_dir = output_dir;
    request.values.trackers = std::vector<std::string>{"https://tracker.example/announce"};

    const CreationPlan plan = planner().plan(request);
    EXPECT_TRUE(plan.auto_named);
    EXPECT_EQ(plan.config.output.parent_path(), output_dir);
    EXPECT_EQ(plan.config.output.extension(), ".torrent");
}

TEST_F(CreationPlannerTest, OutputDirectoryIsCreated)
{
    const auto output_dir = temp_dir_ / "nested" / "output";
    CreationRequest request = base_request();
    request.output = std::nullopt;
    request.output_dir = output_dir;

    const CreationPlan plan = planner().plan(request);
    EXPECT_TRUE(fs::is_directory(output_dir));
    EXPECT_TRUE(plan.auto_named);
}

TEST_F(CreationPlannerTest, AutoNamingResolvesCollisions)
{
    const auto output_dir = temp_dir_ / "generated";
    fs::create_directories(output_dir);

    CreationRequest request = base_request();
    request.output = std::nullopt;
    request.output_dir = output_dir;

    const auto first = planner().plan(request);
    write_file(first.config.output, "occupied");

    const auto second = planner().plan(request);
    EXPECT_NE(first.config.output, second.config.output);
}

TEST_F(CreationPlannerTest, InvalidTrackerUrlThrows)
{
    CreationRequest request = base_request();
    request.values.trackers = std::vector<std::string>{"ws://tracker.example/announce"};

    const std::string message = thrown_message([&] { planner().plan(request); });
    EXPECT_NE(message.find("Invalid tracker URL"), std::string::npos) << message;
}

TEST_F(CreationPlannerTest, DuplicateTrackerThrows)
{
    CreationRequest request = base_request();
    request.values.trackers = std::vector<std::string>{"https://tracker.example/announce",
                                                       "https://tracker.example/announce"};

    const std::string message = thrown_message([&] { planner().plan(request); });
    EXPECT_NE(message.find("Duplicate tracker URL"), std::string::npos) << message;
}

TEST_F(CreationPlannerTest, DefaultTrackersArePrepended)
{
    CreationRequest request = base_request();
    request.use_default_trackers = true;
    request.values.trackers = std::vector<std::string>{"https://custom.example/announce"};

    const CreationPlan plan = planner().plan(request);
    ASSERT_EQ(plan.config.trackers.size(), Defaults::trackers.size() + 1);
    EXPECT_EQ(plan.config.trackers.front(), Defaults::trackers.front());
    EXPECT_EQ(plan.config.trackers.back(), "https://custom.example/announce");
}

TEST_F(CreationPlannerTest, PresetBaseIsApplied)
{
    const auto presets = write_presets(R"(
version: 1
default:
  private: true
presets:
  ptp:
    source: "PRESET_SRC"
    comment: "preset comment"
    torrent_version: 2
    creator: "Preset Creator"
    trackers:
      - "https://preset.example/announce"
)");

    CreationRequest request = base_request();
    request.preset = "ptp";
    request.preset_file = presets;

    const CreationPlan plan = planner().plan(request);
    ASSERT_TRUE(plan.config.source.has_value());
    EXPECT_EQ(*plan.config.source, "PRESET_SRC");
    ASSERT_TRUE(plan.config.comment.has_value());
    EXPECT_EQ(*plan.config.comment, "preset comment");
    EXPECT_EQ(plan.config.version, TorrentVersion::V2);
    ASSERT_TRUE(plan.config.creator.has_value());
    EXPECT_EQ(*plan.config.creator, "Preset Creator");
    EXPECT_TRUE(plan.config.is_private);
    ASSERT_EQ(plan.config.trackers.size(), 1u);
    EXPECT_EQ(plan.config.trackers.front(), "https://preset.example/announce");
}

TEST_F(CreationPlannerTest, RequestValuesOverridePreset)
{
    const auto presets = write_presets(R"(
version: 1
presets:
  ptp:
    source: "PRESET_SRC"
    comment: "preset comment"
    torrent_version: 2
    creator: "Preset Creator"
)");

    CreationRequest request = base_request();
    request.preset = "ptp";
    request.preset_file = presets;
    request.values.source = "CLI_SRC";
    request.values.comment = "cli comment";
    request.values.torrent_version = 1;
    request.values.creator = "CLI Creator";

    const CreationPlan plan = planner().plan(request);
    ASSERT_TRUE(plan.config.source.has_value());
    EXPECT_EQ(*plan.config.source, "CLI_SRC");
    ASSERT_TRUE(plan.config.comment.has_value());
    EXPECT_EQ(*plan.config.comment, "cli comment");
    EXPECT_EQ(plan.config.version, TorrentVersion::V1);
    ASSERT_TRUE(plan.config.creator.has_value());
    EXPECT_EQ(*plan.config.creator, "CLI Creator");
}

TEST_F(CreationPlannerTest, RequestTrackersReplacePresetList)
{
    const auto presets = write_presets(R"(
version: 1
presets:
  ptp:
    trackers:
      - "https://preset.example/announce"
)");

    CreationRequest request = base_request();
    request.preset = "ptp";
    request.preset_file = presets;
    request.values.trackers = std::vector<std::string>{"https://cli.example/announce"};

    const CreationPlan plan = planner().plan(request);
    ASSERT_EQ(plan.config.trackers.size(), 1u);
    EXPECT_EQ(plan.config.trackers.front(), "https://cli.example/announce");
}

TEST_F(CreationPlannerTest, NoCreatorWinsOverPresetCreator)
{
    const auto presets = write_presets(R"(
version: 1
presets:
  ptp:
    creator: "Preset Creator"
)");

    CreationRequest request = base_request();
    request.preset = "ptp";
    request.preset_file = presets;
    request.values.no_creator = true;

    const CreationPlan plan = planner().plan(request);
    EXPECT_FALSE(plan.config.creator.has_value());
}

TEST_F(CreationPlannerTest, NoDateOmitsCreationDate)
{
    CreationRequest request = base_request();
    request.values.no_date = true;

    const CreationPlan plan = planner().plan(request);
    EXPECT_FALSE(plan.config.include_creation_date);
}

TEST_F(CreationPlannerTest, ExplicitEmptySourceClearsPreset)
{
    const auto presets = write_presets(R"(
version: 1
presets:
  ptp:
    source: "PRESET_SRC"
)");

    CreationRequest request = base_request();
    request.preset = "ptp";
    request.preset_file = presets;
    request.values.source = "";

    const CreationPlan plan = planner().plan(request);
    EXPECT_FALSE(plan.config.source.has_value());
}

TEST_F(CreationPlannerTest, InvalidRequestPieceSizeThrows)
{
    CreationRequest request = base_request();
    request.values.piece_size = 100;

    const std::string message = thrown_message([&] { planner().plan(request); });
    EXPECT_NE(message.find("Invalid piece size"), std::string::npos) << message;
    EXPECT_NE(message.find("16"), std::string::npos) << message;
}

TEST_F(CreationPlannerTest, ValidPieceSizeIsStoredInBytes)
{
    CreationRequest request = base_request();
    request.values.piece_size = 1024;

    const CreationPlan plan = planner().plan(request);
    ASSERT_TRUE(plan.config.piece_size.has_value());
    EXPECT_EQ(*plan.config.piece_size, 1024 * 1024);
}

TEST_F(CreationPlannerTest, InvalidPresetPieceSizeIsIgnored)
{
    const auto presets = write_presets(R"(
version: 1
presets:
  broken:
    piece_size: 100
)");

    CreationRequest request = base_request();
    request.preset = "broken";
    request.preset_file = presets;

    const CreationPlan plan = planner().plan(request);
    EXPECT_FALSE(plan.config.piece_size.has_value());
}

TEST_F(CreationPlannerTest, TargetPieceCountResolvesToPieceSize)
{
    auto large = temp_dir_ / "large.bin";
    write_file(large, std::string(1024 * 1024, 'Y'));

    CapturingSink sink;
    CreationRequest request = base_request();
    request.input = large;
    request.values.target_piece_count = 4;

    const CreationPlan plan = planner().plan(request, sink);
    ASSERT_TRUE(plan.config.piece_size.has_value());
    EXPECT_EQ(*plan.config.piece_size, 256 * 1024);
    ASSERT_TRUE(plan.config.target_piece_count.has_value());
    EXPECT_EQ(*plan.config.target_piece_count, 4);
    EXPECT_FALSE(sink.verboses.empty());
}

TEST_F(CreationPlannerTest, ContentSizeHintAvoidsRescan)
{
    const auto empty = temp_dir_ / "empty_hint.bin";
    write_file(empty, "");

    CapturingSink sink;
    CreationRequest request = base_request();
    request.input = empty;
    request.values.target_piece_count = 4;
    request.content_size_hint = 1024 * 1024;

    const CreationPlan plan = planner().plan(request, sink);
    ASSERT_TRUE(plan.config.piece_size.has_value());
    EXPECT_EQ(*plan.config.piece_size, 256 * 1024);
    EXPECT_TRUE(sink.infos.empty()) << "empty-content warning means the hint was ignored";
}

TEST_F(CreationPlannerTest, PieceSizeAndTargetAreMutuallyExclusive)
{
    CreationRequest request = base_request();
    request.values.piece_size = 512;
    request.values.target_piece_count = 10;

    const std::string message = thrown_message([&] { planner().plan(request); });
    EXPECT_NE(message.find("mutually exclusive"), std::string::npos) << message;
}

TEST_F(CreationPlannerTest, InvalidRequestVersionThrows)
{
    CreationRequest request = base_request();
    request.values.torrent_version = 5;

    const std::string message = thrown_message([&] { planner().plan(request); });
    EXPECT_NE(message.find("Invalid torrent version"), std::string::npos) << message;
}

TEST_F(CreationPlannerTest, InvalidPresetVersionThrows)
{
    const auto presets = write_presets(R"(
version: 1
presets:
  broken:
    torrent_version: 9
)");

    CreationRequest request = base_request();
    request.preset = "broken";
    request.preset_file = presets;

    const std::string message = thrown_message([&] { planner().plan(request); });
    EXPECT_NE(message.find("Invalid torrent version"), std::string::npos) << message;
}

TEST_F(CreationPlannerTest, WhitespaceNameThrows)
{
    CreationRequest request = base_request();
    request.values.name = "   ";

    const std::string message = thrown_message([&] { planner().plan(request); });
    EXPECT_NE(message.find("whitespace-only"), std::string::npos) << message;
}

TEST_F(CreationPlannerTest, EmptyCommentIsPreserved)
{
    CreationRequest request = base_request();
    request.values.comment = "";

    const CreationPlan plan = planner().plan(request);
    ASSERT_TRUE(plan.config.comment.has_value());
    EXPECT_TRUE(plan.config.comment->empty());
}

TEST_F(CreationPlannerTest, SilentIsForwarded)
{
    CreationRequest request = base_request();
    request.silent = true;

    const CreationPlan plan = planner().plan(request);
    EXPECT_TRUE(plan.config.silent);
}

TEST_F(CreationPlannerTest, BuiltinExcludesCanBeDisabled)
{
    CreationRequest request = base_request();

    const CreationPlan with_builtin = planner().plan(request);
    EXPECT_FALSE(with_builtin.config.exclude_regex.empty());

    request.values.builtin_excludes = false;
    const CreationPlan without_builtin = planner().plan(request);
    EXPECT_TRUE(without_builtin.config.exclude_regex.empty());
}

TEST_F(CreationPlannerTest, RulesAutoSetSource)
{
    const auto rules = write_rules(R"(
version: 1
trackers:
  ptp:
    domain: "tracker.example"
    source: "PTP"
)");

    CreationRequest request = base_request();
    request.values.trackers = std::vector<std::string>{"https://tracker.example/announce"};

    const CreationPlan plan = planner_with_rules(rules).plan(request);
    ASSERT_TRUE(plan.config.source.has_value());
    EXPECT_EQ(*plan.config.source, "PTP");
}

TEST_F(CreationPlannerTest, RulesApplyAutomaticPieceSize)
{
    const auto rules = write_rules(R"(
version: 1
trackers:
  ptp:
    domain: "tracker.example"
    piece_length_overrides:
      - size_below: 1073741824
        piece_length: 32
)");

    CreationRequest request = base_request();
    request.values.trackers = std::vector<std::string>{"https://tracker.example/announce"};

    const CreationPlan plan = planner_with_rules(rules).plan(request);
    ASSERT_TRUE(plan.config.piece_size.has_value());
    EXPECT_EQ(*plan.config.piece_size, 32 * 1024);
}

TEST_F(CreationPlannerTest, RulesPreserveUserPieceSize)
{
    const auto rules = write_rules(R"(
version: 1
trackers:
  ptp:
    domain: "tracker.example"
    max_piece_length: 524288
)");

    CreationRequest request = base_request();
    request.values.trackers = std::vector<std::string>{"https://tracker.example/announce"};
    request.values.piece_size = 1024;

    const CreationPlan plan = planner_with_rules(rules).plan(request);
    ASSERT_TRUE(plan.config.piece_size.has_value());
    EXPECT_EQ(*plan.config.piece_size, 1024 * 1024);
}

TEST_F(CreationPlannerTest, RulesCapTargetDerivedPieceSize)
{
    auto large = temp_dir_ / "target_rule.bin";
    write_file(large, std::string(1024 * 1024, 'Z'));

    const auto rules = write_rules(R"(
version: 1
trackers:
  ptp:
    domain: "tracker.example"
    max_piece_length: 131072
)");

    CreationRequest request = base_request();
    request.input = large;
    request.values.trackers = std::vector<std::string>{"https://tracker.example/announce"};
    request.values.target_piece_count = 4;

    const CreationPlan plan = planner_with_rules(rules).plan(request);
    ASSERT_TRUE(plan.config.piece_size.has_value());
    EXPECT_EQ(*plan.config.piece_size, 128 * 1024);
}

TEST_F(CreationPlannerTest, MalformedDiscoveredRulesAreSkipped)
{
    write_file(temp_dir_ / "rules.yaml", "version: 1\ntrackers: [broken\n");

    const auto original_cwd = fs::current_path();
    CwdGuard guard{original_cwd};
    std::error_code cwd_ec;
    fs::current_path(temp_dir_, cwd_ec);
    ASSERT_FALSE(cwd_ec);

    CreationRequest request = base_request();
    request.discover_rules = true;
    request.values.trackers = std::vector<std::string>{"https://tracker.example/announce"};

    CreationPlan plan = CreationPlanner{}.plan(request);
    EXPECT_FALSE(plan.config.source.has_value());

    std::ifstream log_file(temp_dir_ / "torrent_builder.log");
    const std::string log((std::istreambuf_iterator<char>(log_file)),
                          std::istreambuf_iterator<char>());
    EXPECT_NE(log.find("Failed to load tracker rules"), std::string::npos) << log;
}

TEST_F(CreationPlannerTest, SeasonPackFailsWhenRequested)
{
    const auto season_dir = temp_dir_ / "Show.Name.S01";
    fs::create_directories(season_dir);
    write_file(season_dir / "Show.Name.S01E01.mkv", "one");
    write_file(season_dir / "Show.Name.S01E03.mkv", "three");

    CreationRequest request = base_request();
    request.input = season_dir;
    request.fail_on_season_warning = true;

    const std::string message = thrown_message([&] { planner().plan(request); });
    EXPECT_NE(message.find("E02"), std::string::npos) << message;
}

TEST_F(CreationPlannerTest, InvalidPresetTargetPieceCountIsIgnored)
{
    const auto presets = write_presets(R"(
version: 1
presets:
  broken:
    target_piece_count: 0
)");

    CreationRequest request = base_request();
    request.preset = "broken";
    request.preset_file = presets;

    const CreationPlan plan = planner().plan(request);
    EXPECT_FALSE(plan.config.target_piece_count.has_value());
}

TEST_F(CreationPlannerTest, InvalidRequestTargetPieceCountThrows)
{
    CreationRequest request = base_request();
    request.values.target_piece_count = 0;

    const std::string message = thrown_message([&] { planner().plan(request); });
    EXPECT_NE(message.find("positive integer"), std::string::npos) << message;
}

TEST_F(CreationPlannerTest, EmptyContentTargetFallsBackToAutomaticPieceSize)
{
    const auto empty_input = temp_dir_ / "empty.bin";
    write_file(empty_input, "");

    CreationRequest request = base_request();
    request.input = empty_input;
    request.values.target_piece_count = 4;

    CapturingSink sink;
    const CreationPlan plan = planner().plan(request, sink);
    EXPECT_FALSE(plan.config.piece_size.has_value());
    EXPECT_FALSE(plan.config.target_piece_count.has_value());
    EXPECT_FALSE(sink.infos.empty());
}

TEST_F(CreationPlannerTest, EmptyRequestTrackersDiscardPresetList)
{
    const auto presets = write_presets(R"(
version: 1
presets:
  ptp:
    trackers:
      - "https://preset.example/announce"
)");

    CreationRequest request = base_request();
    request.preset = "ptp";
    request.preset_file = presets;
    request.values.trackers = std::vector<std::string>{};

    const CreationPlan plan = planner().plan(request);
    EXPECT_TRUE(plan.config.trackers.empty());
}

TEST_F(CreationPlannerTest, InjectedPresetsTakePrecedenceOverPresetFile)
{
    const auto injected_path = temp_dir_ / "injected_presets.yaml";
    write_file(injected_path, R"(
version: 1
presets:
  ptp:
    source: "INJECTED"
)");
    PresetLoader injected;
    injected.load(injected_path);

    const auto file_presets = write_presets(R"(
version: 1
presets:
  ptp:
    source: "FROM_FILE"
)");

    CreationRequest request = base_request();
    request.preset = "ptp";
    request.preset_file = file_presets;

    const CreationPlan plan =
        CreationPlanner(PlannerResources{
            std::make_shared<PresetLoader>(injected),
            std::make_shared<TrackerRulesDatabase>()})
            .plan(request);
    ASSERT_TRUE(plan.config.source.has_value());
    EXPECT_EQ(*plan.config.source, "INJECTED");
}

TEST_F(CreationPlannerTest, InjectedRulesTakePrecedenceOverRulesFile)
{
    const auto injected_path = temp_dir_ / "injected_rules.yaml";
    write_file(injected_path, R"(
version: 1
trackers:
  injected:
    domain: "tracker.example"
    source: "INJECTED"
)");
    auto injected = std::make_shared<TrackerRulesDatabase>();
    injected->load(injected_path);

    const auto file_rules = write_rules(R"(
version: 1
trackers:
  from_file:
    domain: "tracker.example"
    source: "FROM_FILE"
)");

    CreationRequest request = base_request();
    request.rules_file = file_rules;
    request.values.trackers = std::vector<std::string>{"https://tracker.example/announce"};

    const CreationPlan plan =
        CreationPlanner(PlannerResources{nullptr, injected}).plan(request);
    ASSERT_TRUE(plan.config.source.has_value());
    EXPECT_EQ(*plan.config.source, "INJECTED");
}

TEST_F(CreationPlannerTest, RulesUserSizeWarningIsLogged)
{
    const auto rules = write_rules(R"(
version: 1
trackers:
  ptp:
    domain: "tracker.example"
    max_piece_length: 524288
)");

    const auto original_cwd = fs::current_path();
    CwdGuard guard{original_cwd};
    std::error_code cwd_ec;
    fs::current_path(temp_dir_, cwd_ec);
    ASSERT_FALSE(cwd_ec);

    CreationRequest request = base_request();
    request.values.trackers = std::vector<std::string>{"https://tracker.example/announce"};
    request.values.piece_size = 1024;
    planner_with_rules(rules).plan(request);

    std::ifstream log_file(temp_dir_ / "torrent_builder.log");
    const std::string log((std::istreambuf_iterator<char>(log_file)),
                          std::istreambuf_iterator<char>());
    EXPECT_NE(log.find("user-specified piece size"), std::string::npos) << log;
}

TEST_F(CreationPlannerTest, DefaultRulesDiscoveryIsOptIn)
{
    write_rules(R"(
version: 1
trackers:
  ptp:
    domain: "tracker.example"
    source: "DISCOVERED"
)");

    const auto original_cwd = fs::current_path();
    CwdGuard guard{original_cwd};
    std::error_code cwd_ec;
    fs::current_path(temp_dir_, cwd_ec);
    ASSERT_FALSE(cwd_ec);

    CreationRequest request = base_request();
    request.values.trackers = std::vector<std::string>{"https://tracker.example/announce"};

    const CreationPlan without_discovery = CreationPlanner{}.plan(request);
    EXPECT_FALSE(without_discovery.config.source.has_value());

    request.discover_rules = true;
    const CreationPlan with_discovery = CreationPlanner{}.plan(request);
    ASSERT_TRUE(with_discovery.config.source.has_value());
    EXPECT_EQ(*with_discovery.config.source, "DISCOVERED");
}

TEST_F(CreationPlannerTest, SeasonPackIgnoredWhenNotRequested)
{
    const auto season_dir = temp_dir_ / "Show.Name.S01";
    fs::create_directories(season_dir);
    write_file(season_dir / "Show.Name.S01E01.mkv", "one");
    write_file(season_dir / "Show.Name.S01E03.mkv", "three");

    CreationRequest request = base_request();
    request.input = season_dir;

    EXPECT_NO_THROW(planner().plan(request));
}

} // namespace
