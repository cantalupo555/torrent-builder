#include <gtest/gtest.h>
#include "utils.hpp"
#include "updater.hpp"
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef __APPLE__
#include <openssl/evp.h>
#endif

namespace fs = std::filesystem;

namespace
{
#ifdef __APPLE__
std::string sha256_file(const std::string &filepath)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file)
        return "";

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
        return "";

    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

    char buf[8192];
    while (file.read(buf, sizeof(buf)) || file.gcount() > 0)
    {
        EVP_DigestUpdate(ctx, buf, static_cast<size_t>(file.gcount()));
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);

    std::string result;
    result.reserve(digest_len * 2);
    for (unsigned int i = 0; i < digest_len; ++i)
    {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02x", digest[i]);
        result += hex;
    }
    return result;
}

bool create_mock_zip(const std::string &dest_path, const std::string &binary_content)
{
    fs::path staging = fs::temp_directory_path() / "tb_mock_zip_staging";
    fs::remove_all(staging);
    fs::create_directories(staging);
    {
        std::ofstream f(staging / "torrent_builder", std::ios::binary);
        f << binary_content;
    }
    fs::permissions(staging / "torrent_builder", fs::perms::owner_all);

    fs::remove(dest_path);
    std::string cmd = "cd \"" + staging.string() + "\" && zip -r \"" + dest_path + "\" torrent_builder > /dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    fs::remove_all(staging);
    return rc == 0;
}
#endif
}

class ParseVersionTest : public ::testing::Test
{
};

TEST_F(ParseVersionTest, StandardVersion)
{
    auto v = utils::parse_version("1.2.3");
    EXPECT_EQ(v.major, 1);
    EXPECT_EQ(v.minor, 2);
    EXPECT_EQ(v.patch, 3);
}

TEST_F(ParseVersionTest, VersionWithVPrefix)
{
    auto v = utils::parse_version("v2.1.0");
    EXPECT_EQ(v.major, 2);
    EXPECT_EQ(v.minor, 1);
    EXPECT_EQ(v.patch, 0);
}

TEST_F(ParseVersionTest, VersionWithUpperVPrefix)
{
    auto v = utils::parse_version("V3.0.1");
    EXPECT_EQ(v.major, 3);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 1);
}

TEST_F(ParseVersionTest, VersionWithPreReleaseSuffix)
{
    auto v = utils::parse_version("1.0.0-rc1");
    EXPECT_EQ(v.major, 1);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
    EXPECT_EQ(v.prerelease, "rc1");
    EXPECT_TRUE(v.is_prerelease());
}

TEST_F(ParseVersionTest, VersionWithVPrefixAndPreRelease)
{
    auto v = utils::parse_version("v2.0.0-beta.1");
    EXPECT_EQ(v.major, 2);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
}

TEST_F(ParseVersionTest, TwoPartVersion)
{
    auto v = utils::parse_version("1.5");
    EXPECT_EQ(v.major, 1);
    EXPECT_EQ(v.minor, 5);
    EXPECT_EQ(v.patch, 0);
}

TEST_F(ParseVersionTest, SinglePartVersion)
{
    auto v = utils::parse_version("42");
    EXPECT_EQ(v.major, 42);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
}

TEST_F(ParseVersionTest, DevVersion)
{
    auto v = utils::parse_version("dev");
    EXPECT_EQ(v.major, 0);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
}

TEST_F(ParseVersionTest, DevVersionWithHash)
{
    auto v = utils::parse_version("dev (abc1234)");
    EXPECT_EQ(v.major, 0);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
}

TEST_F(ParseVersionTest, EmptyString)
{
    auto v = utils::parse_version("");
    EXPECT_EQ(v.major, 0);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
}

TEST_F(ParseVersionTest, ZeroVersion)
{
    auto v = utils::parse_version("0.0.0");
    EXPECT_EQ(v.major, 0);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
}

class CompareVersionsTest : public ::testing::Test
{
};

TEST_F(CompareVersionsTest, EqualVersions)
{
    EXPECT_EQ(utils::compare_versions("1.0.0", "1.0.0"), 0);
    EXPECT_EQ(utils::compare_versions("2.3.4", "2.3.4"), 0);
}

TEST_F(CompareVersionsTest, FirstLessThanSecond)
{
    EXPECT_LT(utils::compare_versions("1.0.0", "2.0.0"), 0);
    EXPECT_LT(utils::compare_versions("1.0.0", "1.1.0"), 0);
    EXPECT_LT(utils::compare_versions("1.0.0", "1.0.1"), 0);
}

TEST_F(CompareVersionsTest, FirstGreaterThanSecond)
{
    EXPECT_GT(utils::compare_versions("2.0.0", "1.0.0"), 0);
    EXPECT_GT(utils::compare_versions("1.1.0", "1.0.0"), 0);
    EXPECT_GT(utils::compare_versions("1.0.1", "1.0.0"), 0);
}

TEST_F(CompareVersionsTest, DevVersionIsLessThanRelease)
{
    EXPECT_LT(utils::compare_versions("dev", "1.0.0"), 0);
    EXPECT_LT(utils::compare_versions("dev (abc1234)", "0.1.0"), 0);
}

TEST_F(CompareVersionsTest, BothDev)
{
    EXPECT_EQ(utils::compare_versions("dev", "dev"), 0);
}

TEST_F(CompareVersionsTest, WithVPrefixes)
{
    EXPECT_EQ(utils::compare_versions("v1.0.0", "1.0.0"), 0);
    EXPECT_LT(utils::compare_versions("v1.0.0", "v2.0.0"), 0);
}

TEST_F(CompareVersionsTest, PreReleaseLessThanRelease)
{
    EXPECT_LT(utils::compare_versions("1.0.0-rc1", "1.0.1"), 0);
    EXPECT_LT(utils::compare_versions("1.0.0-rc1", "1.0.0"), 0);
    EXPECT_EQ(utils::compare_versions("1.0.0-rc1", "1.0.0-rc1"), 0);
}

TEST_F(CompareVersionsTest, PreReleaseIdentifierOrdering)
{
    EXPECT_LT(utils::compare_versions("1.0.0-rc1", "1.0.0-rc2"), 0);
    EXPECT_GT(utils::compare_versions("1.0.0-rc2", "1.0.0-rc1"), 0);
    EXPECT_LT(utils::compare_versions("1.0.0-alpha", "1.0.0-beta"), 0);
    EXPECT_LT(utils::compare_versions("1.0.0-alpha.1", "1.0.0-alpha.2"), 0);
    EXPECT_LT(utils::compare_versions("1.0.0-alpha", "1.0.0-alpha.1"), 0);
    EXPECT_LT(utils::compare_versions("1.0.0-1", "1.0.0-2"), 0);
    EXPECT_GT(utils::compare_versions("1.0.0-rc.10", "1.0.0-rc.9"), 0);
    EXPECT_LT(utils::compare_versions("1.0.0-1", "1.0.0-alpha"), 0);
}

TEST_F(CompareVersionsTest, PreReleaseNumericOverflow)
{
    std::string huge = "1.0.0-" + std::string(30, '9');
    std::string huge2 = "1.0.0-" + std::string(30, '9');
    EXPECT_EQ(utils::compare_versions(huge, huge2), 0);
    EXPECT_GT(utils::compare_versions(huge, "1.0.0-999999"), 0);
}

class PlatformInfoTest : public ::testing::Test
{
};

TEST_F(PlatformInfoTest, ReturnsValidPlatform)
{
    auto info = Updater::get_platform_info();
    EXPECT_FALSE(info.os.empty());
    EXPECT_FALSE(info.arch.empty());
}

TEST_F(PlatformInfoTest, OsIsKnownValue)
{
    auto info = Updater::get_platform_info();
    EXPECT_TRUE(info.os == "linux" || info.os == "macos" || info.os == "windows" || info.os == "unknown");
}

TEST_F(PlatformInfoTest, ArchIsKnownValue)
{
    auto info = Updater::get_platform_info();
    EXPECT_TRUE(info.arch == "amd64" || info.arch == "arm64" || info.arch == "unknown");
}

class GetCurrentVersionTest : public ::testing::Test
{
protected:
    void TearDown() override { Updater::reset_test_overrides(); }
};

TEST_F(GetCurrentVersionTest, ReturnsNonEmpty)
{
    std::string version = Updater::get_current_version();
    EXPECT_FALSE(version.empty());
}

TEST_F(GetCurrentVersionTest, OverrideVersion)
{
    Updater::set_current_version_for_testing("9.9.9");
    EXPECT_EQ(Updater::get_current_version(), "9.9.9");
}

TEST_F(GetCurrentVersionTest, ResetRestoresReal)
{
    Updater::set_current_version_for_testing("9.9.9");
    Updater::reset_test_overrides();
    std::string v = Updater::get_current_version();
    EXPECT_NE(v, "9.9.9");
}

class CurlDetectionTest : public ::testing::Test
{
};

TEST_F(CurlDetectionTest, FindCurlReturnsNonEmpty)
{
    std::string curl = Updater::find_curl_executable();
    EXPECT_FALSE(curl.empty());
}

class FindMatchingAssetTest : public ::testing::Test
{
};

TEST_F(FindMatchingAssetTest, MatchesLinuxAmd64)
{
    std::string json = R"({
        "assets": [
            {"name": "torrent_builder_1.0.0_linux_amd64", "browser_download_url": "https://example.com/linux_amd64"},
            {"name": "torrent_builder_1.0.0_linux_arm64", "browser_download_url": "https://example.com/linux_arm64"},
            {"name": "torrent_builder_1.0.0_windows_amd64.exe", "browser_download_url": "https://example.com/windows_amd64"}
        ]
    })";
    PlatformInfo platform{"linux", "amd64"};
    auto result = Updater::find_matching_asset(json, platform);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "https://example.com/linux_amd64");
}

