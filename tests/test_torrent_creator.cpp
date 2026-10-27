// Regression tests for torrent creation layout correctness.
//
// Hybrid torrents align every file to a piece boundary, which makes
// libtorrent insert pad files named ".pad/<size>". When two pads have the same
// size their paths collide; libtorrent tolerates this (and normalizes pad
// names when parsing), but strict clients (e.g. Transmission) reject the raw
// torrent as having duplicate paths. These tests decode the generated bencode
// directly and assert unique paths while preserving v1/v2 behavior.

#include <gtest/gtest.h>

#include "torrent_creator.hpp"
#include "output_sink.hpp"

#include <libtorrent/bdecode.hpp>
#include <libtorrent/error_code.hpp>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

class NullOutputSink : public OutputSink {
public:
    void info(const std::string&) override {}
    void verbose(const std::string&) override {}
    void error(const std::string&) override {}
    void progress(const ProgressInfo&) override {}
};

class TorrentCreatorLayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = fs::temp_directory_path() / "tb_torrent_creator_test";
        fs::remove_all(temp_dir_);
        fs::create_directories(temp_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    fs::path create_file(const fs::path& dir, const std::string& name,
                         std::size_t size = 2) {
        fs::create_directories(dir);
        auto path = dir / name;
        std::ofstream out(path, std::ios::binary);
        out << std::string(size, 'x');
        return path;
    }

    void create_torrent_file(const fs::path& input, const fs::path& output,
                             TorrentVersion version) {
        NullOutputSink sink;
        TorrentConfig config(input, output, {}, version);
        config.silent = true;
        TorrentCreator creator(std::move(config), sink);
        creator.create_torrent();
    }

    fs::path temp_dir_;
};

// Decodes the raw torrent into `buffer`; the returned node points into it.
lt::bdecode_node decode_torrent(const fs::path& torrent_path,
                                std::vector<char>& buffer,
                                lt::error_code& ec) {
    std::ifstream in(torrent_path, std::ios::binary);
    buffer.assign(std::istreambuf_iterator<char>(in),
                  std::istreambuf_iterator<char>());
    return lt::bdecode(lt::span<char const>(buffer.data(), buffer.size()), ec);
}

struct RawFile {
    std::string path;
    std::int64_t length = 0;
    bool is_pad = false;
};

std::string join_path(const lt::bdecode_node& path) {
    std::string joined;
    for (int i = 0; i < path.list_size(); ++i) {
        if (!joined.empty()) joined += "/";
        const auto component = path.list_string_value_at(i);
        joined.append(component.data(), component.size());
    }
    return joined;
}

// Reads the v1 "files" list (the only place hybrid pad paths are emitted).
std::vector<RawFile> v1_files(const lt::bdecode_node& info) {
    std::vector<RawFile> files_out;
    const auto files = info.dict_find_list("files");
    if (!files) return files_out;

    for (int i = 0; i < files.list_size(); ++i) {
        const auto file = files.list_at(i);
        const auto path = file.dict_find_list("path");
        if (!path) continue;
        files_out.push_back(RawFile{
            join_path(path),
            file.dict_find_int_value("length"),
            file.dict_find_string_value("attr").find('p')
                != std::string_view::npos,
        });
    }
    return files_out;
}

void collect_tree_files(const lt::bdecode_node& node,
                        const std::string& prefix,
                        std::vector<RawFile>& out) {
    for (int i = 0; i < node.dict_size(); ++i) {
        auto [key, value] = node.dict_at(i);
        const std::string name(key);
        if (name.empty()) {
            out.push_back(RawFile{
                prefix, value.dict_find_int_value("length"), false});
        } else {
            collect_tree_files(
                value, prefix.empty() ? name : prefix + "/" + name, out);
        }
    }
}

std::vector<RawFile> v2_files(const lt::bdecode_node& info) {
    std::vector<RawFile> files_out;
    const auto tree = info.dict_find_dict("file tree");
    if (tree) collect_tree_files(tree, "", files_out);
    return files_out;
}

