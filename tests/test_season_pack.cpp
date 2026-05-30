#include <gtest/gtest.h>
#include "season_pack.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(DetectSeasonNumber, S01Pattern)
{
    EXPECT_EQ(season_pack::detect_season_number("Show.Name.S01"), 1);
}

TEST(DetectSeasonNumber, S02Pattern)
{
    EXPECT_EQ(season_pack::detect_season_number("Show.Name.S02"), 2);
}

TEST(DetectSeasonNumber, SeasonDotPattern)
{
    EXPECT_EQ(season_pack::detect_season_number("Show.Name.Season.1."), 1);
}

TEST(DetectSeasonNumber, SeasonSpacePattern)
{
    EXPECT_EQ(season_pack::detect_season_number("/path/Season 1/"), 1);
}

TEST(DetectSeasonNumber, SeasonDirSeparator)
{
    EXPECT_EQ(season_pack::detect_season_number("/media/Season 2/"), 2);
}

TEST(DetectSeasonNumber, SDirSeparator)
{
    EXPECT_EQ(season_pack::detect_season_number("/media/S01/"), 1);
}

TEST(DetectSeasonNumber, S01Complete)
{
    EXPECT_EQ(season_pack::detect_season_number("Show.Name.S01.Complete"), 1);
}

TEST(DetectSeasonNumber, S01CompleteHyphen)
{
    EXPECT_EQ(season_pack::detect_season_number("Show.Name.S01-Complete"), 1);
}

TEST(DetectSeasonNumber, S01CompleteUnderscore)
{
    EXPECT_EQ(season_pack::detect_season_number("Show.Name.S01_Complete"), 1);
}

TEST(DetectSeasonNumber, S01WithQuality)
{
    EXPECT_EQ(season_pack::detect_season_number("Show.Name.S01.720p"), 1);
}

TEST(DetectSeasonNumber, S01WithCOMPLETE)
{
    EXPECT_EQ(season_pack::detect_season_number("Show.Name.S01.COMPLETE"), 1);
}

TEST(DetectSeasonNumber, DashS01Dash)
{
    EXPECT_EQ(season_pack::detect_season_number("Something-S01-rest"), 1);
}

TEST(DetectSeasonNumber, UnderscoreS01Underscore)
{
    EXPECT_EQ(season_pack::detect_season_number("Something_S01_rest"), 1);
}

TEST(DetectSeasonNumber, SeasonAtEnd)
{
    EXPECT_EQ(season_pack::detect_season_number("Season 5"), 5);
}

TEST(DetectSeasonNumber, NoSeason)
{
    EXPECT_EQ(season_pack::detect_season_number("Show.Name"), 0);
}

TEST(DetectSeasonNumber, MovieName)
{
    EXPECT_EQ(season_pack::detect_season_number("The.Matrix.1999"), 0);
}

TEST(DetectSeasonNumber, SeasonDoubleDigit)
{
    EXPECT_EQ(season_pack::detect_season_number("Show.Name.S12"), 12);
}

TEST(ExtractSeasonEpisode, S01E01)
{
    auto [s, e] = season_pack::extract_season_episode("Show.Name.S01E01.mkv");
    EXPECT_EQ(s, 1);
    EXPECT_EQ(e, 1);
}

TEST(ExtractSeasonEpisode, S02E12)
{
    auto [s, e] = season_pack::extract_season_episode("Show.Name.S02E12.mkv");
    EXPECT_EQ(s, 2);
    EXPECT_EQ(e, 12);
}

TEST(ExtractSeasonEpisode, S01E001)
{
    auto [s, e] = season_pack::extract_season_episode("Show.Name.S01E001.mkv");
    EXPECT_EQ(s, 1);
    EXPECT_EQ(e, 1);
}

TEST(ExtractSeasonEpisode, AltPattern1x01)
{
    auto [s, e] = season_pack::extract_season_episode("Show.Name.1x01.mkv");
    EXPECT_EQ(s, 1);
    EXPECT_EQ(e, 1);
}

