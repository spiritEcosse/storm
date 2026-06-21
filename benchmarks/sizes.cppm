// storm_benchmark_sizes
//
// Batch size arrays for INSERT/UPDATE/DELETE benchmarks.
// Issue #221 — Phase 2 of the benchmark module conversion.

module;

export module storm_benchmark_sizes;

import std;

export namespace storm::benchmark::sizes {

    // Standard batch sizes for INSERT/UPDATE/DELETE operations
    inline constexpr std::array BATCH_STANDARD = {1, 10, 100, 500, 1000, 5000, 10000, 50000, 100000};

    // Edge case sizes for INSERT (SQLite chunk boundary: 999/4 fields = 249)
    inline constexpr std::array BATCH_INSERT_EDGE = {248, 249, 250};

    // Edge case sizes for UPDATE (999/5 fields = 199)
    inline constexpr std::array BATCH_UPDATE_EDGE = {198, 199, 200};

} // namespace storm::benchmark::sizes