std::string pad_leaf(const std::string& path) {
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Pad leaves are "<size>" or "<size>.<n>"; the numeric prefix must match the
// file length, proving the rename preserved the payload size.
::testing::AssertionResult pad_size_matches_length(const RawFile& pad) {
    const std::string leaf = pad_leaf(pad.path);
    const auto dot = leaf.find('.');
    const std::string_view prefix(leaf.data(), dot == std::string::npos
                                                 ? leaf.size()
                                                 : dot);

    std::int64_t parsed = 0;
    const auto [ptr, ec] = std::from_chars(
        prefix.data(), prefix.data() + prefix.size(), parsed);
    if (ec != std::errc{} || ptr != prefix.data() + prefix.size()) {
        return ::testing::AssertionFailure()
               << "non-numeric pad leaf: " << leaf;
    }
    if (parsed != pad.length) {
        return ::testing::AssertionFailure()
               << "pad size mismatch: leaf=" << leaf
               << " length=" << pad.length;
    }
    return ::testing::AssertionSuccess();
}

// Decodes the raw bencode instead of using lt::torrent_info: libtorrent
// normalizes duplicate pad names on parse, which would hide the very bug we
// need to catch. The v1 "files" list is checked when present; v2-only
// torrents fall back to the "file tree".
::testing::AssertionResult paths_are_unique(const fs::path& torrent_path,
                                            int& pad_count) {
    std::vector<char> buffer;
    lt::error_code ec;
    const auto root = decode_torrent(torrent_path, buffer, ec);
    if (ec) {
        return ::testing::AssertionFailure()
               << "bdecode failed: " << ec.message();
    }

    const auto info = root.dict_find_dict("info");
    if (!info) {
        return ::testing::AssertionFailure() << "missing info dictionary";
    }

    std::set<std::string> seen;
    pad_count = 0;

    const bool has_v1 = static_cast<bool>(info.dict_find_list("files"));
    const auto files = has_v1 ? v1_files(info) : v2_files(info);
    if (files.empty()) {
        return ::testing::AssertionFailure()
               << "missing files/file tree entries";
    }

    for (const auto& file : files) {
        if (!seen.insert(file.path).second) {
            return ::testing::AssertionFailure()
                   << "duplicate path in torrent: " << file.path;
        }
        if (file.is_pad) ++pad_count;
    }
    return ::testing::AssertionSuccess();
}

} // namespace

TEST_F(TorrentCreatorLayoutTest, HybridDirectoryHasUniquePadPaths) {
    const auto show_dir = temp_dir_ / "Show.S01";
    create_file(show_dir, "ep1.bin");
    create_file(show_dir, "ep2.bin");
    const auto output = temp_dir_ / "show.torrent";

    create_torrent_file(show_dir, output, TorrentVersion::HYBRID);

    int pad_count = 0;
    EXPECT_TRUE(paths_are_unique(output, pad_count));
    EXPECT_GE(pad_count, 2);
}

TEST_F(TorrentCreatorLayoutTest, HybridPreservesRealFilesAndV2Tree) {
    const auto show_dir = temp_dir_ / "Show.Layout";
    create_file(show_dir, "a.txt");
    create_file(show_dir, "b.txt");
    const auto output = temp_dir_ / "layout.torrent";

    create_torrent_file(show_dir, output, TorrentVersion::HYBRID);

    std::vector<char> buffer;
    lt::error_code ec;
    const auto root = decode_torrent(output, buffer, ec);
    ASSERT_FALSE(ec);
    const auto info = root.dict_find_dict("info");
    ASSERT_TRUE(info);

    std::map<std::string, std::int64_t> real_files;
    std::vector<RawFile> pads;
    for (const auto& file : v1_files(info)) {
        if (file.is_pad) {
            pads.push_back(file);
        } else {
            real_files[file.path] = file.length;
        }
    }

    EXPECT_EQ(real_files.size(), 2u);
    EXPECT_EQ(real_files["a.txt"], 2);
    EXPECT_EQ(real_files["b.txt"], 2);

    ASSERT_EQ(pads.size(), 2u);
    for (const auto& pad : pads) {
        EXPECT_NE(pad.path.find(".pad/"), std::string::npos);
        EXPECT_TRUE(pad_size_matches_length(pad));
    }

    // The hybrid v2 tree must still describe only the real files.
    const auto v2 = v2_files(info);
    EXPECT_EQ(v2.size(), 2u);
    std::set<std::string> tree_paths;
    for (const auto& file : v2) {
        tree_paths.insert(file.path);
        EXPECT_EQ(file.length, 2);
        EXPECT_EQ(file.path.find(".pad"), std::string::npos);
    }
    EXPECT_EQ(tree_paths.count("a.txt"), 1u);
    EXPECT_EQ(tree_paths.count("b.txt"), 1u);
}

TEST_F(TorrentCreatorLayoutTest, HybridDistinctPadSizesKeepCanonicalNames) {
    const auto show_dir = temp_dir_ / "Show.Mixed";
    create_file(show_dir, "a.txt", 2);
    create_file(show_dir, "b.txt", 100);
    const auto output = temp_dir_ / "mixed.torrent";

    create_torrent_file(show_dir, output, TorrentVersion::HYBRID);

    std::vector<char> buffer;
    lt::error_code ec;
    const auto root = decode_torrent(output, buffer, ec);
    ASSERT_FALSE(ec);
    const auto info = root.dict_find_dict("info");
    ASSERT_TRUE(info);

    std::vector<RawFile> pads;
    for (const auto& file : v1_files(info)) {
        if (file.is_pad) pads.push_back(file);
    }
    ASSERT_GE(pads.size(), 2u);

    std::set<std::int64_t> pad_sizes;
    for (const auto& pad : pads) {
        EXPECT_EQ(pad_leaf(pad.path).find('.'), std::string::npos)
            << "distinct pad sizes must keep the canonical .pad/<size> name";
        EXPECT_TRUE(pad_size_matches_length(pad));
        pad_sizes.insert(pad.length);
    }
    EXPECT_GE(pad_sizes.size(), 2u);
}

