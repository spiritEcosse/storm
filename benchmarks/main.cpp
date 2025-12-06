/**
 * Storm ORM - Compile-Time Benchmark System
 *
 * Features:
 * - Loads benchmark tests from JSON at compile time (#embed)
 * - Zero runtime string parsing overhead
 * - Template metaprogramming unrolls test execution
 * - Each test gets its own specialized function
 * - Field names and operators resolved at compile time
 *
 * Usage:
 *   ./main
 *
 * To add benchmarks:
 *   Edit tests/benchmark_tests.json and rebuild
 */

#include <iostream>
#include <cstdio>
#include <meta>

import storm;
import storm_db_sqlite;
import <expected>;
import <string>;
import <memory>;

#include "runner.hpp"

using namespace storm;
using namespace storm::benchmark;

// Test model
struct Person {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string name;
    int age;
    bool is_active;
    double salary;
};

int main(int argc, char* argv[]) {
    try {
        // Parse command-line arguments
        std::string filter;
        int iterations = 1000;
        bool list_tests = false;
        bool scale_test = false;  // Scale test mode (test performance degradation with increasing sizes)
        bool use_disk = false;    // Use disk-based database instead of in-memory
        std::string db_path = ":memory:";

        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg.find("--filter=") == 0) {
                filter = arg.substr(9);  // Skip "--filter="
            } else if (arg.find("--iterations=") == 0) {
                iterations = std::stoi(arg.substr(13));
            } else if (arg == "--list" || arg == "-l") {
                list_tests = true;
            } else if (arg == "--scale-test") {
                scale_test = true;
            } else if (arg == "--disk") {
                use_disk = true;
                db_path = "benchmark_test.db";
            } else if (arg.find("--db=") == 0) {
                db_path = arg.substr(5);
                use_disk = (db_path != ":memory:");
            } else if (arg == "--help" || arg == "-h") {
                std::cout << "Storm ORM Benchmark System\n\n";
                std::cout << "Usage: " << argv[0] << " [options]\n\n";
                std::cout << "Options:\n";
                std::cout << "  --filter=<pattern>      Run only tests with EXACT name match\n";
                std::cout << "  --scale-test            Test performance with increasing sizes (substring match)\n";
                std::cout << "  --iterations=<n>        Number of iterations per test (default: 1000)\n";
                std::cout << "  --disk                  Use disk-based database (default: in-memory)\n";
                std::cout << "  --db=<path>             Use specific database file path\n";
                std::cout << "  --list, -l              List all available tests\n";
                std::cout << "  --help, -h              Show this help message\n\n";
                std::cout << "Examples:\n";
                std::cout << "  " << argv[0] << " --filter=insert_batch_100                # Run only insert_batch_100\n";
                std::cout << "  " << argv[0] << " --filter=insert_batch --scale-test       # Test degradation: 10,100,1000,10000...\n";
                std::cout << "  " << argv[0] << " --filter=where_int --scale-test          # Run all where_int_* variants\n";
                std::cout << "  " << argv[0] << " --iterations=5000\n";
                std::cout << "  " << argv[0] << " --disk                                   # Use disk-based database\n";
                std::cout << "  " << argv[0] << " --db=/tmp/bench.db                       # Use specific database file\n";
                std::cout << "  " << argv[0] << " --list\n";
                return 0;
            }
        }

        // Handle --list flag
        if (list_tests) {
            BenchmarkRunner runner;
            runner.list_tests();
            return 0;
        }

        // Remove old db file if using disk (clean slate)
        if (use_disk) {
            std::remove(db_path.c_str());
            std::cout << "📁 Using disk database: " << db_path << "\n";
        } else {
            std::cout << "💾 Using in-memory database\n";
        }

        // Set up database connection
        auto result = QuerySet<Person>::set_default_connection(db_path);
        if (!result.has_value()) {
            std::cerr << "Failed to open database: " << result.error().message() << "\n";
            return 1;
        }

        auto& conn = QuerySet<Person>::get_default_connection();

        // Create table
        auto create_result = conn->execute(
            "CREATE TABLE Person ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "age INTEGER NOT NULL, "
            "is_active INTEGER NOT NULL, "
            "salary REAL NOT NULL)"
        );
        if (!create_result.has_value()) {
            std::cerr << "Failed to create table: " << create_result.error().message() << "\n";
            return 1;
        }

        // Determine required dataset size based on filter
        // Use runtime filter-aware calculation to avoid inserting unnecessary data
        int required_dataset_size = get_filtered_dataset_size(BENCHMARK_TESTS, filter, scale_test);

        // Insert test data only if required (skip if dataset_size is 0)
        if (required_dataset_size > 0) {
            std::cout << "Inserting test data (" << required_dataset_size << " records)...\n";
            QuerySet<Person> qs;
            for (int i = 1; i <= required_dataset_size; i++) {
                Person p{
                    .id = 0,  // Auto-increment
                    .name = "Person" + std::to_string(i),
                    .age = 20 + (i % 50),
                    .is_active = (i % 2 == 0),
                    .salary = 30000.0 + (i * 1000.0)
                };
                auto insert_result = qs.insert(p);
                if (!insert_result.has_value()) {
                    std::cerr << "Failed to insert: " << insert_result.error().message() << "\n";
                    return 1;
                }
            }
            std::cout << "✅ Inserted " << required_dataset_size << " test records\n";
        } else {
            std::cout << "⏭️  Skipping test data insertion (dataset_size = 0)\n";
        }

        // Run benchmark tests (with optional filter)
        BenchmarkRunner runner;
        if (filter.empty()) {
            runner.run_all<Person>(iterations);
        } else {
            runner.run_filtered<Person>(filter, iterations, scale_test);
        }

        std::cout << "\n=== SUCCESS ===\n";
        std::cout << "All tests executed with compile-time dispatch!\n";
        std::cout << "Template metaprogramming unrolled the loop:\n";
        std::cout << "  - Each test has its own specialized function\n";
        std::cout << "  - Field names resolved at compile time\n";
        std::cout << "  - Zero runtime string dispatch overhead\n";

        // Cleanup disk database file if used
        if (use_disk) {
            std::remove(db_path.c_str());
            std::cout << "🧹 Cleaned up database file: " << db_path << "\n";
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}
