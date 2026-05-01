#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <string>
#include <regex>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/file_storage.hpp>
#include "torrent_inspector.hpp"
#include "utils.hpp"

namespace fs = std::filesystem;

class InspectorTest : public ::testing::Test
{
  protected:
    fs::path torrent_path_;
    fs::path test_file_;

    void SetUp() override
    {
        torrent_path_ = fs::current_path() / "test.torrent";
        test_file_ = fs::current_path() / "test_file.txt";
        cleanup();
        create_test_file();
        create_simple_torrent();
    }

    void TearDown() override { cleanup(); }

    void cleanup()
    {
        std::error_code ec;
        fs::remove(torrent_path_, ec);
        fs::remove(test_file_, ec);
    }

    void create_test_file()
    {
        std::ofstream file(test_file_);
        file << "Hello World";
        file.close();
    }

    void create_simple_torrent()
    {
        create_test_file();

        lt::file_storage fs;
        fs.add_file("test_file.txt", 11);
        lt::create_torrent ct(fs, 16384);

        const char data[] = "Hello World";
        auto piece_hash = lt::hasher(lt::span<char const>(data, 11)).final();
        ct.set_hash(0, piece_hash);
        ct.add_tracker("udp://tracker.example.com:80/announce");

        lt::entry e = ct.generate();
        std::vector<char> buffer;
        lt::bencode(std::back_inserter(buffer), e);

        std::ofstream torrent(torrent_path_, std::ios::binary);
        torrent.write(buffer.data(), buffer.size());
        torrent.close();
    }
};

TEST_F(InspectorTest, InspectReturnsBasicInfo)
{
    TorrentInspector inspector(torrent_path_.string());
    TorrentMetadata meta = inspector.inspect();

    EXPECT_FALSE(meta.name.empty());
    EXPECT_GT(meta.total_size, 0);
    EXPECT_GT(meta.piece_length, 0);
    EXPECT_GT(meta.piece_count, 0);
}

TEST_F(InspectorTest, InspectThrowsForNonexistentFile)
{
    EXPECT_THROW(
        {
            TorrentInspector inspector("nonexistent.torrent");
            inspector.inspect();
        },
        std::runtime_error);
}

TEST_F(InspectorTest, InspectThrowsForCorruptedFile)
{
    std::ofstream corrupt("corrupt.torrent", std::ios::binary);
    corrupt << "this is not a valid torrent file";
    corrupt.close();

    EXPECT_THROW(
        {
            TorrentInspector inspector("corrupt.torrent");
            inspector.inspect();
        },
        std::runtime_error);

    fs::remove("corrupt.torrent");
}

TEST_F(InspectorTest, VerifyFilesReturnsFalseWhenMissing)
{
    TorrentInspector inspector(torrent_path_.string());
    fs::remove(test_file_);
    bool result = inspector.verify_files(fs::current_path());
    EXPECT_FALSE(result);
}

TEST_F(InspectorTest, FormatMetadataNonJson)
{
    TorrentMetadata meta;
    meta.name = "test_name";
    meta.info_hash_v1 = "1234567890abcdef1234567890abcdef12345678";
    meta.total_size = 1024;
    meta.piece_length = 16384;
    meta.piece_count = 1;
    meta.files = {};
    meta.trackers = {"udp://tracker.example.com"};
    meta.web_seeds = {};
    meta.is_private = false;
    meta.magnet_link = "magnet:?xt=urn:btih:1234567890abcdef1234567890abcdef12345678&dn=test_name";

    std::string output = TorrentInspector::format_metadata(meta, false);

    EXPECT_NE(output.find("Name: test_name"), std::string::npos);
    EXPECT_NE(output.find("Total Size:"), std::string::npos);
    EXPECT_NE(output.find("Trackers:"), std::string::npos);
}

TEST_F(InspectorTest, FormatMetadataJsonValid)
{
    TorrentMetadata meta;
    meta.name = "test_name";
    meta.info_hash_v1 = "1234567890abcdef1234567890abcdef12345678";
    meta.total_size = 1024;
    meta.piece_length = 16384;
    meta.piece_count = 1;
    meta.files = {};
    meta.trackers = {"udp://tracker.example.com"};
    meta.web_seeds = {};
    meta.is_private = false;
    meta.magnet_link = "magnet:?xt=urn:btih:1234567890abcdef1234567890abcdef12345678&dn=test_name";

    std::string json = TorrentInspector::format_metadata(meta, true);

    EXPECT_NE(json.find("\"name\": \"test_name\""), std::string::npos);
    EXPECT_NE(json.find("\"total_size\": 1024"), std::string::npos);
    EXPECT_NE(json.find("\"is_private\": false"), std::string::npos);
}

TEST_F(InspectorTest, FormatFileTreeNonJson)
{
    TorrentMetadata meta;
    TorrentMetadata::FileInfo file;
    file.path = "test.txt";
    file.size = 1024;
    meta.files.push_back(file);

    std::string output = TorrentInspector::format_file_tree(meta, false);

    EXPECT_NE(output.find("test.txt"), std::string::npos);
    EXPECT_NE(output.find("1.00 KiB"), std::string::npos);
}

