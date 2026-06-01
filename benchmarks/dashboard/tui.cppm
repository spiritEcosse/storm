// ANSI TUI for storm_bench_dashboard (Issue #247, Phase 3).
//
// State model + mutators + terminal I/O. Render functions live in
// tui_render.hpp (included below) to stay under the 600-line cap.
// No ncurses, no `import storm`, no gbench.

module;

#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <poll.h>
#include <ranges>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <utility>
#include <vector>

export module storm.bench_dashboard.tui;

import storm.bench_dashboard.wire;

export namespace bench_dashboard::tui {

    // ANSI escape constants — used by both this file and tui_render.hpp.
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

    struct ComplexityEntry {
        std::string base_name;
        std::string complexity_class;
        double      complexity_coef{0.0};
        double      rms_pct{0.0};
        std::string baseline_class; // NOLINT(readability-redundant-member-init)
        double      baseline_coef{0.0};
        bool        shape_regression{false};
        bool        coef_regression{false};
        bool        coef_improvement{false};
        bool        baseline_looked_up{false};
    };

    struct CategoryBucket {
        std::string                  name;
        std::vector<wire::ResultMsg> results;
        std::vector<ComplexityEntry> complexity;
    };

    struct Session {
        std::string                 filter;    // NOLINT(readability-redundant-member-init)
        std::string                 timestamp; // NOLINT(readability-redundant-member-init)
        bool                        is_full_run{false};
        bool                        is_raw{false};
        bool                        expanded{true};
        bool                        complete{false};
        std::size_t                 result_count{0};
        std::size_t                 expected_count{0};
        std::vector<CategoryBucket> categories;
        std::int64_t                run_id{0};
        std::size_t                 ok_count{0};
        std::size_t                 regression_count{0};
        std::size_t                 improvement_count{0};
        std::size_t                 severe_count{0};
        std::size_t                 raw_matched{0};   // rows with an efficiency_pct
        std::size_t                 raw_total{0};     // measurement rows seen while baseline is raw
        double                      raw_eff_sum{0.0}; // sum of efficiency_pct for the average
    };

    struct DashboardState {
        std::vector<Session> sessions;
        std::size_t          spinner_tick{0};
        bool                 order_arrival{false};
        double               regression_threshold{5.0};
        std::string          baseline_label; // NOLINT(readability-redundant-member-init)
        std::int64_t         baseline_run_id{0};
        bool                 baseline_is_raw{false};
        double               complexity_threshold{5.0};
        int                  scroll_offset{0};
    };

    // -----------------------------------------------------------------------
    // State mutators
    // -----------------------------------------------------------------------

    inline auto open_session(DashboardState& s, std::string_view filter, bool is_full_run, bool is_raw) -> Session& {
        for (auto& prev : s.sessions) {
            prev.expanded = false;
        }
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
        ns.is_raw         = is_raw;
        ns.expanded       = true;
        ns.complete       = false;
        ns.expected_count = expected;
        s.sessions.insert(s.sessions.begin(), std::move(ns));
        return s.sessions.front();
    }

    inline auto strip_complexity_suffix(std::string_view name) -> std::string_view {
        for (auto suffix : {std::string_view{"_BigO"}, std::string_view{"_RMS"}}) {
            if (name.size() > suffix.size() && name.substr(name.size() - suffix.size()) == suffix) {
                return name.substr(0, name.size() - suffix.size());
            }
        }
        return name;
    }

    inline auto fill_complexity_from_bigo(ComplexityEntry& ce, wire::ResultMsg const& m, double threshold) -> void {
        ce.complexity_class   = m.complexity_class;
        ce.complexity_coef    = m.complexity_coef;
        ce.baseline_looked_up = m.baseline_looked_up;
        ce.baseline_class     = m.baseline_class;
        ce.baseline_coef      = m.baseline_coef;
        ce.shape_regression   = m.shape_regression;
        if (!ce.shape_regression && ce.baseline_looked_up && ce.baseline_coef > 0.0) {
            const double drift  = (ce.complexity_coef - ce.baseline_coef) / ce.baseline_coef * 100.0;
            ce.coef_regression  = drift >= threshold;
            ce.coef_improvement = drift <= -threshold;
        }
    }

    inline auto apply_complexity_msg(ComplexityEntry& ce, wire::ResultMsg const& m, double threshold) -> void {
        if (m.row_kind == wire::kRowKindBigO) {
            fill_complexity_from_bigo(ce, m, threshold);
        } else {
            ce.rms_pct = m.rms_pct;
        }
    }

    inline auto add_complexity(CategoryBucket& bucket, wire::ResultMsg const& m, double threshold) -> void {
        const std::string base{strip_complexity_suffix(m.test_name)};
        for (auto& entry : bucket.complexity) {
            if (entry.base_name == base) {
                apply_complexity_msg(entry, m, threshold);
                return;
            }
        }
        ComplexityEntry ce{};
        ce.base_name = base;
        apply_complexity_msg(ce, m, threshold);
        bucket.complexity.push_back(std::move(ce));
    }

