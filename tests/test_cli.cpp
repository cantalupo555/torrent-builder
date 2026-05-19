#include "portable.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <array>
#include <memory>
#include <regex>
#include <filesystem>
#include <fstream>
#include "torrent_inspector.hpp"

namespace {

std::string exec_command(const std::string& cmd, int& exit_code) {
    std::array<char, 256> buffer;
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        exit_code = -1;
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    int status = pclose(pipe);
#ifdef _WIN32
    exit_code = status;
#else
    exit_code = WEXITSTATUS(status);
#endif
    return result;
}

std::string get_binary_path() {
    return std::string(BINARY_PATH);
}

// Read entire contents of torrent_builder.log from the given directory
std::string read_log_file(const std::filesystem::path& log_dir) {
    auto log_path = log_dir / "torrent_builder.log";
    std::ifstream f(log_path);
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    return content;
}

}

TEST(CLI, VersionLong) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " --version", exit_code);
    EXPECT_EQ(exit_code, 0);
    std::regex version_re("^torrent_builder (\\d+\\.\\d+\\.\\d+|dev( \\([0-9a-f]+\\))?)\n$");
    EXPECT_TRUE(std::regex_match(output, version_re)) << "Output: " << output;
}

TEST(CLI, VersionShort) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " -v", exit_code);
    EXPECT_EQ(exit_code, 0);
    std::regex version_re("^torrent_builder (\\d+\\.\\d+\\.\\d+|dev( \\([0-9a-f]+\\))?)\n$");
    EXPECT_TRUE(std::regex_match(output, version_re)) << "Output: " << output;
}

TEST(CLI, HelpLong) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " --help", exit_code);
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("--torrent-version"), std::string::npos);
    EXPECT_NE(output.find("--tracker"), std::string::npos);
    EXPECT_NE(output.find("--version"), std::string::npos);
    EXPECT_NE(output.find("--source"), std::string::npos);
    EXPECT_NE(output.find("--entropy"), std::string::npos);
}

TEST(CLI, HelpShort) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " -h", exit_code);
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("--torrent-version"), std::string::npos);
}

TEST(CLI, NoArgsShowsHelp) {
    int exit_code;
    std::string output = exec_command(get_binary_path(), exit_code);
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("--help"), std::string::npos);
}

TEST(CLI, VersionNotAcceptValue) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " --version 2", exit_code);
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("torrent_builder"), std::string::npos);
}

TEST(CLI, TorrentVersionLong) {
    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " --torrent-version 2 2>&1", exit_code);
    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("Path is required"), std::string::npos);
}

TEST(CLI, TorrentVersionShort) {
    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " -t 1 2>&1", exit_code);
    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("Path is required"), std::string::npos);
}

TEST(CLI, TrackerShortFlag) {
    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " -T udp://tracker.example.com:80 2>&1", exit_code);
    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("Path is required"), std::string::npos);
}

TEST(CLI, MissingPath) {
    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " --output /tmp/test.torrent 2>&1", exit_code);
    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("Path is required"), std::string::npos);
}

TEST(CLI, MissingOutputAutoNames) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_autoname_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "MyContent.txt";
    { std::ofstream(input_file) << "test content for auto-naming"; }

    std::string cmd = "cd " + temp_dir.string() + " && "
        + get_binary_path() + " --path " + input_file.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    auto expected_file = temp_dir / "MyContent.torrent";
    EXPECT_TRUE(fs::exists(expected_file))
        << "Expected auto-named file MyContent.torrent to exist. Dir contents: ";

    if (fs::exists(expected_file))
    {
        EXPECT_GT(fs::file_size(expected_file), 0u);
    }

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, CreateTorrentEndToEnd) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_cli_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content for e2e"; }

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_TRUE(fs::exists(output_file)) << "Torrent file not created";
    EXPECT_GT(fs::file_size(output_file), 0u) << "Torrent file is empty";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, OverwriteDeclinedExitsZero) {
#ifdef _WIN32
    GTEST_SKIP() << "stdin piping via popen() is unreliable on Windows";
#endif
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_overwrite_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content"; }
    { std::ofstream(output_file) << "existing torrent"; }

    std::string cmd = "echo n| " + get_binary_path()
        + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 1);
    EXPECT_NE(output.find("Operation cancelled"), std::string::npos) << "Output: " << output;

    // Verify the original file was NOT overwritten
    std::ifstream check(output_file);
    std::string content((std::istreambuf_iterator<char>(check)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "existing torrent") << "Original file should be preserved";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, NonExistentPathShowsDetails) {
    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " --path /nonexistent/path/that/does/not/exist --output /tmp/test.torrent 2>&1",
        exit_code
    );

    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("/nonexistent/path/that/does/not/exist"), std::string::npos)
        << "Error message should include the path. Output: " << output;
}

TEST(CLI, MissingPathLogsToFile) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_log_test";
    fs::create_directories(temp_dir);

    auto log_path = temp_dir / "torrent_builder.log";
    std::error_code ec;
    fs::remove(log_path, ec);

    std::string cmd = "cd " + temp_dir.string() + " && "
        + get_binary_path() + " --output /tmp/test.torrent 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_NE(exit_code, 0);

    std::string log = read_log_file(temp_dir);
    EXPECT_NE(log.find("[ERROR]"), std::string::npos) << "Log: " << log;
    EXPECT_NE(log.find("Path is required"), std::string::npos) << "Log: " << log;

    fs::remove_all(temp_dir, ec);
}

TEST(CLI, OverwriteDeclinedLogsToFile) {
#ifdef _WIN32
    GTEST_SKIP() << "stdin piping via popen() is unreliable on Windows";
#endif
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_overwrite_log_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content"; }
    { std::ofstream(output_file) << "existing torrent"; }

    auto log_path = temp_dir / "torrent_builder.log";
    std::error_code ec;
    fs::remove(log_path, ec);

    std::string cmd = "cd " + temp_dir.string() + " && echo n| "
        + get_binary_path()
        + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 1);

    std::string log = read_log_file(temp_dir);
    EXPECT_NE(log.find("[INFO]"), std::string::npos) << "Log: " << log;
    EXPECT_NE(log.find("Overwrite declined"), std::string::npos) << "Log: " << log;

    fs::remove_all(temp_dir, ec);
}

TEST(CLI, InvalidOptionLogsUnexpectedError) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_invalid_arg_test";
    fs::create_directories(temp_dir);

    auto log_path = temp_dir / "torrent_builder.log";
    std::error_code ec;
    fs::remove(log_path, ec);

    std::string cmd = "cd " + temp_dir.string() + " && "
        + get_binary_path() + " --path /tmp --output /tmp/test.torrent --piece-size abc 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("unexpected error"), std::string::npos)
        << "Should report unexpected error. Output: " << output;

    std::string log = read_log_file(temp_dir);
    EXPECT_NE(log.find("[ERROR]"), std::string::npos) << "Log: " << log;
    EXPECT_NE(log.find("Unexpected error"), std::string::npos) << "Log: " << log;

    fs::remove_all(temp_dir, ec);
}

TEST(CLI, DiskSpaceWarningLogged) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_diskspace_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }

    auto log_path = temp_dir / "torrent_builder.log";
    std::error_code ec;
    fs::remove(log_path, ec);

    // Output to a non-existent directory so fs::space() throws
    std::string cmd = "cd " + temp_dir.string() + " && "
        + get_binary_path()
        + " --path " + input_file.string()
        + " --output /nonexistent/subdir/test.torrent"
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_NE(exit_code, 0);

    std::string log = read_log_file(temp_dir);
    EXPECT_NE(log.find("[WARNING]"), std::string::npos) << "Log: " << log;
    EXPECT_NE(log.find("Could not verify disk space"), std::string::npos) << "Log: " << log;

    fs::remove_all(temp_dir, ec);
}

TEST(CLI, OverwriteDeclinedPreservesFileContent) {
#ifdef _WIN32
    GTEST_SKIP() << "stdin piping via popen() is unreliable on Windows";
#endif
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_preserve_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content"; }
    { std::ofstream(output_file) << "original torrent data that must not change"; }

    auto log_path = temp_dir / "torrent_builder.log";
    std::error_code ec;
    fs::remove(log_path, ec);

    std::string cmd = "cd " + temp_dir.string() + " && echo n| "
        + get_binary_path()
        + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 1);

    // Verify file content is unchanged
    std::ifstream check(output_file);
    std::string content((std::istreambuf_iterator<char>(check)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "original torrent data that must not change")
        << "Original file should be preserved unchanged";

    // Verify log entry includes the output path
    std::string log = read_log_file(temp_dir);
    EXPECT_NE(log.find("Overwrite declined"), std::string::npos) << "Log: " << log;
    EXPECT_NE(log.find(output_file.filename().string()), std::string::npos) << "Log: " << log;

    fs::remove_all(temp_dir, ec);
}