TEST_F(InspectorTest, FormatFileTreeJsonValid)
{
    TorrentMetadata meta;
    TorrentMetadata::FileInfo file;
    file.path = "test.txt";
    file.size = 1024;
    meta.files.push_back(file);

    std::string json = TorrentInspector::format_file_tree(meta, true);

    EXPECT_NE(json.find("\"path\": \"test.txt\""), std::string::npos);
    EXPECT_NE(json.find("\"size\": 1024"), std::string::npos);
    EXPECT_NE(json.find("\"size_formatted\": \"1.00 KiB\""), std::string::npos);
}

TEST_F(InspectorTest, V1TorrentHashLength)
{
    TorrentInspector inspector(torrent_path_.string());
    TorrentMetadata meta = inspector.inspect();

    EXPECT_FALSE(meta.info_hash_v1.empty());
    EXPECT_EQ(meta.info_hash_v1.size(), 40);
}

TEST(UtilsTest, UrlEncodeBasic)
{
    EXPECT_EQ(utils::url_encode("hello world"), "hello%20world");
    EXPECT_EQ(utils::url_encode("test@email"), "test%40email");
    EXPECT_EQ(utils::url_encode(""), "");
}

TEST(UtilsTest, UrlEncodeSpecialChars)
{
    EXPECT_EQ(utils::url_encode("a/b/c"), "a%2Fb%2Fc");
    EXPECT_EQ(utils::url_encode("a&b=c"), "a%26b%3Dc");
    EXPECT_EQ(utils::url_encode("a?b=c"), "a%3Fb%3Dc");
}

TEST(UtilsTest, EscapeJsonBasic)
{
    EXPECT_EQ(utils::escape_json("Hello \"World\""), "Hello \\\"World\\\"");
    EXPECT_EQ(utils::escape_json("Back\\slash"), "Back\\\\slash");
    EXPECT_EQ(utils::escape_json(""), "");
}

TEST(UtilsTest, EscapeJsonNewline)
{
    EXPECT_EQ(utils::escape_json("Line1\nLine2"), "Line1\\nLine2");
    EXPECT_EQ(utils::escape_json("Tab\there"), "Tab\\there");
    EXPECT_EQ(utils::escape_json("Carriage\rreturn"), "Carriage\\rreturn");
}

TEST(UtilsTest, EscapeJsonNonAscii)
{
    std::string input;
    input.push_back(static_cast<char>(0x80));
    input.push_back(static_cast<char>(0xFF));
    std::string output = utils::escape_json(input);

    EXPECT_NE(output.find("\\u0080"), std::string::npos);
    EXPECT_NE(output.find("\\u00FF"), std::string::npos);
}

TEST(UtilsTest, FormatFileSize)
{
    EXPECT_EQ(utils::format_file_size(0), "0.00 B");
    EXPECT_EQ(utils::format_file_size(1024), "1.00 KiB");
    EXPECT_EQ(utils::format_file_size(1048576), "1.00 MiB");
    EXPECT_EQ(utils::format_file_size(1073741824), "1.00 GiB");
    EXPECT_EQ(utils::format_file_size(1099511627776), "1.00 TiB");
}

TEST(UtilsTest, FormatTimestampValid)
{
    std::string result = utils::format_timestamp(1640995200);
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result, "Invalid timestamp");
}

TEST(UtilsTest, FormatTimestampInvalid)
{
    EXPECT_EQ(utils::format_timestamp(-1), "Invalid timestamp");
    EXPECT_EQ(utils::format_timestamp(0), "Invalid timestamp");
}

TEST(UtilsTest, ToLower)
{
    EXPECT_EQ(utils::to_lower("Hello World"), "hello world");
    EXPECT_EQ(utils::to_lower("UPPERCASE"), "uppercase");
    EXPECT_EQ(utils::to_lower("MiXeD CaSe"), "mixed case");
    EXPECT_EQ(utils::to_lower(""), "");
}

TEST(UtilsTest, StartsWith)
{
    EXPECT_TRUE(utils::starts_with("hello world", "hello"));
    EXPECT_FALSE(utils::starts_with("hello world", "world"));
    EXPECT_TRUE(utils::starts_with("", ""));
    EXPECT_TRUE(utils::starts_with("test", ""));
}

TEST(UtilsTest, Join)
{
    std::vector<std::string> parts = {"a", "b", "c"};
    EXPECT_EQ(utils::join(parts, ", "), "a, b, c");
    EXPECT_EQ(utils::join({}, ", "), "");
    EXPECT_EQ(utils::join({"single"}, ", "), "single");
}

TEST(UtilsTest, Split)
{
    std::vector<std::string> result = utils::split("a,b,c", ',');
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "c");

    result = utils::split("single", ',');
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], "single");

    result = utils::split("", ',');
    EXPECT_TRUE(result.empty());
}

TEST(UtilsTest, IsValidUrl)
{
    EXPECT_TRUE(utils::is_valid_url("http://example.com"));
    EXPECT_TRUE(utils::is_valid_url("https://example.com"));
    EXPECT_TRUE(utils::is_valid_url("udp://tracker.example.com:80/announce"));
    EXPECT_FALSE(utils::is_valid_url("invalid"));
    EXPECT_FALSE(utils::is_valid_url("ftp://example.com"));
}