TEST(ExtractSeasonEpisode, AltPattern2x15)
{
    auto [s, e] = season_pack::extract_season_episode("Show.Name.2x15.mkv");
    EXPECT_EQ(s, 2);
    EXPECT_EQ(e, 15);
}

TEST(ExtractSeasonEpisode, NoEpisode)
{
    auto [s, e] = season_pack::extract_season_episode("Movie.2024.mkv");
    EXPECT_EQ(e, 0);
}

TEST(ExtractSeasonEpisode, Lowercase)
{
    auto [s, e] = season_pack::extract_season_episode("show.name.s01e05.mkv");
    EXPECT_EQ(s, 1);
    EXPECT_EQ(e, 5);
}

TEST(ExtractSeasonEpisode, EpisodeOnlyNoSeason)
{
    auto [s, e] = season_pack::extract_season_episode("E05.mkv");
    EXPECT_EQ(e, 0);
}

TEST(ExtractSeasonEpisode, SeasonOnlyNoEpisode)
{
    auto [s, e] = season_pack::extract_season_episode("Show.S05.mkv");
    EXPECT_EQ(s, 5);
    EXPECT_EQ(e, 0);
}

TEST(ExtractMultiEpisodes, RangeE01ToE03)
{
    auto eps = season_pack::extract_multi_episodes("Show.S01E01-E03.mkv");
    ASSERT_EQ(eps.size(), 3u);
    EXPECT_EQ(eps[0], 1);
    EXPECT_EQ(eps[1], 2);
    EXPECT_EQ(eps[2], 3);
}

TEST(ExtractMultiEpisodes, RangeE05ToE08)
{
    auto eps = season_pack::extract_multi_episodes("Show.S01E05-E08.mkv");
    ASSERT_EQ(eps.size(), 4u);
    EXPECT_EQ(eps[0], 5);
    EXPECT_EQ(eps[1], 6);
    EXPECT_EQ(eps[2], 7);
    EXPECT_EQ(eps[3], 8);
}

TEST(ExtractMultiEpisodes, RangeExplicitE)
{
    auto eps = season_pack::extract_multi_episodes("Show.S01E01E03.mkv");
    ASSERT_EQ(eps.size(), 3u);
    EXPECT_EQ(eps[0], 1);
    EXPECT_EQ(eps[1], 2);
    EXPECT_EQ(eps[2], 3);
}

TEST(ExtractMultiEpisodes, NoRange)
{
    auto eps = season_pack::extract_multi_episodes("Show.S01E01.mkv");
    EXPECT_TRUE(eps.empty());
}

TEST(ExtractMultiEpisodes, SingleEpisode)
{
    auto eps = season_pack::extract_multi_episodes("Show.S01E05-E05.mkv");
    ASSERT_EQ(eps.size(), 1u);
    EXPECT_EQ(eps[0], 5);
}

TEST(IsVideoFile, Mkv) { EXPECT_TRUE(season_pack::is_video_file("video.mkv")); }
TEST(IsVideoFile, Mp4) { EXPECT_TRUE(season_pack::is_video_file("video.mp4")); }
TEST(IsVideoFile, Avi) { EXPECT_TRUE(season_pack::is_video_file("video.avi")); }
TEST(IsVideoFile, Ts) { EXPECT_TRUE(season_pack::is_video_file("video.ts")); }
TEST(IsVideoFile, M2ts) { EXPECT_TRUE(season_pack::is_video_file("video.m2ts")); }
TEST(IsVideoFile, Webm) { EXPECT_TRUE(season_pack::is_video_file("video.webm")); }
TEST(IsVideoFile, Flv) { EXPECT_TRUE(season_pack::is_video_file("video.flv")); }
TEST(IsVideoFile, Wmv) { EXPECT_TRUE(season_pack::is_video_file("video.wmv")); }
TEST(IsVideoFile, Mov) { EXPECT_TRUE(season_pack::is_video_file("video.mov")); }
TEST(IsVideoFile, M4v) { EXPECT_TRUE(season_pack::is_video_file("video.m4v")); }
TEST(IsVideoFile, Mpg) { EXPECT_TRUE(season_pack::is_video_file("video.mpg")); }
TEST(IsVideoFile, Mpeg) { EXPECT_TRUE(season_pack::is_video_file("video.mpeg")); }
TEST(IsVideoFile, Ogv) { EXPECT_TRUE(season_pack::is_video_file("video.ogv")); }

