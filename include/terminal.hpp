#ifndef TERMINAL_HPP
#define TERMINAL_HPP

#include <memory>
#include <stdexcept>

/**
 * @brief Exception thrown when the user interrupts an operation or a hashing timeout occurs.
 *
 * Thrown on 'q'/'Q'/Ctrl-C key press or after 30 seconds of no hashing progress.
 * Inherits from std::runtime_error so existing catch blocks remain compatible.
 */
struct UserInterrupt : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/**
 * @brief RAII guard that manages terminal raw mode for non-blocking key press detection.
 *
 * On construction, detects whether stdin is a TTY and, if so, enters raw mode
 * (non-canonical, no echo, VMIN=0, VTIME=1). On destruction, restores the
 * original terminal settings unconditionally — even if an exception is thrown.
 *
 * In non-interactive environments (pipes, redirection), the guard is a safe no-op.
 * Non-copyable to prevent double-restore.
 *
 * Platform support: POSIX (termios) and Windows (conio.h).
 */
class TerminalGuard
{
  public:
    TerminalGuard();
    ~TerminalGuard();

    /**
     * @brief Non-blocking check for a pending key press on stdin.
     * @param c Output parameter set to the read character when returning true.
     * @return true if a character was read, false otherwise (no input available).
     *
     * Uses read(STDIN_FILENO) on POSIX, _kbhit()/_getch() on Windows.
     * Returns immediately when no input is available.
     */
    bool check_key_press(char &c) const;

    TerminalGuard(const TerminalGuard &) = delete;
    TerminalGuard &operator=(const TerminalGuard &) = delete;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
