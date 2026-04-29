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
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_explicit_overwrite_test";
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

TEST(CLI, OutputDirNonExistentFails) {
    namespace fs = std::filesystem;
    auto temp_dir = fs::temp_directory_path() / "torrent_builder_bad_output_dir_test";
    fs::create_directories(temp_dir);
    auto input_file = temp_dir / "input.txt";
    { std::ofstream(input_file) << "test content"; }

    std::string cmd = get_binary_path() + " --path " + input_file.string()
        + " --output-dir /nonexistent/dir/that/does/not/exist"
        + " --torrent-version 1 2>&1";

    int exit_code;
    std::string output = exec_command(cmd, exit_code);

    EXPECT_NE(exit_code, 0)
        << "Should fail when --output-dir does not exist. Output: " << output;
    EXPECT_NE(output.find("Output directory does not exist"), std::string::npos)
        << "Should report missing directory. Output: " << output;

    std::error_code ec2;
    fs::remove_all(temp_dir, ec2);
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
