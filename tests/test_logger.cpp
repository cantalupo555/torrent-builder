#include <regex>
#include <gtest/gtest.h>
#include "logger.hpp"
#include <fstream>
#include <filesystem>
#include <string>
#include <chrono>
#include <thread>

namespace fs = std::filesystem;

class LoggerTest : public ::testing::Test
{
  protected:
    fs::path log_path_;

    void SetUp() override
    {
        log_path_ = fs::current_path() / "torrent_builder.log";
        cleanup_log();
    }

    void TearDown() override { cleanup_log(); }

    void cleanup_log()
    {
        std::error_code ec;
        fs::remove(log_path_, ec);
    }

    std::string read_log()
    {
        std::ifstream f(log_path_);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return content;
    }

    std::string last_log_line()
    {
        std::ifstream f(log_path_);
        std::string line, last;
        while (std::getline(f, line))
        {
            last = line;
        }
        return last;
    }
};

TEST_F(LoggerTest, CreatesLogFile)
{
    log_message("test file creation", LogLevel::INFO);
    EXPECT_TRUE(fs::exists(log_path_));
}

TEST_F(LoggerTest, InfoLevel)
{
    log_message("info test message", LogLevel::INFO);
    std::string log = read_log();
    EXPECT_NE(log.find("[INFO]"), std::string::npos);
    EXPECT_NE(log.find("info test message"), std::string::npos);
}

TEST_F(LoggerTest, WarningLevel)
{
    log_message("warning test message", LogLevel::WARNING);
    std::string log = read_log();
    EXPECT_NE(log.find("[WARNING]"), std::string::npos);
    EXPECT_NE(log.find("warning test message"), std::string::npos);
}

TEST_F(LoggerTest, ErrorLevel)
{
    log_message("error test message", LogLevel::ERR);
    std::string log = read_log();
    EXPECT_NE(log.find("[ERROR]"), std::string::npos);
    EXPECT_NE(log.find("error test message"), std::string::npos);
}

TEST_F(LoggerTest, DefaultLevelIsInfo)
{
    log_message("default level message");
    std::string log = read_log();
    EXPECT_NE(log.find("[INFO]"), std::string::npos);
}

TEST_F(LoggerTest, TimestampFormat)
{
    log_message("timestamp check", LogLevel::INFO);
    std::string line = last_log_line();
    std::regex ts_re(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \[)");
    EXPECT_TRUE(std::regex_search(line, ts_re)) << "Line: " << line;
}

TEST_F(LoggerTest, AppendMode)
{
    log_message("first entry", LogLevel::INFO);
    log_message("second entry", LogLevel::ERR);
    std::string log = read_log();
    size_t first = log.find("first entry");
    size_t second = log.find("second entry");
    EXPECT_NE(first, std::string::npos);
    EXPECT_NE(second, std::string::npos);
    EXPECT_LT(first, second);
}

TEST_F(LoggerTest, MultipleLevelsDistinct)
{
    log_message("msg info", LogLevel::INFO);
    log_message("msg warn", LogLevel::WARNING);
    log_message("msg err", LogLevel::ERR);
    std::string log = read_log();
    EXPECT_NE(log.find("[INFO]"), std::string::npos);
    EXPECT_NE(log.find("[WARNING]"), std::string::npos);
    EXPECT_NE(log.find("[ERROR]"), std::string::npos);
}

TEST_F(LoggerTest, EntryFormat)
{
    log_message("format check", LogLevel::WARNING);
    std::string line = last_log_line();
    std::regex entry_re(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \[WARNING\] - format check$)");
    EXPECT_TRUE(std::regex_match(line, entry_re)) << "Line: " << line;
}
