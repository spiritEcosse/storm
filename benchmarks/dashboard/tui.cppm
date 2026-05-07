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

    // Phase 7: one row in the per-category complexity footer.
    struct ComplexityEntry {
        std::string base_name;        // e.g. "Storm/WHERE/where_int_gt"
        std::string complexity_class; // "N", "NlgN", "N^2", ...
        double      complexity_coef{0.0};
        double      rms_pct{0.0};

        // Baseline values (set when a baseline run is active).
        std::string baseline_class{};
        double      baseline_coef{0.0};
        bool        shape_regression{false};
        bool        coef_regression{false}; // drift above threshold
        bool        coef_improvement{false};
        bool        baseline_looked_up{false};
    };

    struct CategoryBucket {
        std::string                  name;       // e.g. "WHERE"
        std::vector<wire::ResultMsg> results;    // newest at front
        std::vector<ComplexityEntry> complexity; // Phase 7: one per unique base name
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

        // Phase 6: baseline comparison counters.
        std::size_t ok_count{0};
        std::size_t regression_count{0};
        std::size_t improvement_count{0};
        std::size_t severe_count{0}; // delta_pct ≥ 2× threshold
    };

    struct DashboardState {
        std::vector<Session> sessions; // newest at front (push_front)
        std::size_t          spinner_tick{0};
        // Render-time toggle: when true, results inside each category render
        // in arrival (insertion) order rather than newest-first. The DB stays
        // in insert order regardless — this only affects the rendered frame.
        bool order_arrival{false};

        // Phase 6: set from --regression-threshold (default 5%). Stored here
        // so render functions receive it without extra parameters at the call site.
        double regression_threshold{5.0};

        // Phase 6: label of the active baseline, shown in the header.
        // Empty when --baseline none or no baseline found.
        std::string baseline_label{};
        // run_id of the baseline BenchRun row; 0 = none.
        std::int64_t baseline_run_id{0};

        // Phase 7: threshold for complexity coefficient drift (default 5%).
        double complexity_threshold{5.0};

        // Scroll: number of body lines scrolled past the top (↑/↓ or j/k).
        int scroll_offset{0};
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

    // Phase 7: upsert a ComplexityEntry in a bucket from a BigO or RMS wire message.
    // strip_bigo_suffix removes "_BigO" or "_RMS" to get the benchmark family name.
    inline auto strip_complexity_suffix(std::string_view name) -> std::string_view {
        for (auto suffix : {std::string_view{"_BigO"}, std::string_view{"_RMS"}}) {
            if (name.size() > suffix.size() && name.substr(name.size() - suffix.size()) == suffix)
                return name.substr(0, name.size() - suffix.size());
        }
        return name;
    }

    inline auto add_complexity(CategoryBucket& bucket, wire::ResultMsg const& m, double threshold) -> void {
        const std::string base{strip_complexity_suffix(m.test_name)};
        for (auto& entry : bucket.complexity) {
            if (entry.base_name == base) {
                if (m.row_kind == wire::kRowKindBigO) {
                    entry.complexity_class   = m.complexity_class;
                    entry.complexity_coef    = m.complexity_coef;
                    entry.baseline_looked_up = m.baseline_looked_up;
                    entry.baseline_class     = m.baseline_class;
                    entry.baseline_coef      = m.baseline_coef;
                    entry.shape_regression   = m.shape_regression;
                    if (!entry.shape_regression && entry.baseline_looked_up && entry.baseline_coef > 0.0) {
                        const double drift =
                                (entry.complexity_coef - entry.baseline_coef) / entry.baseline_coef * 100.0;
                        entry.coef_regression  = drift >= threshold;
                        entry.coef_improvement = drift <= -threshold;
                    }
                } else {
                    entry.rms_pct = m.rms_pct;
                }
                return;
            }
        }
        ComplexityEntry ce{};
        ce.base_name = base;
        if (m.row_kind == wire::kRowKindBigO) {
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
        } else {
            ce.rms_pct = m.rms_pct;
        }
        bucket.complexity.push_back(std::move(ce));
    }

    inline auto add_result(Session& sess, wire::ResultMsg const& m, double regression_threshold) -> void {
        // Phase 7: BigO/RMS rows go to complexity entries, not result rows.
        if (m.row_kind == wire::kRowKindBigO || m.row_kind == wire::kRowKindRms) {
            for (auto& bucket : sess.categories) {
                if (bucket.name == m.category) {
                    add_complexity(bucket, m, regression_threshold);
                    return;
                }
            }
            CategoryBucket nb{m.category, {}, {}};
            add_complexity(nb, m, regression_threshold);
            sess.categories.push_back(std::move(nb));
            return;
        }

        ++sess.result_count;

        if (m.delta_pct.has_value()) {
            const double d = *m.delta_pct;
            if (d >= regression_threshold * 2.0)
                ++sess.severe_count;
            else if (d >= regression_threshold)
                ++sess.regression_count;
            else if (d <= -regression_threshold)
                ++sess.improvement_count;
            else
                ++sess.ok_count;
        }

        // Find bucket by category, in first-seen order; newest result at front.
        for (auto& bucket : sess.categories) {
            if (bucket.name == m.category) {
                bucket.results.insert(bucket.results.begin(), m);
                return;
            }
        }
        CategoryBucket nb{m.category, {}, {}};
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

    [[nodiscard]] inline auto format_delta(double pct, double threshold) -> std::pair<std::string_view, std::string> {
        if (pct >= threshold * 2.0)
            return {ansi::kFgRed, std::format("{:+.1f}% SEVERE", pct)};
        if (pct >= threshold)
            return {ansi::kFgRed, std::format("{:+.1f}% REGRESS", pct)};
        if (pct <= -threshold)
            return {ansi::kFgGreen, std::format("{:+.1f}% IMPROVE", pct)};
        return {ansi::kFgGrey, std::format("{:+.1f}%", pct)};
    }

    inline auto append_result_line(std::string& out, wire::ResultMsg const& r, double regression_threshold) -> void {
        const auto             colour = colour_for_latency(r.real_ns);
        const std::string_view name{r.test_name};
        const std::string_view shown = name.size() > 48 ? name.substr(name.size() - 48) : name;

        if (r.delta_pct.has_value()) {
            const auto [dcol, dtxt] = format_delta(*r.delta_pct, regression_threshold);
            out += std::format(
                    "      {}{:<48}{}  {}{}{}  {}  {}{}{}\n",
                    ansi::kReset,
                    shown,
                    ansi::kReset,
                    colour,
                    format_latency(r.real_ns),
                    ansi::kReset,
                    format_ips(r.items_per_second),
                    dcol,
                    dtxt,
                    ansi::kReset
            );
        } else if (r.baseline_looked_up) {
            // Baseline active but no matching row — partial/filtered run.
            out += std::format(
                    "      {}{:<48}{}  {}{}{}  {}  {}—{}\n",
                    ansi::kReset,
                    shown,
                    ansi::kReset,
                    colour,
                    format_latency(r.real_ns),
                    ansi::kReset,
                    format_ips(r.items_per_second),
                    ansi::kFgGrey,
                    ansi::kReset
            );
        } else {
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
    }

    // Bucket stores newest at front; arrival order = oldest-first = reverse.
    // views::reverse and views::all have different types, so unify via auto&&
    // in a lambda that accepts either range.
    inline auto append_bucket_results(
            std::string& out, CategoryBucket const& bucket, bool order_arrival, double regression_threshold
    ) -> void {
        auto emit = [&](auto&& range) {
            for (auto const& r : range)
                append_result_line(out, r, regression_threshold);
        };
        if (order_arrival)
            emit(bucket.results | std::views::reverse);
        else
            emit(bucket.results);
    }

    inline auto
    append_complexity_footer(std::string& out, CategoryBucket const& bucket, double /*complexity_threshold*/) -> void {
        if (bucket.complexity.empty())
            return;
        out += std::format("    {}Complexity:{}\n", ansi::kFgGrey, ansi::kReset);
        for (auto const& ce : bucket.complexity) {
            if (ce.baseline_looked_up) {
                if (ce.shape_regression) {
                    out += std::format(
                            "      {}{:<40}{}  {} {} → {}  {}✗ SHAPE{}\n",
                            ansi::kReset,
                            ce.base_name.size() > 40 ? std::string_view{ce.base_name}.substr(ce.base_name.size() - 40)
                                                     : std::string_view{ce.base_name},
                            ansi::kReset,
                            ce.baseline_class,
                            "→",
                            ce.complexity_class,
                            ansi::kFgRed,
                            ansi::kReset
                    );
                } else if (ce.coef_regression) {
                    const double drift = ce.baseline_coef > 0.0
                                                 ? (ce.complexity_coef - ce.baseline_coef) / ce.baseline_coef * 100.0
                                                 : 0.0;
                    out += std::format(
                            "      {}{:<40}{}  {}  coef {:.2g} → {:.2g}  {:+.1f}%  {}⚠ DRIFT{}\n",
                            ansi::kReset,
                            ce.base_name.size() > 40 ? std::string_view{ce.base_name}.substr(ce.base_name.size() - 40)
                                                     : std::string_view{ce.base_name},
                            ansi::kReset,
                            ce.complexity_class,
                            ce.baseline_coef,
                            ce.complexity_coef,
                            drift,
                            ansi::kFgYellow,
                            ansi::kReset
                    );
                } else if (ce.coef_improvement) {
                    const double drift = ce.baseline_coef > 0.0
                                                 ? (ce.complexity_coef - ce.baseline_coef) / ce.baseline_coef * 100.0
                                                 : 0.0;
                    out += std::format(
                            "      {}{:<40}{}  {}  coef {:.2g} → {:.2g}  {:+.1f}%  {}↑ IMPROVE{}\n",
                            ansi::kReset,
                            ce.base_name.size() > 40 ? std::string_view{ce.base_name}.substr(ce.base_name.size() - 40)
                                                     : std::string_view{ce.base_name},
                            ansi::kReset,
                            ce.complexity_class,
                            ce.baseline_coef,
                            ce.complexity_coef,
                            drift,
                            ansi::kFgGreen,
                            ansi::kReset
                    );
                } else {
                    const double drift = ce.baseline_coef > 0.0
                                                 ? (ce.complexity_coef - ce.baseline_coef) / ce.baseline_coef * 100.0
                                                 : 0.0;
                    out += std::format(
                            "      {}{:<40}{}  {}  coef {:.2g} → {:.2g}  {:+.1f}%  {}✓{}\n",
                            ansi::kReset,
                            ce.base_name.size() > 40 ? std::string_view{ce.base_name}.substr(ce.base_name.size() - 40)
                                                     : std::string_view{ce.base_name},
                            ansi::kReset,
                            ce.complexity_class,
                            ce.baseline_coef,
                            ce.complexity_coef,
                            drift,
                            ansi::kFgGreen,
                            ansi::kReset
                    );
                }
            } else {
                out += std::format(
                        "      {}{:<40}{}  {}  coef {:.2g}  rms {:.1f}%\n",
                        ansi::kReset,
                        ce.base_name.size() > 40 ? std::string_view{ce.base_name}.substr(ce.base_name.size() - 40)
                                                 : std::string_view{ce.base_name},
                        ansi::kReset,
                        ce.complexity_class,
                        ce.complexity_coef,
                        ce.rms_pct
                );
            }
        }
    }

    inline auto append_session_body(
            std::string&   out,
            Session const& sess,
            bool           order_arrival,
            double         regression_threshold,
            double         complexity_threshold
    ) -> void {
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
            append_bucket_results(out, bucket, order_arrival, regression_threshold);
            append_complexity_footer(out, bucket, complexity_threshold);
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
        for (auto const& bucket : sess.categories) {
            // header + result rows + optional complexity header + complexity rows
            const int complexity_lines = bucket.complexity.empty() ? 0 : 1 + static_cast<int>(bucket.complexity.size());
            lines += 1 + static_cast<int>(bucket.results.size()) + complexity_lines;
        }
        return lines;
    }

    // Render the per-session summary line when a baseline is active.
    inline auto append_summary_line(std::string& out, Session const& sess) -> void {
        const std::size_t total = sess.ok_count + sess.regression_count + sess.improvement_count + sess.severe_count;
        if (total == 0)
            return;
        out += std::format("  {}Summary:{}", ansi::kFgGrey, ansi::kReset);
        if (sess.ok_count > 0)
            out += std::format("  {} ok", sess.ok_count);
        if (sess.improvement_count > 0)
            out += std::format("  {}{} improve{}", ansi::kFgGreen, sess.improvement_count, ansi::kReset);
        if (sess.regression_count > 0)
            out += std::format("  {}{} regress{}", ansi::kFgRed, sess.regression_count, ansi::kReset);
        if (sess.severe_count > 0)
            out += std::format("  {}{} SEVERE{}", ansi::kFgRed, sess.severe_count, ansi::kReset);
        out += '\n';
    }

    // Build all session lines into a flat vector (one string per terminal row).
    // Scroll is applied by slicing this vector, so the header stays pinned.
    inline auto build_body_lines(DashboardState const& s) -> std::vector<std::string> {
        std::vector<std::string> lines;
        lines.reserve(256);

        int ordinal = 1;
        for (auto const& sess : s.sessions) {
            {
                std::string hdr;
                append_session_header(hdr, sess, ordinal++, s.spinner_tick);
                lines.push_back(std::move(hdr));
            }

            if (!sess.expanded)
                continue;

            const std::size_t compared =
                    sess.ok_count + sess.regression_count + sess.improvement_count + sess.severe_count;
            if (compared > 0) {
                std::string sumline;
                append_summary_line(sumline, sess);
                lines.push_back(std::move(sumline));
            }

            for (auto const& bucket : sess.categories) {
                std::string bkt_hdr = std::format(
                        "  {}{}{} {}({}){}\n",
                        ansi::kBold,
                        bucket.name,
                        ansi::kReset,
                        ansi::kFgGrey,
                        bucket.results.size(),
                        ansi::kReset
                );
                lines.push_back(std::move(bkt_hdr));

                auto emit = [&](auto&& range) {
                    for (auto const& r : range) {
                        std::string row;
                        append_result_line(row, r, s.regression_threshold);
                        lines.push_back(std::move(row));
                    }
                };
                if (s.order_arrival)
                    emit(bucket.results | std::views::reverse);
                else
                    emit(bucket.results);

                if (!bucket.complexity.empty()) {
                    std::string footer;
                    append_complexity_footer(footer, bucket, s.complexity_threshold);
                    // complexity footer is multi-line — split on '\n' and push each
                    std::size_t pos = 0;
                    while (pos < footer.size()) {
                        const auto nl  = footer.find('\n', pos);
                        const auto end = nl == std::string::npos ? footer.size() : nl + 1;
                        lines.push_back(footer.substr(pos, end - pos));
                        pos = end;
                        if (nl == std::string::npos)
                            break;
                    }
                }
            }

            lines.emplace_back("\n"); // blank separator between sessions
        }
        return lines;
    }

    // Build a full-frame ANSI string for the current state. Caller writes it
    // to stdout in one shot — single write keeps tearing imperceptible.
    // The header is pinned; body lines scroll via s.scroll_offset (↑/↓ or j/k).
    inline auto render(DashboardState const& s) -> std::string {
        std::string out;
        out.reserve(4096);
        out += ansi::kClearScreen;

        const bool has_baseline = !s.baseline_label.empty();
        const int  term_rows    = terminal_rows();
        // Header: title (1) + baseline (0 or 1) + blank (1)
        const int header_lines = has_baseline ? 3 : 2;
        const int viewport     = term_rows - header_lines;

        // Build body lines first so we know total height for the scroll indicator.
        std::vector<std::string> body_lines;
        if (!s.sessions.empty())
            body_lines = build_body_lines(s);

        const int  total      = static_cast<int>(body_lines.size());
        const int  max_offset = std::max(0, total - viewport);
        const int  offset     = std::min(s.scroll_offset, max_offset);
        const bool scrollable = total > viewport;

        // Title line — append scroll hint when content overflows.
        if (scrollable) {
            out += std::format(
                    "{}storm_bench_dashboard{}  ·  {}q{} quit  {}r{} refresh  {}↑↓/jk{} scroll  {}1-9{} toggle"
                    "  {}{}%{}\n",
                    ansi::kBold,
                    ansi::kReset,
                    ansi::kFgCyan,
                    ansi::kReset,
                    ansi::kFgCyan,
                    ansi::kReset,
                    ansi::kFgCyan,
                    ansi::kReset,
                    ansi::kFgCyan,
                    ansi::kReset,
                    ansi::kFgGrey,
                    total > 0 ? (offset + viewport) * 100 / total : 100,
                    ansi::kReset
            );
        } else {
            out += std::format(
                    "{}storm_bench_dashboard{}  ·  press {}q{} to quit, {}r{} to refresh, {}1-9{} to toggle sessions\n",
                    ansi::kBold,
                    ansi::kReset,
                    ansi::kFgCyan,
                    ansi::kReset,
                    ansi::kFgCyan,
                    ansi::kReset,
                    ansi::kFgCyan,
                    ansi::kReset
            );
        }
        if (has_baseline)
            out += std::format("  {}Baseline: {}{}{}\n", ansi::kFgGrey, ansi::kReset, s.baseline_label, ansi::kReset);
        out += '\n';

        if (s.sessions.empty()) {
            out += std::format("  {}waiting for storm_bench …{}\n", ansi::kDim, ansi::kReset);
            return out;
        }

        // Emit the viewport slice.
        const int end = std::min(offset + viewport, total);
        for (int i = offset; i < end; ++i)
            out += body_lines[static_cast<std::size_t>(i)];

        // Scroll-position bar at the bottom when overflowing.
        if (scrollable) {
            const int   bar_width = 20;
            const int   filled = total > 0 ? std::min(bar_width, (offset + viewport) * bar_width / total) : bar_width;
            std::string bar(static_cast<std::size_t>(bar_width), '-');
            for (int i = 0; i < filled; ++i)
                bar[static_cast<std::size_t>(i)] = '#';
            out += std::format("  {}[{}] ↑↓ to scroll{}\n", ansi::kFgGrey, bar, ansi::kReset);
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

    enum class Key : std::uint8_t { None, Quit, Refresh, Digit, ScrollUp, ScrollDown };

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
        if (c == 'j' || c == 'J')
            return {Key::ScrollDown, '\0'};
        if (c == 'k' || c == 'K')
            return {Key::ScrollUp, '\0'};
        if (c >= '1' && c <= '9')
            return {Key::Digit, c};

        // Arrow keys: ESC [ A (up) / ESC [ B (down). Read the two follow-on
        // bytes without blocking (termios VMIN=0). Anything else is drained.
        if (c == 0x1b) {
            char seq[2]{};
            if (::read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
                if (::read(STDIN_FILENO, &seq[1], 1) == 1) {
                    if (seq[1] == 'A')
                        return {Key::ScrollUp, '\0'};
                    if (seq[1] == 'B')
                        return {Key::ScrollDown, '\0'};
                }
            }
            // Drain any remaining escape bytes.
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