TEST_F(FindMatchingAssetTest, MatchesLinuxArm64)
{
    std::string json = R"({
        "assets": [
            {"name": "torrent_builder_1.0.0_linux_amd64", "browser_download_url": "https://example.com/linux_amd64"},
            {"name": "torrent_builder_1.0.0_linux_arm64", "browser_download_url": "https://example.com/linux_arm64"}
        ]
    })";
    PlatformInfo platform{"linux", "arm64"};
    auto result = Updater::find_matching_asset(json, platform);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "https://example.com/linux_arm64");
}

TEST_F(FindMatchingAssetTest, MatchesWindowsAmd64)
{
    std::string json = R"({
        "assets": [
            {"name": "torrent_builder_1.0.0_linux_amd64", "browser_download_url": "https://example.com/linux_amd64"},
            {"name": "torrent_builder_1.0.0_windows_amd64.exe", "browser_download_url": "https://example.com/windows_amd64"}
        ]
    })";
    PlatformInfo platform{"windows", "amd64"};
    auto result = Updater::find_matching_asset(json, platform);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "https://example.com/windows_amd64");
}

TEST_F(FindMatchingAssetTest, MatchesMacosArm64)
{
    std::string json = R"({
        "assets": [
            {"name": "torrent_builder_1.0.0_macos_arm64.zip", "browser_download_url": "https://example.com/macos_arm64"},
            {"name": "torrent_builder_1.0.0_macos_amd64.zip", "browser_download_url": "https://example.com/macos_amd64"}
        ]
    })";
    PlatformInfo platform{"macos", "arm64"};
    auto result = Updater::find_matching_asset(json, platform);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "https://example.com/macos_arm64");
}

TEST_F(FindMatchingAssetTest, NoMatchReturnsNullopt)
{
    std::string json = R"({
        "assets": [
            {"name": "torrent_builder_1.0.0_linux_amd64", "browser_download_url": "https://example.com/linux_amd64"}
        ]
    })";
    PlatformInfo platform{"windows", "arm64"};
    auto result = Updater::find_matching_asset(json, platform);
    EXPECT_FALSE(result.has_value());
}

TEST_F(FindMatchingAssetTest, EmptyAssetsReturnsNullopt)
{
    std::string json = R"({"assets": []})";
    PlatformInfo platform{"linux", "amd64"};
    auto result = Updater::find_matching_asset(json, platform);
    EXPECT_FALSE(result.has_value());
}

TEST_F(FindMatchingAssetTest, NoAssetsKeyReturnsNullopt)
{
    std::string json = R"({"tag_name": "v1.0.0"})";
    PlatformInfo platform{"linux", "amd64"};
    auto result = Updater::find_matching_asset(json, platform);
    EXPECT_FALSE(result.has_value());
}

TEST_F(FindMatchingAssetTest, InvalidJsonReturnsNullopt)
{
    std::string json = "not valid json";
    PlatformInfo platform{"linux", "amd64"};
    auto result = Updater::find_matching_asset(json, platform);
    EXPECT_FALSE(result.has_value());
}

class FindMatchingAssetFromJsonTest : public ::testing::Test
{
};

TEST_F(FindMatchingAssetFromJsonTest, MatchesFromParsedJson)
{
    nlohmann::json j = R"({
        "assets": [
            {"name": "torrent_builder_2.0.0_linux_amd64", "browser_download_url": "https://example.com/v2_linux"}
        ]
    })"_json;
    PlatformInfo platform{"linux", "amd64"};
    auto result = Updater::find_matching_asset_from_json(j, platform);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "https://example.com/v2_linux");
}

TEST_F(FindMatchingAssetFromJsonTest, NoMatchFromParsedJson)
{
    nlohmann::json j = R"({
        "assets": [
            {"name": "torrent_builder_2.0.0_linux_amd64", "browser_download_url": "https://example.com/v2_linux"}
        ]
    })"_json;
    PlatformInfo platform{"macos", "arm64"};
    auto result = Updater::find_matching_asset_from_json(j, platform);
    EXPECT_FALSE(result.has_value());
}

TEST_F(FindMatchingAssetFromJsonTest, SkipsAssetWithEmptyUrl)
{
    nlohmann::json j = R"({
        "assets": [
            {"name": "torrent_builder_2.0.0_linux_amd64", "browser_download_url": ""},
            {"name": "torrent_builder_2.0.0_linux_amd64", "browser_download_url": "https://example.com/valid"}
        ]
    })"_json;
    PlatformInfo platform{"linux", "amd64"};
    auto result = Updater::find_matching_asset_from_json(j, platform);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "https://example.com/valid");
}

class LookupChecksumTest : public ::testing::Test
{
};

TEST_F(LookupChecksumTest, FindsExactMatch)
{
    std::string checksums = "abc123def456  torrent_builder_1.0.0_linux_amd64\n"
                            "789abc123def  torrent_builder_1.0.0_linux_arm64\n";
    auto result = Updater::lookup_checksum(checksums, "torrent_builder_1.0.0_linux_amd64");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "abc123def456");
}

TEST_F(LookupChecksumTest, NotFoundReturnsNullopt)
{
    std::string checksums = "abc123  other_file.bin\n";
    auto result = Updater::lookup_checksum(checksums, "nonexistent_file");
    EXPECT_FALSE(result.has_value());
}

TEST_F(LookupChecksumTest, HandlesMixedCase)
{
    std::string checksums = "ABC123DEF456  torrent_builder_1.0.0_linux_amd64\n";
    auto result = Updater::lookup_checksum(checksums, "torrent_builder_1.0.0_linux_amd64");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "abc123def456");
}

TEST_F(LookupChecksumTest, EmptyContentReturnsNullopt)
{
    auto result = Updater::lookup_checksum("", "any_file");
    EXPECT_FALSE(result.has_value());
}

TEST_F(LookupChecksumTest, HandlesBsdStarFormat)
{
    std::string checksums = "abc123def456 *torrent_builder_1.0.0_linux_amd64\n";
    auto result = Updater::lookup_checksum(checksums, "torrent_builder_1.0.0_linux_amd64");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "abc123def456");
}

class MockedUpdaterTest : public ::testing::Test
{
protected:
    void TearDown() override { Updater::reset_test_overrides(); }

    static std::string make_release_json(const std::string &version,
                                          const std::string &body = "",
                                          const std::vector<std::pair<std::string, std::string>> &assets = {})
    {
        nlohmann::json j;
        j["tag_name"] = version;
        j["body"] = body;
        j["assets"] = nlohmann::json::array();
        for (const auto &[name, url] : assets)
        {
            j["assets"].push_back({{"name", name}, {"browser_download_url", url}});
        }
        return j.dump();
    }

    void setup_curl_mock(const std::string &response)
    {
        Updater::set_curl_executor_for_testing(
            [response](const std::string &, const std::vector<std::string> &) -> std::string
            {
                return response;
            });
    }

    void setup_curl_mock_throwing(const std::string &msg)
    {
        Updater::set_curl_executor_for_testing(
            [msg](const std::string &, const std::vector<std::string> &) -> std::string
            {
                throw std::runtime_error(msg);
            });
    }
};

class FetchLatestReleaseTest : public MockedUpdaterTest
{
protected:
    void TearDown() override
    {
        MockedUpdaterTest::TearDown();
#ifdef _WIN32
        _putenv_s("GITHUB_TOKEN", "");
#else
        unsetenv("GITHUB_TOKEN");
#endif
    }
};

TEST_F(FetchLatestReleaseTest, ReturnsValidRelease)
{
    setup_curl_mock(make_release_json("v2.0.0", "Bug fixes", {
        {"torrent_builder_2.0.0_linux_amd64", "https://example.com/binary"}
    }));

    auto result = Updater::fetch_latest_release();
    EXPECT_EQ(result.version, "2.0.0");
    EXPECT_EQ(result.changelog, "Bug fixes");
    EXPECT_TRUE(result.release_json.contains("assets"));
}

TEST_F(FetchLatestReleaseTest, StripsVPrefix)
{
    setup_curl_mock(make_release_json("v3.1.0"));

    auto result = Updater::fetch_latest_release();
    EXPECT_EQ(result.version, "3.1.0");
}

TEST_F(FetchLatestReleaseTest, TagWithoutVPrefix)
{
    setup_curl_mock(make_release_json("1.5.0"));

    auto result = Updater::fetch_latest_release();
    EXPECT_EQ(result.version, "1.5.0");
}

