#include <gtest/gtest.h>
#include "utils.hpp"
#include "constants.hpp"
#include <fstream>

TEST(AutoPieceSize, ZeroBytes) {
    EXPECT_EQ(utils::auto_piece_size(0), PieceSizes::k16KB);
}

TEST(AutoPieceSize, SmallFile) {
    EXPECT_EQ(utils::auto_piece_size(1024), PieceSizes::k16KB);
}

TEST(AutoPieceSize, Boundary64MBMinus1) {
    EXPECT_EQ(utils::auto_piece_size(64LL * 1024 * 1024 - 1), PieceSizes::k16KB);
}

TEST(AutoPieceSize, Boundary64MB) {
    EXPECT_EQ(utils::auto_piece_size(64LL * 1024 * 1024), PieceSizes::k32KB);
}

TEST(AutoPieceSize, Boundary128MB) {
    EXPECT_EQ(utils::auto_piece_size(128LL * 1024 * 1024), PieceSizes::k64KB);
}

TEST(AutoPieceSize, Boundary256MB) {
    EXPECT_EQ(utils::auto_piece_size(256LL * 1024 * 1024), PieceSizes::k128KB);
}

TEST(AutoPieceSize, Boundary512MB) {
    EXPECT_EQ(utils::auto_piece_size(512LL * 1024 * 1024), PieceSizes::k256KB);
}

TEST(AutoPieceSize, Boundary1GBMinus1) {
    EXPECT_EQ(utils::auto_piece_size(1LL * 1024 * 1024 * 1024 - 1), PieceSizes::k256KB);
}

TEST(AutoPieceSize, Boundary1GB) {
    EXPECT_EQ(utils::auto_piece_size(1LL * 1024 * 1024 * 1024), PieceSizes::k512KB);
}

TEST(AutoPieceSize, Boundary2GB) {
    EXPECT_EQ(utils::auto_piece_size(2LL * 1024 * 1024 * 1024), PieceSizes::k1024KB);
}

TEST(AutoPieceSize, Boundary4GB) {
    EXPECT_EQ(utils::auto_piece_size(4LL * 1024 * 1024 * 1024), PieceSizes::k2048KB);
}

TEST(AutoPieceSize, Boundary8GB) {
    EXPECT_EQ(utils::auto_piece_size(8LL * 1024 * 1024 * 1024), PieceSizes::k4096KB);
}

TEST(AutoPieceSize, Boundary16GB) {
    EXPECT_EQ(utils::auto_piece_size(16LL * 1024 * 1024 * 1024), PieceSizes::k8192KB);
}

TEST(AutoPieceSize, Boundary32GB) {
    EXPECT_EQ(utils::auto_piece_size(32LL * 1024 * 1024 * 1024), PieceSizes::k16384KB);
}

TEST(AutoPieceSize, Boundary64GB) {
    EXPECT_EQ(utils::auto_piece_size(64LL * 1024 * 1024 * 1024), PieceSizes::k32768KB);
}

TEST(AutoPieceSize, Max100GB) {
    EXPECT_EQ(utils::auto_piece_size(100LL * 1024 * 1024 * 1024), PieceSizes::k32768KB);
}

TEST(IsValidUrl, HTTP) {
    EXPECT_TRUE(utils::is_valid_url("http://example.com"));
}

TEST(IsValidUrl, HTTPSWithPath) {
    EXPECT_TRUE(utils::is_valid_url("https://site.com/path"));
}

TEST(IsValidUrl, UDPWithPort) {
    EXPECT_TRUE(utils::is_valid_url("udp://tracker:80/announce"));
}

TEST(IsValidUrl, CaseInsensitive) {
    EXPECT_TRUE(utils::is_valid_url("HTTP://CAPS.COM"));
}

TEST(IsValidUrl, FTPInvalid) {
    EXPECT_FALSE(utils::is_valid_url("ftp://files.com"));
}

TEST(IsValidUrl, NoScheme) {
    EXPECT_FALSE(utils::is_valid_url("not-a-url"));
}

TEST(IsValidUrl, Empty) {
    EXPECT_FALSE(utils::is_valid_url(""));
}

TEST(IsValidUrl, NoHost) {
    EXPECT_FALSE(utils::is_valid_url("http://"));
}

TEST(IsValidUrl, OnlyScheme) {
    EXPECT_FALSE(utils::is_valid_url("udp://"));
}

TEST(IsValidUrl, InvalidScheme) {
    EXPECT_FALSE(utils::is_valid_url("ws://socket.com"));
}