TEST(CLI, AutoNamingWithTracker) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_tracker_prefix_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "MyMovie.mkv";
    { std::ofstream(input_file) << "fake movie content"; }

    std::string cmd = "cd " + temp_dir.string() + " && "
        + get_binary_path() + " --path " + input_file.string()
        + " --tracker \"https://tracker.example.com/announce\""
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    auto expected_file = temp_dir / "tracker.example.com_MyMovie.torrent";
    EXPECT_TRUE(fs::exists(expected_file))
        << "Expected tracker.example.com_MyMovie.torrent";

    if (fs::exists(expected_file))
    {
        EXPECT_GT(fs::file_size(expected_file), 0u);
    }

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, AutoNamingWithDefaultTrackers) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_default_trackers_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "MyMovie.mkv";
    { std::ofstream(input_file) << "fake movie content"; }

    std::string cmd = "cd " + temp_dir.string() + " && "
        + get_binary_path() + " --path " + input_file.string()
        + " --default-trackers"
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    auto expected_file = temp_dir / "open.stealth.si_MyMovie.torrent";
    EXPECT_TRUE(fs::exists(expected_file))
        << "Expected open.stealth.si_MyMovie.torrent (auto-named with default trackers)";

    if (fs::exists(expected_file))
    {
        EXPECT_GT(fs::file_size(expected_file), 0u);
    }

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, AutoNamingDefaultTrackersLogsPath) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_autoname_log_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "SomeFile.bin";
    { std::ofstream(input_file) << "data"; }

    auto log_path = temp_dir / "torrent_builder.log";
    std::error_code ec;
    fs::remove(log_path, ec);

    std::string cmd = "cd " + temp_dir.string() + " && "
        + get_binary_path() + " --path " + input_file.string()
        + " --default-trackers"
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    std::string log = read_log_file(temp_dir);
    EXPECT_NE(log.find("Auto-generated output path:"), std::string::npos)
        << "Log should contain auto-generated output path. Log: " << log;
    EXPECT_NE(log.find("open.stealth.si_SomeFile.torrent"), std::string::npos)
        << "Log should contain expected filename. Log: " << log;

    fs::remove_all(temp_dir, ec);
}

TEST(CLI, AutoNameCollisionResolved) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_autoname_overwrite_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "Content.dat";
    { std::ofstream(input_file) << "test content"; }

    auto auto_named_file = temp_dir / "Content.torrent";
    { std::ofstream(auto_named_file) << "pre-existing torrent"; }

    auto log_path = temp_dir / "torrent_builder.log";
    std::error_code ec;
    fs::remove(log_path, ec);

    std::string cmd = "cd " + temp_dir.string() + " && "
        + get_binary_path() + " --path " + input_file.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    auto collision_file = temp_dir / "Content(1).torrent";
    EXPECT_TRUE(fs::exists(collision_file))
        << "Expected Content(1).torrent to be auto-created";

    std::ifstream check(auto_named_file);
    std::string content((std::istreambuf_iterator<char>(check)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "pre-existing torrent")
        << "Original auto-named file should be preserved";

    std::string log = read_log_file(temp_dir);
    EXPECT_NE(log.find("Filename collision detected"), std::string::npos)
        << "Log should record collision resolution. Log: " << log;

    fs::remove_all(temp_dir, ec);
}

TEST(CLI, AutoNamingWithSkipPrefix) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_skip_prefix_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "MyMovie.mkv";
    { std::ofstream(input_file) << "fake movie content"; }

    std::string cmd = "cd " + temp_dir.string() + " && "
        + get_binary_path() + " --path " + input_file.string()
        + " --tracker \"https://tracker.example.com/announce\""
        + " --skip-prefix"
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    auto expected_file = temp_dir / "MyMovie.torrent";
    EXPECT_TRUE(fs::exists(expected_file))
        << "Expected MyMovie.torrent (no tracker prefix)";

    auto prefixed_file = temp_dir / "tracker.example.com_MyMovie.torrent";
    EXPECT_FALSE(fs::exists(prefixed_file))
        << "Should NOT have created prefixed file when --skip-prefix is used";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, AutoNamePrefixedCollisionResolved) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_prefixed_overwrite_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "MyMovie.mkv";
    { std::ofstream(input_file) << "fake movie content"; }

    auto prefixed_file = temp_dir / "tracker.example.com_MyMovie.torrent";
    { std::ofstream(prefixed_file) << "pre-existing prefixed torrent"; }

    auto log_path = temp_dir / "torrent_builder.log";
    std::error_code ec;
    fs::remove(log_path, ec);

    std::string cmd = "cd " + temp_dir.string() + " && "
        + get_binary_path() + " --path " + input_file.string()
        + " --tracker \"https://tracker.example.com/announce\""
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    auto collision_file = temp_dir / "tracker.example.com_MyMovie(1).torrent";
    EXPECT_TRUE(fs::exists(collision_file))
        << "Expected tracker.example.com_MyMovie(1).torrent to be auto-created";

    std::ifstream check(prefixed_file);
    std::string content((std::istreambuf_iterator<char>(check)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "pre-existing prefixed torrent")
        << "Original prefixed file should be preserved";

    std::string log = read_log_file(temp_dir);
    EXPECT_NE(log.find("Filename collision detected"), std::string::npos)
        << "Log should record collision resolution. Log: " << log;

    fs::remove_all(temp_dir, ec);
}

TEST(CLI, AutoNamingWithOutputDir) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_output_dir_test";
    auto output_dir = temp_dir / "output";
    fs::create_directories(output_dir);
    auto input_file = temp_dir / "MyMovie.mkv";
    { std::ofstream(input_file) << "fake movie content"; }

    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --tracker \"https://tracker.example.com/announce\""
        + " --output-dir " + output_dir.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    auto expected_file = output_dir / "tracker.example.com_MyMovie.torrent";
    EXPECT_TRUE(fs::exists(expected_file))
        << "Expected file in output-dir";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, AutoNamingWithTrackerIndex) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_tracker_index_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "MyMovie.mkv";
    { std::ofstream(input_file) << "fake movie content"; }

    std::string cmd = "cd " + temp_dir.string() + " && "
        + get_binary_path() + " --path " + input_file.string()
        + " --tracker \"https://tracker1.com/announce\""
        + " --tracker \"https://tracker2.com/announce\""
        + " --tracker-index 1"
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    auto expected_file = temp_dir / "tracker2.com_MyMovie.torrent";
    EXPECT_TRUE(fs::exists(expected_file))
        << "Expected tracker2.com_MyMovie.torrent (using tracker-index 1)";

    auto wrong_file = temp_dir / "tracker1.com_MyMovie.torrent";
    EXPECT_FALSE(fs::exists(wrong_file))
        << "Should NOT have created file with first tracker when --tracker-index 1 is used";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, ExplicitOutputStillPromptsOverwrite) {
#ifdef _WIN32
    GTEST_SKIP() << "stdin piping via popen() is unreliable on Windows";
#endif
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_explicit_overwrite_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content"; }
    { std::ofstream(output_file) << "existing torrent"; }

    std::string cmd = "echo n| " + get_binary_path()
        + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 1);
    EXPECT_NE(output.find("Operation cancelled"), std::string::npos)
        << "Explicit --output should still prompt overwrite. Output: " << output;

    std::ifstream check(output_file);
    std::string content((std::istreambuf_iterator<char>(check)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "existing torrent")
        << "Original file should be preserved";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, OutputDirAutoCreate) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_output_dir_autocreate_test";
    fs::create_directories(temp_dir);
    auto output_dir = temp_dir / "nested" / "output";
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }

    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output-dir " + output_dir.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Should succeed by auto-creating output-dir. Output: " << output;
    EXPECT_TRUE(fs::is_directory(output_dir))
        << "Output directory should have been created";
    EXPECT_TRUE(fs::exists(output_dir / "input.torrent"))
        << "Torrent file should exist in auto-created directory";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, InvalidTrackerIndexFallsBack) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_invalid_index_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }

    std::string cmd = "cd " + temp_dir.string() + " && "
        + get_binary_path() + " --path " + input_file.string()
        + " --tracker \"https://tracker1.com/announce\""
        + " --tracker-index 99"
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Should succeed with fallback. Output: " << output;

    auto expected_file = temp_dir / "tracker1.com_input.torrent";
    EXPECT_TRUE(fs::exists(expected_file))
        << "Should use first tracker as fallback. Output: " << output;

    std::string log = read_log_file(temp_dir);
    EXPECT_NE(log.find("Tracker index 99 out of range"), std::string::npos)
        << "Log should warn about out-of-range index. Log: " << log;

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, InvalidTrackerIndexWithNoTrackers) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_no_trackers_index_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }

    std::string cmd = "cd " + temp_dir.string() + " && "
        + get_binary_path() + " --path " + input_file.string()
        + " --tracker-index 5"
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Should succeed with no trackers. Output: " << output;

    auto expected_file = temp_dir / "input.torrent";
    EXPECT_TRUE(fs::exists(expected_file))
        << "Should create file with no tracker prefix. Output: " << output;

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, OutputDirWithTrackerIndex) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_dir_index_test";
    auto output_dir = temp_dir / "out";
    fs::create_directories(output_dir);
    auto input_file = temp_dir / "Movie.mkv";
    { std::ofstream(input_file) << "data"; }

    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --tracker \"https://t1.com/announce\""
        + " --tracker \"https://t2.com/announce\""
        + " --tracker-index 1"
        + " --output-dir " + output_dir.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_TRUE(fs::exists(output_dir / "t2.com_Movie.torrent"))
        << "Should use tracker-index 1 in output-dir";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, OutputDirIsFileFails) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_output_is_file_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto file_as_dir = temp_dir / "not_a_dir";
    { std::ofstream(input_file) << "test content"; }
    { std::ofstream(file_as_dir) << "I am a file"; }

    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output-dir " + file_as_dir.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_NE(exit_code, 0)
        << "Should fail when --output-dir is a file. Output: " << output;
    EXPECT_NE(output.find("Output directory is not a directory"), std::string::npos)
        << "Should report not-a-directory. Output: " << output;

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

