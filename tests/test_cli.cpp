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

TEST(CLI, MissingOutput) {
    int exit_code;
    std::string output = exec_command(
        get_binary_path() + " --path /tmp 2>&1", exit_code);
    EXPECT_NE(exit_code, 0);
    EXPECT_NE(output.find("Output path is required"), std::string::npos);
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