TEST(IsVideoFile, UpperCaseExt) { EXPECT_TRUE(season_pack::is_video_file("video.MKV")); }
TEST(IsVideoFile, MixedCaseExt) { EXPECT_TRUE(season_pack::is_video_file("video.Mkv")); }

TEST(IsVideoFile, Nfo) { EXPECT_FALSE(season_pack::is_video_file("info.nfo")); }
TEST(IsVideoFile, Txt) { EXPECT_FALSE(season_pack::is_video_file("readme.txt")); }
TEST(IsVideoFile, Srt) { EXPECT_FALSE(season_pack::is_video_file("subs.srt")); }
TEST(IsVideoFile, Mp3) { EXPECT_FALSE(season_pack::is_video_file("audio.mp3")); }
TEST(IsVideoFile, NoExtension) { EXPECT_FALSE(season_pack::is_video_file("video")); }

TEST(Analyze, SingleFileNotSeasonPack)
{
    auto temp_dir = fs::temp_directory_path() / "tb_season_single";
    fs::create_directories(temp_dir);
    auto f = temp_dir / "Movie.2024.mkv";
    { std::ofstream(f) << "data"; }

    SeasonPackInfo info = season_pack::analyze(f);
    EXPECT_FALSE(info.is_season_pack);

    fs::remove_all(temp_dir);
}

TEST(Analyze, CompleteSeasonPack)
{
    auto temp_dir = fs::temp_directory_path() / "tb_season_complete";
    fs::create_directories(temp_dir);
    for (int i = 1; i <= 10; ++i)
    {
        std::string ep = (i < 10 ? "0" : "") + std::to_string(i);
        std::ofstream(temp_dir / ("Show.S01E" + ep + ".mkv")) << "data";
    }

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_TRUE(info.is_season_pack);
    EXPECT_EQ(info.season, 1);
    EXPECT_EQ(info.episodes.size(), 10u);
    EXPECT_EQ(info.max_episode, 10);
    EXPECT_EQ(info.video_file_count, 10);
    EXPECT_TRUE(info.missing_episodes.empty());
    EXPECT_FALSE(info.is_suspicious);

    fs::remove_all(temp_dir);
}

TEST(Analyze, IncompleteSeasonPack)
{
    auto temp_dir = fs::temp_directory_path() / "tb_season_incomplete";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "Show.S01E01.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E02.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E05.mkv") << "data";

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_TRUE(info.is_season_pack);
    EXPECT_EQ(info.season, 1);
    EXPECT_EQ(info.episodes.size(), 3u);
    EXPECT_EQ(info.max_episode, 5);
    ASSERT_EQ(info.missing_episodes.size(), 2u);
    EXPECT_EQ(info.missing_episodes[0], 3);
    EXPECT_EQ(info.missing_episodes[1], 4);
    EXPECT_TRUE(info.is_suspicious);

    fs::remove_all(temp_dir);
}

TEST(Analyze, OnlyOneEpisodeNotPack)
{
    auto temp_dir = fs::temp_directory_path() / "tb_season_one_ep";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "Show.S01E01.mkv") << "data";

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_FALSE(info.is_season_pack);

    fs::remove_all(temp_dir);
}