TEST(IsValidUrl, LeadingWhitespace) {
    EXPECT_FALSE(utils::is_valid_url(" http://example.com"));
}

TEST(IsValidUrl, TrailingWhitespace) {
    EXPECT_TRUE(utils::is_valid_url("http://example.com "));
}

TEST(SanitizePath, Spaces) {
    EXPECT_EQ(utils::sanitize_path("  /path/to/file  "), "/path/to/file");
}

TEST(SanitizePath, DoubleQuotes) {
    EXPECT_EQ(utils::sanitize_path("\"quoted\""), "quoted");
}

TEST(SanitizePath, SingleQuotes) {
    EXPECT_EQ(utils::sanitize_path("'single'"), "single");
}

TEST(SanitizePath, Empty) {
    EXPECT_EQ(utils::sanitize_path(""), "");
}

TEST(SanitizePath, OnlySpaces) {
    EXPECT_EQ(utils::sanitize_path("   "), "");
}

TEST(SanitizePath, NoQuotes) {
    EXPECT_EQ(utils::sanitize_path("normal_path"), "normal_path");
}

TEST(SanitizePath, TrailingBackslash) {
    EXPECT_EQ(utils::sanitize_path("path\\"), "path");
}

TEST(SanitizePath, MiddleBackslash) {
    EXPECT_EQ(utils::sanitize_path("path\\file"), "pathfile");
}

TEST(SanitizePath, QuotedWithSpaces) {
    EXPECT_EQ(utils::sanitize_path("  \"quoted\"  "), "quoted");
}

TEST(SanitizePath, SingleQuotedWithSpaces) {
    EXPECT_EQ(utils::sanitize_path("  'path'  "), "path");
}

TEST(SanitizePath, ConsecutiveBackslashes) {
    EXPECT_EQ(utils::sanitize_path("a\\\\b"), "a\\b");
}

TEST(SanitizePath, EscapedQuote) {
    EXPECT_EQ(utils::sanitize_path("a\\\"b"), "a\"b");
}

TEST(SanitizePath, MultipleEscapes) {
    EXPECT_EQ(utils::sanitize_path("\\\\server\\\\share"), "\\server\\share");
}

TEST(FormatSize, Zero) {
    EXPECT_EQ(utils::format_size(0), "0.00 B");
}

TEST(FormatSize, OneByte) {
    EXPECT_EQ(utils::format_size(1), "1.00 B");
}

TEST(FormatSize, MaxBytes) {
    EXPECT_EQ(utils::format_size(1023), "1023.00 B");
}

TEST(FormatSize, OneKB) {
    EXPECT_EQ(utils::format_size(1024), "1.00 KB");
}

TEST(FormatSize, OneAndHalfKB) {
    EXPECT_EQ(utils::format_size(1536), "1.50 KB");
}

TEST(FormatSize, OneMB) {
    EXPECT_EQ(utils::format_size(1048576), "1.00 MB");
}

TEST(FormatSize, OneGB) {
    EXPECT_EQ(utils::format_size(1073741824), "1.00 GB");
}

TEST(FormatSize, OneTB) {
    EXPECT_EQ(utils::format_size(1099511627776), "1.00 TB");
}

TEST(FormatSize, TenTB) {
    EXPECT_EQ(utils::format_size(10995116277760), "10.00 TB");
}

TEST(FormatSpeed, Zero) {
    EXPECT_EQ(utils::format_speed(0.0), "0.00 MB/s");
}

TEST(FormatSpeed, Low) {
    EXPECT_EQ(utils::format_speed(1024.0), "0.00 MB/s");
}

TEST(FormatSpeed, OneMB) {
    EXPECT_EQ(utils::format_speed(1048576.0), "1.00 MB/s");
}

TEST(FormatSpeed, TwoAndHalfMB) {
    EXPECT_EQ(utils::format_speed(2621440.0), "2.50 MB/s");
}

TEST(FormatSpeed, HundredMB) {
    EXPECT_EQ(utils::format_speed(104857600.0), "100.00 MB/s");
}

TEST(FormatEta, Zero) {
    EXPECT_EQ(utils::format_eta(0.0), "0m 0s");
}

TEST(FormatEta, Seconds59) {
    EXPECT_EQ(utils::format_eta(59.0), "0m 59s");
}

TEST(FormatEta, OneMinute) {
    EXPECT_EQ(utils::format_eta(60.0), "1m 0s");
}

TEST(FormatEta, OneMinuteOneSecond) {
    EXPECT_EQ(utils::format_eta(61.0), "1m 1s");
}

TEST(FormatEta, TwoMinFiveSec) {
    EXPECT_EQ(utils::format_eta(125.0), "2m 5s");
}

