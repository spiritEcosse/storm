#ifndef STORM_BENCH_DASHBOARD_TUI_TERMINAL_HPP
#define STORM_BENCH_DASHBOARD_TUI_TERMINAL_HPP

// Terminal RAII + key reader for storm_bench_dashboard's TUI (Issue #247).
//
// Textual header — `#include`d ONCE at the bottom of `tui.cppm` (inside the
// purview, after the export namespace closes) so every symbol here lives at
// module-internal linkage. Split out of tui.cppm purely to keep each TU
// under the 600-line code-quality cap. Do NOT include this from any other
// TU; depends on the `ansi::` constants from tui.cppm being visible. All
// system headers (`<termios.h>`, `<unistd.h>`, `<poll.h>`) come from the
// global module fragment of tui.cppm.

namespace bench_dashboard::tui {

    // Switches the terminal into alt-screen + cbreak (no line buffering, no
    // echo). Restores both — and the original termios — on destruction.
    class TerminalGuard {
      public:
        TerminalGuard() {
            if (::isatty(STDIN_FILENO) != 0) {
                if (::tcgetattr(STDIN_FILENO, &saved_) == 0) {
                    termios raw = saved_;
                    raw.c_lflag &= ~(ICANON | ECHO);
                    raw.c_cc[VMIN]  = 0;
                    raw.c_cc[VTIME] = 0;
                    ::tcsetattr(STDIN_FILENO, TCSANOW, &raw);
                    have_termios_ = true;
                }
            }
            std::fwrite(ansi::kAltScreenOn.data(), 1, ansi::kAltScreenOn.size(), stdout);
            std::fwrite(ansi::kCursorHide.data(), 1, ansi::kCursorHide.size(), stdout);
            std::fflush(stdout);
        }
        TerminalGuard(TerminalGuard const&)                    = delete;
        auto operator=(TerminalGuard const&) -> TerminalGuard& = delete;
        TerminalGuard(TerminalGuard&&)                         = delete;
        auto operator=(TerminalGuard&&) -> TerminalGuard&      = delete;
        ~TerminalGuard() {
            std::fwrite(ansi::kCursorShow.data(), 1, ansi::kCursorShow.size(), stdout);
            std::fwrite(ansi::kAltScreenOff.data(), 1, ansi::kAltScreenOff.size(), stdout);
            std::fflush(stdout);
            if (have_termios_)
                ::tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
        }

      private:
        termios saved_{};
        bool    have_termios_{false};
    };

    enum class Key : std::uint8_t { None, Quit, Refresh, Digit, ScrollUp, ScrollDown };

    struct KeyEvent {
        Key  kind{Key::None};
        char digit{'\0'};
    };

    // Map a printable character to a KeyEvent. Returns Key::None on unknowns.
    inline auto map_regular_char(char c) -> KeyEvent {
        if (c >= '1' && c <= '9')
            return {Key::Digit, c};
        if (c == 0x03)
            return {Key::Quit, '\0'};
        const char lc = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
        switch (lc) {
        case 'q':
            return {Key::Quit, '\0'};
        case 'r':
            return {Key::Refresh, '\0'};
        case 'j':
            return {Key::ScrollDown, '\0'};
        case 'k':
            return {Key::ScrollUp, '\0'};
        default:
            return {};
        }
    }

    inline auto read_escape_sequence() -> KeyEvent {
        KeyEvent ev{};
        char     seq[2]{};
        if (::read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[' && ::read(STDIN_FILENO, &seq[1], 1) == 1) {
            if (seq[1] == 'A')
                ev = {Key::ScrollUp, '\0'};
            else if (seq[1] == 'B')
                ev = {Key::ScrollDown, '\0'};
        }
        char drain;
        while (::read(STDIN_FILENO, &drain, 1) == 1) { /* discard */
        }
        return ev;
    }

    inline auto wait_for_stdin_byte(int timeout_ms) -> std::optional<char> {
        if (::isatty(STDIN_FILENO) == 0) {
            if (timeout_ms > 0)
                ::usleep(static_cast<useconds_t>(timeout_ms) * 1000U);
            return std::nullopt;
        }
        pollfd pfd{};
        pfd.fd        = STDIN_FILENO;
        pfd.events    = POLLIN;
        const int prc = ::poll(&pfd, 1, timeout_ms);
        if (prc <= 0 || (pfd.revents & POLLIN) == 0)
            return std::nullopt;
        char c = '\0';
        if (::read(STDIN_FILENO, &c, 1) != 1)
            return std::nullopt;
        return c;
    }

    inline auto read_key(int timeout_ms) -> KeyEvent {
        const auto byte = wait_for_stdin_byte(timeout_ms);
        if (!byte)
            return {};
        if (*byte == 0x1b)
            return read_escape_sequence();
        return map_regular_char(*byte);
    }

    inline auto write_full_frame(std::string const& frame) -> void {
        std::fwrite(frame.data(), 1, frame.size(), stdout);
        std::fflush(stdout);
    }

} // namespace bench_dashboard::tui

#endif // STORM_BENCH_DASHBOARD_TUI_TERMINAL_HPP
