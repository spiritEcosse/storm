// Regression tests for Issue #265.
//
// The streaming reporter (benchmarks/dashboard/reporter.cpp) used to discard
// every Run whose aggregate_name was set. Under
// --benchmark_report_aggregates_only=true gbench emits ONLY those aggregate
// rows, so 100% of timing data was silently dropped. The fix moves the pure
// decision into row_classify.hpp:
//
//   * should_skip_row() skips only genuinely-skipped runs.
//   * classify_row_kind() distinguishes measurement / aggregate / bigo / rms.
//
// These tests pin both functions on the gbench-free RowFlags POD so they run
// in the storm_tests binary without linking Google Benchmark.

#include "row_classify.hpp"

#include <gtest/gtest.h>

namespace {

    using bench_dashboard::classify_row_kind;
    using bench_dashboard::RowFlags;
    using bench_dashboard::should_skip_row;
    namespace wire = bench_dashboard::wire;

} // namespace

// A skipped run is the only thing that should be dropped.
TEST(RowClassify, SkippedRowIsSkipped) {
    EXPECT_TRUE(should_skip_row(RowFlags{.skipped = true}));
}

// Raw per-repetition row (no aggregate_name) flows through as a measurement.
TEST(RowClassify, RawRepetitionRowFlowsThrough) {
    const RowFlags r{};
    EXPECT_FALSE(should_skip_row(r));
    EXPECT_EQ(classify_row_kind(r), wire::kRowKindMeasurement);
}

// Issue #265: an aggregate row (mean/median/stddev) must NOT be skipped and
// must be classified as "aggregate", not dropped.
TEST(RowClassify, AggregateMeanRowFlowsThrough) {
    const RowFlags r{.aggregate_name = "mean"};
    EXPECT_FALSE(should_skip_row(r));
    EXPECT_EQ(classify_row_kind(r), wire::kRowKindAggregate);
}

TEST(RowClassify, AggregateMedianAndStddevFlowThrough) {
    EXPECT_EQ(classify_row_kind(RowFlags{.aggregate_name = "median"}), wire::kRowKindAggregate);
    EXPECT_EQ(classify_row_kind(RowFlags{.aggregate_name = "stddev"}), wire::kRowKindAggregate);
}

// BigO / RMS rows carry an aggregate_name too, so they must be checked before
// the aggregate branch — they classify as bigo / rms, never "aggregate".
TEST(RowClassify, BigORowClassifiesAsBigO) {
    const RowFlags r{.aggregate_name = "mean", .report_big_o = true};
    EXPECT_FALSE(should_skip_row(r));
    EXPECT_EQ(classify_row_kind(r), wire::kRowKindBigO);
}

TEST(RowClassify, RmsRowClassifiesAsRms) {
    const RowFlags r{.aggregate_name = "rms", .report_rms = true};
    EXPECT_FALSE(should_skip_row(r));
    EXPECT_EQ(classify_row_kind(r), wire::kRowKindRms);
}

// A skipped BigO/aggregate row is still skipped — skip wins over kind.
TEST(RowClassify, SkippedWinsOverKind) {
    EXPECT_TRUE(should_skip_row(RowFlags{.skipped = true, .aggregate_name = "mean"}));
    EXPECT_TRUE(should_skip_row(RowFlags{.skipped = true, .report_big_o = true}));
}