TEST_F(FetchLatestReleaseTest, RateLimitResponse)
{
    setup_curl_mock(R"({"message": "API rate limit exceeded for user", "documentation_url": "https://docs.github.com/rest/overview/resources-in-the-rest-api#rate-limiting"})");

    EXPECT_THROW(
        { auto r = Updater::fetch_latest_release(); },
        std::runtime_error);
    try
    {
        auto r = Updater::fetch_latest_release();
    }
    catch (const std::runtime_error &e)
    {
        EXPECT_NE(std::string(e.what()).find("rate limit"), std::string::npos);
    }
}

TEST_F(FetchLatestReleaseTest, InvalidJsonResponse)
{
    setup_curl_mock("this is not json");

    EXPECT_THROW(
        { auto r = Updater::fetch_latest_release(); },
        std::runtime_error);
    try
    {
        auto r = Updater::fetch_latest_release();
    }
    catch (const std::runtime_error &e)
    {
        EXPECT_NE(std::string(e.what()).find("parse"), std::string::npos);
    }
}

TEST_F(FetchLatestReleaseTest, EmptyJsonResponse)
{
    setup_curl_mock("");

    EXPECT_THROW(
        { auto r = Updater::fetch_latest_release(); },
        std::runtime_error);
}

TEST_F(FetchLatestReleaseTest, CurlNetworkError)
{
    setup_curl_mock_throwing("Connection refused");

    EXPECT_THROW(
        { auto r = Updater::fetch_latest_release(); },
        std::runtime_error);
    try
    {
        auto r = Updater::fetch_latest_release();
    }
    catch (const std::runtime_error &e)
    {
        EXPECT_NE(std::string(e.what()).find("Failed to fetch release info"), std::string::npos);
    }
}

TEST_F(FetchLatestReleaseTest, SendsGithubTokenHeader)
{
    auto captured_args = std::make_shared<std::vector<std::string>>();
    Updater::set_curl_executor_for_testing(
        [captured_args](const std::string &, const std::vector<std::string> &args) -> std::string
        {
            *captured_args = args;
            return make_release_json("v2.0.0");
        });

#ifdef _WIN32
    _putenv_s("GITHUB_TOKEN", "test_token_123");
#else
    setenv("GITHUB_TOKEN", "test_token_123", 1);
#endif

    Updater::fetch_latest_release();

    Updater::reset_test_overrides();

    bool found_auth = false;
    for (size_t i = 0; i < captured_args->size(); ++i)
    {
        if ((*captured_args)[i] == "Authorization: Bearer test_token_123")
        {
            found_auth = true;
            break;
        }
    }
    EXPECT_TRUE(found_auth);
}

TEST_F(FetchLatestReleaseTest, NoAuthHeaderWithoutToken)
{
    auto captured_args = std::make_shared<std::vector<std::string>>();
    Updater::set_curl_executor_for_testing(
        [captured_args](const std::string &, const std::vector<std::string> &args) -> std::string
        {
            *captured_args = args;
            return make_release_json("v2.0.0");
        });

#ifdef _WIN32
    _putenv_s("GITHUB_TOKEN", "");
#else
    unsetenv("GITHUB_TOKEN");
#endif

    Updater::fetch_latest_release();
    Updater::reset_test_overrides();

    for (const auto &arg : *captured_args)
    {
        EXPECT_EQ(arg.find("Authorization"), std::string::npos);
    }
}

TEST_F(FetchLatestReleaseTest, GithubApiErrorMessage)
{
    setup_curl_mock(R"({"message": "Not Found", "status": "404"})");

    EXPECT_THROW(
        { auto r = Updater::fetch_latest_release(); },
        std::runtime_error);
    try
    {
        auto r = Updater::fetch_latest_release();
    }
    catch (const std::runtime_error &e)
    {
        EXPECT_NE(std::string(e.what()).find("Not Found"), std::string::npos);
    }
}

class ExecuteCurlTest : public MockedUpdaterTest
{
};

TEST_F(ExecuteCurlTest, UsesMockWhenSet)
{
    Updater::set_curl_executor_for_testing(
        [](const std::string &url, const std::vector<std::string> &args) -> std::string
        {
            return "mocked:" + url;
        });

    std::string result = Updater::execute_curl("https://example.com");
    EXPECT_EQ(result, "mocked:https://example.com");
}

TEST_F(ExecuteCurlTest, MockSeesExtraArgs)
{
    Updater::set_curl_executor_for_testing(
        [](const std::string &url, const std::vector<std::string> &args) -> std::string
        {
            return std::to_string(args.size());
        });

    std::string result = Updater::execute_curl("https://example.com", {"-H", "Accept: json"});
    EXPECT_EQ(result, "2");
}

class DownloadAssetTest : public MockedUpdaterTest
{
};

TEST_F(DownloadAssetTest, MockDownloaderReturnsTrue)
{
    Updater::set_file_downloader_for_testing(
        [](const std::string &url, const std::string &dest, int64_t size) -> bool
        {
            return true;
        });

    EXPECT_TRUE(Updater::download_asset("https://example.com/file", "/tmp/fake_dest"));
}

TEST_F(DownloadAssetTest, MockDownloaderReturnsFalse)
{
    Updater::set_file_downloader_for_testing(
        [](const std::string &url, const std::string &dest, int64_t size) -> bool
        {
            return false;
        });

    EXPECT_FALSE(Updater::download_asset("https://example.com/file", "/tmp/fake_dest"));
}

TEST_F(DownloadAssetTest, MockReceivesExpectedSize)
{
    int64_t captured_size = -1;
    Updater::set_file_downloader_for_testing(
        [&captured_size](const std::string &url, const std::string &dest, int64_t size) -> bool
        {
            captured_size = size;
            return true;
        });

    Updater::download_asset("https://example.com/file", "/tmp/fake_dest", 12345);
    EXPECT_EQ(captured_size, 12345);
}

TEST_F(DownloadAssetTest, RejectsNonHttpsUrl)
{
    EXPECT_FALSE(Updater::download_asset("file:///nonexistent/path/that/does/not/exist", "/tmp/tb_test_dl"));
}

class VerifyDownloadedFileTest : public ::testing::Test
{
protected:
    fs::path temp_file;

    void SetUp() override
    {
        temp_file = fs::temp_directory_path() / "tb_verify_test";
    }

    void TearDown() override
    {
        fs::remove(temp_file);
    }

    void write_file(const std::string &content)
    {
        std::ofstream f(temp_file, std::ios::binary);
        f << content;
    }
};

TEST_F(VerifyDownloadedFileTest, AcceptsCorrectSize)
{
    write_file(std::string(100, 'X'));
    EXPECT_TRUE(Updater::verify_downloaded_file(temp_file.string(), 100));
}

TEST_F(VerifyDownloadedFileTest, RejectsSizeMismatch)
{
    write_file(std::string(50, 'X'));
    EXPECT_FALSE(Updater::verify_downloaded_file(temp_file.string(), 100));
}

TEST_F(VerifyDownloadedFileTest, AcceptsAnySizeWhenExpectedIsZero)
{
    write_file("small");
    EXPECT_TRUE(Updater::verify_downloaded_file(temp_file.string(), 0));
}

TEST_F(VerifyDownloadedFileTest, RejectsEmptyFile)
{
    write_file("");
    EXPECT_FALSE(Updater::verify_downloaded_file(temp_file.string(), 0));
}

TEST_F(VerifyDownloadedFileTest, RejectsMissingFile)
{
    fs::remove(temp_file);
    EXPECT_FALSE(Updater::verify_downloaded_file(temp_file.string(), 0));
}

class GetExePathTest : public MockedUpdaterTest
{
};

TEST_F(GetExePathTest, ReturnsNonEmpty)
{
    std::string path = Updater::get_exe_path();
    EXPECT_FALSE(path.empty());
    EXPECT_TRUE(fs::exists(path)) << "get_exe_path returned non-existent path: " << path;
}

TEST_F(GetExePathTest, UsesOverride)
{
    Updater::set_exe_path_provider_for_testing([]() { return "/custom/path/binary"; });
    EXPECT_EQ(Updater::get_exe_path(), "/custom/path/binary");
}

class PerformUpdateTest : public ::testing::Test
{
protected:
    fs::path temp_dir;

    void SetUp() override
    {
        temp_dir = fs::temp_directory_path() / "tb_update_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override
    {
        fs::remove_all(temp_dir);
    }

    void write_file(const std::string &name, const std::string &content)
    {
        std::ofstream(temp_dir / name) << content;
    }

    std::string read_file(const std::string &name)
    {
        std::ifstream f(temp_dir / name);
        std::string content;
        std::getline(f, content);
        return content;
    }
};

TEST_F(PerformUpdateTest, SwapsBinariesAndKeepsBackupForRollback)
{
    std::string current = (temp_dir / "current_bin").string();
    std::string newbin = (temp_dir / "new_bin").string();

    write_file("current_bin", "old content");
    write_file("new_bin", "new content");

    EXPECT_EQ(Updater::perform_update(newbin, current), UpdateResult::Success);

    EXPECT_FALSE(fs::exists(temp_dir / "new_bin"));
    EXPECT_TRUE(fs::exists(temp_dir / "current_bin.old"));
    EXPECT_EQ(read_file("current_bin.old"), "old content");
    EXPECT_EQ(read_file("current_bin"), "new content");
}

TEST_F(PerformUpdateTest, ReplacesStaleBackupAtStartOfSwap)
{
    std::string current = (temp_dir / "current_bin").string();
    std::string newbin = (temp_dir / "new_bin").string();

    write_file("current_bin", "old");
    write_file("new_bin", "new");
    write_file("current_bin.old", "stale backup");

    EXPECT_EQ(Updater::perform_update(newbin, current), UpdateResult::Success);

    EXPECT_TRUE(fs::exists(temp_dir / "current_bin.old"));
    EXPECT_EQ(read_file("current_bin.old"), "old");
    EXPECT_EQ(read_file("current_bin"), "new");
}

TEST_F(PerformUpdateTest, FailsIfNewBinaryMissing)
{
    std::string current = (temp_dir / "current_bin").string();
    std::string newbin = (temp_dir / "nonexistent").string();

    write_file("current_bin", "old");

    EXPECT_THROW(Updater::perform_update(newbin, current), std::runtime_error);
}

class PerformRollbackTest : public ::testing::Test
{
protected:
    fs::path temp_dir;

    void SetUp() override
    {
        temp_dir = fs::temp_directory_path() / "tb_rollback_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override
    {
        fs::remove_all(temp_dir);
    }

    void write_file(const std::string &name, const std::string &content)
    {
        std::ofstream(temp_dir / name) << content;
    }

    std::string read_file(const std::string &name)
    {
        std::ifstream f(temp_dir / name);
        std::string content;
        std::getline(f, content);
        return content;
    }
};

TEST_F(PerformRollbackTest, RestoresOldVersion)
{
    std::string current = (temp_dir / "current_bin").string();

    write_file("current_bin", "new version");
    write_file("current_bin.old", "old version");

    EXPECT_EQ(Updater::perform_rollback(current), UpdateResult::Success);
    EXPECT_FALSE(fs::exists(temp_dir / "current_bin.old"));
    EXPECT_EQ(read_file("current_bin"), "old version");
}

TEST_F(PerformRollbackTest, FailsWithoutOldFile)
{
    std::string current = (temp_dir / "current_bin").string();
    write_file("current_bin", "new version");

    EXPECT_THROW(Updater::perform_rollback(current), std::runtime_error);
    try
    {
        Updater::perform_rollback(current);
    }
    catch (const std::runtime_error &e)
    {
        EXPECT_NE(std::string(e.what()).find("does not exist"), std::string::npos);
    }
}

TEST_F(PerformRollbackTest, FailsWithNonexistentCurrentDir)
{
    std::string current = (temp_dir / "no_such_dir" / "bin").string();
    std::string old_path = current + ".old";

    EXPECT_THROW(Updater::perform_rollback(current), std::runtime_error);
}

class VerifyChecksumTest : public ::testing::Test
{
protected:
    fs::path temp_dir;

    void SetUp() override
    {
        temp_dir = fs::temp_directory_path() / "tb_checksum_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override
    {
        fs::remove_all(temp_dir);
    }
};

TEST_F(VerifyChecksumTest, ReturnsFalseForMissingFile)
{
    EXPECT_FALSE(Updater::verify_checksum((temp_dir / "nonexistent").string(), "abc123"));
}

class VerifyChecksumMatchTest : public ::testing::Test
{
protected:
    fs::path temp_dir;

    void SetUp() override
    {
        temp_dir = fs::temp_directory_path() / "tb_checksum_match_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override
    {
        fs::remove_all(temp_dir);
    }
};

TEST_F(VerifyChecksumMatchTest, ReturnsTrueForCorrectHash)
{
    std::string filepath = (temp_dir / "test_file").string();
    {
        std::ofstream f(filepath);
        f << "hello world";
    }

    const std::string expected_hash =
        "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9";

    EXPECT_TRUE(Updater::verify_checksum(filepath, expected_hash));
}

TEST_F(VerifyChecksumMatchTest, ReturnsFalseForWrongHash)
{
    std::string filepath = (temp_dir / "test_file2").string();
    {
        std::ofstream f(filepath);
        f << "hello world";
    }

    EXPECT_FALSE(Updater::verify_checksum(filepath, "0000000000000000000000000000000000000000000000000000000000000000"));
}

TEST_F(VerifyChecksumMatchTest, ReturnsTrueForUpperCaseHash)
{
    std::string filepath = (temp_dir / "test_file3").string();
    {
        std::ofstream f(filepath);
        f << "test content";
    }

    const std::string expected_hash =
        "6AE8A75555209FD6C44157C0AED8016E763FF435A19CF186F76863140143FF72";

    EXPECT_TRUE(Updater::verify_checksum(filepath, expected_hash));
}

TEST_F(VerifyChecksumMatchTest, HandlesMultiChunkInput)
{
    std::string filepath = (temp_dir / "test_large").string();
    {
        std::ofstream f(filepath, std::ios::binary);
        f << std::string(10000, 'A');
    }

    const std::string expected_hash =
        "85757d9ef5868bb53472a6be8d81d1e3c398546b69b107141ad336053c40cb54";

    EXPECT_TRUE(Updater::verify_checksum(filepath, expected_hash));
}

TEST(BuildScriptLaunchCmdTest, ReturnsEnvVarCommand)
{
    EXPECT_EQ(Updater::build_script_launch_cmd(),
              "cmd /c start \"\" /b \"%TB_LAUNCH_SCRIPT%\"");
}

class GetAssetSizeTest : public ::testing::Test
{
};

TEST_F(GetAssetSizeTest, ReturnsSizeForMatchingUrl)
{
    nlohmann::json j = R"({
        "assets": [
            {"name": "binary", "browser_download_url": "https://example.com/binary", "size": 12345},
            {"name": "other", "browser_download_url": "https://example.com/other", "size": 99999}
        ]
    })"_json;

    EXPECT_EQ(Updater::get_asset_size(j, "https://example.com/binary"), 12345);
}

TEST_F(GetAssetSizeTest, ReturnsZeroForUnknownUrl)
{
    nlohmann::json j = R"({
        "assets": [
            {"name": "binary", "browser_download_url": "https://example.com/binary", "size": 12345}
        ]
    })"_json;

    EXPECT_EQ(Updater::get_asset_size(j, "https://example.com/nonexistent"), 0);
}

TEST_F(GetAssetSizeTest, ReturnsZeroForMissingSizeField)
{
    nlohmann::json j = R"({
        "assets": [
            {"name": "binary", "browser_download_url": "https://example.com/binary"}
        ]
    })"_json;

    EXPECT_EQ(Updater::get_asset_size(j, "https://example.com/binary"), 0);
}

