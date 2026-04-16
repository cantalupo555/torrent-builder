#include <gtest/gtest.h>
#include "utils.hpp"
#include "constants.hpp"

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
