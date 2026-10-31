#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <random>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/entry.hpp>
#include "torrent_inspector.hpp"
#include "torrent_creator.hpp"
#include "output_sink.hpp"
#include "utils.hpp"

namespace fs = std::filesystem;

namespace
{
class NoopOutputSink : public OutputSink
{
  public:
    void info(const std::string &) override {}
    void verbose(const std::string &) override {}
    void error(const std::string &) override {}
    void progress(const ProgressInfo &) override {}
};

std::string replace_leaf(const std::string &path, const std::string &leaf)
{
    const auto separator = path.find_last_of("/\\");
    if (separator == std::string::npos)
    {
        return leaf;
    }
    return path.substr(0, separator + 1) + leaf;
}
} // namespace

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
        ct.set_hash(lt::piece_index_t(0), piece_hash);
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

TEST_F(InspectorTest, FormatMetadataWithSourceAndEntropy)
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
    meta.source = "PTP";
    meta.entropy = "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2";

    std::string text_output = TorrentInspector::format_metadata(meta, false);
    EXPECT_NE(text_output.find("Source: PTP"), std::string::npos);
    EXPECT_NE(text_output.find("Entropy:"), std::string::npos);

    std::string json_output = TorrentInspector::format_metadata(meta, true);
    EXPECT_NE(json_output.find("\"source\": \"PTP\""), std::string::npos);
    EXPECT_NE(json_output.find("\"entropy\":"), std::string::npos);
}

TEST_F(InspectorTest, FormatMetadataWithoutSourceAndEntropy)
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

    std::string text_output = TorrentInspector::format_metadata(meta, false);
    EXPECT_EQ(text_output.find("Source:"), std::string::npos);
    EXPECT_EQ(text_output.find("Entropy:"), std::string::npos);

    std::string json_output = TorrentInspector::format_metadata(meta, true);
    EXPECT_EQ(json_output.find("\"source\":"), std::string::npos);
    EXPECT_EQ(json_output.find("\"entropy\":"), std::string::npos);
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

TEST(TorrentInspectorHybridPadTest, InspectShowsRawPadPaths)
{
    const fs::path temp_dir = fs::temp_directory_path() /
                              ("tb_inspector_pad_" + std::to_string(std::random_device{}()));
    fs::create_directories(temp_dir);

    struct TempDirGuard
    {
        fs::path path;
        ~TempDirGuard()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    } guard{temp_dir};

    const fs::path source_dir = temp_dir / "Show Pads";
    fs::create_directories(source_dir);
    {
        std::ofstream a(source_dir / "a.txt", std::ios::binary);
        a << "AA";
        std::ofstream b(source_dir / "b.txt", std::ios::binary);
        b << "BB";
    }

    const fs::path torrent_path = temp_dir / "show.torrent";
    NoopOutputSink sink;
    TorrentConfig config(source_dir, torrent_path, {}, TorrentVersion::HYBRID);
    config.silent = true;
    TorrentCreator creator(std::move(config), sink);
    creator.create_torrent();
    ASSERT_TRUE(fs::exists(torrent_path));

    std::ifstream input(torrent_path, std::ios::binary);
    std::vector<char> buffer((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    lt::entry root = lt::bdecode(lt::span<const char>(buffer.data(), buffer.size()));
    const lt::entry *info = root.find_key("info");
    ASSERT_NE(info, nullptr);
    const lt::entry *raw_files = info->find_key("files");
    ASSERT_NE(raw_files, nullptr);

    lt::torrent_info torrent_info(lt::span<const char>(buffer.data(), buffer.size()),
                                  lt::from_span);
    const auto &parsed_files = torrent_info.files();
    ASSERT_EQ(raw_files->list().size(),
              static_cast<size_t>(parsed_files.num_files()));

    TorrentInspector inspector(torrent_path);
    TorrentMetadata meta = inspector.inspect();
    ASSERT_EQ(meta.files.size(), parsed_files.num_files());

    std::vector<std::string> expected_pad_paths;
    bool has_suffixed_pad = false;
    for (size_t i = 0; i < raw_files->list().size(); ++i)
    {
        const auto &raw_file = raw_files->list()[i];
        ASSERT_EQ(raw_file.type(), lt::entry::dictionary_t);
        const lt::entry *attr = raw_file.find_key("attr");
        const lt::entry *path = raw_file.find_key("path");
        ASSERT_NE(path, nullptr);
        ASSERT_EQ(path->type(), lt::entry::list_t);
        ASSERT_FALSE(path->list().empty());

        const auto idx = lt::file_index_t{static_cast<int>(i)};
        const std::string normalized_path = parsed_files.file_path(idx);
        const std::string leaf = path->list().back().string();
        const bool is_pad = attr != nullptr && attr->type() == lt::entry::string_t &&
                            attr->string().find('p') != std::string::npos;
        if (!is_pad)
        {
            EXPECT_EQ(meta.files[i].path, normalized_path);
            continue;
        }

        const std::string expected = replace_leaf(normalized_path, leaf);
        EXPECT_EQ(meta.files[i].path, expected);
        expected_pad_paths.push_back(expected);
        if (leaf.find('.') != std::string::npos)
        {
            has_suffixed_pad = true;
        }
    }

    ASSERT_GE(expected_pad_paths.size(), 2u);
    ASSERT_TRUE(has_suffixed_pad) << "fixture must contain duplicate-size pads";

    std::set<std::string> inspected_paths;
    for (const auto &file : meta.files)
    {
        EXPECT_TRUE(inspected_paths.insert(file.path).second)
            << "duplicate inspected path: " << file.path;
    }

    const std::string text = TorrentInspector::format_file_tree(meta, false);
    const std::string json = TorrentInspector::format_file_tree(meta, true);
    for (const auto &expected : expected_pad_paths)
    {
        EXPECT_NE(inspected_paths.find(expected), inspected_paths.end())
            << "missing raw pad path: " << expected;
        EXPECT_NE(text.find(expected), std::string::npos);
        EXPECT_NE(json.find("\"" + expected + "\""), std::string::npos);
    }
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
