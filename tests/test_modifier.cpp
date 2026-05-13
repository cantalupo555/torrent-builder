#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/hash_picker.hpp>
#include "torrent_modifier.hpp"
#include "torrent_inspector.hpp"
#include "utils.hpp"

namespace fs = std::filesystem;

class ModifierTest : public ::testing::Test
{
  protected:
    fs::path test_dir_;
    fs::path test_file_;
    fs::path torrent_path_;

    void SetUp() override
    {
        test_dir_ = fs::current_path() / "modifier_test_tmp";
        fs::create_directories(test_dir_);
        test_file_ = test_dir_ / "test_file.txt";
        create_test_file();
        torrent_path_ = test_dir_ / "test.torrent";
        create_simple_torrent();
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(test_dir_, ec);
    }

    void create_test_file()
    {
        std::ofstream file(test_file_);
        file << "Hello World - test content for torrent modifier";
        file.close();
    }

    void create_simple_torrent(const std::string &tracker = "udp://tracker.example.com:80/announce")
    {
        create_test_file();

        lt::file_storage fs;
        fs.add_file("test_file.txt", static_cast<std::int64_t>(std::filesystem::file_size(test_file_)));
        lt::create_torrent ct(fs, 16384, lt::create_torrent::v1_only);

        std::ifstream data_file(test_file_, std::ios::binary);
        std::vector<char> file_data((std::istreambuf_iterator<char>(data_file)),
                                     std::istreambuf_iterator<char>());
        auto piece_hash = lt::hasher(lt::span<char const>(file_data.data(), file_data.size())).final();
        ct.set_hash(0, piece_hash);

        if (!tracker.empty())
        {
            ct.add_tracker(tracker);
        }

        lt::entry e = ct.generate();
        std::vector<char> buffer;
        lt::bencode(std::back_inserter(buffer), e);

        std::ofstream torrent(torrent_path_, std::ios::binary);
        torrent.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        torrent.close();
    }

    TorrentMetadata read_torrent_metadata(const fs::path &path)
    {
        TorrentInspector inspector(path.string());
        return inspector.inspect();
    }

