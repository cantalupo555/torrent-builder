#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <array>
#include <memory>
#include <regex>
#include <filesystem>
#include <fstream>

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
    exit_code = WEXITSTATUS(status);
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
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_overwrite_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    auto output_file = temp_dir / "output.torrent";
    { std::ofstream(input_file) << "test content"; }
    { std::ofstream(output_file) << "existing torrent"; }

    std::string cmd = "echo 'n' | " + get_binary_path()
        + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0);
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

    std::string cmd = "cd " + temp_dir.string() + " && echo 'n' | "
        + get_binary_path()
        + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0);

    std::string log = read_log_file(temp_dir);
    EXPECT_NE(log.find("[INFO]"), std::string::npos) << "Log: " << log;
    EXPECT_NE(log.find("User declined to overwrite"), std::string::npos) << "Log: " << log;

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

    std::string cmd = "cd " + temp_dir.string() + " && echo 'n' | "
        + get_binary_path()
        + " --path " + input_file.string()
        + " --output " + output_file.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0);

    // Verify file content is unchanged
    std::ifstream check(output_file);
    std::string content((std::istreambuf_iterator<char>(check)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "original torrent data that must not change")
        << "Original file should be preserved unchanged";

    // Verify log entry includes the output path
    std::string log = read_log_file(temp_dir);
    EXPECT_NE(log.find("User declined to overwrite"), std::string::npos) << "Log: " << log;
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

TEST(CLI, OverwriteDeclinedAutoGeneratedPath) {
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

    std::string cmd = "cd " + temp_dir.string() + " && echo 'n' | "
        + get_binary_path() + " --path " + input_file.string()
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_NE(output.find("Operation cancelled"), std::string::npos)
        << "Should show cancellation message. Output: " << output;

    std::ifstream check(auto_named_file);
    std::string content((std::istreambuf_iterator<char>(check)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "pre-existing torrent")
        << "Original auto-named file should be preserved";

    std::string log = read_log_file(temp_dir);
    EXPECT_NE(log.find("User declined to overwrite"), std::string::npos)
        << "Log should record decline. Log: " << log;
    EXPECT_NE(log.find("Content.torrent"), std::string::npos)
        << "Log should contain auto-generated filename. Log: " << log;

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

TEST(CLI, OverwriteDeclinedWithPrefixedAutoName) {
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

    std::string cmd = "cd " + temp_dir.string() + " && echo 'n' | "
        + get_binary_path() + " --path " + input_file.string()
        + " --tracker \"https://tracker.example.com/announce\""
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_EQ(exit_code, 0) << "Output: " << output;
    EXPECT_NE(output.find("Operation cancelled"), std::string::npos)
        << "Should show cancellation. Output: " << output;

    std::ifstream check(prefixed_file);
    std::string content((std::istreambuf_iterator<char>(check)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "pre-existing prefixed torrent")
        << "Original prefixed file should be preserved";

    std::string log = read_log_file(temp_dir);
    EXPECT_NE(log.find("User declined to overwrite"), std::string::npos)
        << "Log should record decline. Log: " << log;
    EXPECT_NE(log.find("tracker.example.com_MyMovie.torrent"), std::string::npos)
        << "Log should contain the prefixed filename. Log: " << log;

    fs::remove_all(temp_dir, ec);
}