#ifndef _WIN32
TEST(CLI, OutputDirCreatePermissionDeniedFails) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_output_dir_perm_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }

    auto readonly_dir = temp_dir / "readonly";
    fs::create_directories(readonly_dir);
    fs::permissions(readonly_dir, fs::perms::none);

    auto target_dir = readonly_dir / "sub" / "output";

    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output-dir " + target_dir.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_NE(exit_code, 0)
        << "Should fail when output-dir cannot be created. Output: " << output;

    fs::permissions(readonly_dir, fs::perms::all);
    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}
#endif

TEST(CLI, NameFlagSetsTorrentName) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_name_flag_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content for name flag"; }

    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --name \"Custom.Torrent.Name\""
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_TRUE(fs::exists(output_file)) << "Torrent file not created";

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_EQ(meta.name, "Custom.Torrent.Name")
        << "Torrent internal name should match --name value";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, NameFlagIndependentOfOutput) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_name_independent_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "MyFile.txt";
    auto output_file = temp_dir / "OtherName.torrent";
    { std::ofstream(input_file) << "test content"; }

    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --name \"Internal.Name\""
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_EQ(meta.name, "Internal.Name");

    EXPECT_TRUE(fs::exists(output_file))
        << "Output file should be OtherName.torrent, not named after --name";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, NameFlagWithDirectory) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_name_dir_test";
    auto content_dir = temp_dir / "MyFolder";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "file1.txt") << "content1"; }
    { std::ofstream(content_dir / "file2.txt") << "content2"; }
    auto output_file = temp_dir / "output.torrent";

    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --name \"Dir.Custom.Name\""
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_EQ(meta.name, "Dir.Custom.Name")
        << "Name should override directory name";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, DefaultNameUnchanged) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_default_name_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "OriginalName.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content"; }

    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_EQ(meta.name, "OriginalName.txt")
        << "Default name should be inferred from filename";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, EmptyNameRejected) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_empty_name_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }

    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output " + (temp_dir / "output.torrent").string()
        + " --name \"   \""
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_NE(exit_code, 0) << "Should fail with whitespace-only name";
    EXPECT_NE(output.find("cannot be empty"), std::string::npos)
        << "Error message should mention empty name. Output: " << output;

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, NameFlagWithV2Directory) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_name_v2_dir_test";
    auto content_dir = temp_dir / "MyFolder";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "file1.txt") << "content1"; }
    { std::ofstream(content_dir / "file2.txt") << "content2"; }
    auto output_file = temp_dir / "output.torrent";

    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --name \"V2.Custom.Name\""
        + " --torrent-version 2 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_EQ(meta.name, "V2.Custom.Name")
        << "V2 torrent internal name should match --name value";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, NameFlagWithHybridDirectory) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_name_hybrid_dir_test";
    auto content_dir = temp_dir / "MyFolder";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "file1.txt") << "content1"; }
    { std::ofstream(content_dir / "file2.txt") << "content2"; }
    auto output_file = temp_dir / "output.torrent";

    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --name \"Hybrid.Custom.Name\""
        + " --torrent-version 3 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_EQ(meta.name, "Hybrid.Custom.Name")
        << "Hybrid torrent internal name should match --name value";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, InteractiveNamePrompt) {
#ifdef _WIN32
    GTEST_SKIP() << "stdin piping via popen() is unreliable on Windows";
#endif
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_interactive_name_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content"; }

    std::string inputs =
        "'" + input_file.string() + "' "   // path
        "'" + output_file.string() + "' "  // output
        "1 "                               // version (v1)
        "'' "                              // comment (empty)
        "n "                               // private? no
        "n "                               // default trackers? no
        "n "                               // custom trackers? no
        "'' "                              // web seed (empty/finish)
        "n "                               // custom piece size? no
        "n "                               // creator? no
        "'My.Interactive.Name' "           // custom name
        "n "                               // creation date? no
        "'' "                              // source (empty/skip)
        "n "                               // entropy? no
        "n "                               // exclude patterns? no
        "n ";                              // include patterns? no

    std::string cmd = "printf '%s\\n' " + inputs + "| " + get_binary_path() + " --interactive 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_EQ(meta.name, "My.Interactive.Name")
        << "Interactive mode should apply custom name";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, SourceFlagSetsInfoSource) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_source_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content for source flag"; }

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1 --source PTP 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_TRUE(fs::exists(output_file)) << "Torrent file not created";

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_TRUE(meta.source.has_value()) << "Source field should be present";
    EXPECT_EQ(*meta.source, "PTP");

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, EntropyFlagAddsEntropyField) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_entropy_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content for entropy flag"; }

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1 -e 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_TRUE(fs::exists(output_file)) << "Torrent file not created";

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_TRUE(meta.entropy.has_value()) << "Entropy field should be present";
    EXPECT_EQ(meta.entropy->size(), 64u) << "Entropy should be 64 hex chars (32 bytes)";
    EXPECT_TRUE(std::all_of(meta.entropy->begin(), meta.entropy->end(),
        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        << "Entropy should contain only valid hex digits (0-9, a-f)";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, SourceAndEntropyTogether) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_source_entropy_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content for both flags"; }

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1 --source HDB -e 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_TRUE(meta.source.has_value());
    EXPECT_EQ(*meta.source, "HDB");
    ASSERT_TRUE(meta.entropy.has_value());
    EXPECT_EQ(meta.entropy->size(), 64u);

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, DifferentSourceProducesDifferentHash) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_hash_diff_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output1 = temp_dir / "output1.torrent";
    auto output2 = temp_dir / "output2.torrent";
    { std::ofstream(input_file) << "test content for hash comparison"; }

    int exit_code;
    std::string cmd1 = get_binary_path() + " --path " + input_file.string()
        + " --output " + output1.string()
        + " --torrent-version 1 --source PTP 2>&1";
    exec_command(cmd1, exit_code);
    ASSERT_EQ(exit_code, 0);

    std::string cmd2 = get_binary_path() + " --path " + input_file.string()
        + " --output " + output2.string()
        + " --torrent-version 1 --source HDB 2>&1";
    exec_command(cmd2, exit_code);
    ASSERT_EQ(exit_code, 0);

    TorrentInspector inspector1(output1.string());
    TorrentMetadata meta1 = inspector1.inspect();

    TorrentInspector inspector2(output2.string());
    TorrentMetadata meta2 = inspector2.inspect();

    EXPECT_NE(meta1.info_hash_v1, meta2.info_hash_v1)
        << "Different source strings should produce different info hashes";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, EntropyProducesUniqueHash) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_entropy_unique_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output1 = temp_dir / "output1.torrent";
    auto output2 = temp_dir / "output2.torrent";
    { std::ofstream(input_file) << "test content for entropy uniqueness"; }

    int exit_code;
    std::string cmd1 = get_binary_path() + " --path " + input_file.string()
        + " --output " + output1.string()
        + " --torrent-version 1 -e 2>&1";
    exec_command(cmd1, exit_code);
    ASSERT_EQ(exit_code, 0);

    std::string cmd2 = get_binary_path() + " --path " + input_file.string()
        + " --output " + output2.string()
        + " --torrent-version 1 -e 2>&1";
    exec_command(cmd2, exit_code);
    ASSERT_EQ(exit_code, 0);

    TorrentInspector inspector1(output1.string());
    TorrentMetadata meta1 = inspector1.inspect();

    TorrentInspector inspector2(output2.string());
    TorrentMetadata meta2 = inspector2.inspect();

    EXPECT_NE(meta1.info_hash_v1, meta2.info_hash_v1)
        << "Entropy flag should produce unique info hashes per run";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, NoSourceNoEntropyByDefault) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_no_source_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content without source"; }

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_FALSE(meta.source.has_value()) << "Source should not be present by default";
    EXPECT_FALSE(meta.entropy.has_value()) << "Entropy should not be present by default";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, EmptySourceStringTreatedAsUnset) {
#ifdef _WIN32
    GTEST_SKIP() << "Skipping empty source test on Windows (shell quoting differs)";
#endif
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_empty_source_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content for empty source"; }

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1 --source '' 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_FALSE(meta.source.has_value()) << "Empty source should be treated as not set";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, SourceWithV2Torrent) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_source_v2_test";
    fs::create_directories(temp_dir);
    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "file.txt") << "test content for v2 source"; }
    auto output_file = temp_dir / "output.torrent";

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --torrent-version 2 --source PTP 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_TRUE(fs::exists(output_file)) << "Torrent file not created";

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_TRUE(meta.source.has_value()) << "Source should be present in v2 torrent";
    EXPECT_EQ(*meta.source, "PTP");
    EXPECT_FALSE(meta.info_hash_v2.empty()) << "v2 hash should be present";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, SourceWithHybridTorrent) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_source_hybrid_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content for hybrid source"; }

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 3 --source HDB 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_TRUE(meta.source.has_value()) << "Source should be present in hybrid torrent";
    EXPECT_EQ(*meta.source, "HDB");
    EXPECT_TRUE(meta.is_hybrid) << "Should be a hybrid torrent";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, EntropyWithHybridTorrent) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_entropy_hybrid_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content for hybrid entropy"; }

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 3 -e 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_TRUE(meta.entropy.has_value()) << "Entropy should be present in hybrid torrent";
    EXPECT_EQ(meta.entropy->size(), 64u);
    EXPECT_TRUE(meta.is_hybrid);

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, SameSourceProducesSameHash) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_same_source_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output1 = temp_dir / "output1.torrent";
    auto output2 = temp_dir / "output2.torrent";
    { std::ofstream(input_file) << "test content for deterministic hash"; }

    int exit_code;
    std::string cmd1 = get_binary_path() + " --path " + input_file.string()
        + " --output " + output1.string()
        + " --torrent-version 1 --source PTP 2>&1";
    exec_command(cmd1, exit_code);
    ASSERT_EQ(exit_code, 0);

    std::string cmd2 = get_binary_path() + " --path " + input_file.string()
        + " --output " + output2.string()
        + " --torrent-version 1 --source PTP 2>&1";
    exec_command(cmd2, exit_code);
    ASSERT_EQ(exit_code, 0);

    TorrentInspector inspector1(output1.string());
    TorrentMetadata meta1 = inspector1.inspect();

    TorrentInspector inspector2(output2.string());
    TorrentMetadata meta2 = inspector2.inspect();

    EXPECT_EQ(meta1.info_hash_v1, meta2.info_hash_v1)
        << "Same source string should produce identical info hashes";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, EntropyWithV2Torrent) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_entropy_v2_test";
    fs::create_directories(temp_dir);
    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "file.txt") << "test content for v2 entropy"; }
    auto output_file = temp_dir / "output.torrent";

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --torrent-version 2 -e 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_TRUE(fs::exists(output_file)) << "Torrent file not created";

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_TRUE(meta.entropy.has_value()) << "Entropy should be present in v2 torrent";
    EXPECT_EQ(meta.entropy->size(), 64u);
    EXPECT_FALSE(meta.info_hash_v2.empty()) << "v2 hash should be present";

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, InteractiveSourceAndEntropy) {
#ifdef _WIN32
    GTEST_SKIP() << "Skipping interactive test on Windows (popen unreliable for stdin piping)";
#endif
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_interactive_src_ent_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "interactive source entropy test"; }

    std::string inputs =
        input_file.string() + " "
        + output_file.string() + " "
        "3 "                               // torrent version: hybrid
        "n "                               // comment? no
        "n "                               // private? no
        "n "                               // default trackers? no
        "n "                               // custom trackers? no
        "'' "                              // web seed (empty/finish)
        "n "                               // custom piece size? no
        "n "                               // creator? no
        "'' "                              // custom name (empty/default)
        "n "                               // creation date? no
        "'PTP' "                           // source: PTP
        "y "                                // entropy? yes
        "n "                                // exclude patterns? no
        "n ";                               // include patterns? no

    std::string cmd = "printf '%s\\n' " + inputs + "| " + get_binary_path() + " --interactive 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_TRUE(meta.source.has_value()) << "Source should be set from interactive input";
    EXPECT_EQ(*meta.source, "PTP");
    ASSERT_TRUE(meta.entropy.has_value()) << "Entropy should be set from interactive input";
    EXPECT_EQ(meta.entropy->size(), 64u);

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, ExcludePatternFiltersFiles) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_exclude_test";
    fs::create_directories(temp_dir);
    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "movie.mkv") << "video content"; }
    { std::ofstream(content_dir / "info.nfo") << "nfo data"; }
    { std::ofstream(content_dir / "readme.txt") << "text data"; }
    auto output_file = temp_dir / "output.torrent";

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --torrent-version 1"
        + " --exclude \"*.nfo\""
        + " --exclude \"*.txt\" 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_TRUE(fs::exists(output_file)) << "Torrent file not created";

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_EQ(meta.files.size(), 1u) << "Should only have 1 file (movie.mkv)";
    EXPECT_NE(meta.files[0].path.find("movie.mkv"), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(CLI, IncludePatternFiltersFiles) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_include_test";
    fs::create_directories(temp_dir);
    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "movie.mkv") << "video content"; }
    { std::ofstream(content_dir / "clip.mp4") << "clip content"; }
    { std::ofstream(content_dir / "readme.txt") << "text data"; }
    auto output_file = temp_dir / "output.torrent";

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --torrent-version 1"
        + " --include \"*.mkv\""
        + " --include \"*.mp4\" 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_TRUE(fs::exists(output_file)) << "Torrent file not created";

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_EQ(meta.files.size(), 2u) << "Should have 2 files (movie.mkv, clip.mp4)";

    fs::remove_all(temp_dir);
}