class IsSafeUrlTest : public ::testing::Test
{
};

TEST_F(IsSafeUrlTest, AcceptsValidHttpsUrl)
{
    EXPECT_TRUE(Updater::is_safe_url("https://api.github.com/repos/user/repo/releases/latest"));
    EXPECT_TRUE(Updater::is_safe_url("https://example.com/file?query=1&other=2"));
    EXPECT_TRUE(Updater::is_safe_url("https://example.com/path%20with%20spaces"));
    EXPECT_TRUE(Updater::is_safe_url("https://example.com/%2F%2f%41%4a"));
}

TEST_F(IsSafeUrlTest, RejectsInvalidPercentEncoding)
{
    EXPECT_FALSE(Updater::is_safe_url("https://example.com/%PATH%"));
    EXPECT_FALSE(Updater::is_safe_url("https://example.com/%TEMP%file"));
    EXPECT_FALSE(Updater::is_safe_url("https://example.com/file%"));
    EXPECT_FALSE(Updater::is_safe_url("https://example.com/%2"));
    EXPECT_FALSE(Updater::is_safe_url("https://example.com/%GG"));
    EXPECT_FALSE(Updater::is_safe_url("https://example.com/100%real"));
}

TEST_F(IsSafeUrlTest, RejectsHttpUrl)
{
    EXPECT_FALSE(Updater::is_safe_url("http://example.com/file"));
}

TEST_F(IsSafeUrlTest, RejectsShellMetacharacters)
{
    EXPECT_FALSE(Updater::is_safe_url("https://example.com/file\"injection"));
    EXPECT_FALSE(Updater::is_safe_url("https://example.com/file$(whoami)"));
    EXPECT_FALSE(Updater::is_safe_url("https://example.com/file`id`"));
    EXPECT_FALSE(Updater::is_safe_url("https://example.com/file;rm -rf /"));
    EXPECT_FALSE(Updater::is_safe_url("https://example.com/file|cat"));
    EXPECT_FALSE(Updater::is_safe_url("https://example.com/file\\n"));
}

TEST_F(IsSafeUrlTest, RejectsEmptyString)
{
    EXPECT_FALSE(Updater::is_safe_url(""));
}

class FetchChecksumsTest : public MockedUpdaterTest
{
};

TEST_F(FetchChecksumsTest, ReturnsChecksumsWhenPresent)
{
    std::string release = make_release_json("v1.0.0", "", {
        {"checksums.txt", "https://example.com/checksums.txt"},
        {"torrent_builder_1.0.0_linux_amd64", "https://example.com/binary"}
    });
    int call_count = 0;
    Updater::set_curl_executor_for_testing(
        [&](const std::string &url, const std::vector<std::string> &) -> std::string
        {
            call_count++;
            if (url.find("checksums") != std::string::npos)
            {
                return "abc123  torrent_builder_1.0.0_linux_amd64\n";
            }
            return release;
        });

    auto result = Updater::fetch_checksums();
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->find("abc123"), std::string::npos);
}

TEST_F(FetchChecksumsTest, ReturnsNulloptWhenNoChecksumsAsset)
{
    std::string release = make_release_json("v1.0.0", "", {
        {"torrent_builder_1.0.0_linux_amd64", "https://example.com/binary"}
    });
    setup_curl_mock(release);

    auto result = Updater::fetch_checksums();
    EXPECT_FALSE(result.has_value());
}

TEST_F(FetchChecksumsTest, JsonOverloadAvoidsExtraApiCall)
{
    nlohmann::json j = nlohmann::json::parse(make_release_json("v1.0.0", "", {
        {"checksums.txt", "https://example.com/checksums.txt"},
        {"torrent_builder_1.0.0_linux_amd64", "https://example.com/binary"}
    }));

    int call_count = 0;
    Updater::set_curl_executor_for_testing(
        [&](const std::string &url, const std::vector<std::string> &) -> std::string
        {
            call_count++;
            return "abc123  torrent_builder_1.0.0_linux_amd64\n";
        });

    auto result = Updater::fetch_checksums(j);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(call_count, 1);
}

TEST_F(FetchChecksumsTest, JsonOverloadReturnsNulloptWhenNoChecksumsAsset)
{
    nlohmann::json j = nlohmann::json::parse(make_release_json("v1.0.0", "", {
        {"torrent_builder_1.0.0_linux_amd64", "https://example.com/binary"}
    }));

    auto result = Updater::fetch_checksums(j);
    EXPECT_FALSE(result.has_value());
}

TEST_F(FetchChecksumsTest, JsonOverloadReturnsNulloptOnCurlFailure)
{
    nlohmann::json j = nlohmann::json::parse(make_release_json("v1.0.0", "", {
        {"checksums.txt", "https://example.com/checksums.txt"},
        {"torrent_builder_1.0.0_linux_amd64", "https://example.com/binary"}
    }));

    Updater::set_curl_executor_for_testing(
        [](const std::string &url, const std::vector<std::string> &) -> std::string
        {
            throw std::runtime_error("Connection refused");
        });

    auto result = Updater::fetch_checksums(j);
    EXPECT_FALSE(result.has_value());
}

int handle_update_command(const std::vector<std::string> &args);

class HandleUpdateCommandTest : public MockedUpdaterTest
{
protected:
    std::streambuf *old_cout_ = nullptr;
    std::streambuf *old_cerr_ = nullptr;
    std::ostringstream captured_out_;
    std::ostringstream captured_err_;
    std::vector<fs::path> cleanup_paths_;

