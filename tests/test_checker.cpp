#include "portable.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include "torrent_checker.hpp"
#include "utils.hpp"

namespace fs = std::filesystem;

class CheckerTest : public ::testing::Test
{
  protected:
    fs::path temp_dir_;
    fs::path content_dir_;
    fs::path torrent_path_;

    void SetUp() override
    {
        temp_dir_ = fs::temp_directory_path() / ("torrent_checker_test_" + std::to_string(portable_getpid()));
        content_dir_ = temp_dir_ / "content";
        fs::create_directories(content_dir_);
        torrent_path_ = temp_dir_ / "test.torrent";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    void create_file(const fs::path &path, const std::string &content)
    {
        fs::create_directories(path.parent_path());
        std::ofstream f(path, std::ios::binary);
        f << content;
        f.close();
    }

    void create_single_file_torrent(const std::string &filename, const std::string &content,
                                     int piece_size = 16384)
    {
        create_file(content_dir_ / filename, content);

        lt::file_storage file_storage;
        file_storage.add_file(filename, static_cast<int64_t>(content.size()));
        lt::create_torrent ct(file_storage, piece_size, lt::create_torrent::v1_only);

        lt::span<char const> data(content.data(), content.size());
        auto hash = lt::hasher(data).final();
        ct.set_hash(0, hash);

        lt::entry e = ct.generate();
        std::vector<char> buffer;
        lt::bencode(std::back_inserter(buffer), e);

        std::ofstream torrent(torrent_path_, std::ios::binary);
        torrent.write(buffer.data(), buffer.size());
        torrent.close();
    }

    void create_multi_file_torrent(const std::vector<std::pair<std::string, std::string>> &files,
                                    int piece_size = 16384)
    {
        lt::file_storage file_storage;
        std::string all_content;
        for (const auto &[name, content] : files)
        {
            std::string full_path = std::string("test_torrent/") + name;
            create_file(content_dir_ / "test_torrent" / name, content);
            file_storage.add_file(full_path, static_cast<int64_t>(content.size()));
            all_content += content;
        }

        lt::create_torrent ct(file_storage, piece_size, lt::create_torrent::v1_only);

        for (int i = 0; i < ct.num_pieces(); ++i)
        {
            int64_t piece_start = static_cast<int64_t>(i) * piece_size;
            int64_t piece_end = std::min(piece_start + piece_size, static_cast<int64_t>(all_content.size()));
            int64_t sz = piece_end - piece_start;
            if (sz <= 0)
                break;

            lt::span<char const> piece_data(all_content.data() + piece_start, static_cast<std::size_t>(sz));
            auto hash = lt::hasher(piece_data).final();
            ct.set_hash(i, hash);
        }

        lt::entry e = ct.generate();
        std::vector<char> buffer;
        lt::bencode(std::back_inserter(buffer), e);

        std::ofstream torrent(torrent_path_, std::ios::binary);
        torrent.write(buffer.data(), buffer.size());
        torrent.close();
    }
};

TEST_F(CheckerTest, CheckPassesForValidFiles)
{
    create_single_file_torrent("test_file.txt", "Hello World");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_TRUE(result.passed);
    EXPECT_EQ(result.missing_files.size(), 0u);
    EXPECT_EQ(result.corrupted_pieces.size(), 0u);
    EXPECT_DOUBLE_EQ(result.completion_percentage, 100.0);
    EXPECT_EQ(result.pieces_total, 1);
    EXPECT_EQ(result.pieces_verified, 1);
    EXPECT_EQ(result.pieces_corrupted, 0);
}

TEST_F(CheckerTest, CheckFailsForMissingFile)
{
    create_single_file_torrent("test_file.txt", "Hello World");
    fs::remove(content_dir_ / "test_file.txt");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_FALSE(result.passed);
    EXPECT_EQ(result.missing_files.size(), 1u);
    EXPECT_EQ(result.missing_files[0].path, "test_file.txt");
}

TEST_F(CheckerTest, CheckFailsForCorruptedPiece)
{
    create_single_file_torrent("test_file.txt", "Hello World");

    std::ofstream f(content_dir_ / "test_file.txt", std::ios::binary);
    f << "Goodbye World";
    f.close();

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_FALSE(result.passed);
    EXPECT_EQ(result.corrupted_pieces.size(), 1u);
    EXPECT_EQ(result.corrupted_pieces[0].index, 0);
    EXPECT_LT(result.completion_percentage, 100.0);
}

TEST_F(CheckerTest, CheckDetectsExtraFiles)
{
    create_single_file_torrent("test_file.txt", "Hello World");
    create_file(content_dir_ / "extra.txt", "extra content");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_EQ(result.extra_files.size(), 1u);
    EXPECT_EQ(result.extra_files[0].path, "extra.txt");
    EXPECT_EQ(result.extra_files[0].size, 13);
}

TEST_F(CheckerTest, CheckReportsCompletionPercentage)
{
    create_single_file_torrent("test_file.txt", "Hello World");
    std::ofstream f(content_dir_ / "test_file.txt", std::ios::binary);
    f << "Goodbye World";
    f.close();

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_DOUBLE_EQ(result.completion_percentage, 0.0);
    EXPECT_EQ(result.pieces_total, 1);
    EXPECT_EQ(result.pieces_verified, 0);
}

TEST_F(CheckerTest, CheckHandlesNonexistentTorrent)
{
    EXPECT_THROW(
        {
            TorrentChecker checker(temp_dir_ / "nonexistent.torrent");
            checker.check(content_dir_);
        },
        std::runtime_error);
}

TEST_F(CheckerTest, CheckHandlesCorruptedTorrent)
{
    create_file(torrent_path_, "not a torrent file");

    EXPECT_THROW(
        {
            TorrentChecker checker(torrent_path_);
            checker.check(content_dir_);
        },
        std::runtime_error);
}

TEST_F(CheckerTest, FormatResultJsonContainsAllFields)
{
    CheckResult result;
    result.passed = true;
    result.completion_percentage = 100.0;
    result.total_size_expected = 1024;
    result.total_size_verified = 1024;
    result.pieces_total = 1;
    result.pieces_verified = 1;
    result.pieces_corrupted = 0;

    std::string json = TorrentChecker::format_result(result, true);

    EXPECT_NE(json.find("\"status\": \"PASS\""), std::string::npos);
    EXPECT_NE(json.find("\"completion_percentage\""), std::string::npos);
    EXPECT_NE(json.find("\"total\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"verified\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"corrupted\": 0"), std::string::npos);
    EXPECT_NE(json.find("\"expected\": 1024"), std::string::npos);
    EXPECT_NE(json.find("\"missing_files\": ["), std::string::npos);
    EXPECT_NE(json.find("\"extra_files\": ["), std::string::npos);
}

TEST_F(CheckerTest, FormatResultJsonWithFailures)
{
    CheckResult result;
    result.passed = false;
    result.completion_percentage = 50.0;
    result.pieces_total = 2;
    result.pieces_verified = 1;
    result.pieces_corrupted = 1;
    result.total_size_expected = 2048;
    result.total_size_verified = 1024;

    CheckResult::MissingFile mf;
    mf.path = "missing.bin";
    mf.expected_size = 512;
    result.missing_files.push_back(mf);

    CheckResult::CorruptedPiece cp;
    cp.index = 1;
    cp.offset = 16384;
    result.corrupted_pieces.push_back(cp);

    CheckResult::ExtraFile ef;
    ef.path = "extra.txt";
    ef.size = 64;
    result.extra_files.push_back(ef);

    std::string json = TorrentChecker::format_result(result, true);

    EXPECT_NE(json.find("\"status\": \"FAIL\""), std::string::npos);
    EXPECT_NE(json.find("\"path\": \"missing.bin\""), std::string::npos);
    EXPECT_NE(json.find("\"index\": 1, \"offset\": 16384"), std::string::npos);
    EXPECT_NE(json.find("\"path\": \"extra.txt\""), std::string::npos);
}

TEST_F(CheckerTest, FormatResultHumanReadable)
{
    CheckResult result;
    result.passed = true;
    result.completion_percentage = 100.0;
    result.pieces_total = 1;
    result.pieces_verified = 1;
    result.pieces_corrupted = 0;
    result.total_size_expected = 11;
    result.total_size_verified = 11;

    std::string text = TorrentChecker::format_result(result, false);

    EXPECT_NE(text.find("PASS"), std::string::npos);
    EXPECT_NE(text.find("100.0%"), std::string::npos);
    EXPECT_NE(text.find("1 / 1 verified"), std::string::npos);
}

TEST_F(CheckerTest, FormatResultHumanReadableWithFailures)
{
    CheckResult result;
    result.passed = false;
    result.completion_percentage = 50.0;
    result.pieces_total = 2;
    result.pieces_verified = 1;
    result.pieces_corrupted = 1;
    result.total_size_expected = 2048;
    result.total_size_verified = 1024;

    CheckResult::MissingFile mf;
    mf.path = "missing.bin";
    mf.expected_size = 512;
    result.missing_files.push_back(mf);

    CheckResult::CorruptedPiece cp;
    cp.index = 1;
    cp.offset = 16384;
    result.corrupted_pieces.push_back(cp);

    CheckResult::ExtraFile ef;
    ef.path = "notes.txt";
    ef.size = 128;
    result.extra_files.push_back(ef);

    std::string text = TorrentChecker::format_result(result, false);

    EXPECT_NE(text.find("FAIL"), std::string::npos);
    EXPECT_NE(text.find("Missing files:"), std::string::npos);
    EXPECT_NE(text.find("missing.bin"), std::string::npos);
    EXPECT_NE(text.find("Corrupted pieces:"), std::string::npos);
    EXPECT_NE(text.find("Piece #1"), std::string::npos);
    EXPECT_NE(text.find("Extra files:"), std::string::npos);
    EXPECT_NE(text.find("notes.txt"), std::string::npos);
}

TEST_F(CheckerTest, MultiFileTorrentCheckPasses)
{
    create_multi_file_torrent({
        {"file1.txt", "Content of file 1"},
        {"file2.txt", "Content of file 2"},
    });

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_TRUE(result.passed);
    EXPECT_EQ(result.missing_files.size(), 0u);
    EXPECT_EQ(result.corrupted_pieces.size(), 0u);
}

TEST_F(CheckerTest, MultiFileTorrentMissingOneFile)
{
    create_multi_file_torrent({
        {"file1.txt", "Content of file 1"},
        {"file2.txt", "Content of file 2"},
    });
    fs::remove(content_dir_ / "test_torrent" / "file2.txt");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_FALSE(result.passed);
    EXPECT_GE(result.missing_files.size(), 1u);
}

TEST_F(CheckerTest, FileSizeMismatch)
{
    create_single_file_torrent("test_file.txt", "Hello World");

    std::ofstream f(content_dir_ / "test_file.txt", std::ios::binary);
    f << "Hi";
    f.close();

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_FALSE(result.passed);
    EXPECT_EQ(result.pieces_corrupted, 1);
}

TEST_F(CheckerTest, ExtraFilesInSubdirectory)
{
    create_single_file_torrent("test_file.txt", "Hello World");
    fs::create_directories(content_dir_ / "subdir");
    create_file(content_dir_ / "subdir" / "extra.bin", "binary data");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_EQ(result.extra_files.size(), 1u);
    EXPECT_NE(result.extra_files[0].path.find("extra.bin"), std::string::npos);
}

TEST_F(CheckerTest, NoExtraFilesWhenContentClean)
{
    create_single_file_torrent("test_file.txt", "Hello World");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_EQ(result.extra_files.size(), 0u);
}

TEST_F(CheckerTest, NoExtraFilesFromMultiFileTorrent)
{
    create_multi_file_torrent({
        {"file1.txt", "Content A"},
        {"file2.txt", "Content B"},
    });

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_EQ(result.extra_files.size(), 0u);
}

class V2CheckerTest : public ::testing::Test
{
  protected:
    fs::path temp_dir_;
    fs::path content_dir_;
    fs::path torrent_path_;