TEST(FormatEta, OneHour) {
    EXPECT_EQ(utils::format_eta(3600.0), "60m 0s");
}

TEST(ExtractDomain, HTTPSWithPortAndPath) {
    EXPECT_EQ(utils::extract_domain("https://tracker.example.com:8080/announce"), "tracker.example.com");
}

TEST(ExtractDomain, UDPWithPort) {
    EXPECT_EQ(utils::extract_domain("udp://tracker.opentrackr.org:1337/announce"), "tracker.opentrackr.org");
}

TEST(ExtractDomain, HTTPNoPort) {
    EXPECT_EQ(utils::extract_domain("http://bt.example.com/announce"), "bt.example.com");
}

TEST(ExtractDomain, HTTPSNoPath) {
    EXPECT_EQ(utils::extract_domain("https://tracker.example.com"), "tracker.example.com");
}

TEST(ExtractDomain, HTTPPortNoPath) {
    EXPECT_EQ(utils::extract_domain("http://tracker.example.com:6969"), "tracker.example.com");
}

TEST(ExtractDomain, EmptyString) {
    EXPECT_EQ(utils::extract_domain(""), "");
}

TEST(ExtractDomain, NoScheme) {
    EXPECT_EQ(utils::extract_domain("not-a-url"), "");
}

TEST(ExtractDomain, OnlyScheme) {
    EXPECT_EQ(utils::extract_domain("https://"), "");
}

TEST(ExtractDomain, UserInfoStripped) {
    EXPECT_EQ(utils::extract_domain("https://user@tracker.example.com/announce"), "tracker.example.com");
}

TEST(ExtractDomain, UserInfoWithPort) {
    EXPECT_EQ(utils::extract_domain("https://user:pass@tracker.example.com:8080/announce"), "tracker.example.com");
}

TEST(ExtractDomain, IPv6Address) {
    EXPECT_EQ(utils::extract_domain("http://[::1]:80/announce"), "::1");
}

TEST(ExtractDomain, IPv6FullAddress) {
    EXPECT_EQ(utils::extract_domain("http://[2001:db8::1]/announce"), "2001:db8::1");
}

TEST(ExtractDomain, IPv6MalformedMissingClosingBracket) {
    EXPECT_EQ(utils::extract_domain("http://[::1/announce"), "");
}

TEST(GenerateOutputFilename, WithTrackerNoSkip) {
    std::vector<std::string> trackers = {"https://tracker.example.com/announce"};
    EXPECT_EQ(utils::generate_output_filename("/path/to/My.Movie", trackers, false),
              "tracker.example.com_My.Movie.torrent");
}

TEST(GenerateOutputFilename, WithTrackerSkipPrefix) {
    std::vector<std::string> trackers = {"https://tracker.example.com/announce"};
    EXPECT_EQ(utils::generate_output_filename("/path/to/My.Movie", trackers, true),
              "My.Movie.torrent");
}

TEST(GenerateOutputFilename, NoTrackers) {
    std::vector<std::string> trackers;
    EXPECT_EQ(utils::generate_output_filename("/path/to/My.Movie", trackers, false),
              "My.Movie.torrent");
}

TEST(GenerateOutputFilename, NoTrackersWithSkip) {
    std::vector<std::string> trackers;
    EXPECT_EQ(utils::generate_output_filename("/path/to/My.Movie", trackers, true),
              "My.Movie.torrent");
}

TEST(GenerateOutputFilename, InvalidTrackerFallsBack) {
    std::vector<std::string> trackers = {"not-a-url"};
    EXPECT_EQ(utils::generate_output_filename("/path/to/My.Movie", trackers, false),
              "My.Movie.torrent");
}

TEST(GenerateOutputFilename, MultipleTrackersUsesFirst) {
    std::vector<std::string> trackers = {
        "udp://t2.com:1337/announce",
        "https://other.com/announce"
    };
    EXPECT_EQ(utils::generate_output_filename("/path/to/My.Movie", trackers, false),
              "t2.com_My.Movie.torrent");
}

TEST(GenerateOutputFilename, FileWithExtensionNonExistent) {
    std::vector<std::string> trackers = {"https://tracker.example.com/announce"};
    EXPECT_EQ(utils::generate_output_filename("/path/to/file.mkv", trackers, false),
              "tracker.example.com_file.mkv.torrent");
}