    void SetUp() override
    {
        old_cout_ = std::cout.rdbuf(captured_out_.rdbuf());
        old_cerr_ = std::cerr.rdbuf(captured_err_.rdbuf());
    }

    void TearDown() override
    {
        std::cout.rdbuf(old_cout_);
        std::cerr.rdbuf(old_cerr_);
        Updater::reset_test_overrides();
        for (const auto &p : cleanup_paths_)
            fs::remove_all(p);
    }

    static std::string release_with_version(const std::string &version)
    {
        auto platform = Updater::get_platform_info();
        std::string ext;
        if (platform.os == "windows")
            ext = ".exe";
        else if (platform.os == "macos")
            ext = ".zip";

        nlohmann::json j;
        j["tag_name"] = version;
        j["body"] = "Test release";
        j["assets"] = nlohmann::json::array();
        j["assets"].push_back({{"name", "torrent_builder_" + version + "_" + platform.os + "_" + platform.arch + ext},
                               {"browser_download_url", "https://example.com/binary"}});
        j["assets"].push_back({{"name", "checksums.txt"},
                               {"browser_download_url", "https://example.com/checksums.txt"}});
        return j.dump();
    }

    static std::string asset_name_for_version(const std::string &version)
    {
        auto platform = Updater::get_platform_info();
        std::string ext;
        if (platform.os == "windows")
            ext = ".exe";
        else if (platform.os == "macos")
            ext = ".zip";
        return "torrent_builder_" + version + "_" + platform.os + "_" + platform.arch + ext;
    }
};

TEST_F(HandleUpdateCommandTest, HelpReturnsZero)
{
    EXPECT_EQ(handle_update_command({"--help"}), 0);
    EXPECT_NE(captured_out_.str().find("update"), std::string::npos);
}

TEST_F(HandleUpdateCommandTest, RejectsEmptyVersionTag)
{
    nlohmann::json j;
    j["assets"] = nlohmann::json::array();
    setup_curl_mock(j.dump());

    EXPECT_EQ(handle_update_command({"--check"}), 1);
    EXPECT_NE(captured_err_.str().find("latest release version"), std::string::npos);
}

TEST_F(HandleUpdateCommandTest, CheckShowsUpdateAvailable)
{
    Updater::set_current_version_for_testing("0.5.0");
    setup_curl_mock(release_with_version("2.0.0"));

    EXPECT_EQ(handle_update_command({"--check"}), 0);
    EXPECT_NE(captured_out_.str().find("2.0.0"), std::string::npos);
    EXPECT_NE(captured_out_.str().find("Update available"), std::string::npos);
}

TEST_F(HandleUpdateCommandTest, ChangelogStripsAnsiEscapes)
{
    Updater::set_current_version_for_testing("0.1.0");
    std::string body = "\x1b[1;31;40mBold red\x1b[0m and \x1b[2Jclear and \x1b[Hhome";
    setup_curl_mock(make_release_json("v2.0.0", body, {
        {"torrent_builder_2.0.0_linux_amd64", "https://example.com/binary"}
    }));

    handle_update_command({"--check"});
    EXPECT_EQ(captured_out_.str().find('\x1b'), std::string::npos);
    EXPECT_NE(captured_out_.str().find("Bold red"), std::string::npos);
    EXPECT_NE(captured_out_.str().find("clear"), std::string::npos);
}

TEST_F(HandleUpdateCommandTest, ChangelogStripsOscAndCharsetEscapes)
{
    Updater::set_current_version_for_testing("0.1.0");
    std::string body = "\x1b]0;Title\x07OSC and \x1b(Bcharset\x1b[31mred\x1b[0m";
    setup_curl_mock(make_release_json("v2.0.0", body, {
        {"torrent_builder_2.0.0_linux_amd64", "https://example.com/binary"}
    }));

    handle_update_command({"--check"});
    EXPECT_EQ(captured_out_.str().find('\x1b'), std::string::npos);
    EXPECT_EQ(captured_out_.str().find("]0;Title"), std::string::npos);
    EXPECT_NE(captured_out_.str().find("red"), std::string::npos);
}

TEST_F(HandleUpdateCommandTest, CheckShowsAlreadyUpToDate)
{
    Updater::set_current_version_for_testing("2.0.0");
    setup_curl_mock(release_with_version("2.0.0"));

    EXPECT_EQ(handle_update_command({"--check"}), 0);
    EXPECT_NE(captured_out_.str().find("Already up to date"), std::string::npos);
}

TEST_F(HandleUpdateCommandTest, DevVersionAlwaysShowsUpdate)
{
    Updater::set_current_version_for_testing("dev");
    setup_curl_mock(release_with_version("0.0.0"));

    EXPECT_EQ(handle_update_command({"--check"}), 0);
    std::string output = captured_out_.str();
    EXPECT_TRUE(output.find("Update available") != std::string::npos ||
                output.find("Latest") != std::string::npos);
}

TEST_F(HandleUpdateCommandTest, FetchFailureReturnsOne)
{
    setup_curl_mock_throwing("Network error");

    EXPECT_EQ(handle_update_command({"--check"}), 1);
}

TEST_F(HandleUpdateCommandTest, YesFlagSkipsPrompt)
{
    Updater::set_current_version_for_testing("0.1.0");
    setup_curl_mock(release_with_version("2.0.0"));

    fs::path temp = fs::temp_directory_path() / "tb_handle_update_test";
    fs::create_directories(temp);
    cleanup_paths_.push_back(temp);
    std::string fake_bin = (temp / "current").string();
    std::string old_bin = fake_bin + ".old";
    {
        std::ofstream(fake_bin) << "old binary";
    }
    fs::permissions(fake_bin, fs::perms::owner_all);

    Updater::set_exe_path_provider_for_testing([fake_bin]() { return fake_bin; });

    Updater::set_binary_validator_for_testing([](const std::string &) { return true; });

    Updater::set_file_downloader_for_testing(
        [temp](const std::string &, const std::string &dest_path, int64_t) -> bool
        {
#ifdef __APPLE__
            return create_mock_zip(dest_path, "new binary content for update");
#else
            std::ofstream(dest_path) << "new binary content for update";
            return true;
#endif
        });

    int rc = handle_update_command({"--yes"});

    Updater::reset_test_overrides();

    EXPECT_EQ(rc, 0);
}

TEST_F(HandleUpdateCommandTest, DownloadFailureReturnsOne)
{
    Updater::set_current_version_for_testing("0.1.0");
    setup_curl_mock(release_with_version("2.0.0"));

    Updater::set_file_downloader_for_testing(
        [](const std::string &, const std::string &, int64_t) -> bool
        {
            return false;
        });

    Updater::set_exe_path_provider_for_testing([]() { return "/tmp/fake_binary"; });

    EXPECT_EQ(handle_update_command({"--yes"}), 1);
}

TEST_F(HandleUpdateCommandTest, RollbackSuccess)
{
    fs::path temp = fs::temp_directory_path() / "tb_rollback_cmd_test";
    fs::create_directories(temp);
    cleanup_paths_.push_back(temp);
    std::string fake_bin = (temp / "current").string();
    std::string old_bin = fake_bin + ".old";

    {
        std::ofstream(fake_bin) << "new binary";
        std::ofstream(old_bin) << "old binary";
    }
    fs::permissions(fake_bin, fs::perms::owner_all);
    fs::permissions(old_bin, fs::perms::owner_all);

    Updater::set_exe_path_provider_for_testing([fake_bin]() { return fake_bin; });

    std::istringstream input("y\n");
    auto *old_cin = std::cin.rdbuf(input.rdbuf());

    int rc = handle_update_command({"--rollback"});

    std::cin.rdbuf(old_cin);

    EXPECT_EQ(rc, 0);
}

TEST_F(HandleUpdateCommandTest, RollbackCancelled)
{
    fs::path temp = fs::temp_directory_path() / "tb_rollback_cancel_test";
    fs::create_directories(temp);
    cleanup_paths_.push_back(temp);
    std::string fake_bin = (temp / "current").string();
    std::string old_bin = fake_bin + ".old";

    {
        std::ofstream(fake_bin) << "new";
        std::ofstream(old_bin) << "old";
    }

    Updater::set_exe_path_provider_for_testing([fake_bin]() { return fake_bin; });

    std::istringstream input("n\n");
    auto *old_cin = std::cin.rdbuf(input.rdbuf());

    int rc = handle_update_command({"--rollback"});

    std::cin.rdbuf(old_cin);

    EXPECT_EQ(rc, 0);
    EXPECT_NE(captured_out_.str().find("cancelled"), std::string::npos);
}

TEST_F(HandleUpdateCommandTest, RollbackNoOldFileReturnsOne)
{
    Updater::set_exe_path_provider_for_testing([]() { return "/tmp/nonexistent_binary_tb_test"; });

    std::istringstream input("y\n");
    auto *old_cin = std::cin.rdbuf(input.rdbuf());

    int rc = handle_update_command({"--rollback"});

    std::cin.rdbuf(old_cin);

    EXPECT_EQ(rc, 1);
}

TEST_F(HandleUpdateCommandTest, RollbackYesFlagSkipsPrompt)
{
    fs::path temp = fs::temp_directory_path() / "tb_rollback_yes_test";
    fs::create_directories(temp);
    cleanup_paths_.push_back(temp);
    std::string fake_bin = (temp / "current").string();
    std::string old_bin = fake_bin + ".old";

    {
        std::ofstream(fake_bin) << "new binary";
        std::ofstream(old_bin) << "old binary";
    }
    fs::permissions(fake_bin, fs::perms::owner_all);
    fs::permissions(old_bin, fs::perms::owner_all);

    Updater::set_exe_path_provider_for_testing([fake_bin]() { return fake_bin; });

    std::istringstream empty_input("");
    auto *old_cin = std::cin.rdbuf(empty_input.rdbuf());

    int rc = handle_update_command({"--rollback", "--yes"});

    std::cin.rdbuf(old_cin);

    EXPECT_EQ(rc, 0);
    EXPECT_NE(captured_out_.str().find("rolled back"), std::string::npos);
    EXPECT_TRUE(fs::exists(fake_bin));
}