TEST(CLI, IncludeOverridesExclude) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_inc_over_exc_test";
    fs::create_directories(temp_dir);
    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "movie.mkv") << "video content"; }
    { std::ofstream(content_dir / "subs.srt") << "subtitle data"; }
    { std::ofstream(content_dir / "readme.txt") << "text data"; }
    auto output_file = temp_dir / "output.torrent";

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --torrent-version 1"
        + " --include \"*.mkv\""
        + " --include \"*.srt\""
        + " --exclude \"*.srt\" 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_EQ(meta.files.size(), 2u) << "Include should override exclude for .srt";

    bool has_srt = false;
    bool has_mkv = false;
    for (const auto &f : meta.files) {
        if (f.path.find(".srt") != std::string::npos) has_srt = true;
        if (f.path.find(".mkv") != std::string::npos) has_mkv = true;
    }
    EXPECT_TRUE(has_mkv) << "movie.mkv should be included";
    EXPECT_TRUE(has_srt) << "subs.srt should be included (include wins over exclude)";

    fs::remove_all(temp_dir);
}

TEST(CLI, ExcludeAllFilesFails) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_exclude_all_test";
    fs::create_directories(temp_dir);
    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "info.nfo") << "nfo data"; }
    auto output_file = temp_dir / "output.torrent";

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --torrent-version 1"
        + " --exclude \"*\" 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_NE(exit_code, 0) << "Should fail when all files are excluded";
    EXPECT_NE(output.find("No files matched"), std::string::npos)
        << "Output should mention no files matched";

    fs::remove_all(temp_dir);
}

TEST(CLI, HelpShowsExcludeInclude) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " --help", exit_code);
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("--exclude"), std::string::npos);
    EXPECT_NE(output.find("--include"), std::string::npos);
}

