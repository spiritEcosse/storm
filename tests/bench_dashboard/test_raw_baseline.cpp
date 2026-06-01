// Tests for the Storm-vs-raw SQLite baseline behaviour (Issue #74).
//
// Covers five areas added across Tasks 1-5:
//   1. wire round-trip of the run_start is_raw flag (and backward-compat default)
//   2. efficiency_label colour/text thresholds (>= 95% green, below red)
//   3. append_result_line render paths (efficiency, no-raw, non-raw delta)
//   4. add_result raw-counter gating — raw counters move ONLY when the active
//      baseline is raw (locks in the Task 5 fix)
//   5. append_summary_line — raw summary vs regression summary

#include <gtest/gtest.h>

#include <string>

import storm.bench_dashboard.tui;
import storm.bench_dashboard.wire;

namespace {

    constexpr double kRegressionThreshold = 5.0;

    auto make_measurement(std::string_view name) -> bench_dashboard::wire::ResultMsg {
        bench_dashboard::wire::ResultMsg m{};
        m.kind      = bench_dashboard::wire::MessageKind::Result;
        m.test_name = std::string{name};
        m.row_kind  = std::string{bench_dashboard::wire::kRowKindMeasurement};
        m.real_ns   = 1000.0;
        return m;
    }

} // namespace

// ---------------------------------------------------------------------------
// Group 1: wire round-trip of is_raw
// ---------------------------------------------------------------------------

TEST(WireRunStart, IsRawRoundTrips) {
    using namespace bench_dashboard::wire;
    const auto json = build_run_start("WHERE.*", /*is_full_run=*/false, /*is_raw=*/true);
    const auto msg  = parse(json);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->kind, MessageKind::RunStart);
    EXPECT_EQ(msg->filter, "WHERE.*");
    EXPECT_FALSE(msg->is_full_run);
    EXPECT_TRUE(msg->is_raw);
}

TEST(WireRunStart, IsRawDefaultsFalseWhenAbsent) {
    using namespace bench_dashboard::wire;
    // Backward compat: an older producer omits is_raw → parses as false.
    const auto msg = parse(R"({"type":"run_start","filter":"","is_full_run":true})");
    ASSERT_TRUE(msg.has_value());
    EXPECT_TRUE(msg->is_full_run);
    EXPECT_FALSE(msg->is_raw);
}

TEST(WireRunStart, IsRawFalseRoundTrips) {
    using namespace bench_dashboard::wire;
    const auto json = build_run_start("", /*is_full_run=*/true, /*is_raw=*/false);
    const auto msg  = parse(json);
    ASSERT_TRUE(msg.has_value());
    EXPECT_FALSE(msg->is_raw);
}

// ---------------------------------------------------------------------------
// Group 2: efficiency_label thresholds
// ---------------------------------------------------------------------------

TEST(EfficiencyLabel, GreenAtOrAbove95) {
    auto [colour, text] = bench_dashboard::tui::efficiency_label(96.6);
    EXPECT_EQ(text, "96.6% of raw");
    EXPECT_EQ(colour, bench_dashboard::tui::ansi::kFgGreen);
}

TEST(EfficiencyLabel, GreenExactly95) {
    auto [colour, text] = bench_dashboard::tui::efficiency_label(95.0);
    EXPECT_EQ(colour, bench_dashboard::tui::ansi::kFgGreen); // >= 95 is green
}

TEST(EfficiencyLabel, RedBelow95) {
    auto [colour, text] = bench_dashboard::tui::efficiency_label(94.9);
    EXPECT_EQ(text, "94.9% of raw");
    EXPECT_EQ(colour, bench_dashboard::tui::ansi::kFgRed);
}

// ---------------------------------------------------------------------------
// Group 3: append_result_line render paths
// ---------------------------------------------------------------------------

TEST(AppendResultLine, ShowsEfficiencyWhenPresent) {
    auto r           = make_measurement("Storm/SELECT/select/10000");
    r.real_ns        = 4.12e6;
    r.efficiency_pct = 96.6;
    std::string out;
    bench_dashboard::tui::append_result_line(out, r, kRegressionThreshold);
    EXPECT_NE(out.find("96.6% of raw"), std::string::npos);
}

