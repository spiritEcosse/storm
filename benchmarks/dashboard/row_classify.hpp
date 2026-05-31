#pragma once

// Pure, Google-Benchmark-free classification of a benchmark Run into a
// dashboard row_kind (Issue #265).
//
// The reporter (reporter.cpp) extracts the four relevant fields from a
// benchmark::BenchmarkReporter::Run into RowFlags and delegates the decision
// here. Keeping the logic in a plain header (no <benchmark/benchmark.h>, no
// import storm) lets the unit tests exercise it directly without pulling in
// gbench — see tests/bench_dashboard/test_row_classify.cpp.

#include "wire.hpp"

#include <string_view>

namespace bench_dashboard {

    // The subset of benchmark::BenchmarkReporter::Run fields that decide how a
    // row is classified.
    struct RowFlags {
        bool             skipped{false};      // run was skipped by gbench
        std::string_view aggregate_name;      // "mean"/"median"/"stddev" for aggregate rows
        bool             report_big_o{false}; // BigO complexity row
        bool             report_rms{false};   // RMS row
    };

    // Skip only genuinely-skipped runs. Aggregate rows (mean/median/stddev that
    // gbench emits under --benchmark_repetitions=N, and the *only* rows it emits
    // under --benchmark_report_aggregates_only=true) MUST flow through — dropping
    // them silently discarded every timing when the aggregates-only flag was set
    // (Issue #265).
    inline auto should_skip_row(RowFlags const& r) -> bool {
        return r.skipped;
    }

    // Map a non-skipped row to its dashboard row_kind. BigO and RMS rows carry
    // an aggregate_name too, so they must be checked first.
    inline auto classify_row_kind(RowFlags const& r) -> std::string_view {
        if (r.report_big_o) {
            return wire::kRowKindBigO;
        }
        if (r.report_rms) {
            return wire::kRowKindRms;
        }
        if (!r.aggregate_name.empty()) {
            return wire::kRowKindAggregate; // mean / median / stddev summary row
        }
        return wire::kRowKindMeasurement; // raw per-repetition row
    }

} // namespace bench_dashboard