TEST(CLI, ExcludePatternSubdirectory) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_exclude_subdir_test";
    fs::create_directories(temp_dir);
    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    fs::create_directories(content_dir / "subs");
    { std::ofstream(content_dir / "movie.mkv") << "video content"; }
    { std::ofstream(content_dir / "subs" / "en.srt") << "subtitle"; }
    { std::ofstream(content_dir / "subs" / "es.srt") << "subtitulo"; }
    auto output_file = temp_dir / "output.torrent";

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --torrent-version 1"
        + " --exclude \"subs/**\" 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_EQ(meta.files.size(), 1u) << "Should only have movie.mkv (subs/** excluded)";
    EXPECT_NE(meta.files[0].path.find("movie.mkv"), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(CLI, IncludeOnlyNoMatchFails) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_include_nomatch_test";
    fs::create_directories(temp_dir);
    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "readme.txt") << "text data"; }
    auto output_file = temp_dir / "output.torrent";

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --torrent-version 1"
        + " --include \"*.mkv\" 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_NE(exit_code, 0) << "Should fail when include matches nothing";
    EXPECT_NE(output.find("No files matched"), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(CLI, SingleFileWithPatterns) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_single_pattern_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "movie.mkv";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "video content"; }

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1"
        + " --exclude \"*.txt\" 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Patterns should not affect single-file mode. Output: " << output;
    EXPECT_TRUE(fs::exists(output_file)) << "Torrent file should be created";

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_EQ(meta.files.size(), 1u);

    fs::remove_all(temp_dir);
}

TEST(CLI, InteractiveExcludePatterns) {
#ifdef _WIN32
    GTEST_SKIP() << "Skipping interactive test on Windows";
#endif
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_interactive_exc_test";
    fs::create_directories(temp_dir);
    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "movie.mkv") << "video content"; }
    { std::ofstream(content_dir / "info.nfo") << "nfo data"; }
    auto output_file = temp_dir / "output.torrent";

    std::string inputs =
        content_dir.string() + " "
        + output_file.string() + " "
        "1 "                               // torrent version: v1
        "n "                               // comment? no
        "n "                               // private? no
        "n "                               // default trackers? no
        "n "                               // custom trackers? no
        "'' "                              // web seed (empty/finish)
        "n "                               // custom piece size? no
        "n "                               // creator? no
        "'' "                              // custom name (empty/default)
        "n "                               // creation date? no
        "'' "                              // source (empty)
        "n "                               // entropy? no
        "y "                               // exclude patterns? yes
        "*.nfo "                           // exclude pattern
        " "                                // blank to finish exclude
        "n "                               // include patterns? no
        "";

    std::string cmd = "printf '%s\\n' " + inputs + "| " + get_binary_path() + " --interactive 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_EQ(meta.files.size(), 1u) << "Should only have movie.mkv (.nfo excluded)";
    EXPECT_NE(meta.files[0].path.find("movie.mkv"), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(CLI, ExcludePatternV2Torrent) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_exclude_v2_test";
    fs::create_directories(temp_dir);
    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "movie.mkv") << "video content"; }
    { std::ofstream(content_dir / "info.nfo") << "nfo data"; }
    auto output_file = temp_dir / "output.torrent";

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --torrent-version 2"
        + " --exclude \"*.nfo\" 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    bool has_mkv = false;
    bool has_nfo = false;
    for (const auto &f : meta.files) {
        if (f.path.find(".mkv") != std::string::npos) has_mkv = true;
        if (f.path.find(".nfo") != std::string::npos) has_nfo = true;
    }
    EXPECT_TRUE(has_mkv) << "movie.mkv should be present in v2 torrent";
    EXPECT_FALSE(has_nfo) << "info.nfo should be excluded from v2 torrent";
    EXPECT_FALSE(meta.info_hash_v2.empty()) << "v2 hash should be present";

    fs::remove_all(temp_dir);
}

TEST(CLI, ExcludePatternHybridTorrent) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_exclude_hybrid_test";
    fs::create_directories(temp_dir);
    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "movie.mkv") << "video content"; }
    { std::ofstream(content_dir / "readme.txt") << "text data"; }
    auto output_file = temp_dir / "output.torrent";

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --torrent-version 3"
        + " --exclude \"*.txt\" 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    bool has_mkv = false;
    bool has_txt = false;
    for (const auto &f : meta.files) {
        if (f.path.find(".mkv") != std::string::npos) has_mkv = true;
        if (f.path.find(".txt") != std::string::npos) has_txt = true;
    }
    EXPECT_TRUE(has_mkv) << "movie.mkv should be present in hybrid torrent";
    EXPECT_FALSE(has_txt) << "readme.txt should be excluded from hybrid torrent";

    fs::remove_all(temp_dir);
}

TEST(CLI, ExcludeSubdirectoryPruning) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_dir_prune_test";
    fs::create_directories(temp_dir);
    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    fs::create_directories(content_dir / "extras");
    fs::create_directories(content_dir / "extras" / "deep");
    { std::ofstream(content_dir / "movie.mkv") << "video content"; }
    { std::ofstream(content_dir / "extras" / "trailer.mkv") << "trailer"; }
    { std::ofstream(content_dir / "extras" / "deep" / "bonus.mkv") << "bonus"; }
    auto output_file = temp_dir / "output.torrent";

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --torrent-version 1"
        + " --exclude \"extras/**\" 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_EQ(meta.files.size(), 1u) << "Should only have movie.mkv (extras/ pruned)";
    EXPECT_NE(meta.files[0].path.find("movie.mkv"), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(CLI, InteractiveMultipleExcludePatterns) {
#ifdef _WIN32
    GTEST_SKIP() << "Skipping interactive test on Windows";
#endif
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_interactive_multi_exc_test";
    fs::create_directories(temp_dir);
    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "movie.mkv") << "video content"; }
    { std::ofstream(content_dir / "info.nfo") << "nfo data"; }
    { std::ofstream(content_dir / "sample.srt") << "subtitle data"; }
    auto output_file = temp_dir / "output.torrent";

    std::string inputs =
        content_dir.string() + " "
        + output_file.string() + " "
        "1 "                               // torrent version: v1
        "n "                               // comment? no
        "n "                               // private? no
        "n "                               // default trackers? no
        "n "                               // custom trackers? no
        "'' "                              // web seed (empty/finish)
        "n "                               // custom piece size? no
        "n "                               // creator? no
        "'' "                              // custom name (empty/default)
        "n "                               // creation date? no
        "'' "                              // source (empty)
        "n "                               // entropy? no
        "y "                               // exclude patterns? yes
        "*.nfo "                           // exclude pattern 1
        "*.srt "                           // exclude pattern 2
        " "                                // blank to finish exclude
        "n "                               // include patterns? no
        " ";

    std::string cmd = "printf '%s\\n' " + inputs + "| " + get_binary_path() + " --interactive 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_EQ(meta.files.size(), 1u) << "Should only have movie.mkv";
    EXPECT_NE(meta.files[0].path.find("movie.mkv"), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(CLI, IncludePatternNestedDirectories) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_include_nested_test";
    fs::create_directories(temp_dir);
    auto content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    fs::create_directories(content_dir / "subs");
    { std::ofstream(content_dir / "movie.mkv") << "video content"; }
    { std::ofstream(content_dir / "subs" / "en.srt") << "subtitle"; }
    { std::ofstream(content_dir / "readme.txt") << "text"; }
    auto output_file = temp_dir / "output.torrent";

    int exit_code;
    std::string cmd = get_binary_path() + " --path " + content_dir.string()
        + " --output " + output_file.string()
        + " --torrent-version 1"
        + " --include \"*.mkv\""
        + " --include \"**/*.srt\" 2>&1";
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    bool has_mkv = false;
    bool has_srt = false;
    bool has_txt = false;
    for (const auto &f : meta.files) {
        if (f.path.find(".mkv") != std::string::npos) has_mkv = true;
        if (f.path.find(".srt") != std::string::npos) has_srt = true;
        if (f.path.find(".txt") != std::string::npos) has_txt = true;
    }
    EXPECT_TRUE(has_mkv) << "movie.mkv should be included";
    EXPECT_TRUE(has_srt) << "subs/en.srt should be included (directory traversed)";
    EXPECT_FALSE(has_txt) << "readme.txt should be excluded";

    fs::remove_all(temp_dir);
}

TEST(CLI, VerboseQuietMutualExclusion) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " --verbose --quiet 2>&1", exit_code);
    EXPECT_EQ(exit_code, 1);
    EXPECT_NE(output.find("mutually exclusive"), std::string::npos);
}

TEST(CLI, VerboseJsonMutualExclusion) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " --verbose --json 2>&1", exit_code);
    EXPECT_EQ(exit_code, 1);
    EXPECT_NE(output.find("mutually exclusive"), std::string::npos);
}

TEST(CLI, HelpShowsVerbose) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " --help", exit_code);
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("--verbose"), std::string::npos);
}

TEST(CLI, HelpShowsQuiet) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " --help", exit_code);
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("--quiet"), std::string::npos);
}

TEST(CLI, HelpShowsJson) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " --help", exit_code);
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("--json"), std::string::npos);
}

TEST(CLI, QuietModeSuppressesOutput) {
    auto temp_dir = fs::temp_directory_path() / "tb_quiet_test";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "file.bin") << "quiet test data";

    std::string output_file = (temp_dir / "quiet.torrent").string();
    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " --path " + (temp_dir / "file.bin").string()
        + " --output " + output_file + " --quiet 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0);
    EXPECT_TRUE(output.empty() || output.find("TORRENT CREATED") == std::string::npos)
        << "Quiet mode should suppress creation summary. Output: " << output;

    ASSERT_TRUE(fs::exists(output_file));
    fs::remove_all(temp_dir);
}

TEST(CLI, JsonModeOutputIsValidJson) {
    auto temp_dir = fs::temp_directory_path() / "tb_json_test";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "file.bin") << "json test data here";

    std::string output_file = (temp_dir / "json_out.torrent").string();
    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " --path " + (temp_dir / "file.bin").string()
        + " --output " + output_file + " --json 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    ASSERT_TRUE(fs::exists(output_file));

    EXPECT_NE(output.find("\"name\""), std::string::npos) << "JSON should contain 'name' field";
    EXPECT_NE(output.find("\"info_hash_v1\""), std::string::npos) << "JSON should contain info_hash_v1";
    EXPECT_NE(output.find("\"total_size\""), std::string::npos) << "JSON should contain total_size";
    EXPECT_NE(output.find("\"piece_length\""), std::string::npos) << "JSON should contain piece_length";
    EXPECT_NE(output.find("\"piece_count\""), std::string::npos) << "JSON should contain piece_count";
    EXPECT_NE(output.find("\"is_private\""), std::string::npos) << "JSON should contain is_private";
    EXPECT_NE(output.find("\"trackers\""), std::string::npos) << "JSON should contain trackers";
    EXPECT_NE(output.find("\"output_path\""), std::string::npos) << "JSON should contain output_path";
    EXPECT_NE(output.find("json_out.torrent"), std::string::npos) << "JSON output_path should contain the torrent filename";

    ASSERT_GT(output.size(), 1u);
    EXPECT_EQ(output.front(), '{') << "JSON output should start with '{'";
    std::string trimmed = output;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r'))
        trimmed.pop_back();
    EXPECT_EQ(trimmed.back(), '}') << "JSON output should end with '}'";

    fs::remove_all(temp_dir);
}

TEST(CLI, JsonWithTrackers) {
    auto temp_dir = fs::temp_directory_path() / "tb_json_trackers";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "file.bin") << "json tracker test";

    std::string output_file = (temp_dir / "json_track.torrent").string();
    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " --path " + (temp_dir / "file.bin").string()
        + " --output " + output_file
        + " --tracker \"https://tracker.example/announce\""
        + " --json 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("tracker.example"), std::string::npos)
        << "JSON output should contain tracker URL";

    fs::remove_all(temp_dir);
}

TEST(CLI, QuietOverwriteAutoDeclines) {
    auto temp_dir = fs::temp_directory_path() / "tb_quiet_overwrite";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "file.bin") << "overwrite test";

    std::string output_file = (temp_dir / "existing.torrent").string();
    std::ofstream(output_file) << "dummy torrent content";

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " --path " + (temp_dir / "file.bin").string()
        + " --output " + output_file + " --quiet 2>&1", exit_code);

    EXPECT_EQ(exit_code, 1) << "Quiet mode should auto-decline overwrite with exit code 1";
    {
        std::ifstream check(output_file);
        std::string content((std::istreambuf_iterator<char>(check)),
                            std::istreambuf_iterator<char>());
        EXPECT_EQ(content, "dummy torrent content")
            << "Original file should be preserved when quiet auto-declines overwrite";
    }

    fs::remove_all(temp_dir);
}