    inline auto find_or_create_bucket(Session& sess, std::string_view category) -> CategoryBucket& {
        for (auto& bucket : sess.categories) {
            if (bucket.name == category) {
                return bucket;
            }
        }
        sess.categories
                .emplace_back(std::string{category}, std::vector<wire::ResultMsg>{}, std::vector<ComplexityEntry>{});
        return sess.categories.back();
    }

    inline auto bump_delta_counters(Session& sess, double delta_pct, double regression_threshold) -> void {
        if (delta_pct >= regression_threshold * 2.0) {
            ++sess.severe_count;
        } else if (delta_pct >= regression_threshold) {
            ++sess.regression_count;
        } else if (delta_pct <= -regression_threshold) {
            ++sess.improvement_count;
        } else {
            ++sess.ok_count;
        }
    }

    inline auto add_result(Session& sess, wire::ResultMsg const& m, double regression_threshold) -> void {
        if (m.row_kind == wire::kRowKindBigO || m.row_kind == wire::kRowKindRms) {
            add_complexity(find_or_create_bucket(sess, m.category), m, regression_threshold);
            return;
        }
        ++sess.result_count;
        if (m.efficiency_pct.has_value()) {
            ++sess.raw_total;
            ++sess.raw_matched;
            sess.raw_eff_sum += *m.efficiency_pct;
        } else if (m.baseline_looked_up && !m.delta_pct.has_value()) {
            ++sess.raw_total;
        }
        if (m.delta_pct.has_value()) {
            bump_delta_counters(sess, *m.delta_pct, regression_threshold);
        }
        auto& bucket = find_or_create_bucket(sess, m.category);
        bucket.results.insert(bucket.results.begin(), m);
    }

    inline auto mark_complete(DashboardState& s) -> void {
        if (!s.sessions.empty()) {
            s.sessions.front().complete = true;
        }
    }

    inline auto toggle_session(DashboardState& s, int ordinal) -> void {
        const auto idx = static_cast<std::size_t>(ordinal - 1);
        if (idx < s.sessions.size()) {
            s.sessions[idx].expanded = !s.sessions[idx].expanded;
        }
    }

    // -----------------------------------------------------------------------
    // Terminal RAII + key reader
    // -----------------------------------------------------------------------

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
            if (have_termios_) {
                ::tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
            }
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

    inline auto map_regular_char(char c) -> KeyEvent {
        if (c >= '1' && c <= '9') {
            return {Key::Digit, c};
        }
        if (c == 0x03) {
            return {Key::Quit, '\0'};
        }
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
        KeyEvent            ev{};
        std::array<char, 2> seq{};
        if (::read(STDIN_FILENO, seq.data(), 1) == 1 && seq[0] == '[' && ::read(STDIN_FILENO, seq.data() + 1, 1) == 1) {
            if (seq[1] == 'A') {
                ev = {Key::ScrollUp, '\0'};
            } else if (seq[1] == 'B') {
                ev = {Key::ScrollDown, '\0'};
            }
        }
        char drain{};
        while (::read(STDIN_FILENO, &drain, 1) == 1) { /* discard */
        }
        return ev;
    }

    inline auto wait_for_stdin_byte(int timeout_ms) -> std::optional<char> {
        if (::isatty(STDIN_FILENO) == 0) {
            if (timeout_ms > 0) {
                ::usleep(static_cast<useconds_t>(timeout_ms) * 1000U);
            }
            return std::nullopt;
        }
        pollfd pfd{};
        pfd.fd        = STDIN_FILENO;
        pfd.events    = POLLIN;
        const int prc = ::poll(&pfd, 1, timeout_ms);
        if (prc <= 0 || (pfd.revents & POLLIN) == 0) {
            return std::nullopt;
        }
        char c = '\0';
        if (::read(STDIN_FILENO, &c, 1) != 1) {
            return std::nullopt;
        }
        return c;
    }

    inline auto read_key(int timeout_ms) -> KeyEvent {
        const auto byte = wait_for_stdin_byte(timeout_ms);
        if (!byte) {
            return {};
        }
        if (*byte == 0x1b) {
            return read_escape_sequence();
        }
        return map_regular_char(*byte);
    }

    inline auto write_full_frame(std::string const& frame) -> void {
        std::fwrite(frame.data(), 1, frame.size(), stdout);
        std::fflush(stdout);
    }

    // -----------------------------------------------------------------------
    // Render functions (textual include — depends on types above)
    // -----------------------------------------------------------------------

#include "tui_render.hpp"

} // namespace bench_dashboard::tui