TEST(GenerateOutputFilename, ExistingFileStripsExtension) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_gen_fname_test";
    fs::create_directories(temp_dir);
    auto real_file = temp_dir / "MyMovie.mkv";
    { std::ofstream(real_file) << "fake content"; }

    std::vector<std::string> trackers = {"https://tracker.example.com/announce"};
    EXPECT_EQ(utils::generate_output_filename(real_file, trackers, false),
              "tracker.example.com_MyMovie.torrent");

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(GenerateOutputFilename, TrailingSeparatorUsesParentDirName) {
    std::vector<std::string> trackers;
    EXPECT_EQ(utils::generate_output_filename("/path/to/MyDir/", trackers, false),
              "MyDir.torrent");
}

TEST(GenerateOutputFilename, DotPathResolvedToAbsolute) {
    std::vector<std::string> trackers;
    std::string result = utils::generate_output_filename(".", trackers, false);
    EXPECT_NE(result, ".torrent");
    EXPECT_NE(result, "..torrent");
    EXPECT_TRUE(result.ends_with(".torrent"));
}

TEST(GenerateOutputFilename, DotDotPathResolvedToAbsolute) {
    std::vector<std::string> trackers;
    std::string result = utils::generate_output_filename("..", trackers, false);
    EXPECT_NE(result, ".torrent");
    EXPECT_NE(result, "..torrent");
    EXPECT_TRUE(result.ends_with(".torrent"));
}

TEST(SanitizeFilenamePart, ReplacesWindowsInvalidChars) {
    EXPECT_EQ(utils::sanitize_filename_part("hello:world"), "hello_world");
    EXPECT_EQ(utils::sanitize_filename_part("a<b>c"), "a_b_c");
    EXPECT_EQ(utils::sanitize_filename_part("test\"file"), "test_file");
    EXPECT_EQ(utils::sanitize_filename_part("a|b?c*d"), "a_b_c_d");
}

TEST(SanitizeFilenamePart, ReplacesSlashes) {
    EXPECT_EQ(utils::sanitize_filename_part("path\\to\\file"), "path_to_file");
    EXPECT_EQ(utils::sanitize_filename_part("path/to/file"), "path_to_file");
}

TEST(SanitizeFilenamePart, NoChangeOnCleanString) {
    EXPECT_EQ(utils::sanitize_filename_part("clean_name-123"), "clean_name-123");
}

TEST(GenerateOutputFilename, DomainWithInvalidCharsSanitized) {
    std::vector<std::string> trackers = {"https://track|er:8080/announce"};
    EXPECT_EQ(utils::generate_output_filename("/path/to/Content", trackers, false),
              "track_er_Content.torrent");
}

TEST(GenerateOutputFilename, IPv6DomainSanitized) {
    std::vector<std::string> trackers = {"http://[::1]:80/announce"};
    EXPECT_EQ(utils::generate_output_filename("/path/to/Content", trackers, false),
              "_1_Content.torrent");
}

TEST(GenerateOutputFilename, DotDotNonExistentFallback) {
    std::vector<std::string> trackers;
    std::string result = utils::generate_output_filename("/nonexistent/path/that/does/not/exist/..", trackers, false);
    EXPECT_NE(result, "..torrent");
    EXPECT_NE(result, ".torrent");
    EXPECT_TRUE(result.ends_with(".torrent"));
    EXPECT_FALSE(result.empty());
}

TEST(GenerateOutputFilename, ContentNameWithInvalidCharsSanitized) {
    std::vector<std::string> trackers = {"https://tracker.example.com/announce"};
    EXPECT_EQ(utils::generate_output_filename("/path/to/file:name", trackers, false),
              "tracker.example.com_file_name.torrent");
}

TEST(GenerateOutputFilename, ContentNameWithPipeSanitized) {
    std::vector<std::string> trackers;
    EXPECT_EQ(utils::generate_output_filename("/path/to/bad|file", trackers, false),
              "bad_file.torrent");
}

TEST(SanitizeFilenamePart, EmptyInput) {
    EXPECT_EQ(utils::sanitize_filename_part(""), "");
}

TEST(SanitizeFilenamePart, CollapsesDuplicateUnderscores) {
    EXPECT_EQ(utils::sanitize_filename_part("a__b"), "a_b");
}

TEST(SanitizeFilenamePart, CollapsesTripleUnderscores) {
    EXPECT_EQ(utils::sanitize_filename_part("a___b"), "a_b");
}

TEST(SanitizeFilenamePart, CollapsesMixedInvalidChars) {
    EXPECT_EQ(utils::sanitize_filename_part("a::b"), "a_b");
}

TEST(SanitizeFilenamePart, NoCollapseOnSingleUnderscore) {
    EXPECT_EQ(utils::sanitize_filename_part("a_b"), "a_b");
}

