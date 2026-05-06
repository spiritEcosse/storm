// ANSI TUI for storm_bench_dashboard (Issue #247, Phase 3).
//
// Pure-stdlib + termios. No ncurses, no `import storm`, no gbench. Two
// concerns:
//
//   1. State — DashboardState holds a vector of Session, each session has
//      categorised buckets of results. apply_message() is the sole mutator.
//   2. Render — full-frame ANSI redraw on every state change. Bench rate is
//      ~1 result every few seconds; full redraws are cheap and keep the diff
//      logic out of the picture.
//
// Terminal RAII: TerminalGuard enables alt-screen + raw mode in its
// constructor and restores both in its destructor, so quitting via `q`,
// Ctrl-C, or any throw leaves the user's terminal sane.

module;

#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>
#include <ranges>
#include <sys/ioctl.h>
#include <vector>

// wire.hpp must be included in the global module fragment so its types stay
// at global attachment — same TU layout as the textual include in main.cpp
// and reporter.cpp; otherwise tui:: signatures would refer to a different
// `wire::ResultMsg` than the one main.cpp sees.
#include "wire.hpp"

export module storm.bench_dashboard.tui;

export namespace bench_dashboard::tui {

    // ANSI escape strings. Constants over std::format calls in the hot path.
    namespace ansi {
        inline constexpr std::string_view kAltScreenOn  = "\x1b[?1049h";
        inline constexpr std::string_view kAltScreenOff = "\x1b[?1049l";
        inline constexpr std::string_view kCursorHide   = "\x1b[?25l";
        inline constexpr std::string_view kCursorShow   = "\x1b[?25h";
        inline constexpr std::string_view kClearScreen  = "\x1b[2J\x1b[H";
        inline constexpr std::string_view kHome         = "\x1b[H";
        inline constexpr std::string_view kReset        = "\x1b[0m";
        inline constexpr std::string_view kBold         = "\x1b[1m";
        inline constexpr std::string_view kDim          = "\x1b[2m";
        inline constexpr std::string_view kFgGreen      = "\x1b[32m";
        inline constexpr std::string_view kFgYellow     = "\x1b[33m";
        inline constexpr std::string_view kFgRed        = "\x1b[31m";
        inline constexpr std::string_view kFgCyan       = "\x1b[36m";
        inline constexpr std::string_view kFgGrey       = "\x1b[90m";
    } // namespace ansi

    // -----------------------------------------------------------------------
    // State model
    // -----------------------------------------------------------------------

    struct CategoryBucket {
        std::string                  name;    // e.g. "WHERE"
        std::vector<wire::ResultMsg> results; // newest at front
    };

    struct Session {
        std::string                 filter{};
        std::string                 timestamp{}; // ISO 8601, shown in header
        bool                        is_full_run{false};
        bool                        expanded{true}; // newest = expanded; older auto-collapse
        bool                        complete{false};
        std::size_t                 result_count{0};
        std::size_t                 expected_count{0}; // from previous run, 0 = unknown
        std::vector<CategoryBucket> categories;        // first-seen order
        std::int64_t                run_id{0};         // FK into BenchRun (set by main.cpp)
    };

    struct DashboardState {
        std::vector<Session> sessions; // newest at front (push_front)
        std::size_t          spinner_tick{0};
        // Render-time toggle: when true, results inside each category render
        // in arrival (insertion) order rather than newest-first. The DB stays
        // in insert order regardless — this only affects the rendered frame.
        bool order_arrival{false};
    };

    // -----------------------------------------------------------------------
    // State mutators
    // -----------------------------------------------------------------------

    inline auto open_session(DashboardState& s, std::string_view filter, bool is_full_run) -> Session& {
        // Auto-collapse the prior session — only the active one stays expanded.
        for (auto& prev : s.sessions)
            prev.expanded = false;

        // Seed expected_count from the most recent completed session with the
        // same filter so the progress bar has a target on the first result.
        std::size_t expected = 0;
        for (auto const& prev : s.sessions) {
            if (prev.complete && prev.filter == filter) {
                expected = prev.result_count;
                break;
            }
        }

        Session ns{};
        ns.filter         = std::string{filter};
        ns.is_full_run    = is_full_run;
        ns.expanded       = true;
        ns.complete       = false;
        ns.expected_count = expected;
        s.sessions.insert(s.sessions.begin(), std::move(ns));
        return s.sessions.front();
    }

