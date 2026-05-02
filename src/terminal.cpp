#include "terminal.hpp"

#ifndef _WIN32
#include <unistd.h>
#include <termios.h>
#else
#include <conio.h>
#include <io.h>
#endif

struct TerminalGuard::Impl
{
    bool is_tty = false;
    bool terminal_modified = false;

#ifndef _WIN32
    struct termios original_termios;
#endif

    Impl()
    {
#ifndef _WIN32
        is_tty = isatty(STDIN_FILENO) != 0;
        if (is_tty)
        {
            tcgetattr(STDIN_FILENO, &original_termios);
            struct termios raw = original_termios;
            raw.c_lflag &= ~(ICANON | ECHO);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 1;
            tcsetattr(STDIN_FILENO, TCSANOW, &raw);
            terminal_modified = true;
        }
#else
        is_tty = _isatty(_fileno(stdin)) != 0;
#endif
    }

    ~Impl()
    {
#ifndef _WIN32
        if (terminal_modified)
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
        }
#endif
    }

    bool check_key(char &c) const
    {
#ifndef _WIN32
        return read(STDIN_FILENO, &c, 1) > 0;
#else
        if (_kbhit())
        {
            c = static_cast<char>(_getch());
            return true;
        }
        return false;
#endif
    }
};

TerminalGuard::TerminalGuard() : impl_(std::make_unique<Impl>())
{
}

TerminalGuard::~TerminalGuard() = default;

bool TerminalGuard::check_key_press(char &c) const
{
    return impl_->check_key(c);
}
