#ifndef STORM_BENCH_DASHBOARD_TUI_RENDER_HPP
#define STORM_BENCH_DASHBOARD_TUI_RENDER_HPP

// ANSI render functions for the storm_bench_dashboard TUI (Issue #247).
//
// Textual header — #included inside the export namespace of tui.cppm so every
// symbol here is exported as part of bench_dashboard::tui. Depends on the
// state types (Session, DashboardState, CategoryBucket, ComplexityEntry),
// ansi:: constants, and wire::ResultMsg all being in scope. Split out of
// tui.cppm solely to keep that TU under the 600-line code-quality cap.
// Do NOT include from any other TU.

inline auto colour_for_latency(double real_ns) -> std::string_view {
    if (real_ns < 1.0e6)
        return ansi::kFgGreen;
    if (real_ns < 1.0e7)
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
        return std::string(12, ' ');
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
    const std::string_view ts            = sess.timestamp.size() >= 19 ? std::string_view{sess.timestamp}.substr(11, 8)
                                                                       : std::string_view{sess.timestamp};
    const std::string      label = sess.is_full_run ? std::string{"full run"} : std::format("filter='{}'", sess.filter);
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
        const std::string bar      = make_progress_bar(sess.result_count, sess.expected_count, 20);
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

inline auto truncated_name(std::string_view name, std::size_t width) -> std::string_view {
    return name.size() > width ? name.substr(name.size() - width) : name;
}

// Issue #346: an aggregate row's items_per_second is a real throughput only for
// the mean/median aggregates. _stddev is the std-dev OF throughput and _cv a
// dimensionless ratio — neither is an ips value. Mirrors
// bench_dashboard::aggregate_carries_throughput (row_classify.hpp), inlined here
// because this header lives in the tui.cppm module purview and cannot include it.
inline auto row_carries_throughput(wire::ResultMsg const& r) -> bool {
    // NOSONAR(cpp:S4144) source of truth: row_classify.hpp::aggregate_carries_throughput
    return !(r.test_name.ends_with("_stddev") || r.test_name.ends_with("_cv"));
}

inline auto format_result_prefix(wire::ResultMsg const& r) -> std::string {
    return std::format(
            "      {}{:<48}{}  {}{}{}  {}",
            ansi::kReset,
            truncated_name(r.test_name, 48),
            ansi::kReset,
            colour_for_latency(r.real_ns),
            format_latency(r.real_ns),
            ansi::kReset,
            row_carries_throughput(r) ? format_ips(r.items_per_second) : std::string(12, ' ')
    );
}

// Storm targets ≥95% of raw SQLite efficiency; rows above the line render green.
inline constexpr double kRawEfficiencyTarget = 95.0;

inline auto efficiency_label(double pct) -> std::pair<std::string_view, std::string> {
    const std::string_view colour = pct >= kRawEfficiencyTarget ? ansi::kFgGreen : ansi::kFgRed;
    return {colour, std::format("{:.1f}% of raw", pct)};
}

inline auto append_result_line(std::string& out, wire::ResultMsg const& r, double regression_threshold) -> void {
    out += format_result_prefix(r);
    if (r.efficiency_pct.has_value() && row_carries_throughput(r)) {
        const auto [ecol, etxt] = efficiency_label(*r.efficiency_pct);
        out += std::format("  {}{}{}\n", ecol, etxt, ansi::kReset);
    } else if (r.baseline_looked_up && r.delta_pct.has_value()) {
        const auto [dcol, dtxt] = format_delta(*r.delta_pct, regression_threshold);
        out += std::format("  {}{}{}\n", dcol, dtxt, ansi::kReset);
    } else if (r.baseline_looked_up) {
        out += std::format("  {}— (no raw){}\n", ansi::kFgGrey, ansi::kReset);
    } else {
        out += '\n';
    }
}

inline auto coef_drift_pct(ComplexityEntry const& ce) -> double {
    return ce.baseline_coef > 0.0 ? (ce.complexity_coef - ce.baseline_coef) / ce.baseline_coef * 100.0 : 0.0;
}

inline auto pick_complexity_status(ComplexityEntry const& ce) -> std::pair<std::string_view, std::string_view> {
    if (ce.coef_regression)
        return {ansi::kFgYellow, "⚠ DRIFT"};
    if (ce.coef_improvement)
        return {ansi::kFgGreen, "↑ IMPROVE"};
    return {ansi::kFgGreen, "✓"};
}

inline auto format_shape_regression_line(ComplexityEntry const& ce) -> std::string {
    return std::format(
            "      {}{:<40}{}  {} → {}  {}✗ SHAPE{}\n",
            ansi::kReset,
            truncated_name(ce.base_name, 40),
            ansi::kReset,
            ce.baseline_class,
            ce.complexity_class,
            ansi::kFgRed,
            ansi::kReset
    );
}

inline auto format_coef_compare_line(ComplexityEntry const& ce) -> std::string {
    const auto [colour, glyph] = pick_complexity_status(ce);
    return std::format(
            "      {}{:<40}{}  {}  coef {:.2g} → {:.2g}  {:+.1f}%  {}{}{}\n",
            ansi::kReset,
            truncated_name(ce.base_name, 40),
            ansi::kReset,
            ce.complexity_class,
            ce.baseline_coef,
            ce.complexity_coef,
            coef_drift_pct(ce),
            colour,
            glyph,
            ansi::kReset
    );
}

inline auto format_plain_complexity_line(ComplexityEntry const& ce) -> std::string {
    return std::format(
            "      {}{:<40}{}  {}  coef {:.2g}  rms {:.1f}%\n",
            ansi::kReset,
            truncated_name(ce.base_name, 40),
            ansi::kReset,
            ce.complexity_class,
            ce.complexity_coef,
            ce.rms_pct
    );
}

inline auto format_complexity_line(ComplexityEntry const& ce) -> std::string {
    if (!ce.baseline_looked_up)
        return format_plain_complexity_line(ce);
    if (ce.shape_regression)
        return format_shape_regression_line(ce);
    return format_coef_compare_line(ce);
}

inline auto append_complexity_footer(std::string& out, CategoryBucket const& bucket, double /*complexity_threshold*/)
        -> void {
    if (bucket.complexity.empty())
        return;
    out += std::format("    {}Complexity:{}\n", ansi::kFgGrey, ansi::kReset);
    for (auto const& ce : bucket.complexity)
        out += format_complexity_line(ce);
}

inline auto format_bucket_header(CategoryBucket const& bucket) -> std::string {
    return std::format(
            "  {}{}{} {}({}){}\n",
            ansi::kBold,
            bucket.name,
            ansi::kReset,
            ansi::kFgGrey,
            bucket.results.size(),
            ansi::kReset
    );
}

inline auto terminal_rows() -> int {
    struct winsize ws{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        return static_cast<int>(ws.ws_row);
    return 40;
}

inline auto append_summary_line(std::string& out, Session const& sess) -> void {
    if (sess.raw_total > 0) {
        const double avg = sess.raw_matched > 0 ? sess.raw_eff_sum / static_cast<double>(sess.raw_matched) : 0.0;
        out += std::format(
                "  {}session: {}/{} matched · avg {:.1f}% of raw · target ≥{:.0f}%{}\n",
                ansi::kFgGrey,
                sess.raw_matched,
                sess.raw_total,
                avg,
                kRawEfficiencyTarget,
                ansi::kReset
        );
        return;
    }
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

inline auto push_bucket_result_rows(
        std::vector<std::string>& lines, CategoryBucket const& bucket, bool order_arrival, double regression_threshold
) -> void {
    auto emit = [&](auto&& range) {
        for (auto const& r : range) {
            std::string row;
            append_result_line(row, r, regression_threshold);
            lines.push_back(std::move(row));
        }
    };
    if (order_arrival)
        emit(bucket.results | std::views::reverse);
    else
        emit(bucket.results);
}

inline auto push_split_lines(std::vector<std::string>& lines, std::string const& footer) -> void {
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

inline auto push_bucket_lines(std::vector<std::string>& lines, CategoryBucket const& bucket, DashboardState const& s)
        -> void {
    lines.push_back(format_bucket_header(bucket));
    push_bucket_result_rows(lines, bucket, s.order_arrival, s.regression_threshold);
    if (!bucket.complexity.empty()) {
        std::string footer;
        append_complexity_footer(footer, bucket, s.complexity_threshold);
        push_split_lines(lines, footer);
    }
}

inline auto push_session_summary(std::vector<std::string>& lines, Session const& sess) -> void {
    const std::size_t compared = sess.ok_count + sess.regression_count + sess.improvement_count + sess.severe_count;
    if (compared == 0 && sess.raw_total == 0)
        return;
    std::string sumline;
    append_summary_line(sumline, sess);
    lines.push_back(std::move(sumline));
}

inline auto
push_session_lines(std::vector<std::string>& lines, Session const& sess, int ordinal, DashboardState const& s) -> void {
    std::string hdr;
    append_session_header(hdr, sess, ordinal, s.spinner_tick);
    lines.push_back(std::move(hdr));
    if (!sess.expanded)
        return;
    push_session_summary(lines, sess);
    for (auto const& bucket : sess.categories)
        push_bucket_lines(lines, bucket, s);
    lines.emplace_back("\n");
}

inline auto build_body_lines(DashboardState const& s) -> std::vector<std::string> {
    std::vector<std::string> lines;
    lines.reserve(256);
    int ordinal = 1;
    for (auto const& sess : s.sessions)
        push_session_lines(lines, sess, ordinal++, s);
    return lines;
}

inline auto cyan(std::string_view text) -> std::string {
    return std::format("{}{}{}", ansi::kFgCyan, text, ansi::kReset);
}

inline auto bold(std::string_view text) -> std::string {
    return std::format("{}{}{}", ansi::kBold, text, ansi::kReset);
}

inline auto format_title_scrollable(int offset, int viewport, int total) -> std::string {
    const int pct = total > 0 ? (offset + viewport) * 100 / total : 100;
    return std::format(
            "{}  ·  {} quit  {} refresh  {} scroll  {} toggle  {}{}%{}\n",
            bold("storm_bench_dashboard"),
            cyan("q"),
            cyan("r"),
            cyan("↑↓/jk"),
            cyan("1-9"),
            ansi::kFgGrey,
            pct,
            ansi::kReset
    );
}

inline auto format_title_plain() -> std::string {
    return std::format(
            "{}  ·  press {} to quit, {} to refresh, {} to toggle sessions\n",
            bold("storm_bench_dashboard"),
            cyan("q"),
            cyan("r"),
            cyan("1-9")
    );
}

inline auto format_scroll_bar(int offset, int viewport, int total) -> std::string {
    constexpr int bar_width = 20;
    const int     filled    = total > 0 ? std::min(bar_width, (offset + viewport) * bar_width / total) : bar_width;
    std::string   bar(static_cast<std::size_t>(bar_width), '-');
    for (int i = 0; i < filled; ++i)
        bar[static_cast<std::size_t>(i)] = '#';
    return std::format("  {}[{}] ↑↓ to scroll{}\n", ansi::kFgGrey, bar, ansi::kReset);
}

inline auto
append_header_block(std::string& out, DashboardState const& s, bool scrollable, int offset, int viewport, int total)
        -> void {
    out += scrollable ? format_title_scrollable(offset, viewport, total) : format_title_plain();
    if (!s.baseline_label.empty())
        out += std::format("  {}Baseline: {}{}{}\n", ansi::kFgGrey, ansi::kReset, s.baseline_label, ansi::kReset);
    out += '\n';
}

inline auto render(DashboardState const& s) -> std::string {
    std::string out;
    out.reserve(4096);
    out += ansi::kClearScreen;
    const int                header_lines = s.baseline_label.empty() ? 2 : 3;
    const int                viewport     = terminal_rows() - header_lines;
    std::vector<std::string> body_lines;
    if (!s.sessions.empty())
        body_lines = build_body_lines(s);
    const int  total      = static_cast<int>(body_lines.size());
    const int  offset     = std::min(s.scroll_offset, std::max(0, total - viewport));
    const bool scrollable = total > viewport;
    append_header_block(out, s, scrollable, offset, viewport, total);
    if (s.sessions.empty()) {
        out += std::format("  {}waiting for storm_bench …{}\n", ansi::kDim, ansi::kReset);
        return out;
    }
    const int end = std::min(offset + viewport, total);
    for (int i = offset; i < end; ++i)
        out += body_lines[static_cast<std::size_t>(i)];
    if (scrollable)
        out += format_scroll_bar(offset, viewport, total);
    return out;
}

#endif // STORM_BENCH_DASHBOARD_TUI_RENDER_HPP
