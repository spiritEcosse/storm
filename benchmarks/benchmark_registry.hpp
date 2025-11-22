#pragma once

#include <string>
#include <optional>

/**
 * Storm ORM Models for Benchmark Registry Database
 *
 * This file defines the schema for the unified benchmark system.
 * All database operations are handled through Storm ORM.
 */

namespace storm::benchmark {

// ============================================================================
// Table: benchmark_tests (Test Registry)
// ============================================================================
struct BenchmarkTest {
    [[= storm::meta::FieldAttr::primary]] int test_id;

    // Test identification
    std::string test_name;        // Unique identifier (e.g., "storm_insert_single")
    std::string test_category;    // "CRUD", "JOIN", "WHERE", "DISTINCT", "AGGREGATE", "ORDER_BY"
    std::string operation_type;   // "INSERT", "SELECT", "UPDATE", "DELETE", "JOIN", etc.
    std::string batch_mode;       // "single", "batch", "mixed", "n/a"
    std::string description;      // Human-readable description

    // Execution details
    std::string binary_name;      // "bench_storm", "bench_join", etc.
    std::optional<std::string> binary_args;  // "--mode=insert --size={size}"
    std::string output_parser;    // "StormCRUDParser", "JoinParser", etc.

    // Metrics to extract
    bool extract_storm_throughput = true;
    bool extract_raw_throughput = true;
    bool extract_efficiency = true;
    bool extract_latency = false;

    // Control
    bool enabled = true;
    int priority = 100;           // Lower = run first
    int min_dataset_size = 1000;
    int max_dataset_size = 100000;

    // Metadata
    std::string created_at;       // ISO 8601 timestamp
    std::string updated_at;       // ISO 8601 timestamp
    std::optional<std::string> notes;
};

// ============================================================================
// Table: benchmark_runs (Test History)
// ============================================================================
struct BenchmarkRun {
    [[= storm::meta::FieldAttr::primary]] int run_id;
    int test_id;  // Foreign key -> benchmark_tests.test_id

    // Execution context
    std::string run_timestamp;                // ISO 8601 timestamp
    std::optional<std::string> git_commit_hash;  // Git SHA
    std::optional<std::string> git_branch;       // Git branch name
    std::string build_type;                   // "Debug", "Release"
    std::optional<std::string> compiler_version; // Clang version

    // Test parameters
    int dataset_size;
    int iterations;

    // Performance metrics
    std::optional<double> storm_throughput;    // rows/sec or ops/sec
    std::optional<double> raw_throughput;      // rows/sec or ops/sec
    std::optional<double> efficiency_percent;  // (storm/raw) * 100
    std::optional<double> total_time_ms;
    std::optional<double> avg_time_ms;
    std::optional<double> min_time_ms;
    std::optional<double> max_time_ms;

    // Additional data (JSON for flexibility)
    std::optional<std::string> additional_metrics;  // JSON: {"cache_hits": 1234}

    // Status
    std::string status = "success";  // "success", "failed", "skipped"
    std::optional<std::string> error_message;
};

// ============================================================================
// Table: benchmark_config (Global Configuration)
// ============================================================================
struct BenchmarkConfig {
    std::string config_key;    // Primary key
    std::string config_value;
    std::optional<std::string> description;
    std::string updated_at;    // ISO 8601 timestamp
};

// ============================================================================
// Table: benchmark_suites (Test Groupings)
// ============================================================================
struct BenchmarkSuite {
    [[= storm::meta::FieldAttr::primary]] int suite_id;
    std::string suite_name;  // "quick", "full", "regression", "nightly"
    std::optional<std::string> description;
    std::string created_at;  // ISO 8601 timestamp
};

// ============================================================================
// Table: benchmark_suite_tests (Many-to-Many Relationship)
// ============================================================================
struct BenchmarkSuiteTest {
    int suite_id;  // Foreign key -> benchmark_suites.suite_id
    int test_id;   // Foreign key -> benchmark_tests.test_id
    // Note: Composite primary key (suite_id, test_id)
};

}  // namespace storm::benchmark