TEST(TruncateFilename, ShortNameUnchanged) {
    EXPECT_EQ(utils::truncate_filename("file.torrent"), "file.torrent");
}

TEST(TruncateFilename, ExactLimitUnchanged) {
    std::string name(246, 'a');
    std::string filename = name + ".torrent";
    EXPECT_EQ(utils::truncate_filename(filename, 255), filename);
}

TEST(TruncateFilename, LongNameTruncated) {
    std::string name(291, 'a');
    std::string filename = name + ".torrent";
    std::string result = utils::truncate_filename(filename, 255);
    EXPECT_EQ(result.size(), 255u);
    EXPECT_TRUE(result.ends_with(".torrent"));
}

TEST(TruncateFilename, PreservesTorrentExtension) {
    std::string name(300, 'x');
    std::string filename = name + ".torrent";
    std::string result = utils::truncate_filename(filename, 255);
    EXPECT_EQ(result.substr(result.size() - 8), ".torrent");
}

TEST(ResolveCollision, NoCollision) {
    auto temp_dir = std::filesystem::temp_directory_path() / "torrent_builder_test_no_collision";
    std::filesystem::create_directories(temp_dir);
    std::string result = utils::resolve_collision(temp_dir, "unique.torrent");
    EXPECT_EQ(result, "unique.torrent");
    std::filesystem::remove_all(temp_dir);
}

TEST(ResolveCollision, FirstCollision) {
    auto temp_dir = std::filesystem::temp_directory_path() / "torrent_builder_test_first_collision";
    std::filesystem::create_directories(temp_dir);
    { std::ofstream(temp_dir / "sample.torrent") << "existing"; }
    std::string result = utils::resolve_collision(temp_dir, "sample.torrent");
    EXPECT_EQ(result, "sample(1).torrent");
    std::filesystem::remove_all(temp_dir);
}

TEST(ResolveCollision, MultipleCollisions) {
    auto temp_dir = std::filesystem::temp_directory_path() / "torrent_builder_test_multi_collision";
    std::filesystem::create_directories(temp_dir);
    { std::ofstream(temp_dir / "sample.torrent") << "1"; }
    { std::ofstream(temp_dir / "sample(1).torrent") << "2"; }
    { std::ofstream(temp_dir / "sample(2).torrent") << "3"; }
    std::string result = utils::resolve_collision(temp_dir, "sample.torrent");
    EXPECT_EQ(result, "sample(3).torrent");
    std::filesystem::remove_all(temp_dir);
}

TEST(GenerateOutputFilename, TrackerIndexOutOfRange) {
    std::vector<std::string> trackers = {
        "https://tracker1.com/announce",
        "https://tracker2.com/announce"
    };
    EXPECT_EQ(utils::generate_output_filename("/path/to/File", trackers, false, 99),
              "tracker1.com_File.torrent");
}

TEST(GenerateOutputFilename, TrackerIndexOne) {
    std::vector<std::string> trackers = {
        "https://tracker1.com/announce",
        "https://tracker2.com/announce"
    };
    EXPECT_EQ(utils::generate_output_filename("/path/to/File", trackers, false, 1),
              "tracker2.com_File.torrent");
}

TEST(GenerateOutputFilename, TrackerIndexNegative) {
    std::vector<std::string> trackers = {
        "https://tracker1.com/announce",
        "https://tracker2.com/announce"
    };
    EXPECT_EQ(utils::generate_output_filename("/path/to/File", trackers, false, -1),
              "tracker1.com_File.torrent");
}

TEST(TruncateFilename, UTF8MultiBytePreserved) {
    std::string name = std::string(240, 'a') + "\xc3\xa9" + std::string(10, 'a');
    std::string filename = name + ".torrent";
    std::string result = utils::truncate_filename(filename, 255);
    EXPECT_TRUE(result.ends_with(".torrent"));
    bool prev_was_leader = false;
    for (size_t i = 0; i < result.size() - 8; ++i)
    {
        unsigned char c = static_cast<unsigned char>(result[i]);
        if ((c & 0xC0) == 0x80)
        {
            EXPECT_TRUE(prev_was_leader)
                << "Dangling continuation byte at position " << i;
            prev_was_leader = false;
        }
        else
        {
            prev_was_leader = (c & 0x80) != 0;
        }
    }
}