TEST_F(HandleUpdateCommandTest, NoMatchingPlatformReturnsOne)
{
    Updater::set_current_version_for_testing("0.1.0");

    nlohmann::json j;
    j["tag_name"] = "v99.0.0";
    j["body"] = "";
    j["assets"] = nlohmann::json::array();
    j["assets"].push_back({{"name", "torrent_builder_99.0.0_windows_amd64.exe"},
                           {"browser_download_url", "https://example.com/win_only"}});
    setup_curl_mock(j.dump());

    EXPECT_EQ(handle_update_command({"--yes"}), 1);
}

TEST_F(HandleUpdateCommandTest, ChecksumVerificationSuccess)
{
    Updater::set_current_version_for_testing("0.1.0");

    std::string asset_name = asset_name_for_version("2.0.0");

    std::string test_content = "new binary content for checksum test";
    std::string actual_hash =
        "6356eb22316bf8a23ea42ef2d7ede5ea829140b4dae064b01b96f9e9c9b5a1fa";

    nlohmann::json j;
    j["tag_name"] = "v2.0.0";
    j["body"] = "Test release";
    j["assets"] = nlohmann::json::array();
    j["assets"].push_back({{"name", asset_name},
                           {"browser_download_url", "https://github.com/user/repo/releases/download/v2.0.0/" + asset_name}});
    j["assets"].push_back({{"name", "checksums.txt"},
                           {"browser_download_url", "https://github.com/user/repo/releases/download/v2.0.0/checksums.txt"}});

    std::string captured_hash;

    Updater::set_curl_executor_for_testing(
        [j, actual_hash, asset_name, &captured_hash](const std::string &url, const std::vector<std::string> &) -> std::string
        {
            if (url.find("checksums") != std::string::npos)
            {
                std::string hash = captured_hash.empty() ? actual_hash : captured_hash;
                return hash + "  " + asset_name + "\n";
            }
            return j.dump();
        });

    fs::path temp = fs::temp_directory_path() / "tb_checksum_verify_test";
    fs::create_directories(temp);
    cleanup_paths_.push_back(temp);
    std::string fake_bin = (temp / "current").string();
    {
        std::ofstream(fake_bin) << "old binary";
    }
    fs::permissions(fake_bin, fs::perms::owner_all);

    Updater::set_exe_path_provider_for_testing([fake_bin]() { return fake_bin; });

    Updater::set_file_downloader_for_testing(
        [&captured_hash, test_content](const std::string &, const std::string &dest_path, int64_t) -> bool
        {
#ifdef __APPLE__
            bool ok = create_mock_zip(dest_path, test_content);
            if (ok)
                captured_hash = sha256_file(dest_path);
            return ok;
#else
            std::ofstream(dest_path) << test_content;
            return true;
#endif
        });

    int rc = handle_update_command({"--yes"});

    Updater::reset_test_overrides();

    EXPECT_EQ(rc, 0);
    EXPECT_NE(captured_out_.str().find("Checksum verified"), std::string::npos);
}

TEST_F(HandleUpdateCommandTest, ChecksumMismatchReturnsOne)
{
    Updater::set_current_version_for_testing("0.1.0");

    std::string asset_name = asset_name_for_version("2.0.0");

    nlohmann::json j;
    j["tag_name"] = "v2.0.0";
    j["body"] = "Test release";
    j["assets"] = nlohmann::json::array();
    j["assets"].push_back({{"name", asset_name},
                           {"browser_download_url", "https://github.com/user/repo/releases/download/v2.0.0/" + asset_name}});
    j["assets"].push_back({{"name", "checksums.txt"},
                           {"browser_download_url", "https://github.com/user/repo/releases/download/v2.0.0/checksums.txt"}});

    Updater::set_curl_executor_for_testing(
        [j, asset_name](const std::string &url, const std::vector<std::string> &) -> std::string
        {
            if (url.find("checksums") != std::string::npos)
            {
                return "0000000000000000000000000000000000000000000000000000000000000000  " + asset_name + "\n";
            }
            return j.dump();
        });

    fs::path temp = fs::temp_directory_path() / "tb_checksum_mismatch_test";
    fs::create_directories(temp);
    cleanup_paths_.push_back(temp);
    std::string fake_bin = (temp / "current").string();
    {
        std::ofstream(fake_bin) << "old binary";
    }
    fs::permissions(fake_bin, fs::perms::owner_all);

    Updater::set_exe_path_provider_for_testing([fake_bin]() { return fake_bin; });

    Updater::set_file_downloader_for_testing(
        [](const std::string &, const std::string &dest_path, int64_t) -> bool
        {
            std::ofstream(dest_path) << "downloaded binary content";
            return true;
        });

    int rc = handle_update_command({"--yes"});

    Updater::reset_test_overrides();

    EXPECT_EQ(rc, 1);
    EXPECT_NE(captured_err_.str().find("Checksum verification failed"), std::string::npos);
}

TEST_F(HandleUpdateCommandTest, DevVersionBypassWhenNewer)
{
    Updater::set_current_version_for_testing("dev");
    setup_curl_mock(release_with_version("0.0.1"));

    EXPECT_EQ(handle_update_command({"--check"}), 0);
    std::string output = captured_out_.str();
    EXPECT_TRUE(output.find("Update available") != std::string::npos ||
                output.find("Latest") != std::string::npos);
}

TEST_F(HandleUpdateCommandTest, DevVersionAheadOfReleaseDeclinesUpdate)
{
    Updater::set_current_version_for_testing("2.0.0-dev");
    setup_curl_mock(release_with_version("1.0.0"));

    EXPECT_EQ(handle_update_command({"--check"}), 0);
    EXPECT_NE(captured_out_.str().find("newer than latest release"), std::string::npos);
}

TEST_F(HandleUpdateCommandTest, UserDeclinesUpdate)
{
    Updater::set_current_version_for_testing("0.1.0");
    setup_curl_mock(release_with_version("2.0.0"));

    Updater::set_exe_path_provider_for_testing([]() { return "/tmp/fake_binary"; });

    std::istringstream input("n\n");
    auto *old_cin = std::cin.rdbuf(input.rdbuf());

    int rc = handle_update_command({});

    std::cin.rdbuf(old_cin);

    EXPECT_EQ(rc, 0);
    EXPECT_NE(captured_out_.str().find("cancelled"), std::string::npos);
}

TEST_F(HandleUpdateCommandTest, ProceedsWithoutChecksumsWhenAbsentFromRelease)
{
    Updater::set_current_version_for_testing("0.1.0");

    std::string asset_name = asset_name_for_version("2.0.0");

    nlohmann::json j;
    j["tag_name"] = "v2.0.0";
    j["body"] = "Test release";
    j["assets"] = nlohmann::json::array();
    j["assets"].push_back({{"name", asset_name},
                           {"browser_download_url", "https://github.com/user/repo/releases/download/v2.0.0/" + asset_name}});

    setup_curl_mock(j.dump());

    fs::path temp = fs::temp_directory_path() / "tb_no_checksums_test";
    fs::create_directories(temp);
    cleanup_paths_.push_back(temp);
    std::string fake_bin = (temp / "current").string();
    {
        std::ofstream(fake_bin) << "old binary";
    }
    fs::permissions(fake_bin, fs::perms::owner_all);

    Updater::set_exe_path_provider_for_testing([fake_bin]() { return fake_bin; });

    Updater::set_binary_validator_for_testing([](const std::string &) { return true; });

    Updater::set_file_downloader_for_testing(
        [](const std::string &, const std::string &dest_path, int64_t) -> bool
        {
#ifdef __APPLE__
            return create_mock_zip(dest_path, "new binary without checksums");
#else
            std::ofstream(dest_path) << "new binary without checksums";
            return true;
#endif
        });

    int rc = handle_update_command({"--yes"});

    Updater::reset_test_overrides();

    EXPECT_EQ(rc, 0);
    EXPECT_NE(captured_out_.str().find("Successfully updated"), std::string::npos);
}

TEST_F(HandleUpdateCommandTest, ProceedsWithoutChecksumWhenAssetNotListed)
{
    Updater::set_current_version_for_testing("0.1.0");

    std::string asset_name = asset_name_for_version("2.0.0");

    nlohmann::json j;
    j["tag_name"] = "v2.0.0";
    j["body"] = "Test release";
    j["assets"] = nlohmann::json::array();
    j["assets"].push_back({{"name", asset_name},
                           {"browser_download_url", "https://github.com/user/repo/releases/download/v2.0.0/" + asset_name}});
    j["assets"].push_back({{"name", "checksums.txt"},
                           {"browser_download_url", "https://github.com/user/repo/releases/download/v2.0.0/checksums.txt"}});

    Updater::set_curl_executor_for_testing(
        [j](const std::string &url, const std::vector<std::string> &) -> std::string
        {
            if (url.find("checksums") != std::string::npos)
            {
                return "abcdef1234567890  other_platform_binary\n";
            }
            return j.dump();
        });

    fs::path temp = fs::temp_directory_path() / "tb_no_asset_checksum_test";
    fs::create_directories(temp);
    cleanup_paths_.push_back(temp);
    std::string fake_bin = (temp / "current").string();
    {
        std::ofstream(fake_bin) << "old binary";
    }
    fs::permissions(fake_bin, fs::perms::owner_all);

    Updater::set_exe_path_provider_for_testing([fake_bin]() { return fake_bin; });

    Updater::set_binary_validator_for_testing([](const std::string &) { return true; });

    Updater::set_file_downloader_for_testing(
        [](const std::string &, const std::string &dest_path, int64_t) -> bool
        {
#ifdef __APPLE__
            return create_mock_zip(dest_path, "new binary asset not in checksums");
#else
            std::ofstream(dest_path) << "new binary asset not in checksums";
            return true;
#endif
        });

    int rc = handle_update_command({"--yes"});

    Updater::reset_test_overrides();

    EXPECT_EQ(rc, 0);
    EXPECT_NE(captured_out_.str().find("Successfully updated"), std::string::npos);
}

