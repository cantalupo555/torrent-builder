#include "portable.hpp"
#include <gtest/gtest.h>
#include "preset.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>

namespace fs = std::filesystem;

namespace {
void portable_setenv(const std::string& key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key.c_str(), value.c_str());
#else
    setenv(key.c_str(), value.c_str(), 1);
#endif
}

void portable_unsetenv(const std::string& key) {
#ifdef _WIN32
    _putenv_s(key.c_str(), "");
#else
    unsetenv(key.c_str());
#endif
}
}

class PresetTest : public ::testing::Test {
protected:
    fs::path temp_dir;

    void SetUp() override {
        temp_dir = fs::temp_directory_path() / ("preset_test_" + std::to_string(portable_getpid()));
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

TEST_F(PresetTest, MergeOverlayWinsForTrackers) {
    ConfigValues base;
    base.trackers = std::vector<std::string>{"https://a.com/announce"};

    ConfigValues overlay;
    overlay.trackers = std::vector<std::string>{"https://b.com/announce"};

    auto result = merge_config_values(base, overlay);
    ASSERT_TRUE(result.trackers.has_value());
    EXPECT_EQ(result.trackers->size(), 1u);
    EXPECT_EQ((*result.trackers)[0], "https://b.com/announce");
}

TEST_F(PresetTest, MergeBaseUsedWhenOverlayMissing) {
    ConfigValues base;
    base.source = "BASE";
    base.is_private = true;

    ConfigValues overlay;

    auto result = merge_config_values(base, overlay);
    EXPECT_TRUE(result.source.has_value());
    EXPECT_EQ(*result.source, "BASE");
    EXPECT_TRUE(result.is_private.has_value());
    EXPECT_TRUE(*result.is_private);
}

TEST_F(PresetTest, MergeBothEmpty) {
    ConfigValues base;
    ConfigValues overlay;
    auto result = merge_config_values(base, overlay);
    EXPECT_FALSE(result.path.has_value());
    EXPECT_FALSE(result.trackers.has_value());
}

TEST_F(PresetTest, MergeExplicitFalseOverridesTrue) {
    ConfigValues base;
    base.is_private = true;
    base.creation_date = true;
    base.entropy = true;

    ConfigValues overlay;
    overlay.is_private = false;
    overlay.creation_date = false;
    overlay.entropy = false;

    auto result = merge_config_values(base, overlay);
    ASSERT_TRUE(result.is_private.has_value());
    EXPECT_FALSE(*result.is_private);
    ASSERT_TRUE(result.creation_date.has_value());
    EXPECT_FALSE(*result.creation_date);
    ASSERT_TRUE(result.entropy.has_value());
    EXPECT_FALSE(*result.entropy);
}

TEST_F(PresetTest, MergeAllFieldsOverridden) {
    ConfigValues base;
    base.path = "base_path";
    base.output = "base_output";
    base.trackers = std::vector<std::string>{"a"};
    base.web_seeds = std::vector<std::string>{"b"};
    base.is_private = true;
    base.source = "base_source";
    base.piece_size = 1024;
    base.comment = "base_comment";
    base.creator = "base_creator";
    base.name = "base_name";
    base.creation_date = true;
    base.torrent_version = 1;
    base.entropy = true;
    base.exclude_patterns = std::vector<std::string>{"*.nfo"};
    base.include_patterns = std::vector<std::string>{"*.mkv"};

    ConfigValues overlay;
    overlay.path = "overlay_path";
    overlay.output = "overlay_output";
    overlay.trackers = std::vector<std::string>{"c"};
    overlay.web_seeds = std::vector<std::string>{"d"};
    overlay.is_private = false;
    overlay.source = "overlay_source";
    overlay.piece_size = 2048;
    overlay.comment = "overlay_comment";
    overlay.creator = "overlay_creator";
    overlay.name = "overlay_name";
    overlay.creation_date = false;
    overlay.torrent_version = 2;
    overlay.entropy = false;
    overlay.exclude_patterns = std::vector<std::string>{"*.txt"};
    overlay.include_patterns = std::vector<std::string>{"*.mp4"};

    auto result = merge_config_values(base, overlay);

    EXPECT_EQ(*result.path, "overlay_path");
    EXPECT_EQ(*result.output, "overlay_output");
    EXPECT_EQ(result.trackers->size(), 1u);
    EXPECT_EQ((*result.trackers)[0], "c");
    EXPECT_EQ(result.web_seeds->size(), 1u);
    EXPECT_EQ((*result.web_seeds)[0], "d");
    EXPECT_FALSE(*result.is_private);
    EXPECT_EQ(*result.source, "overlay_source");
    EXPECT_EQ(*result.piece_size, 2048);
    EXPECT_EQ(*result.comment, "overlay_comment");
    EXPECT_EQ(*result.creator, "overlay_creator");
    EXPECT_EQ(*result.name, "overlay_name");
    EXPECT_FALSE(*result.creation_date);
    EXPECT_EQ(*result.torrent_version, 2);
    EXPECT_FALSE(*result.entropy);
    EXPECT_EQ(result.exclude_patterns->size(), 1u);
    EXPECT_EQ((*result.exclude_patterns)[0], "*.txt");
    EXPECT_EQ(result.include_patterns->size(), 1u);
    EXPECT_EQ((*result.include_patterns)[0], "*.mp4");
}

TEST_F(PresetTest, LoadAndResolvePreset) {
    write_file("presets.yaml", R"(
version: 1
default:
  private: true
  exclude_patterns: ["*.nfo"]
presets:
  ptp:
    source: "PTP"
    trackers:
      - "https://ptp.example/announce"
    exclude_patterns: ["*.nfo", "*.txt"]
  public:
    private: false
    trackers:
      - "udp://tracker.example/announce"
)");

    PresetLoader loader;
    loader.load(temp_dir / "presets.yaml");

    EXPECT_TRUE(loader.has_preset("ptp"));
    EXPECT_TRUE(loader.has_preset("public"));
    EXPECT_FALSE(loader.has_preset("nonexistent"));

    auto ptp = loader.resolve("ptp");
    EXPECT_TRUE(ptp.is_private.has_value() && *ptp.is_private);
    EXPECT_TRUE(ptp.source.has_value());
    EXPECT_EQ(*ptp.source, "PTP");
    ASSERT_TRUE(ptp.trackers.has_value());
    EXPECT_EQ(ptp.trackers->size(), 1u);
    EXPECT_EQ((*ptp.trackers)[0], "https://ptp.example/announce");
    ASSERT_TRUE(ptp.exclude_patterns.has_value());
    EXPECT_EQ(ptp.exclude_patterns->size(), 2u);

    auto pub = loader.resolve("public");
    EXPECT_TRUE(pub.is_private.has_value());
    EXPECT_FALSE(*pub.is_private);
    EXPECT_TRUE(pub.exclude_patterns.has_value());
    EXPECT_EQ(pub.exclude_patterns->size(), 1u);
}

TEST_F(PresetTest, ResolveUnknownPresetThrows) {
    write_file("presets.yaml", R"(
version: 1
presets: {}
)");

    PresetLoader loader;
    loader.load(temp_dir / "presets.yaml");

    EXPECT_THROW(loader.resolve("nonexistent"), std::runtime_error);
}

TEST_F(PresetTest, LoadInvalidVersionThrows) {
    write_file("presets.yaml", R"(
version: 99
presets: {}
)");

    PresetLoader loader;
    EXPECT_THROW(loader.load(temp_dir / "presets.yaml"), std::runtime_error);
}

TEST_F(PresetTest, LoadMissingVersionThrows) {
    write_file("presets.yaml", R"(
presets: {}
)");

    PresetLoader loader;
    EXPECT_THROW(loader.load(temp_dir / "presets.yaml"), std::runtime_error);
}

TEST_F(PresetTest, FindPresetFileExplicitPath) {
    write_file("presets.yaml", "version: 1\npresets: {}\n");

    auto path = PresetLoader::find_preset_file(temp_dir / "presets.yaml");
    EXPECT_EQ(path, temp_dir / "presets.yaml");
}

TEST_F(PresetTest, FindPresetFileExplicitNotFound) {
    EXPECT_THROW(
        PresetLoader::find_preset_file(temp_dir / "nonexistent.yaml"),
        std::runtime_error);
}

TEST_F(PresetTest, FindPresetFileNoFileThrows) {
    auto old_cwd = fs::current_path();
    auto old_xdg = std::getenv("XDG_CONFIG_HOME");

    fs::path isolated_cwd = temp_dir / ("no_preset_cwd_" + std::to_string(portable_getpid()));
    fs::create_directories(isolated_cwd);
    fs::current_path(isolated_cwd);
    portable_setenv("XDG_CONFIG_HOME", (temp_dir / "nonexistent_xdg").string());

    EXPECT_THROW(
        PresetLoader::find_preset_file(std::nullopt),
        std::runtime_error);

    fs::current_path(old_cwd);
    portable_unsetenv("XDG_CONFIG_HOME");
    if (old_xdg && old_xdg[0] != '\0') {
        portable_setenv("XDG_CONFIG_HOME", old_xdg);
    }
}

TEST_F(PresetTest, LoadAllFieldTypes) {
    write_file("presets.yaml", R"(
version: 1
presets:
  full:
    trackers:
      - "https://t1.com/announce"
      - "https://t2.com/announce"
    web_seeds:
      - "https://ws1.com/file"
    private: true
    source: "SRC"
    piece_size: 4096
    comment: "Test comment"
    creator: "TestCreator"
    name: "custom_name"
    creation_date: true
    torrent_version: 2
    entropy: true
    exclude_patterns: ["*.nfo", "*.txt"]
    include_patterns: ["*.mkv", "*.mp4"]
)");

    PresetLoader loader;
    loader.load(temp_dir / "presets.yaml");

    auto full = loader.resolve("full");
    ASSERT_TRUE(full.trackers.has_value());
    EXPECT_EQ(full.trackers->size(), 2u);
    ASSERT_TRUE(full.web_seeds.has_value());
    EXPECT_EQ(full.web_seeds->size(), 1u);
    EXPECT_TRUE(*full.is_private);
    EXPECT_EQ(*full.source, "SRC");
    EXPECT_EQ(*full.piece_size, 4096);
    EXPECT_EQ(*full.comment, "Test comment");
    EXPECT_EQ(*full.creator, "TestCreator");
    EXPECT_EQ(*full.name, "custom_name");
    EXPECT_TRUE(*full.creation_date);
    EXPECT_EQ(*full.torrent_version, 2);
    EXPECT_TRUE(*full.entropy);
    EXPECT_EQ(full.exclude_patterns->size(), 2u);
    EXPECT_EQ(full.include_patterns->size(), 2u);
}

TEST_F(PresetTest, DefaultMergeWithPreset) {
    write_file("presets.yaml", R"(
version: 1
default:
  private: true
  exclude_patterns: ["*.nfo"]
presets:
  ptp:
    source: "PTP"
    trackers:
      - "https://ptp.example/announce"
)");

    PresetLoader loader;
    loader.load(temp_dir / "presets.yaml");

    auto resolved = loader.resolve("ptp");
    EXPECT_TRUE(resolved.is_private.has_value() && *resolved.is_private);
    EXPECT_TRUE(resolved.exclude_patterns.has_value());
    EXPECT_EQ(resolved.exclude_patterns->size(), 1u);
    EXPECT_EQ((*resolved.exclude_patterns)[0], "*.nfo");
    EXPECT_EQ(*resolved.source, "PTP");
}

TEST_F(PresetTest, FindPresetFileXDGConfigHome) {
    fs::path xdg_dir = temp_dir / "xdg_config" / "torrent-builder";
    fs::create_directories(xdg_dir);
    {
        std::ofstream f(xdg_dir / "presets.yaml");
        f << "version: 1\npresets: {}\n";
    }

    auto old_cwd = fs::current_path();
    auto old_xdg = std::getenv("XDG_CONFIG_HOME");

    fs::path isolated_cwd = temp_dir / ("isolated_cwd_" + std::to_string(portable_getpid()));
    fs::create_directories(isolated_cwd);
    fs::current_path(isolated_cwd);
    portable_setenv("XDG_CONFIG_HOME", (temp_dir / "xdg_config").string());

    try {
        auto path = PresetLoader::find_preset_file(std::nullopt);
        EXPECT_EQ(path, xdg_dir / "presets.yaml");
    } catch (...) {
        FAIL() << "Should have found preset via XDG_CONFIG_HOME";
    }

    fs::current_path(old_cwd);
    portable_unsetenv("XDG_CONFIG_HOME");
    if (old_xdg && old_xdg[0] != '\0') {
        portable_setenv("XDG_CONFIG_HOME", old_xdg);
    }
}

TEST_F(PresetTest, FindPresetFileCwdFound) {
    auto old_cwd = fs::current_path();
    auto old_xdg = std::getenv("XDG_CONFIG_HOME");

    fs::path cwd_dir = temp_dir / ("cwd_preset_" + std::to_string(portable_getpid()));
    fs::create_directories(cwd_dir);
    {
        std::ofstream f(cwd_dir / "presets.yaml");
        f << "version: 1\npresets: {}\n";
    }

    portable_setenv("XDG_CONFIG_HOME", (temp_dir / "nonexistent_xdg").string());
    fs::current_path(cwd_dir);

    try {
        auto path = PresetLoader::find_preset_file(std::nullopt);
        auto expected = fs::current_path() / "presets.yaml";
        EXPECT_EQ(path, expected);
    } catch (...) {
        FAIL() << "Should have found preset in CWD";
    }

    fs::current_path(old_cwd);
    portable_unsetenv("XDG_CONFIG_HOME");
    if (old_xdg && old_xdg[0] != '\0') {
        portable_setenv("XDG_CONFIG_HOME", old_xdg);
    }
}

TEST_F(PresetTest, LoadInvalidTrackerUrlThrows) {
    write_file("presets.yaml", R"(
version: 1
presets:
  bad:
    trackers:
      - "not_a_valid_url"
)");

    PresetLoader loader;
    EXPECT_THROW(loader.load(temp_dir / "presets.yaml"), std::runtime_error);
}

TEST_F(PresetTest, LoadInvalidWebSeedUrlThrows) {
    write_file("presets.yaml", R"(
version: 1
presets:
  bad:
    web_seeds:
      - "ftp://[invalid"
)");

    PresetLoader loader;
    EXPECT_THROW(loader.load(temp_dir / "presets.yaml"), std::runtime_error);
}

TEST_F(PresetTest, LoadFileTooLargeThrows) {
    write_file("presets.yaml", std::string(1024 * 1024 + 100, ' '));
    PresetLoader loader;
    EXPECT_THROW(loader.load(temp_dir / "presets.yaml"), std::runtime_error);
}

TEST_F(PresetTest, ParseNoCreatorNoDate) {
    write_file("presets.yaml", R"(
version: 1
presets:
  clean:
    no_creator: true
    no_date: true
)");

    PresetLoader loader;
    loader.load(temp_dir / "presets.yaml");

    auto clean = loader.resolve("clean");
    ASSERT_TRUE(clean.no_creator.has_value());
    EXPECT_TRUE(*clean.no_creator);
    ASSERT_TRUE(clean.no_date.has_value());
    EXPECT_TRUE(*clean.no_date);
}

TEST_F(PresetTest, MergeNoCreatorNoDate) {
    ConfigValues base;
    base.no_creator = false;
    base.no_date = false;

    ConfigValues overlay;
    overlay.no_creator = true;
    overlay.no_date = true;

    auto result = merge_config_values(base, overlay);
    ASSERT_TRUE(result.no_creator.has_value());
    EXPECT_TRUE(*result.no_creator);
    ASSERT_TRUE(result.no_date.has_value());
    EXPECT_TRUE(*result.no_date);
}

TEST_F(PresetTest, MergeNoCreatorOverlayWinsOverPresetCreator) {
    ConfigValues base;
    base.creator = "CustomCreator";

    ConfigValues overlay;
    overlay.no_creator = true;

    auto result = merge_config_values(base, overlay);
    ASSERT_TRUE(result.no_creator.has_value());
    EXPECT_TRUE(*result.no_creator);
    EXPECT_TRUE(result.creator.has_value())
        << "Creator string preserved in merge; resolution happens at config build time";
}

TEST_F(PresetTest, DefaultSectionNoCreatorNoDatePropagatesToPreset) {
    write_file("presets.yaml", R"(
version: 1
default:
  no_creator: true
  no_date: true
presets:
  mypreset:
    source: "TEST"
)");

    PresetLoader loader;
    loader.load(temp_dir / "presets.yaml");

    auto resolved = loader.resolve("mypreset");
    ASSERT_TRUE(resolved.no_creator.has_value());
    EXPECT_TRUE(*resolved.no_creator) << "no_creator from default section should propagate";
    ASSERT_TRUE(resolved.no_date.has_value());
    EXPECT_TRUE(*resolved.no_date) << "no_date from default section should propagate";
    ASSERT_TRUE(resolved.source.has_value());
    EXPECT_EQ(*resolved.source, "TEST") << "Preset-specific fields should still work";
}
