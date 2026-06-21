// Regression test for Issue #266.
//
// The dashboard session header renders "N results" where N = the count of
// measurement rows added via tui::add_result. BigO/RMS rows fold into the
// per-category Complexity footer and MUST NOT bump the counter.
//
// The bug: replay_run_results_into_session in benchmarks/dashboard/events.hpp
// incremented sess.result_count for measurement rows AND called add_result(),
// which also increments — so DB rebuilds reported 2x the real count.

#include <gtest/gtest.h>

import storm.bench_dashboard.tui;
import storm.bench_dashboard.wire;

namespace {

    constexpr double kRegressionThreshold = 5.0;

    auto make_measurement(std::string_view name, std::string_view category) -> bench_dashboard::wire::ResultMsg {
        bench_dashboard::wire::ResultMsg m{};
        m.kind      = bench_dashboard::wire::MessageKind::Result;
        m.test_name = std::string{name};
        m.category  = std::string{category};
        m.row_kind  = std::string{bench_dashboard::wire::kRowKindMeasurement};
        m.real_ns   = 1000.0;
        return m;
    }

    auto make_bigo(std::string_view name, std::string_view category) -> bench_dashboard::wire::ResultMsg {
        bench_dashboard::wire::ResultMsg m{};
        m.kind             = bench_dashboard::wire::MessageKind::Result;
        m.test_name        = std::string{name};
        m.category         = std::string{category};
        m.row_kind         = std::string{bench_dashboard::wire::kRowKindBigO};
        m.complexity_class = "N";
        m.complexity_coef  = 1.0e-6;
        return m;
    }

    auto make_rms(std::string_view name, std::string_view category) -> bench_dashboard::wire::ResultMsg {
        bench_dashboard::wire::ResultMsg m{};
        m.kind      = bench_dashboard::wire::MessageKind::Result;
        m.test_name = std::string{name};
        m.category  = std::string{category};
        m.row_kind  = std::string{bench_dashboard::wire::kRowKindRms};
        m.rms_pct   = 0.5;
        return m;
    }

    // These tests are about result_count / complexity folding only — never raw.
    auto add(bench_dashboard::tui::Session& sess, bench_dashboard::wire::ResultMsg const& m) -> void {
        bench_dashboard::tui::add_result(sess, m, kRegressionThreshold, /*baseline_is_raw=*/false);
    }

} // namespace

// "N results" counts measurement rows only — BigO and RMS rows are aggregate
// data shown in the Complexity footer and must not inflate the session total.
TEST(DashboardResultCounter, OnlyMeasurements) {
    bench_dashboard::tui::Session sess{};
    for (int i = 0; i < 9; ++i) {
        add(sess, make_measurement("Storm/INSERT/insert/N:1000", "INSERT"));
    }

    EXPECT_EQ(sess.result_count, 9U);
}

TEST(DashboardResultCounter, OnlyBigOAndRms) {
    bench_dashboard::tui::Session sess{};
    add(sess, make_bigo("Storm/INSERT/insert_BigO", "INSERT"));
    add(sess, make_rms("Storm/INSERT/insert_RMS", "INSERT"));

    EXPECT_EQ(sess.result_count, 0U);
    ASSERT_EQ(sess.categories.size(), 1U);
    EXPECT_EQ(sess.categories.front().complexity.size(), 1U);
}

// The Issue #266 repro: 18 measurement rows + 2 BigO + 2 RMS = 22 DB rows.
// Counter must show 18, not 22 and not 36.
TEST(DashboardResultCounter, MixedMeasurementsAndComplexity) {
    bench_dashboard::tui::Session sess{};
    for (int i = 0; i < 9; ++i) {
        add(sess, make_measurement("Storm/INSERT/insert/N:1000", "INSERT"));
    }
    for (int i = 0; i < 9; ++i) {
        add(sess, make_measurement("Storm/UPDATE_PK/update_pk/N:1000", "UPDATE_PK"));
    }
    add(sess, make_bigo("Storm/INSERT/insert_BigO", "INSERT"));
    add(sess, make_rms("Storm/INSERT/insert_RMS", "INSERT"));
    add(sess, make_bigo("Storm/UPDATE_PK/update_pk_BigO", "UPDATE_PK"));
    add(sess, make_rms("Storm/UPDATE_PK/update_pk_RMS", "UPDATE_PK"));

    EXPECT_EQ(sess.result_count, 18U);
}

// Issue #266 root cause: tui::add_result is the single source of truth for
// result_count. A caller that ALSO bumps the counter (as
// replay_run_results_into_session used to do) double-counts measurements on
// every DB rebuild.
//
// This test pins that contract: passing a measurement to add_result must
// produce exactly one increment, not two. If a future caller forgets and adds
// its own manual ++sess.result_count alongside add_result, this assert fails
// with a value of 2.
TEST(DashboardResultCounter, AddResultIncrementsExactlyOnce) {
    bench_dashboard::tui::Session sess{};
    const auto                    before = sess.result_count;
    add(sess, make_measurement("Storm/INSERT/insert/N:1000", "INSERT"));
    EXPECT_EQ(sess.result_count - before, 1U);
}