    inline auto add_result(Session& sess, wire::ResultMsg const& m) -> void {
        ++sess.result_count;

        // Find bucket by category, in first-seen order; newest result at front.
        for (auto& bucket : sess.categories) {
            if (bucket.name == m.category) {
                bucket.results.insert(bucket.results.begin(), m);
                return;
            }
        }
        CategoryBucket nb{m.category, {}};
        nb.results.insert(nb.results.begin(), m);
        sess.categories.push_back(std::move(nb));
    }

    inline auto mark_complete(DashboardState& s) -> void {
        if (!s.sessions.empty())
            s.sessions.front().complete = true;
    }

    // Toggle session by 1-based display position (position in vector = ordinal).
    inline auto toggle_session(DashboardState& s, int ordinal) -> void {
        const auto idx = static_cast<std::size_t>(ordinal - 1);
        if (idx < s.sessions.size())
            s.sessions[idx].expanded = !s.sessions[idx].expanded;
    }

    // -----------------------------------------------------------------------
    // Render helpers
    // -----------------------------------------------------------------------

    inline auto colour_for_latency(double real_ns) -> std::string_view {
        if (real_ns < 1.0e6) // < 1 ms
            return ansi::kFgGreen;
        if (real_ns < 1.0e7) // < 10 ms
            return ansi::kFgYellow;
        return ansi::kFgRed;
    }

    inline auto format_latency(double ns) -> std::string {
        if (ns < 1.0e3)
            return std::format("{:7.1f} ns", ns);
        if (ns < 1.0e6)
            return std::format("{:7.2f} us", ns / 1.0e3);
        if (ns < 1.0e9)
            return std::format("{:7.2f} ms", ns / 1.0e6);
        return std::format("{:7.2f}  s", ns / 1.0e9);
    }

    inline auto format_ips(double ips) -> std::string {
        if (ips <= 0.0)
            return std::string(12, ' '); // match width of widest numeric branch
        if (ips >= 1.0e9)
            return std::format("{:6.2f}G ips", ips / 1.0e9);
        if (ips >= 1.0e6)
            return std::format("{:6.2f}M ips", ips / 1.0e6);
        if (ips >= 1.0e3)
            return std::format("{:6.2f}k ips", ips / 1.0e3);
        return std::format("{:6.0f}  ips", ips);
    }

    inline auto spinner_glyph(std::size_t tick) -> char {
        constexpr char glyphs[] = {'|', '/', '-', '\\'};
        return glyphs[tick % 4];
    }

    inline auto make_progress_bar(std::size_t done, std::size_t total, std::size_t width) -> std::string {
        std::string bar;
        bar.reserve(width + 2);
        bar += '[';
        if (total == 0) {
            // Unknown total — pulse: fill proportional to spinner_tick not
            // available here, so just show all dots.
            for (std::size_t i = 0; i < width; ++i)
                bar += '.';
        } else {
            const std::size_t filled = done >= total ? width : (done * width) / total;
            for (std::size_t i = 0; i < width; ++i)
                bar += (i < filled) ? '#' : '.';
        }
        bar += ']';
        return bar;
    }