TEST(Analyze, NonTVContent)
{
    auto temp_dir = fs::temp_directory_path() / "tb_season_not_tv";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "track01.mp3") << "data";
    std::ofstream(temp_dir / "track02.mp3") << "data";
    std::ofstream(temp_dir / "track03.mp3") << "data";

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_FALSE(info.is_season_pack);

    fs::remove_all(temp_dir);
}

TEST(Analyze, MixedFilesOnlyVideoScanned)
{
    auto temp_dir = fs::temp_directory_path() / "tb_season_mixed";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "Show.S01E01.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E02.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E03.nfo") << "data";
    std::ofstream(temp_dir / "readme.txt") << "data";

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_TRUE(info.is_season_pack);
    EXPECT_EQ(info.video_file_count, 2);
    EXPECT_EQ(info.episodes.size(), 2u);

    fs::remove_all(temp_dir);
}

TEST(Analyze, SubdirectoriesScanned)
{
    auto temp_dir = fs::temp_directory_path() / "tb_season_subdirs";
    auto sub = temp_dir / "Subs";
    fs::create_directories(sub);
    std::ofstream(temp_dir / "Show.S01E01.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E02.mkv") << "data";
    std::ofstream(sub / "srt1.srt") << "data";

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_TRUE(info.is_season_pack);
    EXPECT_EQ(info.video_file_count, 2);

    fs::remove_all(temp_dir);
}

TEST(Analyze, MultiEpisodeFile)
{
    auto temp_dir = fs::temp_directory_path() / "tb_season_multi_ep";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "Show.S01E01-E03.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E04.mkv") << "data";

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_TRUE(info.is_season_pack);
    EXPECT_EQ(info.episodes.size(), 4u);
    EXPECT_EQ(info.max_episode, 4);
    EXPECT_TRUE(info.missing_episodes.empty());
    EXPECT_FALSE(info.is_suspicious);

    fs::remove_all(temp_dir);
}

TEST(Analyze, SeasonFromDirName)
{
    auto temp_dir = fs::temp_directory_path() / "Show.Name.S01";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "Episode.S01E01.mkv") << "data";
    std::ofstream(temp_dir / "Episode.S01E02.mkv") << "data";

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_TRUE(info.is_season_pack);
    EXPECT_EQ(info.season, 1);

    fs::remove_all(temp_dir);
}

TEST(Analyze, SeasonFromDirNameSeasonFormat)
{
    auto temp_dir = fs::temp_directory_path() / "Show.Name.Season.1";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "Show.S01E01.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E02.mkv") << "data";

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_TRUE(info.is_season_pack);
    EXPECT_EQ(info.season, 1);

    fs::remove_all(temp_dir);
}

TEST(Analyze, AltPattern1x)
{
    auto temp_dir = fs::temp_directory_path() / "tb_season_alt";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "Show.1x01.mkv") << "data";
    std::ofstream(temp_dir / "Show.1x02.mkv") << "data";
    std::ofstream(temp_dir / "Show.1x03.mkv") << "data";

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_TRUE(info.is_season_pack);
    EXPECT_EQ(info.season, 1);
    EXPECT_EQ(info.episodes.size(), 3u);

    fs::remove_all(temp_dir);
}

TEST(Analyze, EmptyDirectory)
{
    auto temp_dir = fs::temp_directory_path() / "tb_season_empty";
    fs::create_directories(temp_dir);

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_FALSE(info.is_season_pack);

    fs::remove_all(temp_dir);
}

TEST(Analyze, NonExistentPath)
{
    SeasonPackInfo info = season_pack::analyze("/nonexistent/path/that/does/not/exist");
    EXPECT_FALSE(info.is_season_pack);
}

TEST(Analyze, SpecialEpisodeE00)
{
    auto temp_dir = fs::temp_directory_path() / "tb_season_special";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "Show.S01E00.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E01.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E02.mkv") << "data";

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_TRUE(info.is_season_pack);
    EXPECT_EQ(info.max_episode, 2);
    EXPECT_TRUE(info.missing_episodes.empty());
    EXPECT_EQ(info.episodes.size(), 2u);

    fs::remove_all(temp_dir);
}