TEST_F(TorrentCreatorLayoutTest, HybridRealPadPathCollisionKeepsRealFile) {
    const auto show_dir = temp_dir_ / "Show.Collision";
    create_file(show_dir, "a.txt");
    create_file(show_dir / ".pad", "16382");
    const auto output = temp_dir_ / "collision.torrent";

    create_torrent_file(show_dir, output, TorrentVersion::HYBRID);

    std::vector<char> buffer;
    lt::error_code ec;
    const auto root = decode_torrent(output, buffer, ec);
    ASSERT_FALSE(ec);
    const auto info = root.dict_find_dict("info");
    ASSERT_TRUE(info);

    bool saw_real_file = false;
    std::int64_t real_length = 0;
    std::vector<RawFile> pads;
    for (const auto& file : v1_files(info)) {
        if (!file.is_pad && file.path == ".pad/16382") {
            saw_real_file = true;
            real_length = file.length;
        }
        if (file.is_pad) pads.push_back(file);
    }

    EXPECT_TRUE(saw_real_file);
    EXPECT_EQ(real_length, 2);
    ASSERT_FALSE(pads.empty());

    bool saw_renamed_pad = false;
    for (const auto& pad : pads) {
        EXPECT_TRUE(pad_size_matches_length(pad));
        if (pad.path.find(".pad/16382.") != std::string::npos) {
            saw_renamed_pad = true;
        }
    }
    EXPECT_TRUE(saw_renamed_pad);
}

TEST_F(TorrentCreatorLayoutTest, HybridSingleFileHasUniquePaths) {
    const auto show_dir = temp_dir_ / "Show.Single";
    create_file(show_dir, "only.bin");
    const auto output = temp_dir_ / "single.torrent";

    create_torrent_file(show_dir, output, TorrentVersion::HYBRID);

    int pad_count = 0;
    EXPECT_TRUE(paths_are_unique(output, pad_count));
}

TEST_F(TorrentCreatorLayoutTest, HybridThreeSmallFilesGetUniqueSuffixes) {
    const auto show_dir = temp_dir_ / "Show.S02";
    create_file(show_dir, "ep1.bin");
    create_file(show_dir, "ep2.bin");
    create_file(show_dir, "ep3.bin");
    const auto output = temp_dir_ / "show3.torrent";

    create_torrent_file(show_dir, output, TorrentVersion::HYBRID);

    int pad_count = 0;
    EXPECT_TRUE(paths_are_unique(output, pad_count));
    EXPECT_GE(pad_count, 3);

    std::vector<char> buffer;
    lt::error_code ec;
    const auto root = decode_torrent(output, buffer, ec);
    ASSERT_FALSE(ec);

    const auto files = root.dict_find_dict("info").dict_find_list("files");
    ASSERT_TRUE(files);

    bool saw_suffixed_pad = false;
    for (int i = 0; i < files.list_size(); ++i) {
        const auto file = files.list_at(i);
        if (file.dict_find_string_value("attr").find('p')
            == std::string_view::npos) {
            continue;
        }
        const auto path = file.dict_find_list("path");
        if (!path || path.list_size() != 2) continue;
        const auto leaf = path.list_string_value_at(1);
        // The canonical pad keeps ".pad/<size>"; duplicates get a numeric
        // ".N" suffix, so any dot in the leaf marks a renamed pad.
        if (leaf.find('.') != std::string_view::npos) {
            saw_suffixed_pad = true;
        }
    }
    EXPECT_TRUE(saw_suffixed_pad);
}

TEST_F(TorrentCreatorLayoutTest, V1DirectoryHasNoPadEntries) {
    const auto show_dir = temp_dir_ / "Show.V1";
    create_file(show_dir, "ep1.bin");
    create_file(show_dir, "ep2.bin");
    const auto output = temp_dir_ / "v1.torrent";

    create_torrent_file(show_dir, output, TorrentVersion::V1);

    int pad_count = 0;
    EXPECT_TRUE(paths_are_unique(output, pad_count));
    EXPECT_EQ(pad_count, 0);
}

TEST_F(TorrentCreatorLayoutTest, V2DirectoryHasUniquePaths) {
    const auto show_dir = temp_dir_ / "Show.V2";
    create_file(show_dir, "ep1.bin");
    create_file(show_dir, "ep2.bin");
    const auto output = temp_dir_ / "v2.torrent";

    create_torrent_file(show_dir, output, TorrentVersion::V2);

    int pad_count = 0;
    EXPECT_TRUE(paths_are_unique(output, pad_count));
    EXPECT_EQ(pad_count, 0);
}
