/**
 * Storm ORM - Unified Benchmark System
 *
 * A complete C++ benchmark management system that uses Storm ORM to:
 * - Register and manage benchmark tests in SQLite database
 * - Execute tests dynamically with configurable parameters
 * - Track performance history with git commit hashes
 * - Detect regressions automatically
 * - Export JSON baselines for version control
 * - Provide interactive CLI for test management
 *
 * Usage:
 *   ./bench_unified --register              # Interactive test registration
 *   ./bench_unified --list                  # List all tests
 *   ./bench_unified --run <test_name>       # Run specific test
 *   ./bench_unified --run-all               # Run all enabled tests
 *   ./bench_unified --run-category CRUD     # Run all CRUD tests
 *   ./bench_unified --history <test_name>   # Show test history
 *   ./bench_unified --export-baseline       # Export JSON baseline
 *   ./bench_unified --check-regression      # Detect performance regressions
 */

import storm;
import <expected>;
import <optional>;
import <vector>;
import <iostream>;
import <string>;
import <iomanip>;

#include "benchmark_registry.hpp"
#include "benchmark_cli.hpp"
#include "benchmark_git.hpp"

using namespace storm;
using namespace storm::benchmark;

// ============================================================================
// Database Initialization
// ============================================================================

/**
 * Initialize the benchmark registry database with Storm ORM
 */
std::expected<void, db::Error> init_database(const std::string& db_path) {
    auto conn_result = db::sqlite::Connection::create(db_path);
    if (!conn_result.has_value()) {
        return std::unexpected(conn_result.error());
    }

    auto& conn = *conn_result;

    // Create benchmark_tests table
    const char* create_tests_table = R"(
        CREATE TABLE IF NOT EXISTS benchmark_tests (
            test_id INTEGER PRIMARY KEY AUTOINCREMENT,
            test_name TEXT UNIQUE NOT NULL,
            test_category TEXT NOT NULL,
            operation_type TEXT NOT NULL,
            batch_mode TEXT NOT NULL,
            description TEXT NOT NULL,
            binary_name TEXT NOT NULL,
            binary_args TEXT,
            output_parser TEXT NOT NULL,
            extract_storm_throughput INTEGER DEFAULT 1,
            extract_raw_throughput INTEGER DEFAULT 1,
            extract_efficiency INTEGER DEFAULT 1,
            extract_latency INTEGER DEFAULT 0,
            enabled INTEGER DEFAULT 1,
            priority INTEGER DEFAULT 100,
            min_dataset_size INTEGER DEFAULT 1000,
            max_dataset_size INTEGER DEFAULT 100000,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            updated_at TEXT DEFAULT CURRENT_TIMESTAMP,
            notes TEXT
        )
    )";

    // Create benchmark_runs table
    const char* create_runs_table = R"(
        CREATE TABLE IF NOT EXISTS benchmark_runs (
            run_id INTEGER PRIMARY KEY AUTOINCREMENT,
            test_id INTEGER NOT NULL,
            run_timestamp TEXT DEFAULT CURRENT_TIMESTAMP,
            git_commit_hash TEXT,
            git_branch TEXT,
            build_type TEXT,
            compiler_version TEXT,
            dataset_size INTEGER NOT NULL,
            iterations INTEGER NOT NULL,
            storm_throughput REAL,
            raw_throughput REAL,
            efficiency_percent REAL,
            total_time_ms REAL,
            avg_time_ms REAL,
            min_time_ms REAL,
            max_time_ms REAL,
            additional_metrics TEXT,
            status TEXT DEFAULT 'success',
            error_message TEXT,
            FOREIGN KEY (test_id) REFERENCES benchmark_tests(test_id)
        )
    )";

    // Create benchmark_config table
    const char* create_config_table = R"(
        CREATE TABLE IF NOT EXISTS benchmark_config (
            config_key TEXT PRIMARY KEY,
            config_value TEXT NOT NULL,
            description TEXT,
            updated_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
    )";

    // Create benchmark_suites table
    const char* create_suites_table = R"(
        CREATE TABLE IF NOT EXISTS benchmark_suites (
            suite_id INTEGER PRIMARY KEY AUTOINCREMENT,
            suite_name TEXT UNIQUE NOT NULL,
            description TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        )
    )";

    // Create benchmark_suite_tests table
    const char* create_suite_tests_table = R"(
        CREATE TABLE IF NOT EXISTS benchmark_suite_tests (
            suite_id INTEGER NOT NULL,
            test_id INTEGER NOT NULL,
            PRIMARY KEY (suite_id, test_id),
            FOREIGN KEY (suite_id) REFERENCES benchmark_suites(suite_id),
            FOREIGN KEY (test_id) REFERENCES benchmark_tests(test_id)
        )
    )";

    // Create indexes
    const char* create_indexes = R"(
        CREATE INDEX IF NOT EXISTS idx_runs_commit ON benchmark_runs(git_commit_hash);
        CREATE INDEX IF NOT EXISTS idx_runs_timestamp ON benchmark_runs(run_timestamp);
        CREATE INDEX IF NOT EXISTS idx_tests_category ON benchmark_tests(test_category);
        CREATE INDEX IF NOT EXISTS idx_tests_name ON benchmark_tests(test_name);
    )";

    // Execute all CREATE statements
    auto result = conn->execute(create_tests_table);
    if (!result.has_value()) return std::unexpected(result.error());

    result = conn->execute(create_runs_table);
    if (!result.has_value()) return std::unexpected(result.error());

    result = conn->execute(create_config_table);
    if (!result.has_value()) return std::unexpected(result.error());

    result = conn->execute(create_suites_table);
    if (!result.has_value()) return std::unexpected(result.error());

    result = conn->execute(create_suite_tests_table);
    if (!result.has_value()) return std::unexpected(result.error());

    result = conn->execute(create_indexes);
    if (!result.has_value()) return std::unexpected(result.error());

    CLI::success("Database initialized successfully: " + db_path);

    return {};
}