TEST(CLI, VerboseShowsPieceSizeInfo) {
    auto temp_dir = fs::temp_directory_path() / "tb_verbose_test";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "file.bin") << "verbose test data";

    std::string output_file = (temp_dir / "verbose.torrent").string();
    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " --path " + (temp_dir / "file.bin").string()
        + " --output " + output_file + " --verbose 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_NE(output.find("Piece size:"), std::string::npos)
        << "Verbose should show piece size info";
    EXPECT_NE(output.find("Files added to storage:"), std::string::npos)
        << "Verbose should show file count";

    ASSERT_TRUE(fs::exists(output_file));
    fs::remove_all(temp_dir);
}

TEST(CLI, JsonOverwriteAutoDeclines) {
    auto temp_dir = fs::temp_directory_path() / "tb_json_overwrite";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "file.bin") << "overwrite json test";

    std::string output_file = (temp_dir / "existing.torrent").string();
    std::ofstream(output_file) << "dummy torrent content";

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " --path " + (temp_dir / "file.bin").string()
        + " --output " + output_file + " --json 2>&1", exit_code);

    EXPECT_EQ(exit_code, 1) << "JSON mode should auto-decline overwrite. Output: " << output;
    {
        std::ifstream check(output_file);
        std::string content((std::istreambuf_iterator<char>(check)),
                            std::istreambuf_iterator<char>());
        EXPECT_EQ(content, "dummy torrent content")
            << "Original file should be preserved when JSON auto-declines overwrite";
    }

    fs::remove_all(temp_dir);
}

TEST(CLI, NormalModeNoVerboseMessages) {
    auto temp_dir = fs::temp_directory_path() / "tb_normal_test";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "file.bin") << "normal mode test";

    std::string output_file = (temp_dir / "normal.torrent").string();
    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " --path " + (temp_dir / "file.bin").string()
        + " --output " + output_file + " 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_EQ(output.find("Files added to storage:"), std::string::npos)
        << "Normal mode should not show verbose file count";
    EXPECT_EQ(output.find("Piece size:"), std::string::npos)
        << "Normal mode should not show verbose piece size info";

    ASSERT_TRUE(fs::exists(output_file));
    fs::remove_all(temp_dir);
}

TEST(CLI, JsonQuietComboWorks) {
    auto temp_dir = fs::temp_directory_path() / "tb_json_quiet";
    fs::create_directories(temp_dir);
    std::ofstream(temp_dir / "file.bin") << "json+quiet test";

    std::string output_file = (temp_dir / "jq.torrent").string();
    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " --path " + (temp_dir / "file.bin").string()
        + " --output " + output_file + " --json --quiet 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_NE(output.find("\"name\""), std::string::npos) << "JSON output expected";

    fs::remove_all(temp_dir);
}

TEST(CLI, HelpAlwaysVisibleEvenWithQuiet) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " --help --quiet 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("--verbose"), std::string::npos) << "--help should show output even with --quiet";
}

TEST(CLI, VersionAlwaysVisibleEvenWithQuiet) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " --version --quiet 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("torrent_builder"), std::string::npos) << "--version should show output even with --quiet";
}

TEST(CLI, ModifyHelpShowsOptions) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " modify --help", exit_code);
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("--tracker"), std::string::npos);
    EXPECT_NE(output.find("--add-tracker"), std::string::npos);
    EXPECT_NE(output.find("--remove-tracker"), std::string::npos);
    EXPECT_NE(output.find("--private"), std::string::npos);
    EXPECT_NE(output.find("--public"), std::string::npos);
    EXPECT_NE(output.find("--source"), std::string::npos);
    EXPECT_NE(output.find("--comment"), std::string::npos);
    EXPECT_NE(output.find("--name"), std::string::npos);
    EXPECT_NE(output.find("--entropy"), std::string::npos);
    EXPECT_NE(output.find("--dry-run"), std::string::npos);
    EXPECT_NE(output.find("--output"), std::string::npos);
}

TEST(CLI, ModifyNoArgsShowsHelp) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " modify 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("Modify torrent metadata"), std::string::npos);
}

TEST(CLI, ModifyTrackerConflict) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "tb_modify_conflict";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }
    auto torrent_file = temp_dir / "input.torrent";
    int create_exit;
    exec_command(get_binary_path() + " --path " + input_file.string()
        + " --output " + torrent_file.string() + " --torrent-version 1 2>&1", create_exit);

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " modify " + torrent_file.string()
        + " --tracker \"https://t1.com/announce\""
        + " --add-tracker \"https://t2.com/announce\" 2>&1", exit_code);

    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("exclusive"), std::string::npos) << "Output: " << output;

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, ModifyPrivatePublicConflict) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "tb_modify_pp_conflict";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }
    auto torrent_file = temp_dir / "input.torrent";
    int create_exit;
    exec_command(get_binary_path() + " --path " + input_file.string()
        + " --output " + torrent_file.string() + " --torrent-version 1 2>&1", create_exit);

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " modify " + torrent_file.string()
        + " --private --public 2>&1", exit_code);

    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("mutually exclusive"), std::string::npos) << "Output: " << output;

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, ModifyNoModificationOption) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "tb_modify_nomod";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }
    auto torrent_file = temp_dir / "input.torrent";
    int create_exit;
    exec_command(get_binary_path() + " --path " + input_file.string()
        + " --output " + torrent_file.string() + " --torrent-version 1 2>&1", create_exit);

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " modify " + torrent_file.string() + " 2>&1", exit_code);

    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("at least one modification"), std::string::npos) << "Output: " << output;

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, ModifyTrackerReplaceEndToEnd) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "tb_modify_tracker";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }
    auto torrent_file = temp_dir / "input.torrent";
    auto output_file = temp_dir / "modified.torrent";
    int create_exit;
    exec_command(get_binary_path() + " --path " + input_file.string()
        + " --output " + torrent_file.string() + " --torrent-version 1 2>&1", create_exit);

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " modify " + torrent_file.string()
        + " --tracker \"https://new-tracker.example.com/announce\""
        + " --output " + output_file.string() + " 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_EQ(meta.trackers.size(), 1u);
    EXPECT_EQ(meta.trackers[0], "https://new-tracker.example.com/announce");

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, ModifySourceEndToEnd) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "tb_modify_source";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }
    auto torrent_file = temp_dir / "input.torrent";
    auto output_file = temp_dir / "modified.torrent";
    int create_exit;
    exec_command(get_binary_path() + " --path " + input_file.string()
        + " --output " + torrent_file.string() + " --torrent-version 1 2>&1", create_exit);

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " modify " + torrent_file.string()
        + " --source PTP --output " + output_file.string() + " 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_TRUE(meta.source.has_value());
    EXPECT_EQ(*meta.source, "PTP");

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, ModifyPrivateEndToEnd) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "tb_modify_private";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }
    auto torrent_file = temp_dir / "input.torrent";
    auto output_file = temp_dir / "modified.torrent";
    int create_exit;
    exec_command(get_binary_path() + " --path " + input_file.string()
        + " --output " + torrent_file.string() + " --torrent-version 1 2>&1", create_exit);

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " modify " + torrent_file.string()
        + " --private --output " + output_file.string() + " 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_TRUE(meta.is_private);

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, ModifyDryRunNoFileWritten) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "tb_modify_dryrun";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }
    auto torrent_file = temp_dir / "input.torrent";
    auto output_file = temp_dir / "should_not_exist.torrent";
    int create_exit;
    exec_command(get_binary_path() + " --path " + input_file.string()
        + " --output " + torrent_file.string() + " --torrent-version 1 2>&1", create_exit);

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " modify " + torrent_file.string()
        + " --source TEST --output " + output_file.string()
        + " --dry-run 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_NE(output.find("Dry run"), std::string::npos) << "Output: " << output;
    EXPECT_FALSE(fs::exists(output_file));

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, ModifyEntropyEndToEnd) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "tb_modify_entropy";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }
    auto torrent_file = temp_dir / "input.torrent";
    auto output_file = temp_dir / "modified.torrent";
    int create_exit;
    exec_command(get_binary_path() + " --path " + input_file.string()
        + " --output " + torrent_file.string() + " --torrent-version 1 2>&1", create_exit);

    TorrentInspector orig(torrent_file.string());
    auto orig_meta = orig.inspect();

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " modify " + torrent_file.string()
        + " --entropy --output " + output_file.string() + " 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector(output_file.string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_TRUE(meta.entropy.has_value());
    EXPECT_EQ(meta.entropy->size(), 64u);
    if (!orig_meta.info_hash_v1.empty() && !meta.info_hash_v1.empty())
    {
        EXPECT_NE(orig_meta.info_hash_v1, meta.info_hash_v1);
    }

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, CheckCommandEndToEnd) {
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_check_e2e";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "content.txt";
    { std::ofstream(input_file) << "check test content"; }
    auto torrent_file = temp_dir / "content.torrent";

    int create_exit;
    exec_command(get_binary_path() + " --path " + input_file.string()
        + " --output " + torrent_file.string() + " --torrent-version 1 2>&1", create_exit);
    ASSERT_EQ(create_exit, 0);

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " check " + torrent_file.string()
        + " --path " + temp_dir.string() + " 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_NE(output.find("PASS"), std::string::npos);

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, CheckCommandFailsForMissingFiles) {
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_check_missing";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "content.txt";
    { std::ofstream(input_file) << "check test content"; }
    auto torrent_file = temp_dir / "content.torrent";

    int create_exit;
    exec_command(get_binary_path() + " --path " + input_file.string()
        + " --output " + torrent_file.string() + " --torrent-version 1 2>&1", create_exit);
    ASSERT_EQ(create_exit, 0);

    fs::remove(input_file);

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " check " + torrent_file.string()
        + " --path " + temp_dir.string() + " 2>&1", exit_code);

    EXPECT_NE(exit_code, 0) << "Output: " << output;
    EXPECT_NE(output.find("FAIL"), std::string::npos);

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, CheckCommandJsonOutput) {
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_check_json";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "content.txt";
    { std::ofstream(input_file) << "json check test"; }
    auto torrent_file = temp_dir / "content.torrent";

    int create_exit;
    exec_command(get_binary_path() + " --path " + input_file.string()
        + " --output " + torrent_file.string() + " --torrent-version 1 2>&1", create_exit);
    ASSERT_EQ(create_exit, 0);

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " check " + torrent_file.string()
        + " --path " + temp_dir.string() + " --json 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_NE(output.find("\"status\""), std::string::npos);
    EXPECT_NE(output.find("\"pieces\""), std::string::npos);
    EXPECT_NE(output.find("\"missing_files\""), std::string::npos);
    EXPECT_NE(output.find("\"extra_files\""), std::string::npos);

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, CheckCommandHelp) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " check --help 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("--verbose"), std::string::npos);
    EXPECT_NE(output.find("--json"), std::string::npos);
    EXPECT_NE(output.find("--path"), std::string::npos);
}