TEST(ResolveCollision, ExhaustedThrows) {
    auto temp_dir = std::filesystem::temp_directory_path() / "torrent_builder_test_exhausted";
    std::filesystem::create_directories(temp_dir);
    { std::ofstream(temp_dir / "file.torrent") << "x"; }
    for (int i = 1; i <= 1000; ++i)
    {
        std::ofstream(temp_dir / ("file(" + std::to_string(i) + ").torrent")) << "x";
    }
    EXPECT_THROW(utils::resolve_collision(temp_dir, "file.torrent"), std::runtime_error);
    std::filesystem::remove_all(temp_dir);
}

TEST(GenerateAutoOutputPath, BasicWithPath) {
    auto temp_dir = std::filesystem::temp_directory_path() / "torrent_builder_test_auto_path";
    std::filesystem::create_directories(temp_dir);
    auto input_file = temp_dir / "Movie.mkv";
    { std::ofstream(input_file) << "data"; }

    std::vector<std::string> trackers = {"https://tracker.example.com/announce"};
    std::string result = utils::generate_auto_output_path(input_file, trackers, false, 0, temp_dir);
    EXPECT_TRUE(result.ends_with("tracker.example.com_Movie.torrent"));
    EXPECT_NE(result.find(temp_dir.string()), std::string::npos);

    std::filesystem::remove_all(temp_dir);
}

TEST(GenerateAutoOutputPath, WithCollision) {
    auto temp_dir = std::filesystem::temp_directory_path() / "torrent_builder_test_auto_collision";
    std::filesystem::create_directories(temp_dir);
    auto input_file = temp_dir / "Movie.mkv";
    { std::ofstream(input_file) << "data"; }
    { std::ofstream(temp_dir / "Movie.torrent") << "existing"; }

    std::vector<std::string> trackers;
    std::string result = utils::generate_auto_output_path(input_file, trackers, false, 0, temp_dir);
    EXPECT_TRUE(result.ends_with("Movie(1).torrent"));

    std::filesystem::remove_all(temp_dir);
}

TEST(TruncateFilename, FilenameShorterThanExtension) {
    EXPECT_EQ(utils::truncate_filename("a.torrent", 100), "a.torrent");
}

TEST(TruncateFilename, MaxBytesSmallerThanExtension) {
    EXPECT_EQ(utils::truncate_filename("file.torrent", 4), "file.torrent");
    EXPECT_EQ(utils::truncate_filename("file.torrent", 7), "file.torrent");
}

TEST(TruncateFilename, MaxBytesExactlyExtensionSize) {
    EXPECT_EQ(utils::truncate_filename("file.torrent", 8), "file.torrent");
}

TEST(TruncateFilename, NonTorrentExtensionPreserved) {
    std::string name(297, 'a');
    std::string filename = name + ".txt";
    std::string result = utils::truncate_filename(filename, 255);
    EXPECT_EQ(result.size(), 255u);
    EXPECT_TRUE(result.ends_with(".txt"));
}

TEST(TruncateFilename, NonTorrentExtensionShortUnchanged) {
    EXPECT_EQ(utils::truncate_filename("file.txt", 100), "file.txt");
}

TEST(TruncateFilename, NoExtensionTruncates) {
    std::string name(300, 'a');
    std::string result = utils::truncate_filename(name, 255);
    EXPECT_EQ(result.size(), 255u);
}

TEST(TruncateFilename, NoExtensionShortUnchanged) {
    EXPECT_EQ(utils::truncate_filename("nofile", 100), "nofile");
}

TEST(TruncateFilename, DotfileTooLongTruncated) {
    std::string name(300, 'a');
    std::string filename = "." + name;
    std::string result = utils::truncate_filename(filename, 255);
    EXPECT_EQ(result.size(), 255u);
    EXPECT_TRUE(result.starts_with("."));
}

TEST(TruncateFilename, UTF8OrphanedLeaderRemoved) {
    std::string name = std::string(246, 'a') + "\xc3\xa9" + std::string(100, 'b');
    std::string filename = name + ".torrent";
    std::string result = utils::truncate_filename(filename, 255);
    EXPECT_TRUE(result.ends_with(".torrent"));
    EXPECT_LE(result.size(), 255u);
    for (size_t i = 0; i < result.size() - 8; ++i)
    {
        unsigned char c = static_cast<unsigned char>(result[i]);
        if ((c & 0xC0) == 0x80)
        {
            ASSERT_GT(i, 0u);
            unsigned char prev = static_cast<unsigned char>(result[i - 1]);
            EXPECT_TRUE((prev & 0x80) != 0)
                << "Dangling continuation byte at position " << i;
        }
    }
}

