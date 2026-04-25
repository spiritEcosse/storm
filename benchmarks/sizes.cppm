// storm_benchmark_sizes
//
// Compile-time size profiles for benchmark categories: standard / smoke / edge
// arrays for batch + dataset operations, plus iteration calculators. Pure
// constexpr — no SQLite, no reflection, no model deps — so it converts cleanly
// to a leaf module with no GMF preprocessing required.
//
// Was: benchmarks/sizes.hpp (textual header with `inline constexpr` arrays).
// Issue #221 — Phase 2 of the benchmark module conversion.

module;

export module storm_benchmark_sizes;

import <array>;
import <format>;
import <span>;
import <string>;
import <string_view>;

export namespace storm::benchmark::sizes {

    // Standard batch sizes for INSERT/UPDATE/DELETE operations
    inline constexpr std::array BATCH_STANDARD = {1, 10, 100, 500, 1000, 5000, 10000, 50000, 100000};

    // Smoke batch sizes — representative subset for fast regression detection
    inline constexpr std::array BATCH_SMOKE = {1, 100, 1000};

    // Edge case sizes for INSERT (SQLite chunk boundary: 999/4 fields = 249)
    inline constexpr std::array BATCH_INSERT_EDGE = {248, 249, 250};

    // Smoke edge sizes — just the boundary value
    inline constexpr std::array BATCH_INSERT_EDGE_SMOKE = {249};

    // Edge case sizes for UPDATE (999/5 fields = 199)
    inline constexpr std::array BATCH_UPDATE_EDGE = {198, 199, 200};

    // Smoke edge sizes — just the boundary value
    inline constexpr std::array BATCH_UPDATE_EDGE_SMOKE = {199};

    // Standard dataset sizes for SELECT/JOIN/DISTINCT operations
    inline constexpr std::array DATASET_STANDARD = {100, 1000, 10000, 100000};

    // Smoke dataset sizes — small + medium only
    inline constexpr std::array DATASET_SMOKE = {100, 10000};

    // Dataset sizes for aggregate tests (≥10000 for reliable COUNT(*) efficiency)
    inline constexpr std::array DATASET_SMALL = {10000, 50000};

    // Smoke aggregate sizes — single representative size
    inline constexpr std::array DATASET_SMALL_SMOKE = {10000};

    // Calculate iterations inversely proportional to batch size
    // Larger batches get fewer iterations to maintain consistent total work
    constexpr auto iterations_for_batch(int size) -> int {
        if (size <= 1)
            return 10000;
        if (size <= 10)
            return 1000;
        if (size <= 100)
            return 100;
        if (size <= 250)
            return 50; // Edge case tests
        if (size <= 500)
            return 20;
        if (size <= 1000)
            return 10;
        if (size <= 5000)
            return 2;
        return 1; // 10000+ rows
    }

    // Calculate iterations inversely proportional to dataset size
    constexpr auto iterations_for_dataset(int size) -> int {
        if (size <= 100)
            return 10000;
        if (size <= 1000)
            return 1000;
        if (size <= 10000)
            return 100;
        return 10; // 100000+ rows
    }

    // Calculate iterations for aggregate operations
    constexpr auto iterations_for_aggregate(int size) -> int {
        if (size <= 1000)
            return 10000;
        if (size <= 10000)
            return 5000;
        if (size <= 50000)
            return 500;
        return 100; // 100000+ rows
    }

    // Size profile enumeration for compile-time dispatch
    enum class SizeProfile {
        None,            // Non-scaled test, run once with fixed parameters
        BatchStandard,   // Standard batch sizes
        BatchInsertEdge, // INSERT chunk boundary tests
        BatchUpdateEdge, // UPDATE chunk boundary tests
        DatasetStandard, // Standard dataset sizes
        DatasetSmall     // Small dataset sizes (for aggregates)
    };

    // Convert string to SizeProfile
    constexpr auto profile_from_string(std::string_view str) -> SizeProfile {
        using enum SizeProfile;
        if (str == "batch_standard")
            return BatchStandard;
        if (str == "batch_insert_edge")
            return BatchInsertEdge;
        if (str == "batch_update_edge")
            return BatchUpdateEdge;
        if (str == "dataset_standard")
            return DatasetStandard;
        if (str == "dataset_small")
            return DatasetSmall;
        return None;
    }

    // Get size arrays for the current mode (smoke = reduced subset)
    inline auto batch_standard_sizes(bool smoke) -> std::span<const int> {
        if (smoke)
            return BATCH_SMOKE;
        return BATCH_STANDARD;
    }

    inline auto batch_insert_edge_sizes(bool smoke) -> std::span<const int> {
        if (smoke)
            return BATCH_INSERT_EDGE_SMOKE;
        return BATCH_INSERT_EDGE;
    }

    inline auto batch_update_edge_sizes(bool smoke) -> std::span<const int> {
        if (smoke)
            return BATCH_UPDATE_EDGE_SMOKE;
        return BATCH_UPDATE_EDGE;
    }

    inline auto dataset_standard_sizes(bool smoke) -> std::span<const int> {
        if (smoke)
            return DATASET_SMOKE;
        return DATASET_STANDARD;
    }

    inline auto dataset_small_sizes(bool smoke) -> std::span<const int> {
        if (smoke)
            return DATASET_SMALL_SMOKE;
        return DATASET_SMALL;
    }

    // Get test name suffix for a given size
    // Returns "_single" for batch_size=1, otherwise "_{size}"
    inline auto get_name_suffix(int size, bool is_batch) -> std::string {
        if (is_batch && size == 1) {
            return "_single";
        }
        return std::format("_{}", size);
    }

} // namespace storm::benchmark::sizes