    inline auto append_session_header(std::string& out, Session const& sess, int ordinal, std::size_t spinner_tick)
            -> void {
        const std::string_view chevron       = sess.expanded ? "▼" : "▶";
        const std::string_view status_colour = sess.complete ? ansi::kFgGreen : ansi::kFgCyan;
        // Show HH:MM:SS from the ISO 8601 timestamp (positions 11-18).
        const std::string_view ts = sess.timestamp.size() >= 19 ? std::string_view{sess.timestamp}.substr(11, 8)
                                                                : std::string_view{sess.timestamp};
        const std::string label = sess.is_full_run ? std::string{"full run"} : std::format("filter='{}'", sess.filter);

        if (sess.complete) {
            out += std::format(
                    "{}{}{} [{}] {} · {} results · {}complete{} {}{}{}UTC{}\n",
                    ansi::kBold,
                    chevron,
                    ansi::kReset,
                    ordinal,
                    label,
                    sess.result_count,
                    status_colour,
                    ansi::kReset,
                    ansi::kFgGrey,
                    ts,
                    ts.empty() ? "" : " ",
                    ansi::kReset
            );
        } else {
            // Active run: show spinner + progress bar.
            const std::string bar      = make_progress_bar(sess.result_count, sess.expected_count, /*width=*/20);
            const std::string progress = sess.expected_count > 0
                                                 ? std::format("{}/{}", sess.result_count, sess.expected_count)
                                                 : std::format("{}", sess.result_count);

            out += std::format(
                    "{}{}{} [{}] {} · {} {} {} {}{}{}\n",
                    ansi::kBold,
                    chevron,
                    ansi::kReset,
                    ordinal,
                    label,
                    spinner_glyph(spinner_tick),
                    status_colour,
                    bar,
                    ansi::kReset,
                    ansi::kFgGrey,
                    progress,
                    ansi::kReset
            );
        }
    }

    inline auto append_result_line(std::string& out, wire::ResultMsg const& r) -> void {
        const auto             colour = colour_for_latency(r.real_ns);
        const std::string_view name{r.test_name};
        const std::string_view shown = name.size() > 48 ? name.substr(name.size() - 48) : name;
        out += std::format(
                "      {}{:<48}{}  {}{}{}  {}\n",
                ansi::kReset,
                shown,
                ansi::kReset,
                colour,
                format_latency(r.real_ns),
                ansi::kReset,
                format_ips(r.items_per_second)
        );
    }

    // Bucket stores newest at front; arrival order = oldest-first = reverse.
    // views::reverse and views::all have different types, so unify via auto&&
    // in a lambda that accepts either range.
    inline auto append_bucket_results(std::string& out, CategoryBucket const& bucket, bool order_arrival) -> void {
        auto emit = [&](auto&& range) {
            for (auto const& r : range)
                append_result_line(out, r);
        };
        if (order_arrival)
            emit(bucket.results | std::views::reverse);
        else
            emit(bucket.results);
    }

    inline auto append_session_body(std::string& out, Session const& sess, bool order_arrival) -> void {
        for (auto const& bucket : sess.categories) {
            out += std::format(
                    "  {}{}{} {}({}){}\n",
                    ansi::kBold,
                    bucket.name,
                    ansi::kReset,
                    ansi::kFgGrey,
                    bucket.results.size(),
                    ansi::kReset
            );
            append_bucket_results(out, bucket, order_arrival);
        }
    }

    inline auto terminal_rows() -> int {
        struct winsize ws{};
        if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
            return static_cast<int>(ws.ws_row);
        return 40; // safe fallback
    }

    // Count the lines a session body occupies.
    inline auto session_body_lines(Session const& sess) -> int {
        int lines = 0;
        for (auto const& bucket : sess.categories)
            lines += 1 + static_cast<int>(bucket.results.size()); // header + rows
        return lines;
    }