// ============================================================================
// Interactive Test Registration
// ============================================================================

/**
 * Interactive wizard for registering a new benchmark test
 */
void register_test_interactive(const std::string& db_path) {
    CLI::print_header("Register New Benchmark Test");

    auto conn_result = db::sqlite::Connection::create(db_path);
    if (!conn_result.has_value()) {
        CLI::error("Failed to open database: " + conn_result.error().message());
        return;
    }

    auto& conn = *conn_result;
    QuerySet<BenchmarkTest> tests{conn};

    // Gather test information interactively
    BenchmarkTest test;

    test.test_name = CLI::prompt("Test name (unique identifier)", "storm_new_test");
    test.test_category = CLI::prompt_choice(
        "Test category",
        {"CRUD", "JOIN", "WHERE", "DISTINCT", "AGGREGATE", "ORDER_BY", "GROUP_BY"},
        "CRUD"
    );
    test.operation_type = CLI::prompt_choice(
        "Operation type",
        {"INSERT", "SELECT", "UPDATE", "DELETE", "JOIN", "DISTINCT", "AGGREGATE"},
        "SELECT"
    );
    test.batch_mode = CLI::prompt_choice(
        "Batch mode",
        {"single", "batch", "mixed", "n/a"},
        "n/a"
    );
    test.description = CLI::prompt("Description", "New benchmark test");

    std::cout << "\n";
    CLI::info("Execution Details:");
    test.binary_name = CLI::prompt("Binary name", "bench_storm");
    test.binary_args = CLI::prompt(
        "Binary arguments (use {size} and {iterations} as placeholders)",
        "--test-size={size}"
    );
    test.output_parser = CLI::prompt("Output parser name", "StormCRUDParser");

    std::cout << "\n";
    CLI::info("Metrics Configuration:");
    test.extract_storm_throughput = CLI::prompt_bool("Extract Storm throughput?", true);
    test.extract_raw_throughput = CLI::prompt_bool("Extract Raw SQLite throughput?", true);
    test.extract_efficiency = CLI::prompt_bool("Extract efficiency?", true);
    test.extract_latency = CLI::prompt_bool("Extract latency?", false);

    std::cout << "\n";
    CLI::info("Control Settings:");
    test.priority = CLI::prompt_int("Priority (lower = run first)", 100);
    test.min_dataset_size = CLI::prompt_int("Min dataset size", 1000);
    test.max_dataset_size = CLI::prompt_int("Max dataset size", 100000);
    test.enabled = CLI::prompt_bool("Enabled by default?", true);

    std::cout << "\n";
    test.notes = CLI::prompt("Notes (optional)", "");

    // Set timestamps
    test.created_at = CLI::get_timestamp();
    test.updated_at = CLI::get_timestamp();

    // Display summary
    std::cout << "\n";
    CLI::print_separator();
    CLI::info("Test Summary:");
    CLI::print_table_row("Name", test.test_name);
    CLI::print_table_row("Category", test.test_category);
    CLI::print_table_row("Operation", test.operation_type);
    CLI::print_table_row("Description", test.description);
    CLI::print_table_row("Binary", test.binary_name + " " + test.binary_args.value_or(""));
    CLI::print_separator();
    std::cout << "\n";

    // Confirm registration
    if (!CLI::prompt_bool("Proceed with registration?", true)) {
        CLI::warning("Registration cancelled.");
        return;
    }

    // Insert into database using Storm ORM
    auto result = tests.insert(test);
    if (result.has_value()) {
        CLI::success("Test registered successfully! (test_id: " + std::to_string(*result) + ")");
    } else {
        CLI::error("Failed to register test: " + result.error().message());
    }
}