TEST(Analyze, SampleFileDoesNotInflateEpisodeCount)
{
    auto temp_dir = fs::temp_directory_path() / "tb_season_sample";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "Show.S01E01.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E03.mkv") << "data";
    std::ofstream(temp_dir / "sample.mkv") << "data";

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_TRUE(info.is_season_pack);
    EXPECT_TRUE(info.is_suspicious);
    EXPECT_EQ(info.episodes.size(), 2u);
    ASSERT_EQ(info.missing_episodes.size(), 1u);
    EXPECT_EQ(info.missing_episodes[0], 2);

    fs::remove_all(temp_dir);
}

TEST(Analyze, HighEpisodeNumbers)
{
    auto temp_dir = fs::temp_directory_path() / "tb_season_high_ep";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "Show.S01E100.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E101.mkv") << "data";

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_TRUE(info.is_season_pack);
    EXPECT_EQ(info.max_episode, 101);
    EXPECT_EQ(info.episodes.size(), 2u);

    fs::remove_all(temp_dir);
}

TEST(Analyze, MovieDirectoryNotSeason)
{
    auto temp_dir = fs::temp_directory_path() / "The.Matrix.1999";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "The.Matrix.1999.mkv") << "data";

    SeasonPackInfo info = season_pack::analyze(temp_dir);
    EXPECT_FALSE(info.is_season_pack);

    fs::remove_all(temp_dir);
}

TEST(Analyze, ExistingFileNotDirectory)
{
    auto temp_dir = fs::temp_directory_path() / "tb_season_file_guard";
    fs::create_directories(temp_dir);
    auto video_file = temp_dir / "Show.S01E01.mkv";
    { std::ofstream(video_file) << "data"; }

    SeasonPackInfo info = season_pack::analyze(video_file);
    EXPECT_FALSE(info.is_season_pack);
    EXPECT_EQ(info.season, 0);
    EXPECT_EQ(info.video_file_count, 0);

    fs::remove_all(temp_dir);
}

TEST(ExtractMultiEpisodes, S01E01E02E03Pattern)
{
    auto eps = season_pack::extract_multi_episodes("Show.Name.S01E01E03.mkv");
    ASSERT_EQ(eps.size(), 3u);
    EXPECT_EQ(eps[0], 1);
    EXPECT_EQ(eps[1], 2);
    EXPECT_EQ(eps[2], 3);
}

TEST(ExtractMultiEpisodes, ReversedRangeReturnsEmpty)
{
    auto eps = season_pack::extract_multi_episodes("Show.S01E05-E01.mkv");
    EXPECT_TRUE(eps.empty());
}

TEST(ExtractMultiEpisodes, HugeRangeReturnsEmpty)
{
    auto eps = season_pack::extract_multi_episodes("Show.S01E01-E200.mkv");
    EXPECT_TRUE(eps.empty());
}

TEST(FormatMissingEpisodes, SingleEpisode)
{
    std::vector<int> missing = {3};
    EXPECT_EQ(season_pack::format_missing_episodes(missing), "E03");
}

TEST(FormatMissingEpisodes, MultipleEpisodes)
{
    std::vector<int> missing = {3, 5, 9};
    EXPECT_EQ(season_pack::format_missing_episodes(missing), "E03, E05, E09");
}

TEST(FormatMissingEpisodes, SingleDigit)
{
    std::vector<int> missing = {1};
    EXPECT_EQ(season_pack::format_missing_episodes(missing), "E01");
}

TEST(FormatMissingEpisodes, Empty)
{
    std::vector<int> missing;
    EXPECT_EQ(season_pack::format_missing_episodes(missing), "");
}

TEST(FormatMissingEpisodes, DoubleDigit)
{
    std::vector<int> missing = {10, 12};
    EXPECT_EQ(season_pack::format_missing_episodes(missing), "E10, E12");
}