TEST(CLI, CheckCommandNoArgsShowsHelp) {
    int exit_code;
    std::string output = exec_command(get_binary_path() + " check 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("--help"), std::string::npos);
}

TEST(CLI, CheckCommandMissingTorrent) {
    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " check /nonexistent/path.torrent 2>&1", exit_code);

    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("Error"), std::string::npos);
}

TEST(CLI, CheckCommandVerboseJsonConflict) {
    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " check test.torrent --verbose --json 2>&1", exit_code);

    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("mutually exclusive"), std::string::npos);
}

TEST(CLI, CheckCommandDetectsCorruption) {
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_check_corrupt";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "content.txt";
    { std::ofstream(input_file) << "original content"; }
    auto torrent_file = temp_dir / "content.torrent";

    int create_exit;
    exec_command(get_binary_path() + " --path " + input_file.string()
        + " --output " + torrent_file.string() + " --torrent-version 1 2>&1", create_exit);
    ASSERT_EQ(create_exit, 0);

    { std::ofstream(input_file) << "corrupted data!!"; }

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " check " + torrent_file.string()
        + " --path " + temp_dir.string() + " 2>&1", exit_code);

    EXPECT_NE(exit_code, 0) << "Output: " << output;
     EXPECT_NE(output.find("FAIL"), std::string::npos);
     EXPECT_NE(output.find("Corrupted pieces"), std::string::npos);

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(BatchCLI, ShowsHelp) {
    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() + " batch --help 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("batch"), std::string::npos);
}

TEST(BatchCLI, MissingFileReturnsError) {
    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() + " batch /nonexistent_batch_file.yaml 2>&1", exit_code);
    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("not found"), std::string::npos);
}

TEST(BatchCLI, InvalidYamlReturnsError) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_batch_test_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);
    {
        std::ofstream f(temp_dir / "batch.yaml");
        f << "not: valid: yaml: [[[";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() + " batch " + (temp_dir / "batch.yaml").string() + " 2>&1", exit_code);
    EXPECT_NE(exit_code, 0);

    fs::remove_all(temp_dir);
}

TEST(BatchCLI, ValidBatchCreatesTorrents) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_batch_valid_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'X');
        f.write(data.data(), data.size());
    }

    {
        std::ofstream f(temp_dir / "batch.yaml");
        f << "version: 1\n"
           << "jobs:\n"
           << "  - path: \"" << test_file.generic_string() << "\"\n"
           << "    output: \"" << (temp_dir / "out.torrent").generic_string() << "\"\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() + " batch " + (temp_dir / "batch.yaml").string() + " 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_NE(output.find("Succeeded: 1"), std::string::npos);
    EXPECT_TRUE(fs::exists(temp_dir / "out.torrent"));

    fs::remove_all(temp_dir);
}

TEST(PresetCLI, UnknownPresetReturnsError) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_preset_test_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'X');
        f.write(data.data(), data.size());
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() + " --preset nonexistent --preset-file " +
        (temp_dir / "presets.yaml").string() +
        " -p " + test_file.string() +
        " -o " + (temp_dir / "out.torrent").string() + " 2>&1", exit_code);
    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("Preset file not found"), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(PresetCLI, ValidPresetAppliedToTorrent) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_preset_valid_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'X');
        f.write(data.data(), data.size());
    }

    {
        std::ofstream f(temp_dir / "presets.yaml");
        f << "version: 1\n"
           << "presets:\n"
           << "  mypreset:\n"
           << "    comment: \"preset test comment\"\n"
           << "    source: \"PRESET_CLI\"\n"
           << "    private: true\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() +
        " --preset mypreset --preset-file " + (temp_dir / "presets.yaml").string() +
        " -p " + test_file.string() +
        " -o " + (temp_dir / "out.torrent").string() + " 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_TRUE(fs::exists(temp_dir / "out.torrent"));
    EXPECT_NE(output.find("Private: Yes"), std::string::npos);
    EXPECT_NE(output.find("PRESET_CLI"), std::string::npos);

    TorrentInspector inspector((temp_dir / "out.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_TRUE(meta.source.has_value());
    EXPECT_EQ(*meta.source, "PRESET_CLI");

    fs::remove_all(temp_dir);
}

TEST(PresetCLI, PresetTrackersAppliedWhenNoCLIFlags) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_preset_trackers_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'X');
        f.write(data.data(), data.size());
    }

    {
        std::ofstream f(temp_dir / "presets.yaml");
        f << "version: 1\n"
           << "presets:\n"
           << "  mypreset:\n"
           << "    trackers:\n"
           << "      - \"https://tracker.example.com/announce\"\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() +
        " --preset mypreset --preset-file " + (temp_dir / "presets.yaml").string() +
        " -p " + test_file.string() +
        " -o " + (temp_dir / "out.torrent").string() + " 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_NE(output.find("Trackers: 1"), std::string::npos) << "Output: " << output;

    fs::remove_all(temp_dir);
}

TEST(PresetCLI, CLITrackerOverridesPresetTrackers) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_preset_tracker_override_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'X');
        f.write(data.data(), data.size());
    }

    {
        std::ofstream f(temp_dir / "presets.yaml");
        f << "version: 1\n"
           << "presets:\n"
           << "  mypreset:\n"
           << "    trackers:\n"
           << "      - \"https://preset-tracker.example.com/announce\"\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() +
        " --preset mypreset --preset-file " + (temp_dir / "presets.yaml").string() +
        " -T https://cli-tracker.example.com/announce" +
        " -p " + test_file.string() +
        " -o " + (temp_dir / "out.torrent").string() + " 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector((temp_dir / "out.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_EQ(meta.trackers.size(), 1u);
    EXPECT_NE(meta.trackers[0].find("cli-tracker.example.com"), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(BatchCLI, WorkersOverride) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_batch_workers_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'X');
        f.write(data.data(), data.size());
    }

    {
        std::ofstream f(temp_dir / "batch.yaml");
        f << "version: 1\n"
           << "jobs:\n"
           << "  - path: \"" << test_file.generic_string() << "\"\n"
           << "    output: \"" << (temp_dir / "out.torrent").generic_string() << "\"\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() + " batch " + (temp_dir / "batch.yaml").string() +
        " --workers 2 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_NE(output.find("Succeeded: 1"), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(BatchCLI, WorkersZeroShowsWarning) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_batch_w0_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'X');
        f.write(data.data(), data.size());
    }

    {
        std::ofstream f(temp_dir / "batch.yaml");
        f << "version: 1\n"
           << "jobs:\n"
           << "  - path: \"" << test_file.generic_string() << "\"\n"
           << "    output: \"" << (temp_dir / "out.torrent").generic_string() << "\"\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() + " batch " + (temp_dir / "batch.yaml").string() +
        " --workers 0 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_NE(output.find("Warning"), std::string::npos) << "Expected warning about --workers 0";
    EXPECT_NE(output.find("Succeeded: 1"), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(CLI, CheckCommandVerboseOutput) {
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_check_verbose";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "content.txt";
    { std::ofstream(input_file) << "verbose check test content here"; }
    auto torrent_file = temp_dir / "content.torrent";

    int create_exit;
    exec_command(get_binary_path() + " --path " + input_file.string()
        + " --output " + torrent_file.string() + " --torrent-version 1 2>&1", create_exit);
    ASSERT_EQ(create_exit, 0);

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " check " + torrent_file.string()
        + " --path " + temp_dir.string() + " --verbose 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_NE(output.find("PASS"), std::string::npos);

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(CLI, CheckCommandDefaultPath) {
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_check_default_path";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "content.txt";
    { std::ofstream(input_file) << "default path test content"; }
    auto torrent_file = temp_dir / "content.torrent";

    int create_exit;
    exec_command(get_binary_path() + " --path " + input_file.string()
        + " --output " + torrent_file.string() + " --torrent-version 1 2>&1", create_exit);
    ASSERT_EQ(create_exit, 0);

    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " check " + torrent_file.string() + " 2>&1", exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_NE(output.find("PASS"), std::string::npos);

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
}

TEST(PresetCLI, PresetCreatorAppliedToTorrent) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_preset_creator_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'X');
        f.write(data.data(), data.size());
    }

    {
        std::ofstream f(temp_dir / "presets.yaml");
        f << "version: 1\n"
           << "presets:\n"
           << "  mypreset:\n"
           << "    creator: \"PresetCreator\"\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() +
        " --preset mypreset --preset-file " + (temp_dir / "presets.yaml").string() +
        " -p " + test_file.string() +
        " -o " + (temp_dir / "out.torrent").string() + " 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector((temp_dir / "out.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_TRUE(meta.created_by.has_value());
    EXPECT_EQ(*meta.created_by, "PresetCreator");

    fs::remove_all(temp_dir);
}

TEST(PresetCLI, PresetNameAppliedToTorrent) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_preset_name_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'X');
        f.write(data.data(), data.size());
    }

    {
        std::ofstream f(temp_dir / "presets.yaml");
        f << "version: 1\n"
           << "presets:\n"
           << "  mypreset:\n"
           << "    name: \"CustomName\"\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() +
        " --preset mypreset --preset-file " + (temp_dir / "presets.yaml").string() +
        " -p " + test_file.string() +
        " -t 1" +
        " -o " + (temp_dir / "out.torrent").string() + " 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector((temp_dir / "out.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_EQ(meta.name, "CustomName");

    fs::remove_all(temp_dir);
}

