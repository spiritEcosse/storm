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
 *   ./storm_bench
 *
 * To add benchmarks:
 *   Edit tests/benchmark_tests.yaml and rebuild
 *   (YAML is auto-converted to JSON during build)
 */

#include <iostream>
#include <cstdio>
#include <meta>

import storm;
import storm_db_sqlite;
import storm_orm_statements_insert;
import <expected>;
import <string>;
import <memory>;

#include "runner.hpp"

using namespace storm;
using namespace storm::benchmark;

// Models are defined in models.hpp and included via runner.hpp

namespace {

    struct Args {
        std::string filter;
        std::string category;
        std::string db_path       = ":memory:";
        int         iterations    = 0; // 0 = use JSON-defined per-test values
        bool        list_tests    = false;
        bool        scale_test    = false;
        bool        use_disk      = false;
        bool        show_help     = false;
        bool        quick_mode    = false; // 0.3x iterations for fast validation
        bool        thorough_mode = false; // 1.5x iterations for regression testing
    };

    void print_help(const char* prog) {
        std::cout << "Storm ORM Benchmark System\n\n";
        std::cout << "Usage: " << prog << " [options]\n\n";
        std::cout << "Options:\n";
        std::cout << "  --filter=<pattern>      Run only tests with EXACT name match\n";
        std::cout << "  -c, --category=<name>   Run tests matching category prefix (SELECT matches SELECT*)\n";
        std::cout << "  --scale-test            Test performance with increasing sizes (substring match)\n";
        std::cout << "  --iterations=<n>        Override iterations for all tests (default: use JSON values)\n";
        std::cout << "  --quick                 Quick validation mode (~3-5 min, 0.3x iterations)\n";
        std::cout << "  --thorough              Thorough regression mode (~15-20 min, 1.5x iterations)\n";
        std::cout << "  --disk                  Use disk-based database (default: in-memory)\n";
        std::cout << "  --db=<path>             Use specific database file path\n";
        std::cout << "  --list, -l              List all available tests\n";
        std::cout << "  --help, -h              Show this help message\n\n";
        std::cout << "Modes:\n";
        std::cout << "  (default)               Use JSON-defined iterations (~10 min for all tests)\n";
        std::cout << "  --quick                 0.3x iterations for fast development feedback\n";
        std::cout << "  --thorough              1.5x iterations for pre-commit validation\n\n";
        std::cout << "Examples:\n";
        std::cout << "  " << prog << " --quick                          # Fast validation (~3-5 min)\n";
        std::cout << "  " << prog << " --quick -c SELECT                # Quick SELECT tests only\n";
        std::cout << "  " << prog << " --thorough                       # Thorough regression test\n";
        std::cout << "  " << prog << " -c SELECT                        # Run SELECT* categories\n";
        std::cout << "  " << prog << " --filter=insert_batch_100        # Run only insert_batch_100\n";
        std::cout << "  " << prog << " --filter=insert_batch --scale-test  # Test all batch sizes\n";
        std::cout << "  " << prog << " --iterations=5000                # Override all iterations\n";
        std::cout << "  " << prog << " --list\n";
    }

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    auto parse_args(int argc, char* argv[]) -> Args {
        Args args;
        for (int i = 1; i < argc; i++) {
            const std::string arg = argv[i];
            if (arg.starts_with("--filter=")) {
                args.filter = arg.substr(9);
            } else if (arg.starts_with("--iterations=")) {
                args.iterations = std::stoi(arg.substr(13));
            } else if (arg == "--list" || arg == "-l") {
                args.list_tests = true;
            } else if (arg == "--scale-test") {
                args.scale_test = true;
            } else if (arg == "--quick") {
                args.quick_mode = true;
            } else if (arg == "--thorough") {
                args.thorough_mode = true;
            } else if (arg.starts_with("--category=")) {
                args.category = arg.substr(11);
            } else if (arg.starts_with("-c=")) {
                args.category = arg.substr(3);
            } else if (arg == "-c" && i + 1 < argc) {
                args.category = argv[++i];
            } else if (arg == "--disk") {
                args.use_disk = true;
                args.db_path  = "benchmark_test.db";
            } else if (arg.starts_with("--db=")) {
                args.db_path  = arg.substr(5);
                args.use_disk = (args.db_path != ":memory:");
            } else if (arg == "--help" || arg == "-h") {
                args.show_help = true;
            }
        }
        return args;
    }

} // namespace

auto main(int argc, char* argv[]) -> int {
    try {
        auto args = parse_args(argc, argv);

        if (args.show_help) {
            print_help(argv[0]);
            return 0;
        }

        // Validate mutually exclusive flags
        if (args.quick_mode && args.thorough_mode) {
            std::cerr << "Error: --quick and --thorough are mutually exclusive\n";
            return 1;
        }

        // Determine benchmark mode
        BenchmarkMode mode = BenchmarkMode::Default;
        if (args.quick_mode) {
            mode = BenchmarkMode::Quick;
        } else if (args.thorough_mode) {
            mode = BenchmarkMode::Thorough;
        }

        // Handle --list flag (supports category filter)
        if (args.list_tests) {
            BenchmarkRunner runner;
            runner.list_tests(args.category);
            return 0;
        }

        // Remove old db file if using disk (clean slate)
        if (args.use_disk) {
            std::remove(args.db_path.c_str());
            std::cout << "📁 Using disk database: " << args.db_path << "\n";
        } else {
            std::cout << "💾 Using in-memory database\n";
        }

        // Set up database connection
        auto result = QuerySet<Person>::set_default_connection(args.db_path);
        if (!result.has_value()) {
            std::cerr << "Failed to open database: " << result.error().message() << "\n";
            return 1;
        }

        const auto& conn = QuerySet<Person>::get_default_connection();

        // Create tables using SchemaStatement for type-safe DDL
        auto create_result = storm::orm::schema::SchemaStatement<Person>::create_table_if_not_exists(conn);
        if (!create_result.has_value()) {
            std::cerr << "Failed to create Person table: " << create_result.error().message() << "\n";
            return 1;
        }

        auto create_user_result = storm::orm::schema::SchemaStatement<User>::create_table_if_not_exists(conn);
        if (!create_user_result.has_value()) {
            std::cerr << "Failed to create User table: " << create_user_result.error().message() << "\n";
            return 1;
        }

        auto create_msg_result = storm::orm::schema::SchemaStatement<FKMessage>::create_table_if_not_exists(conn);
        if (!create_msg_result.has_value()) {
            std::cerr << "Failed to create FKMessage table: " << create_msg_result.error().message() << "\n";
            return 1;
        }

        // Run benchmark tests (with optional filter)
        // Note: Each benchmark handles its own data setup in prepare() method
        BenchmarkRunner runner;
        if (!args.category.empty() || !args.filter.empty()) {
            runner.run_filtered<Person>(args.filter, args.category, args.iterations, args.scale_test, mode);
        } else {
            runner.run_all<Person>(args.iterations, mode);
        }

        std::cout << "\n=== SUCCESS ===\n";
        std::cout << "All tests executed with compile-time dispatch!\n";
        std::cout << "Template metaprogramming unrolled the loop:\n";
        std::cout << "  - Each test has its own specialized function\n";
        std::cout << "  - Field names resolved at compile time\n";
        std::cout << "  - Zero runtime string dispatch overhead\n";

        // Cleanup disk database file if used
        if (args.use_disk) {
            std::remove(args.db_path.c_str());
            std::cout << "🧹 Cleaned up database file: " << args.db_path << "\n";
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}