TEST(EvaluateSeasonWarning, DisabledReturnsNullopt)
{
    auto temp_dir = fs::temp_directory_path() / "tb_eval_disabled";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "Show.S01E01.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E02.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E05.mkv") << "data";

    auto result = season_pack::evaluate_season_warning(temp_dir, false);
    EXPECT_EQ(result, std::nullopt);

    fs::remove_all(temp_dir);
}

TEST(EvaluateSeasonWarning, CompletePackReturnsNullopt)
{
    auto temp_dir = fs::temp_directory_path() / "tb_eval_complete";
    fs::create_directories(temp_dir);
    for (int i = 1; i <= 5; ++i)
    {
        std::string ep = (i < 10 ? "0" : "") + std::to_string(i);
        std::ofstream(temp_dir / ("Show.S01E" + ep + ".mkv")) << "data";
    }

    auto result = season_pack::evaluate_season_warning(temp_dir, true);
    EXPECT_EQ(result, std::nullopt);

    fs::remove_all(temp_dir);
}

TEST(EvaluateSeasonWarning, SuspiciousPackReturnsError)
{
    auto temp_dir = fs::temp_directory_path() / "tb_eval_suspicious";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "Show.S01E01.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E05.mkv") << "data";

    auto result = season_pack::evaluate_season_warning(temp_dir, true);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->find("Season 1"), std::string::npos);
    EXPECT_NE(result->find(temp_dir.filename().string()), std::string::npos);
    EXPECT_NE(result->find("E03"), std::string::npos);
    EXPECT_NE(result->find("E04"), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(EvaluateSeasonWarning, NonDirectoryReturnsNullopt)
{
    auto temp_dir = fs::temp_directory_path() / "tb_eval_nondir";
    fs::create_directories(temp_dir);
    auto video_file = temp_dir / "Show.S01E01.mkv";
    { std::ofstream(video_file) << "data"; }

    auto result = season_pack::evaluate_season_warning(video_file, true);
    EXPECT_EQ(result, std::nullopt);

    fs::remove_all(temp_dir);
}

TEST(EvaluateSeasonWarning, NonSeasonDirectoryReturnsNullopt)
{
    auto temp_dir = fs::temp_directory_path() / "tb_eval_nonseason_dir";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "random_video.mkv") << "data";
    std::ofstream(temp_dir / "another_clip.mp4") << "data";

    auto result = season_pack::evaluate_season_warning(temp_dir, true);
    EXPECT_EQ(result, std::nullopt);

    fs::remove_all(temp_dir);
}

TEST(EvaluateSeasonWarning, JobIndexInLog)
{
    auto temp_dir = fs::temp_directory_path() / "tb_eval_jobidx";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "Show.S01E01.mkv") << "data";
    std::ofstream(temp_dir / "Show.S01E05.mkv") << "data";

    auto result = season_pack::evaluate_season_warning(temp_dir, true, 2);
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->find("Season 1"), std::string::npos);
    EXPECT_NE(result->find(temp_dir.filename().string()), std::string::npos);
    EXPECT_EQ(result->find("Job 3:"), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(ExtractSeasonEpisode, ResolutionStringNotMatched)
{
    auto [s, e] = season_pack::extract_season_episode("Show.Name.1920x1080.mkv");
    EXPECT_EQ(s, 0);
    EXPECT_EQ(e, 0);
}

TEST(ExtractSeasonEpisode, AltPatternWithSeparators)
{
    auto [s, e] = season_pack::extract_season_episode("Show-3x05-720p.mkv");
    EXPECT_EQ(s, 3);
    EXPECT_EQ(e, 5);
}

TEST(ExtractSeasonEpisode, Resolution3840x2160NotMatched)
{
    auto [s, e] = season_pack::extract_season_episode("Movie.3840x2160.UHD.mkv");
    EXPECT_EQ(s, 0);
    EXPECT_EQ(e, 0);
}