    // Build a full-frame ANSI string for the current state. Caller writes it
    // to stdout in one shot — single write keeps tearing imperceptible.
    // Content is clamped to the terminal height so the newest session always
    // stays visible at the top regardless of how many sessions exist.
    inline auto render(DashboardState const& s) -> std::string {
        std::string out;
        out.reserve(4096);
        out += ansi::kClearScreen;

        const int header_lines = 2; // title line + blank
        out += std::format(
                "{}storm_bench_dashboard{}  ·  press {}q{} to quit, {}r{} to refresh, {}1-9{} to toggle sessions\n\n",
                ansi::kBold,
                ansi::kReset,
                ansi::kFgCyan,
                ansi::kReset,
                ansi::kFgCyan,
                ansi::kReset,
                ansi::kFgCyan,
                ansi::kReset
        );

        if (s.sessions.empty()) {
            out += std::format("  {}waiting for storm_bench …{}\n", ansi::kDim, ansi::kReset);
            return out;
        }

        const int max_rows  = terminal_rows() - header_lines;
        int       rows_used = 0;
        int       ordinal   = 1; // always 1-based position in display order

        for (auto const& sess : s.sessions) {
            if (rows_used >= max_rows)
                break;
            append_session_header(out, sess, ordinal++, s.spinner_tick);
            ++rows_used;
            if (sess.expanded) {
                const int body_lines = session_body_lines(sess);
                const int available  = max_rows - rows_used;
                if (body_lines <= available) {
                    append_session_body(out, sess, s.order_arrival);
                    rows_used += body_lines;
                } else {
                    // Partial body — render as many result lines as fit,
                    // then a truncation notice.
                    int written = 0;
                    for (auto const& bucket : sess.categories) {
                        if (rows_used >= max_rows - 1)
                            break;
                        out += std::format(
                                "  {}{}{} {}({}){}\n",
                                ansi::kBold,
                                bucket.name,
                                ansi::kReset,
                                ansi::kFgGrey,
                                bucket.results.size(),
                                ansi::kReset
                        );
                        ++rows_used;
                        auto emit_clipped = [&](auto&& range) {
                            for (auto const& r : range) {
                                if (rows_used >= max_rows - 1)
                                    break;
                                append_result_line(out, r);
                                ++rows_used;
                                ++written;
                            }
                        };
                        if (s.order_arrival)
                            emit_clipped(bucket.results | std::views::reverse);
                        else
                            emit_clipped(bucket.results);
                    }
                    out += std::format("  {}… resize terminal to see all results{}\n", ansi::kDim, ansi::kReset);
                    ++rows_used;
                }
            }
            if (rows_used < max_rows) {
                out += '\n';
                ++rows_used;
            }
        }
        return out;
    }

    // -----------------------------------------------------------------------
    // Terminal RAII + key reader
    // -----------------------------------------------------------------------

    // Switches the terminal into alt-screen + cbreak (no line buffering, no
    // echo). Restores both — and the original termios — on destruction. NOT
    // copyable / movable; one instance per process.
    class TerminalGuard {
      public:
        TerminalGuard() {
            if (::isatty(STDIN_FILENO) != 0) {
                if (::tcgetattr(STDIN_FILENO, &saved_) == 0) {
                    termios raw = saved_;
                    raw.c_lflag &= ~(ICANON | ECHO);
                    raw.c_cc[VMIN]  = 0; // non-blocking single-byte reads
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

    enum class Key : std::uint8_t { None, Quit, Refresh, Digit };

    struct KeyEvent {
        Key  kind{Key::None};
        char digit{'\0'}; // valid when kind == Digit, '1'..'9'
    };

    // Non-blocking read of a single keystroke. Returns Key::None when nothing
    // is available within `timeout_ms` (-1 = forever). When stdin is not a
    // tty (pipe, /dev/null, background process) skip polling entirely and
    // just sleep — polling a non-tty stdin spins at 100% CPU because poll
    // immediately returns POLLHUP on every call.
    inline auto read_key(int timeout_ms) -> KeyEvent {
        if (::isatty(STDIN_FILENO) == 0) {
            if (timeout_ms > 0)
                ::usleep(static_cast<useconds_t>(timeout_ms) * 1000U);
            return {};
        }

        pollfd pfd{};
        pfd.fd        = STDIN_FILENO;
        pfd.events    = POLLIN;
        const int prc = ::poll(&pfd, 1, timeout_ms);
        if (prc <= 0 || (pfd.revents & POLLIN) == 0)
            return {};

        char c = '\0';
        if (::read(STDIN_FILENO, &c, 1) != 1)
            return {};

        if (c == 'q' || c == 'Q' || c == 0x03 /*Ctrl-C*/)
            return {Key::Quit, '\0'};
        if (c == 'r' || c == 'R')
            return {Key::Refresh, '\0'};
        if (c >= '1' && c <= '9')
            return {Key::Digit, c};

        // Drain any remaining bytes of an escape sequence so the next call
        // doesn't see them. termios is in non-blocking single-byte mode.
        if (c == 0x1b) {
            char drain;
            while (::read(STDIN_FILENO, &drain, 1) == 1) { /* discard */
            }
        }
        return {};
    }

    inline auto write_full_frame(std::string const& frame) -> void {
        std::fwrite(frame.data(), 1, frame.size(), stdout);
        std::fflush(stdout);
    }

} // namespace bench_dashboard::tui