TEST_F(HandleUpdateCommandTest, RejectsInvalidBinaryWithoutChecksum)
{
    Updater::set_current_version_for_testing("0.1.0");

    std::string asset_name = asset_name_for_version("2.0.0");

    nlohmann::json j;
    j["tag_name"] = "v2.0.0";
    j["body"] = "Test release";
    j["assets"] = nlohmann::json::array();
    j["assets"].push_back({{"name", asset_name},
                           {"browser_download_url", "https://github.com/user/repo/releases/download/v2.0.0/" + asset_name}});

    Updater::set_curl_executor_for_testing(
        [j](const std::string &, const std::vector<std::string> &) -> std::string
        {
            return j.dump();
        });

    Updater::set_exe_path_provider_for_testing([]() { return "/nonexistent/binary"; });

    Updater::set_binary_validator_for_testing([](const std::string &) { return false; });

    Updater::set_file_downloader_for_testing(
        [](const std::string &, const std::string &dest_path, int64_t) -> bool
        {
            std::ofstream(dest_path) << "not a valid binary";
            return true;
        });

    int rc = handle_update_command({"--yes"});

    Updater::reset_test_overrides();

    EXPECT_EQ(rc, 1);
    EXPECT_NE(captured_err_.str().find("not a valid binary"), std::string::npos);
}

class IsValidBinaryTest : public ::testing::Test
{
protected:
    void SetUp() override { Updater::reset_test_overrides(); }
    void TearDown() override { Updater::reset_test_overrides(); }
};

TEST_F(IsValidBinaryTest, RecognizesElfMagic)
{
    auto path = fs::temp_directory_path() / "test_elf_binary";
    {
        std::ofstream f(path, std::ios::binary);
        unsigned char elf[] = {0x7f, 'E', 'L', 'F', 0x02, 0x01, 0x01, 0x00};
        f.write(reinterpret_cast<char *>(elf), sizeof(elf));
    }
    EXPECT_TRUE(Updater::is_valid_binary(path.string()));
    fs::remove(path);
}

TEST_F(IsValidBinaryTest, RecognizesPeMagic)
{
    auto path = fs::temp_directory_path() / "test_pe_binary";
    {
        std::ofstream f(path, std::ios::binary);
        unsigned char pe[] = {'M', 'Z', 0x90, 0x00};
        f.write(reinterpret_cast<char *>(pe), sizeof(pe));
    }
    EXPECT_TRUE(Updater::is_valid_binary(path.string()));
    fs::remove(path);
}

TEST_F(IsValidBinaryTest, RecognizesMachO64Magic)
{
    auto path = fs::temp_directory_path() / "test_macho_binary";
    {
        std::ofstream f(path, std::ios::binary);
        unsigned char macho[] = {0xfe, 0xed, 0xfa, 0xcf, 0x00, 0x00, 0x00, 0x01};
        f.write(reinterpret_cast<char *>(macho), sizeof(macho));
    }
    EXPECT_TRUE(Updater::is_valid_binary(path.string()));
    fs::remove(path);
}

TEST_F(IsValidBinaryTest, RecognizesMachO32Magic)
{
    auto path = fs::temp_directory_path() / "test_macho32_binary";
    {
        std::ofstream f(path, std::ios::binary);
        unsigned char macho[] = {0xfe, 0xed, 0xfa, 0xce, 0x00, 0x00, 0x00, 0x01};
        f.write(reinterpret_cast<char *>(macho), sizeof(macho));
    }
    EXPECT_TRUE(Updater::is_valid_binary(path.string()));
    fs::remove(path);
}

TEST_F(IsValidBinaryTest, RejectsNonBinary)
{
    auto path = fs::temp_directory_path() / "test_text_file";
    {
        std::ofstream f(path);
        f << "Hello, this is a text file";
    }
    EXPECT_FALSE(Updater::is_valid_binary(path.string()));
    fs::remove(path);
}

#ifdef __APPLE__
TEST_F(IsValidBinaryTest, RecognizesZipMagic)
{
    auto path = fs::temp_directory_path() / "test_zip_file";
    {
        std::ofstream f(path, std::ios::binary);
        unsigned char zip_magic[] = {0x50, 0x4b, 0x03, 0x04};
        f.write(reinterpret_cast<const char *>(zip_magic), 4);
        f << "fake zip content";
    }
    EXPECT_TRUE(Updater::is_valid_binary(path.string()));
    fs::remove(path);
}
#endif

TEST_F(IsValidBinaryTest, RejectsMissingFile)
{
    EXPECT_FALSE(Updater::is_valid_binary("/nonexistent/path/binary"));
}

TEST_F(IsValidBinaryTest, RejectsEmptyFile)
{
    auto path = fs::temp_directory_path() / "test_empty_file";
    {
        std::ofstream f(path);
    }
    EXPECT_FALSE(Updater::is_valid_binary(path.string()));
    fs::remove(path);
}

class PerformUpdateFailureTest : public ::testing::Test
{
protected:
    fs::path temp_dir;

    void SetUp() override
    {
        temp_dir = fs::temp_directory_path() / "tb_update_fail_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override
    {
        fs::remove_all(temp_dir);
    }

    void write_file(const std::string &name, const std::string &content)
    {
        std::ofstream(temp_dir / name) << content;
    }

    std::string read_file(const std::string &name)
    {
        std::ifstream f(temp_dir / name);
        std::string content;
        std::getline(f, content);
        return content;
    }
};

TEST_F(PerformUpdateFailureTest, SwapsWhenExtraDirExists)
{
    std::string current = (temp_dir / "current_bin").string();
    std::string newbin = (temp_dir / "new_bin").string();

    write_file("current_bin", "old content");
    write_file("new_bin", "new content");

    fs::path extra_dir = temp_dir / "extra";
    fs::create_directories(extra_dir);

    EXPECT_EQ(Updater::perform_update(newbin, current), UpdateResult::Success);

    EXPECT_FALSE(fs::exists(temp_dir / "new_bin"));
    EXPECT_TRUE(fs::exists(temp_dir / "current_bin.old"));
    EXPECT_EQ(read_file("current_bin.old"), "old content");
    EXPECT_EQ(read_file("current_bin"), "new content");
}

TEST_F(PerformUpdateFailureTest, FailsWhenCurrentBinaryNotWritable)
{
#ifdef _WIN32
    GTEST_SKIP() << "POSIX-only: fs::perms::none does not prevent writes on Windows";
#else
    std::string current = (temp_dir / "readonly_dir" / "current_bin").string();
    std::string newbin = (temp_dir / "new_bin").string();

    fs::create_directories(temp_dir / "readonly_dir");
    write_file("readonly_dir/current_bin", "old");
    write_file("new_bin", "new");

    fs::permissions(temp_dir / "readonly_dir", fs::perms::none);

    EXPECT_THROW(Updater::perform_update(newbin, current), std::runtime_error);

    fs::permissions(temp_dir / "readonly_dir", fs::perms::owner_all);
#endif
}

TEST_F(PerformUpdateFailureTest, RollbackRestoresOriginalOnSecondRenameFailure)
{
#ifdef _WIN32
    GTEST_SKIP() << "POSIX-only: relies on directory permission to trigger rename failure";
#else
    fs::path src_dir = temp_dir / "srcdir";
    fs::path tgt_dir = temp_dir / "tgtdir";
    fs::create_directories(src_dir);
    fs::create_directories(tgt_dir);

    std::string current = (tgt_dir / "current_bin").string();
    std::string newbin = (src_dir / "new_bin").string();

    {
        std::ofstream(current) << "original content";
        std::ofstream(newbin) << "new content";
    }

    fs::permissions(src_dir, fs::perms::owner_read | fs::perms::owner_exec);

    try
    {
        Updater::perform_update(newbin, current);
        FAIL() << "Expected std::runtime_error";
    }
    catch (const std::runtime_error &e)
    {
        EXPECT_NE(std::string(e.what()).find("rolled back"), std::string::npos);
    }

    fs::permissions(src_dir, fs::perms::owner_all);

    EXPECT_TRUE(fs::exists(tgt_dir / "current_bin"));
    EXPECT_FALSE(fs::exists(tgt_dir / "current_bin.old"));

    std::ifstream f(current);
    std::string content;
    std::getline(f, content);
    EXPECT_EQ(content, "original content");
#endif
}

// ─────────────────────────────────────────────────────────────
// Startup update check (issue #59)
// ─────────────────────────────────────────────────────────────

class CheckForUpdateTest : public ::testing::Test
{
protected:
    void TearDown() override { Updater::reset_test_overrides(); }