    void SetUp() override
    {
        temp_dir_ = fs::temp_directory_path() / ("torrent_checker_v2_" + std::to_string(portable_getpid()));
        content_dir_ = temp_dir_ / "content";
        fs::create_directories(content_dir_);
        torrent_path_ = temp_dir_ / "test.torrent";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    void create_file(const fs::path &path, const std::string &content)
    {
        fs::create_directories(path.parent_path());
        std::ofstream f(path, std::ios::binary);
        f << content;
        f.close();
    }

    void create_v2_single_file_torrent(const std::string &filename, const std::string &content,
                                        int piece_size = 16384)
    {
        create_file(content_dir_ / filename, content);

        lt::file_storage file_storage;
        file_storage.add_file(filename, static_cast<int64_t>(content.size()));
        lt::create_torrent ct(file_storage, piece_size, lt::create_torrent::v2_only);

        lt::set_piece_hashes(ct, content_dir_.string());

        lt::entry e = ct.generate();
        std::vector<char> buffer;
        lt::bencode(std::back_inserter(buffer), e);

        std::ofstream torrent(torrent_path_, std::ios::binary);
        torrent.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        torrent.close();
    }

    void create_v2_multi_file_torrent(const std::vector<std::pair<std::string, std::string>> &files,
                                       int piece_size = 16384)
    {
        for (const auto &[name, content] : files)
        {
            create_file(content_dir_ / name, content);
        }

        lt::file_storage file_storage;
        lt::add_files(file_storage, content_dir_.string());

        lt::create_torrent ct(file_storage, piece_size, lt::create_torrent::v2_only);
        lt::set_piece_hashes(ct, content_dir_.parent_path().string());

        lt::entry e = ct.generate();
        std::vector<char> buffer;
        lt::bencode(std::back_inserter(buffer), e);

        std::ofstream torrent(torrent_path_, std::ios::binary);
        torrent.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        torrent.close();
    }
};

TEST_F(V2CheckerTest, V2SingleFileCheckPasses)
{
    create_v2_single_file_torrent("test_file.txt", "Hello World v2");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_TRUE(result.passed);
    EXPECT_EQ(result.missing_files.size(), 0u);
    EXPECT_EQ(result.corrupted_pieces.size(), 0u);
    EXPECT_DOUBLE_EQ(result.completion_percentage, 100.0);
}

TEST_F(V2CheckerTest, V2DetectsCorruption)
{
    create_v2_single_file_torrent("test_file.txt", "Hello World v2");

    std::ofstream f(content_dir_ / "test_file.txt", std::ios::binary);
    f << "Corrupted v2 data";
    f.close();

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_FALSE(result.passed);
    EXPECT_GE(result.corrupted_pieces.size(), 1u);
}

TEST_F(V2CheckerTest, V2DetectsMissingFile)
{
    create_v2_single_file_torrent("test_file.txt", "Hello World v2");
    fs::remove(content_dir_ / "test_file.txt");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_FALSE(result.passed);
    EXPECT_EQ(result.missing_files.size(), 1u);
}

TEST_F(V2CheckerTest, V2MultiFileCheckPasses)
{
    create_v2_multi_file_torrent({
        {"file1.txt", "Content of file 1 v2"},
        {"file2.txt", "Content of file 2 v2"},
    });

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_.parent_path());

