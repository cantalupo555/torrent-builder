#include <gtest/gtest.h>
#include "terminal.hpp"
#include <stdexcept>
#include <type_traits>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(UserInterrupt, IsRuntimeException) {
    EXPECT_TRUE((std::is_base_of_v<std::runtime_error, UserInterrupt>));
}

TEST(UserInterrupt, ConstructWithMessage) {
    UserInterrupt ex("test interrupt");
    EXPECT_STREQ(ex.what(), "test interrupt");
}

TEST(UserInterrupt, CaughtByRuntimeError) {
    bool caught = false;
    try {
        throw UserInterrupt("caught test");
    } catch (const std::runtime_error& e) {
        caught = true;
        EXPECT_STREQ(e.what(), "caught test");
    }
    EXPECT_TRUE(caught);
}

TEST(UserInterrupt, CaughtBySpecificType) {
    bool caught = false;
    try {
        throw UserInterrupt("specific test");
    } catch (const UserInterrupt& e) {
        caught = true;
        EXPECT_STREQ(e.what(), "specific test");
    }
    EXPECT_TRUE(caught);
}

TEST(UserInterrupt, DoesNotMatchStdExceptionDirectly) {
    bool caught = false;
    try {
        throw UserInterrupt("std exception test");
    } catch (const std::exception& e) {
        caught = true;
        EXPECT_STREQ(e.what(), "std exception test");
    }
    EXPECT_TRUE(caught);
}

TEST(TerminalGuard, NonCopyable) {
    EXPECT_FALSE(std::is_copy_constructible_v<TerminalGuard>);
    EXPECT_FALSE(std::is_copy_assignable_v<TerminalGuard>);
}

TEST(TerminalGuard, ConstructsAndDestructsInNonTty) {
    EXPECT_NO_THROW({
        TerminalGuard guard;
    });
}

TEST(TerminalGuard, CheckKeyPressReturnsFalseInNonTty) {
    TerminalGuard guard;
    char c = 0;
    bool result = guard.check_key_press(c);
    EXPECT_FALSE(result);
}

TEST(TerminalGuard, CheckKeyPressDoesNotModifyCharInNonTty) {
    TerminalGuard guard;
    char c = 'X';
    guard.check_key_press(c);
    EXPECT_EQ(c, 'X');
}

TEST(TerminalGuard, MultipleInstancesSequential) {
    {
        TerminalGuard guard1;
        char c = 0;
        EXPECT_FALSE(guard1.check_key_press(c));
    }
    {
        TerminalGuard guard2;
        char c = 0;
        EXPECT_FALSE(guard2.check_key_press(c));
    }
}

#ifndef _WIN32

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <thread>
#include <atomic>

class PtyTest : public ::testing::Test {
protected:
    int master_fd_ = -1;
    int slave_fd_ = -1;
    int orig_stdin_ = -1;
    std::string slave_path_;

    void SetUp() override {
        master_fd_ = posix_openpt(O_RDWR | O_NOCTTY);
        ASSERT_GE(master_fd_, 0) << "posix_openpt failed";
        ASSERT_EQ(grantpt(master_fd_), 0) << "grantpt failed";
        ASSERT_EQ(unlockpt(master_fd_), 0) << "unlockpt failed";

        char* name = ptsname(master_fd_);
        ASSERT_NE(name, nullptr) << "ptsname failed";
        slave_path_ = name;

        slave_fd_ = open(slave_path_.c_str(), O_RDWR | O_NOCTTY);
        ASSERT_GE(slave_fd_, 0) << "Failed to open PTY slave";

        orig_stdin_ = dup(STDIN_FILENO);
        ASSERT_GE(orig_stdin_, 0) << "dup(STDIN_FILENO) failed";
        ASSERT_EQ(dup2(slave_fd_, STDIN_FILENO), STDIN_FILENO) << "dup2 failed";
    }

    void TearDown() override {
        if (orig_stdin_ >= 0) {
            dup2(orig_stdin_, STDIN_FILENO);
            close(orig_stdin_);
        }
        if (master_fd_ >= 0) close(master_fd_);
        if (slave_fd_ >= 0) close(slave_fd_);
    }

    void write_to_master(const char* data, size_t len) {
        ssize_t written = write(master_fd_, data, len);
        ASSERT_EQ(written, static_cast<ssize_t>(len));
        tcdrain(master_fd_);
        usleep(50000);
    }
};

TEST_F(PtyTest, DetectsTty) {
    TerminalGuard guard;
    char c = 0;
    EXPECT_FALSE(guard.check_key_press(c));
}

TEST_F(PtyTest, ReadsSingleChar) {
    TerminalGuard guard;
    write_to_master("q", 1);

    char c = 0;
    bool result = guard.check_key_press(c);
    EXPECT_TRUE(result);
    EXPECT_EQ(c, 'q');
}