    // Returns a curl executor that serves a fixed GitHub release JSON.
    Updater::CurlExecutor make_release_executor(const std::string &tag)
    {
        return [tag](const std::string & /*url*/, const std::vector<std::string> & /*args*/) {
            return std::string("{\"tag_name\":\"") + tag +
                   "\",\"body\":\"release notes\",\"assets\":[]}";
        };
    }
};

TEST_F(CheckForUpdateTest, ReturnsInfoWhenNewer)
{
    Updater::set_current_version_for_testing("1.0.0");
    Updater::set_curl_executor_for_testing(make_release_executor("v1.4.2"));

    auto r = Updater::check_for_update();
    EXPECT_TRUE(r.fetch_succeeded);
    ASSERT_TRUE(r.info.has_value());
    EXPECT_EQ(r.info->version, "1.4.2");
}

TEST_F(CheckForUpdateTest, ReturnsNulloptWhenUpToDate)
{
    Updater::set_current_version_for_testing("1.4.2");
    Updater::set_curl_executor_for_testing(make_release_executor("v1.4.2"));

    auto r = Updater::check_for_update();
    EXPECT_TRUE(r.fetch_succeeded);   // fetched OK, just no newer version
    EXPECT_FALSE(r.info.has_value());
}

TEST_F(CheckForUpdateTest, ReturnsNulloptWhenCurrentNewer)
{
    Updater::set_current_version_for_testing("2.0.0");
    Updater::set_curl_executor_for_testing(make_release_executor("v1.4.2"));

    auto r = Updater::check_for_update();
    EXPECT_TRUE(r.fetch_succeeded);
    EXPECT_FALSE(r.info.has_value());
}

TEST_F(CheckForUpdateTest, ReturnsLatestForDevBuild)
{
    // dev builds always compare as older than any release → always report latest
    Updater::set_current_version_for_testing("dev (a3f9c21)");
    Updater::set_curl_executor_for_testing(make_release_executor("v1.4.2"));

    auto r = Updater::check_for_update();
    EXPECT_TRUE(r.fetch_succeeded);
    ASSERT_TRUE(r.info.has_value());
    EXPECT_EQ(r.info->version, "1.4.2");
}

TEST_F(CheckForUpdateTest, ReturnsLatestForPlainDevVersion)
{
    Updater::set_current_version_for_testing("dev");
    Updater::set_curl_executor_for_testing(make_release_executor("v0.1.0"));

    auto r = Updater::check_for_update();
    EXPECT_TRUE(r.fetch_succeeded);
    ASSERT_TRUE(r.info.has_value());
    EXPECT_EQ(r.info->version, "0.1.0");
}

TEST_F(CheckForUpdateTest, FetchFailsWhenOffline)
{
    Updater::set_current_version_for_testing("1.0.0");
    Updater::set_curl_executor_for_testing(
        [](const std::string &, const std::vector<std::string> &) -> std::string {
            throw std::runtime_error("connection refused");
        });

    auto r = Updater::check_for_update();
    EXPECT_FALSE(r.fetch_succeeded);  // offline → must NOT be recorded as success
    EXPECT_FALSE(r.info.has_value());
}

TEST_F(CheckForUpdateTest, ReturnsNulloptOnEmptyVersion)
{
    Updater::set_current_version_for_testing("1.0.0");
    Updater::set_curl_executor_for_testing(
        [](const std::string &, const std::vector<std::string> &) {
            return std::string("{\"tag_name\":\"\",\"body\":\"\",\"assets\":[]}");
        });

    auto r = Updater::check_for_update();
    EXPECT_TRUE(r.fetch_succeeded);
    EXPECT_FALSE(r.info.has_value());
}

TEST_F(CheckForUpdateTest, FetchFailsOnRateLimit)
{
    Updater::set_current_version_for_testing("1.0.0");
    Updater::set_curl_executor_for_testing(
        [](const std::string &, const std::vector<std::string> &) {
            return std::string("{\"message\":\"API rate limit exceeded\"}");
        });

    auto r = Updater::check_for_update();
    EXPECT_FALSE(r.fetch_succeeded);  // rate-limit error → fetch did not succeed
    EXPECT_FALSE(r.info.has_value());
}

TEST_F(CheckForUpdateTest, DoesNotThrowOnNetworkError)
{
    Updater::set_current_version_for_testing("1.0.0");
    Updater::set_curl_executor_for_testing(
        [](const std::string &, const std::vector<std::string> &) -> std::string {
            throw std::runtime_error("boom");
        });

    EXPECT_NO_THROW({ (void)Updater::check_for_update(); });
}

// ─── Throttle / state file ───

class UpdateCheckThrottleTest : public ::testing::Test
{
protected:
    fs::path temp_dir_;

    void SetUp() override
    {
        temp_dir_ = fs::temp_directory_path() / ("tb_throttle_" + std::to_string(std::time(nullptr)));
        fs::create_directories(temp_dir_);
        Updater::set_update_check_state_path_for_testing(
            (temp_dir_ / "update_check.state").string());
    }

    void TearDown() override
    {
        Updater::reset_test_overrides();
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    void write_state(long long epoch)
    {
        std::ofstream out(temp_dir_ / "update_check.state", std::ios::trunc);
        ASSERT_TRUE(out) << "could not write state file";
        out << "last_check=" << epoch << "\n";
    }
};

TEST_F(UpdateCheckThrottleTest, StatePathIsOverridden)
{
    std::string p = Updater::get_update_check_state_path();
    EXPECT_EQ(p, (temp_dir_ / "update_check.state").string());
}

TEST_F(UpdateCheckThrottleTest, TrueOnFirstRun)
{
    EXPECT_TRUE(Updater::should_check_for_updates(24));
}

TEST_F(UpdateCheckThrottleTest, FalseWithinInterval)
{
    write_state(static_cast<long long>(std::time(nullptr)));
    EXPECT_FALSE(Updater::should_check_for_updates(24));
}

TEST_F(UpdateCheckThrottleTest, TrueAfterInterval)
{
    long long now = static_cast<long long>(std::time(nullptr));
    write_state(now - 25 * 3600);
    EXPECT_TRUE(Updater::should_check_for_updates(24));
}

TEST_F(UpdateCheckThrottleTest, TrueOnCorruptFile)
{
    {
        std::ofstream out(temp_dir_ / "update_check.state", std::ios::trunc);
        out << "garbage content that is not a timestamp\n";
    }
    EXPECT_TRUE(Updater::should_check_for_updates(24));
}

TEST_F(UpdateCheckThrottleTest, RecordPersistsTimestamp)
{
    ASSERT_TRUE(Updater::should_check_for_updates(24));
    Updater::record_update_check();
    EXPECT_FALSE(Updater::should_check_for_updates(24));
}

TEST_F(UpdateCheckThrottleTest, RecordIsAtomic)
{
    Updater::record_update_check();
    // No leftover temp file after a successful record — scan for any
    // update_check.state.tmp.* sibling (the suffix is randomised).
    bool found_tmp = false;
    for (const auto &entry : fs::directory_iterator(temp_dir_))
    {
        if (entry.path().filename().string().starts_with("update_check.state.tmp"))
        {
            found_tmp = true;
            break;
        }
    }
    EXPECT_FALSE(found_tmp);
    EXPECT_TRUE(fs::exists(temp_dir_ / "update_check.state"));
}

TEST_F(UpdateCheckThrottleTest, EmptyPathMeansAlwaysCheck)
{
    // Empty override string simulates an unresolvable home directory → state
    // path is "". should_check must return true (treated as "never checked")
    // and record must be a silent no-op (no file created, no real path touched).
    Updater::set_update_check_state_path_for_testing(std::string{""});
    EXPECT_TRUE(Updater::should_check_for_updates());
    Updater::record_update_check();
    EXPECT_FALSE(fs::exists(temp_dir_ / "update_check.state"));
}

// ─── Platform state-path resolution (real env vars, override cleared) ───

class UpdateCheckStatePathTest : public ::testing::Test
{
protected:
    void TearDown() override { Updater::reset_test_overrides(); }
};

TEST_F(UpdateCheckStatePathTest, LinuxXdgConfigHomeUsed)
{
#ifndef __linux__
    GTEST_SKIP() << "Linux-only path test";
#else
    Updater::set_update_check_state_path_for_testing(std::nullopt);
    fs::path tmp = fs::temp_directory_path() / ("tb_xdg_" + std::to_string(std::time(nullptr)));
    fs::create_directories(tmp);

    char prev_xdg[4096] = {0};
    bool had_xdg = (std::getenv("XDG_CONFIG_HOME") != nullptr);
    if (had_xdg)
    {
        std::strncpy(prev_xdg, std::getenv("XDG_CONFIG_HOME"), sizeof(prev_xdg) - 1);
    }
    ::setenv("XDG_CONFIG_HOME", tmp.c_str(), 1);

    std::string path = Updater::get_update_check_state_path();
    EXPECT_EQ(path, (tmp / "torrent-builder" / "update_check.state").string());

    if (had_xdg)
        ::setenv("XDG_CONFIG_HOME", prev_xdg, 1);
    else
        ::unsetenv("XDG_CONFIG_HOME");

    std::error_code ec;
    fs::remove_all(tmp, ec);
#endif
}

TEST_F(UpdateCheckStatePathTest, LinuxHomeFallbackUsed)
{
#ifndef __linux__
    GTEST_SKIP() << "Linux-only path test";
#else
    Updater::set_update_check_state_path_for_testing(std::nullopt);
    fs::path tmp = fs::temp_directory_path() / ("tb_home_" + std::to_string(std::time(nullptr)));
    fs::create_directories(tmp);

    // Clear XDG so HOME fallback engages
    char prev_xdg[4096] = {0};
    bool had_xdg = (std::getenv("XDG_CONFIG_HOME") != nullptr);
    if (had_xdg)
        std::strncpy(prev_xdg, std::getenv("XDG_CONFIG_HOME"), sizeof(prev_xdg) - 1);
    ::unsetenv("XDG_CONFIG_HOME");

    char prev_home[4096] = {0};
    bool had_home = (std::getenv("HOME") != nullptr);
    if (had_home)
        std::strncpy(prev_home, std::getenv("HOME"), sizeof(prev_home) - 1);
    ::setenv("HOME", tmp.c_str(), 1);

    std::string path = Updater::get_update_check_state_path();
    EXPECT_EQ(path, (tmp / ".config" / "torrent-builder" / "update_check.state").string());

    if (had_home)
        ::setenv("HOME", prev_home, 1);
    if (had_xdg)
        ::setenv("XDG_CONFIG_HOME", prev_xdg, 1);

    std::error_code ec;
    fs::remove_all(tmp, ec);
#endif
}