// ============================================================================
// List Tests
// ============================================================================

/**
 * List all benchmark tests
 */
void list_tests(const std::string& db_path, const std::optional<std::string>& category_filter) {
    auto conn_result = db::sqlite::Connection::create(db_path);
    if (!conn_result.has_value()) {
        CLI::error("Failed to open database: " + conn_result.error().message());
        return;
    }

    auto& conn = *conn_result;
    QuerySet<BenchmarkTest> tests{conn};

    // Fetch tests (with optional category filter)
    std::vector<BenchmarkTest> test_list;
    if (category_filter.has_value()) {
        CLI::print_header("Benchmark Tests - Category: " + *category_filter);
        // TODO: Add WHERE clause support
        // test_list = tests.where(BenchmarkTest::test_category == *category_filter).select();
        test_list = tests.select();  // Temporary: fetch all and filter manually
    } else {
        CLI::print_header("All Benchmark Tests");
        test_list = tests.select();
    }

    if (test_list.empty()) {
        CLI::warning("No tests found.");
        return;
    }

    // Print table header
    std::cout << std::left
              << std::setw(5) << "ID"
              << std::setw(30) << "Name"
              << std::setw(12) << "Category"
              << std::setw(10) << "Operation"
              << std::setw(8) << "Enabled"
              << "\n";
    CLI::print_separator();

    // Print tests
    int count = 0;
    for (const auto& test : test_list) {
        // Apply category filter if needed
        if (category_filter.has_value() && test.test_category != *category_filter) {
            continue;
        }

        std::cout << std::left
                  << std::setw(5) << test.test_id
                  << std::setw(30) << test.test_name
                  << std::setw(12) << test.test_category
                  << std::setw(10) << test.operation_type
                  << std::setw(8) << (test.enabled ? "✓" : "✗")
                  << "\n";
        count++;
    }

    std::cout << "\n";
    CLI::info("Total: " + std::to_string(count) + " test(s)");
}

// ============================================================================
// Show Test History
// ============================================================================

/**
 * Show historical results for a specific test
 */
void show_history(const std::string& db_path, const std::string& test_name, int limit = 10) {
    auto conn_result = db::sqlite::Connection::create(db_path);
    if (!conn_result.has_value()) {
        CLI::error("Failed to open database: " + conn_result.error().message());
        return;
    }

    auto& conn = *conn_result;
    QuerySet<BenchmarkTest> tests{conn};
    QuerySet<BenchmarkRun> runs{conn};

    // Find test by name
    auto test_list = tests.select();
    std::optional<int> test_id;
    for (const auto& test : test_list) {
        if (test.test_name == test_name) {
            test_id = test.test_id;
            break;
        }
    }

    if (!test_id.has_value()) {
        CLI::error("Test not found: " + test_name);
        return;
    }

    CLI::print_header("Test History: " + test_name);

    // Fetch runs for this test
    // TODO: Add WHERE, ORDER BY, LIMIT support
    // auto run_list = runs.where(BenchmarkRun::test_id == *test_id)
    //                     .order_by<^^BenchmarkRun::run_timestamp>(false)
    //                     .limit(limit)
    //                     .select();
    auto run_list = runs.select();  // Temporary: fetch all and filter manually

    // Filter and limit manually (temporary until WHERE is implemented)
    std::vector<BenchmarkRun> filtered_runs;
    for (const auto& run : run_list) {
        if (run.test_id == *test_id) {
            filtered_runs.push_back(run);
            if (static_cast<int>(filtered_runs.size()) >= limit) break;
        }
    }

    if (filtered_runs.empty()) {
        CLI::warning("No historical runs found for this test.");
        return;
    }

    // Print table
    std::cout << std::left
              << std::setw(20) << "Timestamp"
              << std::setw(12) << "Git Hash"
              << std::setw(10) << "Size"
              << std::setw(15) << "Storm (rows/s)"
              << std::setw(15) << "Raw (rows/s)"
              << std::setw(12) << "Efficiency"
              << "\n";
    CLI::print_separator();

    for (const auto& run : filtered_runs) {
        std::cout << std::left
                  << std::setw(20) << run.run_timestamp
                  << std::setw(12) << (run.git_commit_hash.value_or("N/A"))
                  << std::setw(10) << run.dataset_size
                  << std::setw(15) << (run.storm_throughput.has_value()
                      ? std::to_string(static_cast<int>(*run.storm_throughput))
                      : "N/A")
                  << std::setw(15) << (run.raw_throughput.has_value()
                      ? std::to_string(static_cast<int>(*run.raw_throughput))
                      : "N/A")
                  << std::setw(12) << (run.efficiency_percent.has_value()
                      ? std::to_string(static_cast<int>(*run.efficiency_percent)) + "%"
                      : "N/A")
                  << "\n";
    }

    std::cout << "\n";
    CLI::info("Showing " + std::to_string(filtered_runs.size()) + " most recent run(s)");
}