    EXPECT_TRUE(result.passed);
    EXPECT_EQ(result.missing_files.size(), 0u);
    EXPECT_EQ(result.corrupted_pieces.size(), 0u);
}

TEST_F(V2CheckerTest, V2DetectsExtraFiles)
{
    create_v2_single_file_torrent("test_file.txt", "Hello World v2");
    create_file(content_dir_ / "extra.txt", "extra content");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_EQ(result.extra_files.size(), 1u);
    EXPECT_EQ(result.extra_files[0].path, "extra.txt");
}

class HybridCheckerTest : public ::testing::Test
{
  protected:
    fs::path temp_dir_;
    fs::path content_dir_;
    fs::path torrent_path_;

    void SetUp() override
    {
        temp_dir_ = fs::temp_directory_path() / ("torrent_checker_hybrid_" + std::to_string(portable_getpid()));
        content_dir_ = temp_dir_ / "content";
        fs::create_directories(content_dir_);
        torrent_path_ = temp_dir_ / "test.torrent";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    void create_file(const fs::path &path, const std::string &content)
    {
        fs::create_directories(path.parent_path());
        std::ofstream f(path, std::ios::binary);
        f << content;
        f.close();
    }

    void create_hybrid_single_file_torrent(const std::string &filename, const std::string &content,
                                            int piece_size = 16384)
    {
        create_file(content_dir_ / filename, content);

        lt::file_storage file_storage;
        file_storage.add_file(filename, static_cast<int64_t>(content.size()));
        lt::create_torrent ct(file_storage, piece_size);

        lt::set_piece_hashes(ct, content_dir_.string());

        lt::entry e = ct.generate();
        std::vector<char> buffer;
        lt::bencode(std::back_inserter(buffer), e);

        std::ofstream torrent(torrent_path_, std::ios::binary);
        torrent.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        torrent.close();
    }
};

TEST_F(HybridCheckerTest, HybridSingleFileCheckPasses)
{
    create_hybrid_single_file_torrent("test_file.txt", "Hello Hybrid World");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_TRUE(result.passed);
    EXPECT_EQ(result.missing_files.size(), 0u);
    EXPECT_EQ(result.corrupted_pieces.size(), 0u);
    EXPECT_DOUBLE_EQ(result.completion_percentage, 100.0);
}

TEST_F(HybridCheckerTest, HybridDetectsCorruption)
{
    create_hybrid_single_file_torrent("test_file.txt", "Hello Hybrid World");

    std::ofstream f(content_dir_ / "test_file.txt", std::ios::binary);
    f << "Corrupted hybrid data";
    f.close();

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_FALSE(result.passed);
    EXPECT_GE(result.corrupted_pieces.size(), 1u);
}

TEST_F(HybridCheckerTest, HybridDetectsMissingFile)
{
    create_hybrid_single_file_torrent("test_file.txt", "Hello Hybrid World");
    fs::remove(content_dir_ / "test_file.txt");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_FALSE(result.passed);
    EXPECT_EQ(result.missing_files.size(), 1u);
}

class HybridMultiFileCheckerTest : public ::testing::Test
{
  protected:
    fs::path temp_dir_;
    fs::path content_dir_;
    fs::path torrent_path_;