TEST(TruncateFilename, UTF8CompleteCharPreservedAtBoundary) {
    std::string name = std::string(245, 'a') + "\xc3\xa9" + std::string(100, 'b');
    std::string filename = name + ".torrent";
    std::string result = utils::truncate_filename(filename, 255);
    EXPECT_TRUE(result.ends_with(".torrent"));
    EXPECT_LE(result.size(), 255u);
    std::string stem = result.substr(0, result.size() - 8);
    EXPECT_EQ(static_cast<unsigned char>(stem[245]), 0xC3);
    EXPECT_EQ(static_cast<unsigned char>(stem[246]), 0xA9);
}

TEST(TruncateFilename, UTF8ThreeByteOrphanedLeaderRemoved) {
    std::string name = std::string(246, 'a') + "\xe3\x81\x82" + std::string(100, 'b');
    std::string filename = name + ".torrent";
    std::string result = utils::truncate_filename(filename, 255);
    EXPECT_TRUE(result.ends_with(".torrent"));
    EXPECT_LE(result.size(), 255u);
    std::string stem = result.substr(0, result.size() - 8);
    EXPECT_EQ(stem.size(), 246u);
}

TEST(TruncateFilename, UTF8ThreeByteCompleteCharPreservedAtBoundary) {
    std::string name = std::string(244, 'a') + "\xe3\x81\x82" + std::string(100, 'b');
    std::string filename = name + ".torrent";
    std::string result = utils::truncate_filename(filename, 255);
    EXPECT_TRUE(result.ends_with(".torrent"));
    EXPECT_LE(result.size(), 255u);
    std::string stem = result.substr(0, result.size() - 8);
    EXPECT_EQ(static_cast<unsigned char>(stem[244]), 0xE3);
    EXPECT_EQ(static_cast<unsigned char>(stem[245]), 0x81);
    EXPECT_EQ(static_cast<unsigned char>(stem[246]), 0x82);
}

TEST(TruncateFilename, UTF8FourByteOrphanedLeaderRemoved) {
    std::string name = std::string(246, 'a') + "\xf0\x9f\x98\x80" + std::string(100, 'b');
    std::string filename = name + ".torrent";
    std::string result = utils::truncate_filename(filename, 255);
    EXPECT_TRUE(result.ends_with(".torrent"));
    EXPECT_LE(result.size(), 255u);
    std::string stem = result.substr(0, result.size() - 8);
    EXPECT_EQ(stem.size(), 246u);
}

TEST(TruncateFilename, UTF8FourByteCompleteCharPreservedAtBoundary) {
    std::string name = std::string(243, 'a') + "\xf0\x9f\x98\x80" + std::string(100, 'b');
    std::string filename = name + ".torrent";
    std::string result = utils::truncate_filename(filename, 255);
    EXPECT_TRUE(result.ends_with(".torrent"));
    EXPECT_LE(result.size(), 255u);
    std::string stem = result.substr(0, result.size() - 8);
    EXPECT_EQ(static_cast<unsigned char>(stem[243]), 0xF0);
    EXPECT_EQ(static_cast<unsigned char>(stem[244]), 0x9F);
    EXPECT_EQ(static_cast<unsigned char>(stem[245]), 0x98);
    EXPECT_EQ(static_cast<unsigned char>(stem[246]), 0x80);
}

TEST(TruncateFilename, UTF8LeaderDropsToZeroFallback) {
    std::string name = "\xe3\x81\x82" + std::string(100, 'b');
    std::string filename = name + ".torrent";
    std::string result = utils::truncate_filename(filename, 10);
    EXPECT_TRUE(result.ends_with(".torrent"));
    EXPECT_LE(result.size(), 10u);
    EXPECT_GT(result.size(), 8u);
}

TEST(TruncateFilename, AvailableBecomesZero) {
    std::string stem(300, '\x80');
    std::string input = stem + ".torrent";
    std::string result = utils::truncate_filename(input, 255);
    EXPECT_TRUE(result.ends_with(".torrent"));
    EXPECT_LE(result.size(), 255u);
    EXPECT_GT(result.size(), 8u);
}

TEST(ResolveCollision, NoExtension) {
    auto temp_dir = std::filesystem::temp_directory_path() / "torrent_builder_test_no_ext";
    std::filesystem::create_directories(temp_dir);
    { std::ofstream(temp_dir / "README") << "existing"; }
    std::string result = utils::resolve_collision(temp_dir, "README");
    EXPECT_EQ(result, "README(1)");
    std::filesystem::remove_all(temp_dir);
}

