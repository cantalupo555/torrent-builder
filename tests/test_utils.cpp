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
              "__1_Content.torrent");
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