    std::vector<char> read_file_bytes(const fs::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<char>((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
    }
};

TEST_F(ModifierTest, TrackerReplace)
{
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.trackers = {"https://new-tracker.example.com/announce"};

    TorrentModifier modifier(config);
    modifier.modify();

    auto meta = read_torrent_metadata(config.output);
    ASSERT_EQ(meta.trackers.size(), 1u);
    EXPECT_EQ(meta.trackers[0], "https://new-tracker.example.com/announce");
}

TEST_F(ModifierTest, TrackerAdd)
{
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.add_trackers = {"https://new-tracker.example.com/announce"};

    TorrentModifier modifier(config);
    modifier.modify();

    auto meta = read_torrent_metadata(config.output);
    EXPECT_GE(meta.trackers.size(), 2u);

    bool found_new = false;
    bool found_old = false;
    for (const auto &t : meta.trackers)
    {
        if (t == "https://new-tracker.example.com/announce")
            found_new = true;
        if (t == "udp://tracker.example.com:80/announce")
            found_old = true;
    }
    EXPECT_TRUE(found_new);
    EXPECT_TRUE(found_old);
}

TEST_F(ModifierTest, TrackerRemove)
{
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.remove_trackers = {"udp://tracker.example.com:80/announce"};

    TorrentModifier modifier(config);
    modifier.modify();

    auto meta = read_torrent_metadata(config.output);
    for (const auto &t : meta.trackers)
    {
        EXPECT_NE(t, "udp://tracker.example.com:80/announce");
    }
}

TEST_F(ModifierTest, TogglePrivateOn)
{
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.is_private = true;

    TorrentModifier modifier(config);
    modifier.modify();

    auto meta = read_torrent_metadata(config.output);
    EXPECT_TRUE(meta.is_private);
}

TEST_F(ModifierTest, TogglePrivateOff)
{
    {
        ModifyConfig setup_config;
        setup_config.input = torrent_path_;
        setup_config.output = torrent_path_;
        setup_config.is_private = true;
        TorrentModifier(setup_config).modify();
    }

    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.is_private = false;

    TorrentModifier modifier(config);
    modifier.modify();

    auto meta = read_torrent_metadata(config.output);
    EXPECT_FALSE(meta.is_private);
}

TEST_F(ModifierTest, SetSource)
{
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.source = "PTP";

    TorrentModifier modifier(config);
    modifier.modify();

    auto meta = read_torrent_metadata(config.output);
    ASSERT_TRUE(meta.source.has_value());
    EXPECT_EQ(meta.source.value(), "PTP");
}

TEST_F(ModifierTest, RemoveSource)
{
    {
        ModifyConfig setup_config;
        setup_config.input = torrent_path_;
        setup_config.output = torrent_path_;
        setup_config.source = "PTP";
        TorrentModifier(setup_config).modify();
    }

    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.source = "";

    TorrentModifier modifier(config);
    modifier.modify();

    auto meta = read_torrent_metadata(config.output);
    EXPECT_FALSE(meta.source.has_value());
}

TEST_F(ModifierTest, SetComment)
{
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.comment = "Updated comment";

    TorrentModifier modifier(config);
    modifier.modify();

    auto meta = read_torrent_metadata(config.output);
    ASSERT_TRUE(meta.comment.has_value());
    EXPECT_EQ(meta.comment.value(), "Updated comment");
}

TEST_F(ModifierTest, ChangeName)
{
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.name = "New Torrent Name";

    TorrentModifier modifier(config);
    modifier.modify();

    auto meta = read_torrent_metadata(config.output);
    EXPECT_EQ(meta.name, "New Torrent Name");
}

TEST_F(ModifierTest, EntropyChangesInfoHash)
{
    auto original_meta = read_torrent_metadata(torrent_path_);

    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.entropy = true;

    TorrentModifier modifier(config);
    modifier.modify();

    auto new_meta = read_torrent_metadata(config.output);

    if (!original_meta.info_hash_v1.empty() && !new_meta.info_hash_v1.empty())
    {
        EXPECT_NE(original_meta.info_hash_v1, new_meta.info_hash_v1);
    }
}

TEST_F(ModifierTest, DryRunNoFileWritten)
{
    fs::path output_path = test_dir_ / "should_not_exist.torrent";

    ModifyConfig config;
    config.input = torrent_path_;
    config.output = output_path;
    config.dry_run = true;
    config.source = "TEST";

    TorrentModifier modifier(config);
    modifier.modify();

    EXPECT_FALSE(fs::exists(output_path));
}

TEST_F(ModifierTest, InPlaceModify)
{
    auto original_bytes = read_file_bytes(torrent_path_);

    ModifyConfig config;
    config.input = torrent_path_;
    config.source = "INPLACE";

    TorrentModifier modifier(config);
    modifier.modify();

    auto meta = read_torrent_metadata(torrent_path_);
    ASSERT_TRUE(meta.source.has_value());
    EXPECT_EQ(meta.source.value(), "INPLACE");

    auto new_bytes = read_file_bytes(torrent_path_);
    EXPECT_NE(original_bytes.size(), 0u);
    EXPECT_NE(original_bytes, new_bytes);
}

TEST_F(ModifierTest, OutputToDifferentFile)
{
    auto original_bytes = read_file_bytes(torrent_path_);
    fs::path output_path = test_dir_ / "modified.torrent";

    ModifyConfig config;
    config.input = torrent_path_;
    config.output = output_path;
    config.source = "NEWFILE";

    TorrentModifier modifier(config);
    modifier.modify();

    EXPECT_TRUE(fs::exists(output_path));

    auto original_meta = read_torrent_metadata(torrent_path_);
    EXPECT_FALSE(original_meta.source.has_value());

    auto new_meta = read_torrent_metadata(output_path);
    ASSERT_TRUE(new_meta.source.has_value());
    EXPECT_EQ(new_meta.source.value(), "NEWFILE");
}

TEST_F(ModifierTest, InvalidInputThrows)
{
    ModifyConfig config;
    config.input = test_dir_ / "nonexistent.torrent";
    config.source = "TEST";

    TorrentModifier modifier(config);
    EXPECT_THROW(modifier.modify(), std::runtime_error);
}

TEST_F(ModifierTest, RemoveAllTrackers)
{
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.remove_trackers = {"udp://tracker.example.com:80/announce"};

    TorrentModifier modifier(config);
    modifier.modify();

    auto new_bytes = read_file_bytes(test_dir_ / "modified.torrent");
    lt::entry root = lt::bdecode(lt::span<const char>(new_bytes.data(), new_bytes.size()));

    EXPECT_EQ(root.find_key("announce"), nullptr);
    EXPECT_EQ(root.find_key("announce-list"), nullptr);
}

TEST_F(ModifierTest, PreservesFileData)
{
    auto original_meta = read_torrent_metadata(torrent_path_);

    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.source = "TEST";

    TorrentModifier modifier(config);
    modifier.modify();

    auto new_meta = read_torrent_metadata(config.output);
    EXPECT_EQ(original_meta.total_size, new_meta.total_size);
    EXPECT_EQ(original_meta.piece_length, new_meta.piece_length);
    EXPECT_EQ(original_meta.piece_count, new_meta.piece_count);
    EXPECT_EQ(original_meta.files.size(), new_meta.files.size());
    if (!original_meta.files.empty() && !new_meta.files.empty())
    {
        EXPECT_EQ(original_meta.files[0].path, new_meta.files[0].path);
        EXPECT_EQ(original_meta.files[0].size, new_meta.files[0].size);
    }
}

TEST_F(ModifierTest, MultipleModifications)
{
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.trackers = {"https://new.example.com/announce"};
    config.is_private = true;
    config.source = "MULTI";
    config.comment = "Multi test";
    config.name = "MultiName";

    TorrentModifier modifier(config);
    modifier.modify();

    auto meta = read_torrent_metadata(config.output);
    ASSERT_EQ(meta.trackers.size(), 1u);
    EXPECT_EQ(meta.trackers[0], "https://new.example.com/announce");
    EXPECT_TRUE(meta.is_private);
    ASSERT_TRUE(meta.source.has_value());
    EXPECT_EQ(meta.source.value(), "MULTI");
    ASSERT_TRUE(meta.comment.has_value());
    EXPECT_EQ(meta.comment.value(), "Multi test");
    EXPECT_EQ(meta.name, "MultiName");
}

TEST_F(ModifierTest, TrackerReplaceWithMultiple)
{
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.trackers = {"https://t1.example.com/announce", "https://t2.example.com/announce"};

    TorrentModifier modifier(config);
    modifier.modify();

    auto meta = read_torrent_metadata(config.output);
    ASSERT_EQ(meta.trackers.size(), 2u);
    EXPECT_EQ(meta.trackers[0], "https://t1.example.com/announce");
    EXPECT_EQ(meta.trackers[1], "https://t2.example.com/announce");
}

TEST_F(ModifierTest, PartialTrackerRemovalUpdatesAnnounce)
{
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.trackers = {"https://keep.example.com/announce", "https://remove.example.com/announce"};

    TorrentModifier modifier(config);
    modifier.modify();

    auto first_meta = read_torrent_metadata(config.output);
    ASSERT_EQ(first_meta.trackers.size(), 2u);

    ModifyConfig config2;
    config2.input = config.output;
    config2.output = test_dir_ / "modified2.torrent";
    config2.remove_trackers = {"https://remove.example.com/announce"};

    TorrentModifier modifier2(config2);
    modifier2.modify();

    auto second_meta = read_torrent_metadata(config2.output);
    ASSERT_EQ(second_meta.trackers.size(), 1u);
    EXPECT_EQ(second_meta.trackers[0], "https://keep.example.com/announce");
}

TEST_F(ModifierTest, ChangeSourcePreservesInfoHashOnNoInfoChange)
{
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.comment = "Only comment changes";

    TorrentModifier modifier(config);
    modifier.modify();

    auto original_meta = read_torrent_metadata(torrent_path_);
    auto new_meta = read_torrent_metadata(config.output);

    if (!original_meta.info_hash_v1.empty() && !new_meta.info_hash_v1.empty())
    {
        EXPECT_EQ(original_meta.info_hash_v1, new_meta.info_hash_v1);
    }
}

TEST_F(ModifierTest, GenerateEntropyHexProducesUniqueValues)
{
    std::string first = utils::generate_entropy_hex();
    std::string second = utils::generate_entropy_hex();
    EXPECT_NE(first, second);
    EXPECT_EQ(first.size(), 64u);
    EXPECT_EQ(second.size(), 64u);
}

TEST_F(ModifierTest, RemoveComment)
{
    {
        ModifyConfig setup_config;
        setup_config.input = torrent_path_;
        setup_config.output = torrent_path_;
        setup_config.comment = "Temporary comment";
        TorrentModifier(setup_config).modify();
    }

    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.comment = "";

    TorrentModifier modifier(config);
    modifier.modify();

    auto meta = read_torrent_metadata(config.output);
    EXPECT_FALSE(meta.comment.has_value());
}

TEST_F(ModifierTest, RebuildTrackersWithEmptyList)
{
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.trackers = std::vector<std::string>{};

    TorrentModifier modifier(config);
    modifier.modify();

    auto new_bytes = read_file_bytes(config.output);
    lt::entry root = lt::bdecode(lt::span<const char>(new_bytes.data(), new_bytes.size()));

    EXPECT_EQ(root.find_key("announce"), nullptr);
    EXPECT_EQ(root.find_key("announce-list"), nullptr);
}

TEST_F(ModifierTest, AddTrackersToAnnounceOnlyTorrent)
{
    lt::entry root;
    root["info"]["name"] = lt::entry("test");
    root["info"]["piece length"] = lt::entry(16384);
    root["announce"] = lt::entry("udp://original.example.com:80/announce");

    std::vector<char> buffer;
    lt::bencode(std::back_inserter(buffer), root);
    std::ofstream out(torrent_path_, std::ios::binary);
    out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    out.close();

    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.add_trackers = {"https://new-tracker.example.com/announce"};

    TorrentModifier modifier(config);
    modifier.modify();

    auto new_bytes = read_file_bytes(config.output);
    lt::entry modified = lt::bdecode(lt::span<const char>(new_bytes.data(), new_bytes.size()));

    lt::entry *ann = modified.find_key("announce");
    ASSERT_NE(ann, nullptr);
    EXPECT_EQ(ann->string(), "udp://original.example.com:80/announce");

    lt::entry *al = modified.find_key("announce-list");
    ASSERT_NE(al, nullptr);
    auto &tiers = al->list();
    ASSERT_EQ(tiers.size(), 1u);
    auto &tier = tiers[0].list();
    EXPECT_EQ(tier.size(), 2u);
    EXPECT_EQ(tier[0].string(), "udp://original.example.com:80/announce");
    EXPECT_EQ(tier[1].string(), "https://new-tracker.example.com/announce");
}

TEST_F(ModifierTest, RemoveTrackersFromAnnounceOnlyTorrent)
{
    lt::entry root;
    root["info"]["name"] = lt::entry("test");
    root["info"]["piece length"] = lt::entry(16384);
    root["announce"] = lt::entry("udp://tracker.example.com:80/announce");

    std::vector<char> buffer;
    lt::bencode(std::back_inserter(buffer), root);
    std::ofstream out(torrent_path_, std::ios::binary);
    out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    out.close();

    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.remove_trackers = {"udp://tracker.example.com:80/announce"};

    TorrentModifier modifier(config);
    modifier.modify();

    auto new_bytes = read_file_bytes(config.output);
    lt::entry modified = lt::bdecode(lt::span<const char>(new_bytes.data(), new_bytes.size()));

    EXPECT_EQ(modified.find_key("announce"), nullptr);
    EXPECT_EQ(modified.find_key("announce-list"), nullptr);
}

TEST_F(ModifierTest, MissingInfoDictThrows)
{
    lt::entry root;
    root["comment"] = lt::entry("no info dict");

    std::vector<char> buffer;
    lt::bencode(std::back_inserter(buffer), root);
    std::ofstream out(torrent_path_, std::ios::binary);
    out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    out.close();

    ModifyConfig config;
    config.input = torrent_path_;
    config.source = "TEST";

    TorrentModifier modifier(config);
    EXPECT_THROW(modifier.modify(), std::runtime_error);
}

TEST_F(ModifierTest, EmptyFileThrows)
{
    std::ofstream out(torrent_path_, std::ios::binary);
    out.close();

    ModifyConfig config;
    config.input = torrent_path_;
    config.source = "TEST";

    TorrentModifier modifier(config);
    EXPECT_THROW(modifier.modify(), std::runtime_error);
}

TEST_F(ModifierTest, NoOpPreservesInfoHash)
{
    auto original_meta = read_torrent_metadata(torrent_path_);
    auto original_bytes = read_file_bytes(torrent_path_);

    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.comment = "only comment changes, not info dict";

    TorrentModifier modifier(config);
    modifier.modify();

    auto new_meta = read_torrent_metadata(config.output);

    if (!original_meta.info_hash_v1.empty() && !new_meta.info_hash_v1.empty())
    {
        EXPECT_EQ(original_meta.info_hash_v1, new_meta.info_hash_v1);
    }
    EXPECT_EQ(original_meta.total_size, new_meta.total_size);
    EXPECT_EQ(original_meta.files.size(), new_meta.files.size());
}

TEST_F(ModifierTest, SaveToNewDirectory)
{
    fs::path nested = test_dir_ / "nested" / "output";
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = nested / "modified.torrent";
    config.source = "NESTED";

    TorrentModifier modifier(config);
    modifier.modify();

    EXPECT_TRUE(fs::exists(config.output));
    auto meta = read_torrent_metadata(config.output);
    ASSERT_TRUE(meta.source.has_value());
    EXPECT_EQ(meta.source.value(), "NESTED");
}

class V2ModifierTest : public ::testing::Test
{
  protected:
    fs::path test_dir_;
    fs::path test_file_;
    fs::path torrent_path_;

    void SetUp() override
    {
        test_dir_ = fs::current_path() / "v2_modifier_test_tmp";
        fs::create_directories(test_dir_);
        test_file_ = test_dir_ / "test_file.txt";
        std::ofstream file(test_file_);
        file << "Hello World - v2 test content";
        file.close();

        torrent_path_ = test_dir_ / "test.torrent";

        lt::file_storage fs;
        fs.add_file("test_file.txt", static_cast<std::int64_t>(std::filesystem::file_size(test_file_)));
        lt::create_torrent ct(fs, 16384, lt::create_torrent::v2_only);

        lt::set_piece_hashes(ct, test_dir_.string());

        lt::entry e = ct.generate();
        std::vector<char> buffer;
        lt::bencode(std::back_inserter(buffer), e);
        std::ofstream torrent(torrent_path_, std::ios::binary);
        torrent.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        torrent.close();
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(test_dir_, ec);
    }

    TorrentMetadata read_torrent_metadata(const fs::path &path)
    {
        TorrentInspector inspector(path.string());
        return inspector.inspect();
    }
};

TEST_F(V2ModifierTest, ModifySourcePreservesFileTree)
{
    auto original_meta = read_torrent_metadata(torrent_path_);

    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.source = "V2TEST";

    TorrentModifier modifier(config);
    modifier.modify();

    auto new_meta = read_torrent_metadata(config.output);
    ASSERT_TRUE(new_meta.source.has_value());
    EXPECT_EQ(new_meta.source.value(), "V2TEST");
    EXPECT_EQ(original_meta.total_size, new_meta.total_size);
    EXPECT_EQ(original_meta.files.size(), new_meta.files.size());
    EXPECT_FALSE(new_meta.info_hash_v2.empty());
}

TEST_F(V2ModifierTest, ModifyPrivateFlag)
{
    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.is_private = true;

    TorrentModifier modifier(config);
    modifier.modify();

    auto meta = read_torrent_metadata(config.output);
    EXPECT_TRUE(meta.is_private);
}

class HybridModifierTest : public ::testing::Test
{
  protected:
    fs::path test_dir_;
    fs::path content_dir_;
    fs::path torrent_path_;

    void SetUp() override
    {
        test_dir_ = fs::current_path() / "hybrid_modifier_test_tmp";
        fs::create_directories(test_dir_);
        content_dir_ = test_dir_ / "content";
        fs::create_directories(content_dir_);
        std::ofstream(content_dir_ / "file1.txt") << "hybrid test file 1";
        std::ofstream(content_dir_ / "file2.txt") << "hybrid test file 2";

        torrent_path_ = test_dir_ / "test.torrent";

        lt::file_storage fs;
        lt::add_files(fs, content_dir_.string());

        lt::create_torrent ct(fs, 16384);

        lt::set_piece_hashes(ct, test_dir_.string());

        lt::entry e = ct.generate();
        std::vector<char> buffer;
        lt::bencode(std::back_inserter(buffer), e);
        std::ofstream torrent(torrent_path_, std::ios::binary);
        torrent.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        torrent.close();
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(test_dir_, ec);
    }

    TorrentMetadata read_torrent_metadata(const fs::path &path)
    {
        TorrentInspector inspector(path.string());
        return inspector.inspect();
    }
};

TEST_F(HybridModifierTest, ModifySourcePreservesBothStructures)
{
    auto original_meta = read_torrent_metadata(torrent_path_);
    ASSERT_TRUE(original_meta.is_hybrid);

    ModifyConfig config;
    config.input = torrent_path_;
    config.output = test_dir_ / "modified.torrent";
    config.source = "HYBRID";

    TorrentModifier modifier(config);
    modifier.modify();

    auto new_meta = read_torrent_metadata(config.output);
    ASSERT_TRUE(new_meta.source.has_value());
    EXPECT_EQ(new_meta.source.value(), "HYBRID");
    EXPECT_TRUE(new_meta.is_hybrid);
    EXPECT_EQ(original_meta.total_size, new_meta.total_size);
    EXPECT_EQ(original_meta.files.size(), new_meta.files.size());
    EXPECT_FALSE(new_meta.info_hash_v1.empty());
    EXPECT_FALSE(new_meta.info_hash_v2.empty());
}