TEST(GenerateAutoOutputPath, TruncationWithCollisionStillFitsLimit) {
    auto temp_dir = std::filesystem::temp_directory_path() / "torrent_builder_test_trunc_collision";
    std::filesystem::create_directories(temp_dir);

    std::string long_name(300, 'a');
    auto input_file = temp_dir / (long_name + ".mkv");
    { std::ofstream(input_file) << "data"; }

    std::string truncated_stem(241, 'a');
    std::string base_filename = truncated_stem + ".torrent";
    { std::ofstream(temp_dir / base_filename) << "existing"; }

    std::vector<std::string> trackers;
    std::string result = utils::generate_auto_output_path(input_file, trackers, false, 0, temp_dir);
    std::string filename = std::filesystem::path(result).filename().string();
    EXPECT_LE(filename.size(), 255u);
    EXPECT_TRUE(filename.ends_with(".torrent"));
    EXPECT_NE(filename, base_filename)
        << "Should resolve collision, not overwrite existing file";

    std::filesystem::remove_all(temp_dir);
}

TEST(GenerateAutoOutputPath, CollisionWorstCaseSuffixFitsLimit) {
    auto temp_dir = std::filesystem::temp_directory_path() / "torrent_builder_test_worst_case";
    std::filesystem::create_directories(temp_dir);

    std::string long_name(300, 'a');
    auto input_file = temp_dir / (long_name + ".mkv");
    { std::ofstream(input_file) << "data"; }

    std::string truncated_stem(249, 'a');
    std::string base_filename = truncated_stem + ".torrent";
    { std::ofstream(temp_dir / base_filename) << "existing"; }

    for (int i = 1; i <= 999; ++i)
    {
        std::ofstream(temp_dir / (truncated_stem + "(" + std::to_string(i) + ").torrent")) << "x";
    }

    std::vector<std::string> trackers;
    std::string result = utils::generate_auto_output_path(input_file, trackers, false, 0, temp_dir);
    std::string filename = std::filesystem::path(result).filename().string();
    EXPECT_LE(filename.size(), 255u);
    EXPECT_TRUE(filename.ends_with(".torrent"));
    EXPECT_NE(filename, base_filename);

    std::filesystem::remove_all(temp_dir);
}

TEST(ResolveCollision, MaxBytesTruncatesStem) {
    auto temp_dir = std::filesystem::temp_directory_path() / "torrent_builder_test_max_bytes";
    std::filesystem::create_directories(temp_dir);

    std::string stem(247, 'a');
    std::string base_filename = stem + ".torrent";
    { std::ofstream(temp_dir / base_filename) << "existing"; }

    std::string result = utils::resolve_collision(temp_dir, base_filename, 255);
    EXPECT_LE(result.size(), 255u);
    EXPECT_TRUE(result.ends_with(".torrent"));
    EXPECT_NE(result, base_filename);

    std::filesystem::remove_all(temp_dir);
}

TEST(ResolveCollision, MaxBytesTruncatesUTF8StemSafely) {
    auto temp_dir = std::filesystem::temp_directory_path() / "torrent_builder_test_utf8_max";
    std::filesystem::create_directories(temp_dir);

    std::string stem = std::string(243, 'a') + "\xc3\xa9\xc3\xa9";
    std::string base_filename = stem + ".torrent";
    { std::ofstream(temp_dir / base_filename) << "existing"; }

    std::string result = utils::resolve_collision(temp_dir, base_filename, 255);
    EXPECT_LE(result.size(), 255u);
    EXPECT_TRUE(result.ends_with(".torrent"));
    std::string result_stem = result.substr(0, result.rfind('.'));
    for (size_t i = 0; i < result_stem.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(result_stem[i]);
        if ((c & 0xC0) == 0x80)
        {
            ASSERT_GT(i, 0u);
            unsigned char prev = static_cast<unsigned char>(result_stem[i - 1]);
            EXPECT_TRUE((prev & 0x80) != 0)
                << "Dangling continuation byte at position " << i;
        }
    }

    std::filesystem::remove_all(temp_dir);
}

TEST(GenerateAutoOutputPath, EmptyOutputDirUsesCurrentPath) {
    auto temp_dir = std::filesystem::temp_directory_path() / "torrent_builder_test_empty_dir";
    std::filesystem::create_directories(temp_dir);
    auto input_file = temp_dir / "Movie.mkv";
    { std::ofstream(input_file) << "data"; }

    auto original_dir = std::filesystem::current_path();
    std::filesystem::current_path(temp_dir);

    std::vector<std::string> trackers = {"https://tracker.example.com/announce"};
    std::string result = utils::generate_auto_output_path(input_file, trackers, false, 0, "");
    EXPECT_TRUE(result.starts_with(temp_dir.string()));

    std::filesystem::current_path(original_dir);
    std::filesystem::remove_all(temp_dir);
}