// ============================================================================
// Main Entry Point
// ============================================================================

void print_usage(const char* program_name) {
    std::cout << "Storm ORM - Unified Benchmark System\n\n";
    std::cout << "Usage:\n";
    std::cout << "  " << program_name << " --init                       # Initialize database\n";
    std::cout << "  " << program_name << " --register                   # Register new test (interactive)\n";
    std::cout << "  " << program_name << " --list [--category=CRUD]     # List all tests\n";
    std::cout << "  " << program_name << " --history <test_name>        # Show test history\n";
    std::cout << "  " << program_name << " --run <test_name> [--size=10000] [--iterations=100]\n";
    std::cout << "  " << program_name << " --run-all                    # Run all enabled tests\n";
    std::cout << "  " << program_name << " --run-category <category>    # Run all tests in category\n";
    std::cout << "\n";
    std::cout << "Options:\n";
    std::cout << "  --db=PATH         Database path (default: benchmark_registry.db)\n";
    std::cout << "  --size=N          Dataset size for benchmarks\n";
    std::cout << "  --iterations=N    Number of iterations\n";
    std::cout << "\n";
}

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    std::string db_path = "benchmark_registry.db";
    std::string command;
    std::optional<std::string> test_name;
    std::optional<std::string> category;
    int size = 10000;
    int iterations = 100;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--init") {
            command = "init";
        } else if (arg == "--register") {
            command = "register";
        } else if (arg == "--list") {
            command = "list";
        } else if (arg == "--history") {
            command = "history";
            if (i + 1 < argc) {
                test_name = argv[++i];
            }
        } else if (arg == "--run") {
            command = "run";
            if (i + 1 < argc) {
                test_name = argv[++i];
            }
        } else if (arg == "--run-all") {
            command = "run-all";
        } else if (arg == "--run-category") {
            command = "run-category";
            if (i + 1 < argc) {
                category = argv[++i];
            }
        } else if (arg.starts_with("--db=")) {
            db_path = arg.substr(5);
        } else if (arg.starts_with("--size=")) {
            size = std::stoi(arg.substr(7));
        } else if (arg.starts_with("--iterations=")) {
            iterations = std::stoi(arg.substr(13));
        } else if (arg.starts_with("--category=")) {
            category = arg.substr(11);
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Execute command
    if (command == "init") {
        auto result = init_database(db_path);
        if (!result.has_value()) {
            CLI::error("Failed to initialize database: " + result.error().message());
            return 1;
        }
    } else if (command == "register") {
        register_test_interactive(db_path);
    } else if (command == "list") {
        list_tests(db_path, category);
    } else if (command == "history") {
        if (!test_name.has_value()) {
            CLI::error("Test name required for --history");
            return 1;
        }
        show_history(db_path, *test_name, 10);
    } else if (command == "run") {
        CLI::warning("Test execution not yet implemented");
        // TODO: Implement test execution
    } else if (command == "run-all") {
        CLI::warning("Run-all not yet implemented");
        // TODO: Implement run-all
    } else if (command == "run-category") {
        CLI::warning("Run-category not yet implemented");
        // TODO: Implement run-category
    } else {
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}