TEST_F(PtyTest, ReadsMultipleCharsSequentially) {
    TerminalGuard guard;

    write_to_master("ab", 2);

    char c = 0;
    ASSERT_TRUE(guard.check_key_press(c));
    EXPECT_EQ(c, 'a');
    ASSERT_TRUE(guard.check_key_press(c));
    EXPECT_EQ(c, 'b');
}

TEST_F(PtyTest, ReturnsFalseWhenNoInput) {
    TerminalGuard guard;
    char c = 0;
    EXPECT_FALSE(guard.check_key_press(c));
}

TEST_F(PtyTest, RestoresTerminalStateOnDestruction) {
    struct termios before;
    tcgetattr(STDIN_FILENO, &before);

    {
        TerminalGuard guard;
    }

    struct termios after;
    tcgetattr(STDIN_FILENO, &after);

    EXPECT_EQ(before.c_lflag, after.c_lflag);
    EXPECT_EQ(before.c_cc[VMIN], after.c_cc[VMIN]);
    EXPECT_EQ(before.c_cc[VTIME], after.c_cc[VTIME]);
}

TEST_F(PtyTest, RestoresTerminalStateOnException) {
    struct termios before;
    tcgetattr(STDIN_FILENO, &before);

    try {
        TerminalGuard guard;
        throw std::runtime_error("test exception");
    } catch (const std::runtime_error&) {
    }

    struct termios after;
    tcgetattr(STDIN_FILENO, &after);

    EXPECT_EQ(before.c_lflag, after.c_lflag);
    EXPECT_EQ(before.c_cc[VMIN], after.c_cc[VMIN]);
    EXPECT_EQ(before.c_cc[VTIME], after.c_cc[VTIME]);
}

TEST_F(PtyTest, CancelFlagPropagation) {
    std::atomic<bool> cancel{false};
    std::atomic<bool> worker_saw_cancel{false};

    TerminalGuard guard;

    std::thread worker([&]() {
        while (!cancel.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        worker_saw_cancel.store(true);
    });

    write_to_master("q", 1);
    char c = 0;
    if (guard.check_key_press(c)) {
        if (c == 'q') {
            cancel.store(true);
        }
    }

    worker.join();
    EXPECT_TRUE(worker_saw_cancel.load());
    EXPECT_TRUE(cancel.load());
}

#include <cstdlib>
#include <sys/wait.h>

class CliInterruptTest : public ::testing::Test {
protected:
    fs::path tmp_file_;
    int master_fd_ = -1;
    int pid_ = -1;

    void SetUp() override {
        tmp_file_ = fs::temp_directory_path() / ("cli_interrupt_" + std::to_string(getpid()));
        std::ofstream f(tmp_file_, std::ios::binary);
        std::vector<char> data(1024 * 1024, 'A');
        f.write(data.data(), data.size());
    }

    void TearDown() override {
        if (master_fd_ >= 0) close(master_fd_);
        fs::remove(tmp_file_);
        fs::path out = tmp_file_.parent_path() / (tmp_file_.stem().string() + ".torrent");
        fs::remove(out);
    }

    int spawn_with_pty(const std::string& cmd) {
        master_fd_ = posix_openpt(O_RDWR | O_NOCTTY);
        if (master_fd_ < 0) return -1;
        if (grantpt(master_fd_) != 0 || unlockpt(master_fd_) != 0) return -1;

        char* slave_name = ptsname(master_fd_);
        if (!slave_name) return -1;

        pid_ = fork();
        if (pid_ < 0) return -1;

        if (pid_ == 0) {
            close(master_fd_);
            int slave_fd = open(slave_name, O_RDWR);
            dup2(slave_fd, STDIN_FILENO);
            dup2(slave_fd, STDOUT_FILENO);
            dup2(slave_fd, STDERR_FILENO);
            close(slave_fd);
            setsid();
            execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
            _exit(127);
        }

        return pid_;
    }

    int wait_for_exit() {
        int status;
        waitpid(pid_, &status, 0);
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        return -1;
    }
};

TEST_F(CliInterruptTest, ExitCode1OnQuitKeyPress) {
    std::string binary = std::string(BINARY_PATH);
    std::string cmd = binary + " --path " + tmp_file_.string() +
        " --tracker https://tracker.example.com/announce" +
        " --output-dir " + tmp_file_.parent_path().string();

    ASSERT_GE(spawn_with_pty(cmd), 0);

    usleep(200000);
    (void)write(master_fd_, "q", 1);

    int exit_code = wait_for_exit();
    EXPECT_EQ(exit_code, 1);
}

TEST_F(CliInterruptTest, ExitCode0OnNormalCompletion) {
    std::string binary = std::string(BINARY_PATH);
    std::string cmd = binary + " --path " + tmp_file_.string() +
        " --tracker https://tracker.example.com/announce" +
        " --output-dir " + tmp_file_.parent_path().string();

    ASSERT_GE(spawn_with_pty(cmd), 0);

    int exit_code = wait_for_exit();
    EXPECT_EQ(exit_code, 0);
}

#endif
