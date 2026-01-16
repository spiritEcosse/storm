#pragma once

/**
 * Benchmark Size Profiles
 *
 * Defines constexpr size arrays for different benchmark categories.
 * This centralizes size definitions and iteration calculations,
 * replacing duplicated entries in benchmark_tests.yaml.
 *
 * Size profiles:
 *   - batch_standard: Standard sizes for INSERT/UPDATE/DELETE batch operations
 *   - batch_insert_edge: SQLite chunk boundary tests for INSERT (999/4 fields = 249)
 *   - batch_update_edge: SQLite chunk boundary tests for UPDATE (999/5 fields = 199)
 *   - dataset_standard: Standard sizes for SELECT/JOIN/DISTINCT operations
 *   - dataset_small: Smaller sizes for aggregate tests
 */

#include <array>
#include <algorithm>
#include <string_view>

namespace storm::benchmark::sizes {

    // Standard batch sizes for INSERT/UPDATE/DELETE operations
    inline constexpr std::array BATCH_STANDARD = {1, 10, 100, 500, 1000, 5000, 10000, 50000, 100000};

    // Edge case sizes for INSERT (SQLite chunk boundary: 999/4 fields = 249)
    inline constexpr std::array BATCH_INSERT_EDGE = {248, 249, 250};

    // Edge case sizes for UPDATE (999/5 fields = 199)
    inline constexpr std::array BATCH_UPDATE_EDGE = {198, 199, 200};

    // Standard dataset sizes for SELECT/JOIN/DISTINCT operations
    inline constexpr std::array DATASET_STANDARD = {100, 1000, 10000, 100000};

    // Smaller dataset for aggregate tests
    inline constexpr std::array DATASET_SMALL = {1000, 10000};

    // Calculate iterations inversely proportional to batch size
    // Larger batches get fewer iterations to maintain consistent total work
    constexpr int iterations_for_batch(int size) {
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
    constexpr int iterations_for_dataset(int size) {
        if (size <= 100)
            return 10000;
        if (size <= 1000)
            return 1000;
        if (size <= 10000)
            return 100;
        return 10; // 100000+ rows
    }

    // Calculate iterations for aggregate operations
    constexpr int iterations_for_aggregate(int size) {
        if (size <= 1000)
            return 10000;
        if (size <= 10000)
            return 5000;
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
    constexpr SizeProfile profile_from_string(std::string_view str) {
        if (str == "batch_standard")
            return SizeProfile::BatchStandard;
        if (str == "batch_insert_edge")
            return SizeProfile::BatchInsertEdge;
        if (str == "batch_update_edge")
            return SizeProfile::BatchUpdateEdge;
        if (str == "dataset_standard")
            return SizeProfile::DatasetStandard;
        if (str == "dataset_small")
            return SizeProfile::DatasetSmall;
        return SizeProfile::None;
    }

    // Get test name suffix for a given size
    // Returns "_single" for batch_size=1, otherwise "_{size}"
    inline auto get_name_suffix(int size, bool is_batch) -> std::string {
        if (is_batch && size == 1) {
            return "_single";
        }
        return "_" + std::to_string(size);
    }

} // namespace storm::benchmark::sizes
