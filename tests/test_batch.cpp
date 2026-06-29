#include "portable.hpp"
#include <gtest/gtest.h>
#include "batch.hpp"
#include "preset.hpp"
#include "torrent_inspector.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class BatchTest : public ::testing::Test {
protected:
    fs::path temp_dir;

    void SetUp() override {
        temp_dir = fs::temp_directory_path() / ("batch_test_" + std::to_string(portable_getpid()));
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

TEST_F(BatchTest, ParseBasicBatchConfig) {
    write_file("batch.yaml", R"(
version: 1
workers: 2
jobs:
  - path: "/tmp/nonexistent1"
    output: "test1.torrent"
  - path: "/tmp/nonexistent2"
    output: "test2.torrent"
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    EXPECT_EQ(config.workers, 2);
    EXPECT_EQ(config.jobs.size(), 2u);
    EXPECT_EQ(config.jobs[0].path, "/tmp/nonexistent1");
    EXPECT_EQ(config.jobs[1].path, "/tmp/nonexistent2");
}

TEST_F(BatchTest, ParseDefaultWorkers) {
    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: "/tmp/test"
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    EXPECT_EQ(config.workers, 1);
    EXPECT_EQ(config.jobs.size(), 1u);
}

TEST_F(BatchTest, ParseWithPresetFile) {
    write_file("batch.yaml", R"(
version: 1
preset_file: "/path/to/presets.yaml"
jobs:
  - path: "/tmp/test"
    preset: ptp
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    EXPECT_TRUE(config.preset_file.has_value());
    EXPECT_EQ(*config.preset_file, "/path/to/presets.yaml");
    EXPECT_EQ(config.jobs[0].preset.value(), "ptp");
}

TEST_F(BatchTest, ParseWithOutputDir) {
    write_file("batch.yaml", R"(
version: 1
output_dir: "/tmp/output"
jobs:
  - path: "/tmp/test"
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    EXPECT_TRUE(config.output_dir.has_value());
    EXPECT_EQ(*config.output_dir, "/tmp/output");
}

TEST_F(BatchTest, ParseJobWithAllFields) {
    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: "/tmp/test"
    output: "test.torrent"
    trackers:
      - "https://tracker.example/announce"
    web_seeds:
      - "https://ws.example/file"
    private: true
    source: "SRC"
    piece_size: 1024
    target_piece_count: 600
    comment: "Test"
    creator: "TestCreator"
    name: "custom"
    creation_date: true
    torrent_version: 2
    entropy: true
    exclude_patterns: ["*.nfo"]
    include_patterns: ["*.mkv"]
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    const auto& job = config.jobs[0];
    const auto& cv = job.values;

    ASSERT_TRUE(cv.trackers.has_value());
    EXPECT_EQ(cv.trackers->size(), 1u);
    ASSERT_TRUE(cv.web_seeds.has_value());
    EXPECT_EQ(cv.web_seeds->size(), 1u);
    EXPECT_TRUE(cv.is_private.has_value() && *cv.is_private);
    EXPECT_EQ(*cv.source, "SRC");
    EXPECT_EQ(*cv.piece_size, 1024);
    ASSERT_TRUE(cv.target_piece_count.has_value());
    EXPECT_EQ(*cv.target_piece_count, 600);
    EXPECT_EQ(*cv.comment, "Test");
    EXPECT_EQ(*cv.creator, "TestCreator");
    EXPECT_EQ(*cv.name, "custom");
    EXPECT_TRUE(*cv.creation_date);
    EXPECT_EQ(*cv.torrent_version, 2);
    EXPECT_TRUE(*cv.entropy);
    ASSERT_TRUE(cv.exclude_patterns.has_value());
    EXPECT_EQ(cv.exclude_patterns->size(), 1u);
    ASSERT_TRUE(cv.include_patterns.has_value());
    EXPECT_EQ(cv.include_patterns->size(), 1u);
}

TEST_F(BatchTest, ParseOutputWithoutTorrentExtensionAutoAppends) {
    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: "/tmp/test"
    output: "my_output"
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    ASSERT_EQ(config.jobs.size(), 1u);
    ASSERT_TRUE(config.jobs[0].output.has_value());
    EXPECT_EQ(*config.jobs[0].output, "my_output.torrent");
}

TEST_F(BatchTest, ParseOutputWithTorrentExtensionPreserved) {
    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: "/tmp/test"
    output: "my_output.torrent"
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    ASSERT_EQ(config.jobs.size(), 1u);
    ASSERT_TRUE(config.jobs[0].output.has_value());
    EXPECT_EQ(*config.jobs[0].output, "my_output.torrent");
}

TEST_F(BatchTest, ParseOutputWithUppercaseTorrentExtensionPreserved) {
    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: "/tmp/test"
    output: "my_output.TORRENT"
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    ASSERT_EQ(config.jobs.size(), 1u);
    ASSERT_TRUE(config.jobs[0].output.has_value());
    EXPECT_EQ(*config.jobs[0].output, "my_output.TORRENT");
}

TEST_F(BatchTest, ParseFileTooLargeThrows) {
    write_file("batch.yaml", std::string(1024 * 1024 + 100, ' '));
    EXPECT_THROW(BatchProcessor::parse(temp_dir / "batch.yaml"), std::runtime_error);
}

TEST_F(BatchTest, ParseMissingVersionThrows) {
    write_file("batch.yaml", R"(
jobs:
  - path: "/tmp/test"
)");

    EXPECT_THROW(BatchProcessor::parse(temp_dir / "batch.yaml"), std::runtime_error);
}

TEST_F(BatchTest, ParseInvalidVersionThrows) {
    write_file("batch.yaml", R"(
version: 2
jobs:
  - path: "/tmp/test"
)");

    EXPECT_THROW(BatchProcessor::parse(temp_dir / "batch.yaml"), std::runtime_error);
}

TEST_F(BatchTest, ParseMissingJobsThrows) {
    write_file("batch.yaml", R"(
version: 1
)");

    EXPECT_THROW(BatchProcessor::parse(temp_dir / "batch.yaml"), std::runtime_error);
}

TEST_F(BatchTest, ParseMissingPathThrows) {
    write_file("batch.yaml", R"(
version: 1
jobs:
  - output: "test.torrent"
)");

    EXPECT_THROW(BatchProcessor::parse(temp_dir / "batch.yaml"), std::runtime_error);
}

TEST_F(BatchTest, ParseInvalidWorkersThrows) {
    write_file("batch.yaml", R"(
version: 1
workers: 0
jobs:
  - path: "/tmp/test"
)");

    EXPECT_THROW(BatchProcessor::parse(temp_dir / "batch.yaml"), std::runtime_error);
}

TEST_F(BatchTest, ParseFileNotFoundThrows) {
    EXPECT_THROW(
        BatchProcessor::parse(temp_dir / "nonexistent.yaml"),
        std::runtime_error);
}

TEST_F(BatchTest, PrintSummaryDoesNotCrash) {
    std::vector<BatchResult> results;
    results.push_back({0, "test1.torrent", true, "", 1.5});
    results.push_back({1, "test2.torrent", false, "File not found", 0.1});

    EXPECT_NO_THROW(BatchProcessor::print_summary(results));
}

TEST_F(BatchTest, RunSingleJobSuccess) {
    fs::path test_file = temp_dir / "testfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "output.torrent").generic_string() + R"("
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
}

TEST_F(BatchTest, ParseNoCreatorNoDate) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / ("batch_no_creator_date_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    {
        std::ofstream f(temp_dir / "batch.yaml");
        f << R"(version: 1
jobs:
  - path: "/data/movie.mkv"
    no_creator: true
    no_date: true
)";
    }

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    ASSERT_EQ(config.jobs.size(), 1u);

    const auto& cv = config.jobs[0].values;
    ASSERT_TRUE(cv.no_creator.has_value());
    EXPECT_TRUE(*cv.no_creator);
    ASSERT_TRUE(cv.no_date.has_value());
    EXPECT_TRUE(*cv.no_date);

    fs::remove_all(temp_dir);
}

TEST_F(BatchTest, RunWithSeasonWarningDisabledOnIncompletePackSucceeds) {
    fs::path season_dir = temp_dir / "Show.Name.S01";
    fs::create_directories(season_dir);
    {
        std::ofstream f(season_dir / "Show.Name.S01E01.mkv", std::ios::binary);
        std::vector<char> data(1024, 'A');
        f.write(data.data(), data.size());
    }
    {
        std::ofstream f(season_dir / "Show.Name.S01E03.mkv", std::ios::binary);
        std::vector<char> data(1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: ")" + season_dir.generic_string() + R"("
    output: ")" + (temp_dir / "season.torrent").generic_string() + R"("
    fail_on_season_warning: false
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
}

TEST_F(BatchTest, RunFailedJobDoesNotCrash) {
    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: "/tmp/this_file_does_not_exist_12345"
    output: "/tmp/should_not_create.torrent"
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].success);
    EXPECT_FALSE(results[0].error_message.empty());
}

TEST_F(BatchTest, RunWithRulesAutoPieceSizeAdjustment) {
    fs::path test_file = temp_dir / "testfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(32 * 1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("rules.yaml", R"(
version: 1
trackers:
  ptp:
    domain: "passthepopcorn.me"
    source: "PTP"
    piece_length_overrides:
      - size_below: 1073741824
        piece_length: 32
)");

    write_file("batch.yaml", R"(
version: 1
rules_file: ")" + (temp_dir / "rules.yaml").generic_string() + R"("
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "auto_adjusted.torrent").generic_string() + R"("
    trackers:
      - "https://passthepopcorn.me/announce"
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
    EXPECT_TRUE(fs::exists(temp_dir / "auto_adjusted.torrent"));

    TorrentInspector inspector((temp_dir / "auto_adjusted.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_EQ(meta.piece_length, 32 * 1024) << "Rule override should adjust piece size to 32 KB";
    ASSERT_TRUE(meta.source.has_value());
    EXPECT_EQ(*meta.source, "PTP") << "Rule should auto-set source";
}

TEST_F(BatchTest, ParseWithRulesFile) {
    write_file("rules.yaml", R"(version: 1
trackers: {}
)");

    write_file("batch.yaml", R"(
version: 1
rules_file: ")" + (temp_dir / "rules.yaml").generic_string() + R"("
jobs:
  - path: "/tmp/test"
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    EXPECT_TRUE(config.rules_file.has_value());
    EXPECT_EQ(*config.rules_file, (temp_dir / "rules.yaml").string());
}

TEST_F(BatchTest, RunWithRulesFileAutoSetSource) {
    fs::path test_file = temp_dir / "testfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("rules.yaml", R"(
version: 1
trackers:
  ptp:
    domain: "passthepopcorn.me"
    source: "PTP"
)");

    write_file("batch.yaml", R"(
version: 1
rules_file: ")" + (temp_dir / "rules.yaml").generic_string() + R"("
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "rules_out.torrent").generic_string() + R"("
    trackers:
      - "https://passthepopcorn.me/announce"
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
    EXPECT_TRUE(fs::exists(temp_dir / "rules_out.torrent"));
}

TEST_F(BatchTest, RunWithRulesAndExplicitPieceSizePreserved) {
    fs::path test_file = temp_dir / "testfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(32 * 1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("rules.yaml", R"(
version: 1
trackers:
  ptp:
    domain: "passthepopcorn.me"
    source: "PTP"
    max_piece_length: 8388608
)");

    write_file("batch.yaml", R"(
version: 1
rules_file: ")" + (temp_dir / "rules.yaml").generic_string() + R"("
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "explicit_ps.torrent").generic_string() + R"("
    trackers:
      - "https://passthepopcorn.me/announce"
    piece_size: 16
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
    EXPECT_TRUE(fs::exists(temp_dir / "explicit_ps.torrent"));

    TorrentInspector inspector((temp_dir / "explicit_ps.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_EQ(meta.piece_length, 16 * 1024) << "User-specified piece_size (16 KB) should be preserved";
}

TEST_F(BatchTest, RunWithRulesConstraintViolation) {
    fs::path test_file = temp_dir / "testfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(32 * 1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("rules.yaml", R"(
version: 1
trackers:
  tiny:
    domain: "tiny.tracker.example"
    source: "TINY"
    max_piece_length: 8192
)");

    write_file("batch.yaml", R"(
version: 1
rules_file: ")" + (temp_dir / "rules.yaml").generic_string() + R"("
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "constrained.torrent").generic_string() + R"("
    trackers:
      - "https://tiny.tracker.example/announce"
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
}

TEST_F(BatchTest, PrintSummaryWithControlCharactersDoesNotCrash) {
    std::vector<BatchResult> results;
    results.push_back({0, "test\x01\x02\x03.torrent", true, "", 1.5});
    results.push_back({1, "bad\x1b[31m.torrent", false, "error\x00\x7fmsg", 0.1});

    EXPECT_NO_THROW(BatchProcessor::print_summary(results));
}

TEST_F(BatchTest, RunWithExcludePatternsEndToEnd) {
    fs::path content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "movie.mkv") << "video content"; }
    { std::ofstream(content_dir / "info.nfo") << "nfo data"; }
    { std::ofstream(content_dir / "readme.txt") << "text data"; }

    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: ")" + content_dir.generic_string() + R"("
    output: ")" + (temp_dir / "filtered.torrent").generic_string() + R"("
    torrent_version: 1
    exclude_patterns: ["*.nfo", "*.txt"]
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
    EXPECT_TRUE(fs::exists(temp_dir / "filtered.torrent"));

    TorrentInspector inspector((temp_dir / "filtered.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_EQ(meta.files.size(), 1u) << "Should only have movie.mkv (nfo and txt excluded)";
    EXPECT_NE(meta.files[0].path.find("movie.mkv"), std::string::npos);
}

#ifndef _WIN32
TEST_F(BatchTest, ParseInaccessibleFileThrows) {
    auto sub_dir = temp_dir / "restricted";
    fs::create_directories(sub_dir);
    write_file("restricted/batch.yaml", R"(
version: 1
jobs:
  - path: "/tmp/test"
)");

    fs::permissions(sub_dir, fs::perms::none);

    EXPECT_THROW(
        BatchProcessor::parse(sub_dir / "batch.yaml"),
        std::filesystem::filesystem_error);

    fs::permissions(sub_dir, fs::perms::all);
}
#endif

TEST_F(BatchTest, RunMultipleJobsMixedResults) {
    fs::path test_file = temp_dir / "goodfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'B');
        f.write(data.data(), data.size());
    }

    write_file("batch.yaml", R"(
version: 1
workers: 2
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "good.torrent").generic_string() + R"("
  - path: "/tmp/nonexistent_file_xyz"
    output: "/tmp/bad.torrent"
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "good2.torrent").generic_string() + R"("
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 3u);
    EXPECT_TRUE(results[0].success);
    EXPECT_FALSE(results[1].success);
    EXPECT_TRUE(results[2].success);

    EXPECT_TRUE(fs::exists(temp_dir / "good.torrent"));
    EXPECT_TRUE(fs::exists(temp_dir / "good2.torrent"));
}

TEST_F(BatchTest, RunAutoOutputPath) {
    fs::path test_file = temp_dir / "auto_output.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(2048, 'C');
        f.write(data.data(), data.size());
    }

    write_file("batch.yaml", R"(
version: 1
output_dir: ")" + temp_dir.generic_string() + R"("
jobs:
  - path: ")" + test_file.generic_string() + R"("
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
    EXPECT_TRUE(results[0].error_message.empty());
}

TEST_F(BatchTest, RunWithPresetEndToEnd) {
    fs::path test_file = temp_dir / "preset_test.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'D');
        f.write(data.data(), data.size());
    }

    write_file("presets.yaml", R"(
version: 1
default:
  private: true
presets:
  custom:
    comment: "from preset"
    source: "PRESET_SRC"
)");

    write_file("batch.yaml", R"(
version: 1
preset_file: ")" + (temp_dir / "presets.yaml").generic_string() + R"("
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "preset_out.torrent").generic_string() + R"("
    preset: custom
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
    EXPECT_TRUE(fs::exists(temp_dir / "preset_out.torrent"));
}

TEST_F(BatchTest, RunWithUnknownPresetFails) {
    fs::path test_file = temp_dir / "testfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("presets.yaml", R"(
version: 1
presets:
  ptp:
    source: "PTP"
)");

    write_file("batch.yaml", R"(
version: 1
preset_file: ")" + (temp_dir / "presets.yaml").generic_string() + R"("
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "out.torrent").generic_string() + R"("
    preset: nonexistent_preset
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].success);
    EXPECT_NE(results[0].error_message.find("Unknown preset"), std::string::npos);
}

TEST_F(BatchTest, RunWithoutPresetFileContinues) {
    fs::path test_file = temp_dir / "testfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "out.torrent").generic_string() + R"("
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
    EXPECT_TRUE(fs::exists(temp_dir / "out.torrent"));
}

TEST_F(BatchTest, RunWithInvalidPresetPieceSizeFails) {
    fs::path test_file = temp_dir / "testfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("presets.yaml", R"(
version: 1
presets:
  bad_piecesize:
    piece_size: 999
)");

    write_file("batch.yaml", R"(
version: 1
preset_file: ")" + (temp_dir / "presets.yaml").generic_string() + R"("
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "out.torrent").generic_string() + R"("
    preset: bad_piecesize
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].success);
    EXPECT_NE(results[0].error_message.find("Invalid piece size"), std::string::npos);
}

TEST_F(BatchTest, WorkersCappedWhenExceedingJobCount) {
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }

    write_file("batch.yaml", R"(
version: 1
workers: 100
jobs:
  - path: ")" + input_file.generic_string() + R"("
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
}

TEST_F(BatchTest, RunWithNoCreatorNoDateProducesCleanTorrent) {
    namespace fs = std::filesystem;
    fs::path temp_dir = fs::temp_directory_path() / ("batch_no_cd_e2e_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "movie.mkv") << "video data"; }

    auto output_file = temp_dir / "clean.torrent";

    {
        std::ofstream f(temp_dir / "batch.yaml");
        f << "version: 1\n"
          << "jobs:\n"
          << "  - path: \"" << content_dir.string() << "\"\n"
          << "    output: \"" << output_file.string() << "\"\n"
          << "    no_creator: true\n"
          << "    no_date: true\n";
    }

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success) << "Job should succeed";

    ASSERT_TRUE(fs::exists(output_file)) << "Torrent file should be created";

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_FALSE(meta.created_by.has_value()) << "Creator should be absent with batch no_creator: true";
    EXPECT_FALSE(meta.creation_date.has_value()) << "Creation date should be absent with batch no_date: true";

    fs::remove_all(temp_dir);
}

TEST_F(BatchTest, RunWithDefaultCreatorAndDatePresent) {
    namespace fs = std::filesystem;
    fs::path temp_dir = fs::temp_directory_path() / ("batch_default_cd_e2e_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "movie.mkv") << "video data"; }

    auto output_file = temp_dir / "default.torrent";

    {
        std::ofstream f(temp_dir / "batch.yaml");
        f << "version: 1\n"
          << "jobs:\n"
          << "  - path: \"" << content_dir.string() << "\"\n"
          << "    output: \"" << output_file.string() << "\"\n";
    }

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success) << "Job should succeed";

    ASSERT_TRUE(fs::exists(output_file)) << "Torrent file should be created";

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_TRUE(meta.created_by.has_value()) << "Creator should be present by default in batch";
    EXPECT_EQ(*meta.created_by, "Torrent Builder");
    ASSERT_TRUE(meta.creation_date.has_value()) << "Creation date should be present by default in batch";

    fs::remove_all(temp_dir);
}

TEST_F(BatchTest, RunWithOutputDirAutoCreate) {
    fs::path test_file = temp_dir / "testfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'A');
        f.write(data.data(), data.size());
    }

    fs::path nested_output = temp_dir / "nested" / "output";

    write_file("batch.yaml", R"(
version: 1
output_dir: ")" + nested_output.generic_string() + R"("
jobs:
  - path: ")" + test_file.generic_string() + R"("
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
    EXPECT_TRUE(fs::is_directory(nested_output)) << "Output directory should have been auto-created";
}

TEST_F(BatchTest, RunWithHybridVersion) {
    fs::path test_file = temp_dir / "testfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "hybrid.torrent").generic_string() + R"("
    torrent_version: 3
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
}

TEST_F(BatchTest, RunWithV1Version) {
    fs::path test_file = temp_dir / "testfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "v1.torrent").generic_string() + R"("
    torrent_version: 1
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
}

TEST_F(BatchTest, RunWithPieceSize) {
#ifdef __MINGW32__
    GTEST_SKIP() << "Skipped on MinGW due to known SegFault in libtorrent hashing";
#endif
    fs::path test_file = temp_dir / "testfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(32 * 1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "piecesize.torrent").generic_string() + R"("
    piece_size: 16
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
}

TEST_F(BatchTest, RunWithInvalidPieceSizeFails) {
    fs::path test_file = temp_dir / "testfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "out.torrent").generic_string() + R"("
    piece_size: 999
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].success);
    EXPECT_NE(results[0].error_message.find("Invalid piece size"), std::string::npos);
}

TEST_F(BatchTest, RunWithTargetPieceCount) {
#ifdef __MINGW32__
    GTEST_SKIP() << "Skipped on MinGW due to known SegFault in libtorrent hashing";
#endif
    fs::path test_file = temp_dir / "testfile.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(50 * 1024 * 1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "target.torrent").generic_string() + R"("
    target_piece_count: 100
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success) << results[0].error_message;

    TorrentInspector inspector((temp_dir / "target.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    // 50 MB / 100 pieces → 512 KB piece size
    EXPECT_EQ(meta.piece_length, 512 * 1024);
}

TEST_F(BatchTest, RunWithTargetPieceCountAndPieceSizeFails) {
    fs::path test_file = temp_dir / "testfile.bin";
    { std::ofstream f(test_file, std::ios::binary); f << "test content"; }

    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: ")" + test_file.generic_string() + R"("
    output: ")" + (temp_dir / "out.torrent").generic_string() + R"("
    piece_size: 256
    target_piece_count: 100
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].success);
    EXPECT_NE(results[0].error_message.find("mutually exclusive"), std::string::npos);
}

TEST_F(BatchTest, ParseFailOnSeasonWarning) {
    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: "/tmp/test"
    fail_on_season_warning: true
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    ASSERT_EQ(config.jobs.size(), 1u);
    EXPECT_TRUE(config.jobs[0].fail_on_season_warning);
}

TEST_F(BatchTest, ParseFailOnSeasonWarningDefaultFalse) {
    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: "/tmp/test"
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    ASSERT_EQ(config.jobs.size(), 1u);
    EXPECT_FALSE(config.jobs[0].fail_on_season_warning);
}

TEST_F(BatchTest, RunWithSeasonWarningFailsOnIncompletePack) {
    fs::path season_dir = temp_dir / "Show.Name.S01";
    fs::create_directories(season_dir);
    {
        std::ofstream f(season_dir / "Show.Name.S01E01.mkv", std::ios::binary);
        std::vector<char> data(1024, 'A');
        f.write(data.data(), data.size());
    }
    {
        std::ofstream f(season_dir / "Show.Name.S01E03.mkv", std::ios::binary);
        std::vector<char> data(1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: ")" + season_dir.generic_string() + R"("
    output: ")" + (temp_dir / "season.torrent").generic_string() + R"("
    fail_on_season_warning: true
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].success);
    EXPECT_NE(results[0].error_message.find("E02"), std::string::npos);
}

TEST_F(BatchTest, RunWithSeasonWarningSucceedsOnCompletePack) {
    fs::path season_dir = temp_dir / "Show.Name.S01";
    fs::create_directories(season_dir);
    for (int i = 1; i <= 3; ++i) {
        auto ep = season_dir / ("Show.Name.S01E0" + std::to_string(i) + ".mkv");
        std::ofstream f(ep, std::ios::binary);
        std::vector<char> data(1024, 'A');
        f.write(data.data(), data.size());
    }

    write_file("batch.yaml", R"(
version: 1
jobs:
  - path: ")" + season_dir.generic_string() + R"("
    output: ")" + (temp_dir / "season.torrent").generic_string() + R"("
    fail_on_season_warning: true
)");

    auto config = BatchProcessor::parse(temp_dir / "batch.yaml");
    BatchProcessor processor(std::move(config));
    auto results = processor.run();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].success);
}