    void SetUp() override
    {
        temp_dir_ = fs::temp_directory_path() / ("torrent_checker_hybrid_multi_" + std::to_string(portable_getpid()));
        content_dir_ = temp_dir_ / "content";
        fs::create_directories(content_dir_);
        torrent_path_ = temp_dir_ / "test.torrent";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    void create_file(const fs::path &path, const std::string &content)
    {
        fs::create_directories(path.parent_path());
        std::ofstream f(path, std::ios::binary);
        f << content;
        f.close();
    }

    void create_hybrid_multi_file_torrent(const std::vector<std::pair<std::string, std::string>> &files,
                                           int piece_size = 16384)
    {
        for (const auto &[name, content] : files)
        {
            create_file(content_dir_ / name, content);
        }

        lt::file_storage file_storage;
        lt::add_files(file_storage, content_dir_.string());

        lt::create_torrent ct(file_storage, piece_size);
        lt::set_piece_hashes(ct, content_dir_.parent_path().string());

        lt::entry e = ct.generate();
        std::vector<char> buffer;
        lt::bencode(std::back_inserter(buffer), e);

        std::ofstream torrent(torrent_path_, std::ios::binary);
        torrent.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        torrent.close();
    }
};

TEST_F(HybridMultiFileCheckerTest, HybridMultiFileCheckPasses)
{
    create_hybrid_multi_file_torrent({
        {"file1.txt", "Hybrid multi file 1"},
        {"file2.txt", "Hybrid multi file 2"},
    });

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_.parent_path());