TEST(AppendResultLine, ShowsNoRawWhenBaselineUnmatched) {
    auto r               = make_measurement("Storm/WHERE/where_bool_equality/10000");
    r.baseline_looked_up = true; // active baseline, no efficiency, no delta
    std::string out;
    bench_dashboard::tui::append_result_line(out, r, kRegressionThreshold);
    EXPECT_NE(out.find("— (no raw)"), std::string::npos);
}

TEST(AppendResultLine, ShowsRegressDeltaForNonRawBaseline) {
    // Non-raw baseline path must STILL show the signed delta, not efficiency.
    auto r               = make_measurement("Storm/SELECT/select/10000");
    r.real_ns            = 5.0e6;
    r.baseline_looked_up = true;
    r.delta_pct          = 12.0; // +12% regression
    std::string out;
    bench_dashboard::tui::append_result_line(out, r, kRegressionThreshold);
    EXPECT_NE(out.find("12.0%"), std::string::npos);  // delta rendered
    EXPECT_EQ(out.find("of raw"), std::string::npos); // NOT an efficiency label
}

// ---------------------------------------------------------------------------
// Group 4: add_result raw-counter gating (locks in the Task 5 fix)
// ---------------------------------------------------------------------------

TEST(AddResultRawCounters, RawBaselineCountsMatchedAndTotal) {
    bench_dashboard::tui::Session sess{};
    auto                          matched = make_measurement("Storm/SELECT/select/10000");
    matched.efficiency_pct                = 96.0;
    bench_dashboard::tui::add_result(sess, matched, kRegressionThreshold, /*baseline_is_raw=*/true);
    EXPECT_EQ(sess.raw_total, 1U);
    EXPECT_EQ(sess.raw_matched, 1U);
    EXPECT_DOUBLE_EQ(sess.raw_eff_sum, 96.0);
}

TEST(AddResultRawCounters, RawBaselineUnmatchedCountsTotalOnly) {
    bench_dashboard::tui::Session sess{};
    auto                          unmatched = make_measurement("Storm/WHERE/where_bool_equality/10000");
    unmatched.baseline_looked_up            = true; // raw baseline active, no match → no efficiency_pct
    bench_dashboard::tui::add_result(sess, unmatched, kRegressionThreshold, /*baseline_is_raw=*/true);
    EXPECT_EQ(sess.raw_total, 1U);
    EXPECT_EQ(sess.raw_matched, 0U);
}

TEST(AddResultRawCounters, NonRawBaselineUnmatchedDoesNotTouchRawCounters) {
    // Regression guard (Task 5 bug): a NON-raw baseline session with an
    // unmatched row must NOT bump raw_total, or it would hijack the summary.
    bench_dashboard::tui::Session sess{};
    auto                          unmatched = make_measurement("Storm/SELECT/select/10000");
    unmatched.baseline_looked_up            = true; // active NON-raw baseline, no match
    bench_dashboard::tui::add_result(sess, unmatched, kRegressionThreshold, /*baseline_is_raw=*/false);
    EXPECT_EQ(sess.raw_total, 0U);
    EXPECT_EQ(sess.raw_matched, 0U);
    EXPECT_EQ(sess.result_count, 1U); // still counted as a normal result
}

// ---------------------------------------------------------------------------
// Group 5: append_summary_line — raw vs regression
// ---------------------------------------------------------------------------

TEST(AppendSummaryLine, RawSummaryWhenRawTotalPositive) {
    bench_dashboard::tui::Session sess{};
    sess.raw_total   = 3;
    sess.raw_matched = 2;
    sess.raw_eff_sum = 96.0 + 98.0; // avg 97.0
    std::string out;
    bench_dashboard::tui::append_summary_line(out, sess);
    EXPECT_NE(out.find("2/3 matched"), std::string::npos);
    EXPECT_NE(out.find("97.0% of raw"), std::string::npos);
    EXPECT_NE(out.find("target"), std::string::npos);
}

TEST(AppendSummaryLine, RegressionSummaryWhenNoRaw) {
    bench_dashboard::tui::Session sess{};
    sess.ok_count         = 2;
    sess.regression_count = 1;
    std::string out;
    bench_dashboard::tui::append_summary_line(out, sess);
    EXPECT_EQ(out.find("of raw"), std::string::npos); // not a raw summary
    EXPECT_NE(out.find("Summary"), std::string::npos);
}