TEST(PresetCLI, PresetEntropyAppliedToTorrent) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_preset_entropy_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'X');
        f.write(data.data(), data.size());
    }

    {
        std::ofstream f(temp_dir / "presets.yaml");
        f << "version: 1\n"
           << "presets:\n"
           << "  mypreset:\n"
           << "    entropy: true\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() +
        " --preset mypreset --preset-file " + (temp_dir / "presets.yaml").string() +
        " -p " + test_file.string() +
        " -t 1" +
        " -o " + (temp_dir / "out.torrent").string() + " 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector((temp_dir / "out.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_TRUE(meta.entropy.has_value());

    fs::remove_all(temp_dir);
}

TEST(BatchCLI, FailedJobReturnsExitCode1) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_batch_fail_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    {
        std::ofstream f(temp_dir / "batch.yaml");
        f << "version: 1\n"
           << "jobs:\n"
           << "  - path: \"/nonexistent/path/that/does/not/exist\"\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() + " batch " + (temp_dir / "batch.yaml").string() + " 2>&1", exit_code);
    EXPECT_EQ(exit_code, 1) << "Expected exit code 1 for failed batch job. Output: " << output;
    EXPECT_NE(output.find("Failed: 1"), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(PresetCLI, UnknownPresetNameWithValidFileReturnsError) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_preset_unknown_name_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'X');
        f.write(data.data(), data.size());
    }

    {
        std::ofstream f(temp_dir / "presets.yaml");
        f << "version: 1\n"
          << "presets:\n"
          << "  mypreset:\n"
          << "    source: \"TEST\"\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() +
        " --preset nonexistent_preset --preset-file " + (temp_dir / "presets.yaml").string() +
        " -p " + test_file.string() +
        " -o " + (temp_dir / "out.torrent").string() + " 2>&1", exit_code);
    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("Unknown preset"), std::string::npos) << "Output: " << output;

    fs::remove_all(temp_dir);
}

TEST(PresetCLI, PresetPieceSizeAppliedToTorrent) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_preset_piecesize_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(32 * 1024, 'X');
        f.write(data.data(), data.size());
    }

    {
        std::ofstream f(temp_dir / "presets.yaml");
        f << "version: 1\n"
          << "presets:\n"
          << "  mypreset:\n"
          << "    piece_size: 16\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() +
        " --preset mypreset --preset-file " + (temp_dir / "presets.yaml").string() +
        " -p " + test_file.string() +
        " -t 1" +
        " -o " + (temp_dir / "out.torrent").string() + " 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector((temp_dir / "out.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_EQ(meta.piece_length, 16 * 1024) << "Piece length should be 16 KB (16384 bytes)";

    fs::remove_all(temp_dir);
}

TEST(PresetCLI, PresetCreationDateAppliedToTorrent) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_preset_creationdate_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'X');
        f.write(data.data(), data.size());
    }

    {
        std::ofstream f(temp_dir / "presets.yaml");
        f << "version: 1\n"
          << "presets:\n"
          << "  mypreset:\n"
          << "    creation_date: true\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() +
        " --preset mypreset --preset-file " + (temp_dir / "presets.yaml").string() +
        " -p " + test_file.string() +
        " -t 1" +
        " -o " + (temp_dir / "out.torrent").string() + " 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector((temp_dir / "out.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_TRUE(meta.creation_date.has_value()) << "Creation date should be present when preset sets it to true";

    fs::remove_all(temp_dir);
}

TEST(PresetCLI, PresetTorrentVersionAppliedToTorrent) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_preset_version_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(1024, 'X');
        f.write(data.data(), data.size());
    }

    {
        std::ofstream f(temp_dir / "presets.yaml");
        f << "version: 1\n"
          << "presets:\n"
          << "  mypreset:\n"
          << "    torrent_version: 1\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() +
        " --preset mypreset --preset-file " + (temp_dir / "presets.yaml").string() +
        " -p " + test_file.string() +
        " -o " + (temp_dir / "out.torrent").string() + " 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector((temp_dir / "out.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_FALSE(meta.info_hash_v1.empty()) << "V1 torrent should have v1 info hash";
    EXPECT_TRUE(meta.info_hash_v2.empty()) << "V1 torrent should not have v2 info hash";
    EXPECT_FALSE(meta.is_hybrid) << "V1 torrent should not be hybrid";

    fs::remove_all(temp_dir);
}

TEST(PresetCLI, PresetExcludePatternsAppliedToTorrent) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_preset_exclude_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "movie.mkv") << "video content"; }
    { std::ofstream(content_dir / "info.nfo") << "nfo data"; }
    { std::ofstream(content_dir / "readme.txt") << "text data"; }

    {
        std::ofstream f(temp_dir / "presets.yaml");
        f << "version: 1\n"
          << "presets:\n"
          << "  mypreset:\n"
          << "    exclude_patterns:\n"
          << "      - \"*.nfo\"\n"
          << "      - \"*.txt\"\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() +
        " --preset mypreset --preset-file " + (temp_dir / "presets.yaml").string() +
        " -p " + content_dir.string() +
        " -t 1" +
        " -o " + (temp_dir / "out.torrent").string() + " 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector((temp_dir / "out.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_EQ(meta.files.size(), 1u) << "Should only have movie.mkv (nfo and txt excluded via preset)";
    EXPECT_NE(meta.files[0].path.find("movie.mkv"), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(PresetCLI, PresetIncludePatternsAppliedToTorrent) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_preset_include_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path content_dir = temp_dir / "content";
    fs::create_directories(content_dir);
    { std::ofstream(content_dir / "movie.mkv") << "video content"; }
    { std::ofstream(content_dir / "clip.mp4") << "clip content"; }
    { std::ofstream(content_dir / "readme.txt") << "text data"; }

    {
        std::ofstream f(temp_dir / "presets.yaml");
        f << "version: 1\n"
          << "presets:\n"
          << "  mypreset:\n"
          << "    include_patterns:\n"
          << "      - \"*.mkv\"\n"
          << "      - \"*.mp4\"\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() +
        " --preset mypreset --preset-file " + (temp_dir / "presets.yaml").string() +
        " -p " + content_dir.string() +
        " -t 1" +
        " -o " + (temp_dir / "out.torrent").string() + " 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Output: " << output;

    TorrentInspector inspector((temp_dir / "out.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    ASSERT_EQ(meta.files.size(), 2u) << "Should have 2 files (mkv and mp4 included via preset, txt excluded)";

    fs::remove_all(temp_dir);
}

TEST(PresetCLI, PresetInvalidPieceSizeFallsBackToDefault) {
    fs::path temp_dir = fs::temp_directory_path() / ("cli_preset_invalid_ps_" + std::to_string(portable_getpid()));
    fs::create_directories(temp_dir);

    fs::path test_file = temp_dir / "content.bin";
    {
        std::ofstream f(test_file, std::ios::binary);
        std::vector<char> data(32 * 1024, 'X');
        f.write(data.data(), data.size());
    }

    {
        std::ofstream f(temp_dir / "presets.yaml");
        f << "version: 1\n"
          << "presets:\n"
          << "  mypreset:\n"
          << "    piece_size: 999\n";
    }

    int exit_code = -1;
    std::string output = exec_command(
        get_binary_path() +
        " --preset mypreset --preset-file " + (temp_dir / "presets.yaml").string() +
        " -p " + test_file.string() +
        " -t 1" +
        " -o " + (temp_dir / "out.torrent").string() + " 2>&1", exit_code);
    EXPECT_EQ(exit_code, 0) << "Should succeed with auto-calculated piece size. Output: " << output;
    EXPECT_TRUE(fs::exists(temp_dir / "out.torrent"));

    TorrentInspector inspector((temp_dir / "out.torrent").string());
    TorrentMetadata meta = inspector.inspect();
    EXPECT_NE(meta.piece_length, 999 * 1024) << "Invalid preset piece_size should not be applied";
    EXPECT_GT(meta.piece_length, 0) << "Auto-calculated piece size should be positive";

    fs::remove_all(temp_dir);
}