    EXPECT_TRUE(result.passed);
    EXPECT_EQ(result.missing_files.size(), 0u);
    EXPECT_EQ(result.corrupted_pieces.size(), 0u);
}

TEST_F(HybridMultiFileCheckerTest, HybridMultiFileDetectsCorruption)
{
    create_hybrid_multi_file_torrent({
        {"file1.txt", "Hybrid multi file 1"},
        {"file2.txt", "Hybrid multi file 2"},
    });

    std::ofstream f(content_dir_ / "file1.txt", std::ios::binary);
    f << "Corrupted data!!";
    f.close();

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_.parent_path());

    EXPECT_FALSE(result.passed);
    EXPECT_GE(result.corrupted_pieces.size(), 1u);
}

TEST_F(CheckerTest, SymlinksAreSkippedAsExtraFiles)
{
#ifdef _WIN32
    GTEST_SKIP() << "Symlinks are not supported on Windows";
#endif
    create_single_file_torrent("test_file.txt", "Hello World");
    create_file(content_dir_ / "real_extra.txt", "extra content");
    fs::create_directory_symlink(content_dir_, content_dir_ / "symlink_dir");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_EQ(result.extra_files.size(), 1u);
    EXPECT_EQ(result.extra_files[0].path, "real_extra.txt");
}

TEST_F(CheckerTest, FileResultFieldsArePopulated)
{
    create_single_file_torrent("test_file.txt", "Hello World");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    ASSERT_EQ(result.file_results.size(), 1u);
    const auto &fr = result.file_results[0];
    EXPECT_EQ(fr.path, "test_file.txt");
    EXPECT_EQ(fr.expected_size, 11);
    EXPECT_EQ(fr.actual_size, 11);
    EXPECT_TRUE(fr.exists);
    EXPECT_TRUE(fr.size_matches);
    EXPECT_EQ(result.total_size_verified, 11);
}

TEST_F(CheckerTest, FileResultFieldsForMissingFile)
{
    create_single_file_torrent("test_file.txt", "Hello World");
    fs::remove(content_dir_ / "test_file.txt");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    ASSERT_EQ(result.file_results.size(), 1u);
    const auto &fr = result.file_results[0];
    EXPECT_EQ(fr.expected_size, 11);
    EXPECT_EQ(fr.actual_size, 0);
    EXPECT_FALSE(fr.exists);
    EXPECT_FALSE(fr.size_matches);
    EXPECT_EQ(result.total_size_verified, 0);
}

TEST_F(CheckerTest, ReadPieceDataHandlesFileOpenFailure)
{
#ifdef _WIN32
    GTEST_SKIP() << "Unix file permissions are not enforced on Windows";
#endif
    create_single_file_torrent("test_file.txt", "Hello World");

    std::error_code perm_ec;
    fs::permissions(content_dir_ / "test_file.txt",
                    fs::perms::none, perm_ec);

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    std::error_code restore_ec;
    fs::permissions(content_dir_ / "test_file.txt",
                    fs::perms::owner_read | fs::perms::owner_write, restore_ec);

    EXPECT_FALSE(result.passed);
}

TEST_F(CheckerTest, EmptyContentDirReportsMissingFiles)
{
    create_single_file_torrent("test_file.txt", "Hello World");
    fs::remove(content_dir_ / "test_file.txt");

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_FALSE(result.passed);
    EXPECT_EQ(result.missing_files.size(), 1u);
    EXPECT_EQ(result.corrupted_pieces.size(), 1u);
    EXPECT_DOUBLE_EQ(result.completion_percentage, 0.0);
}

TEST_F(CheckerTest, MultiFilePadFileSkip)
{
    std::string large_content(16384 * 2, 'A');
    create_multi_file_torrent({
        {"file1.txt", large_content},
        {"file2.txt", std::string(100, 'B')},
    }, 16384);

    TorrentChecker checker(torrent_path_);
    CheckResult result = checker.check(content_dir_);

    EXPECT_TRUE(result.passed) << "Valid files should pass";
    for (const auto &fr : result.file_results)
    {
        EXPECT_EQ(fr.path.find("__pad_"), std::string::npos)
            << "Pad files should not appear in file_results: " << fr.path;
    }
}
