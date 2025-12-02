#pragma once

/**
 * BenchmarkRunner - Orchestrates benchmark execution
 */

#include <iostream>
#include <chrono>
#include <iomanip>
#include <type_traits>
#include "schema.hpp"
#include "parser.hpp"
#include "operations/select.hpp"
#include "operations/insert.hpp"
#include "operations/update.hpp"
#include "operations/delete.hpp"

namespace storm::benchmark {

// ANSI color codes for terminal output
namespace Color {
    constexpr const char* RESET   = "\033[0m";
    constexpr const char* BOLD    = "\033[1m";
    constexpr const char* RED     = "\033[31m";
    constexpr const char* GREEN   = "\033[32m";
    constexpr const char* YELLOW  = "\033[33m";
    constexpr const char* BLUE    = "\033[34m";
    constexpr const char* MAGENTA = "\033[35m";
    constexpr const char* CYAN    = "\033[36m";
    constexpr const char* WHITE   = "\033[37m";

    // Bold colors
    constexpr const char* BOLD_GREEN  = "\033[1;32m";
    constexpr const char* BOLD_YELLOW = "\033[1;33m";
    constexpr const char* BOLD_CYAN   = "\033[1;36m";
    constexpr const char* BOLD_WHITE  = "\033[1;37m";
}

class BenchmarkRunner {
public:
    // List all available tests
    void list_tests() {
        std::cout << "=== Available Benchmark Tests ===\n";
        std::cout << "Total: " << BENCHMARK_TESTS.size() << " tests\n\n";

        std::cout << "Test Name                      Category         Operation\n";
        std::cout << "────────────────────────────────────────────────────────────\n";

        for (size_t i = 0; i < BENCHMARK_TESTS.size(); i++) {
            const auto& test = BENCHMARK_TESTS[i];
            std::string test_name(test.test_name.view());
            std::string category(test.test_category.view());
            std::string operation(test.operation.view());

            // Format with padding
            std::cout << test_name;
            for (size_t j = test_name.size(); j < 31; j++) std::cout << " ";
            std::cout << category;
            for (size_t j = category.size(); j < 17; j++) std::cout << " ";
            std::cout << operation << "\n";
        }
        std::cout << "\n";
    }

    // Helper to detect if benchmark class has prepare() method
    template<typename T, typename = void>
    struct has_prepare : std::false_type {};

    template<typename T>
    struct has_prepare<T, std::void_t<decltype(std::declval<T>().prepare(int{}))>> : std::true_type {};

    // Common timing and reporting for all benchmarks
    template<typename BenchmarkClass>
    void run_benchmark(const char* test_name, BenchmarkClass&& bench, int iterations) {
        std::cout << "\n" << Color::BOLD_CYAN << "=== " << test_name << " ===" << Color::RESET << "\n";

        bench.print_info();

        // Prepare data BEFORE timing (if benchmark has prepare() method)
        if constexpr (has_prepare<BenchmarkClass>::value) {
            bench.prepare(iterations);
        }

        // ===== Storm ORM Execution =====
        auto start_storm = std::chrono::steady_clock::now();
        int operations_storm = bench.execute(iterations);
        auto end_storm = std::chrono::steady_clock::now();

        auto duration_storm = std::chrono::duration_cast<std::chrono::nanoseconds>(end_storm - start_storm);
        double duration_storm_us = duration_storm.count() / 1000.0;
        double storm_ops_per_sec = (operations_storm / (duration_storm.count() / 1e9));
        double storm_throughput = storm_ops_per_sec / 1e6;

        // ===== Raw SQLite Execution =====
        auto start_raw = std::chrono::steady_clock::now();
        int operations_raw = bench.execute_raw(iterations);
        auto end_raw = std::chrono::steady_clock::now();

        auto duration_raw = std::chrono::duration_cast<std::chrono::nanoseconds>(end_raw - start_raw);
        double duration_raw_us = duration_raw.count() / 1000.0;
        double raw_ops_per_sec = (operations_raw / (duration_raw.count() / 1e9));
        double raw_throughput = raw_ops_per_sec / 1e6;

        // Calculate efficiency
        double efficiency = (storm_throughput / raw_throughput) * 100.0;

        // Format output with colors
        std::cout << "Iterations: " << Color::YELLOW << iterations << Color::RESET << "\n";
        std::cout << "\n";

        // Storm ORM results
        std::cout << Color::BOLD << "Storm ORM:" << Color::RESET << "\n";
        std::cout << "  Operations: " << Color::YELLOW << operations_storm << Color::RESET << "\n";
        std::cout << "  Duration: " << Color::MAGENTA << std::fixed << std::setprecision(2)
                  << duration_storm_us << " μs" << Color::RESET << "\n";

        const char* storm_color = storm_throughput >= 5.0 ? Color::BOLD_GREEN :
                                  storm_throughput >= 1.0 ? Color::GREEN : Color::YELLOW;
        std::cout << "  Throughput: " << storm_color << std::fixed << std::setprecision(2)
                  << storm_throughput << " M ops/sec" << Color::RESET << "\n";

        // Raw SQLite results
        std::cout << "\n" << Color::BOLD << "Raw SQLite:" << Color::RESET << "\n";
        std::cout << "  Operations: " << Color::YELLOW << operations_raw << Color::RESET << "\n";
        std::cout << "  Duration: " << Color::MAGENTA << std::fixed << std::setprecision(2)
                  << duration_raw_us << " μs" << Color::RESET << "\n";

        const char* raw_color = raw_throughput >= 5.0 ? Color::BOLD_GREEN :
                                raw_throughput >= 1.0 ? Color::GREEN : Color::YELLOW;
        std::cout << "  Throughput: " << raw_color << std::fixed << std::setprecision(2)
                  << raw_throughput << " M ops/sec" << Color::RESET << "\n";

        // Efficiency comparison
        std::cout << "\n" << Color::BOLD << "Efficiency: " << Color::RESET;
        const char* eff_color = efficiency >= 90.0 ? Color::BOLD_GREEN :
                                efficiency >= 70.0 ? Color::GREEN :
                                efficiency >= 50.0 ? Color::YELLOW : Color::RED;
        std::cout << eff_color << std::fixed << std::setprecision(1)
                  << efficiency << "%" << Color::RESET;
        std::cout << " (" << (efficiency >= 100.0 ? Color::BOLD_GREEN : Color::GREEN)
                  << (efficiency >= 100.0 ? "FASTER" : "slower") << Color::RESET
                  << " than raw SQLite)\n";

        std::cout << Color::GREEN << "✅ Benchmark complete!" << Color::RESET << "\n";
    }

private:
    // Operation handlers - one function per operation type
    // Clean, simple, easy to add new operations

    template<typename Model, auto& test>
    static void run_where_operation(BenchmarkRunner& runner, int iterations) {
        constexpr std::string_view field_name = test.where.field.view();
        constexpr auto op_str = test.where.op;
        constexpr int value = test.where.value_int;
        constexpr auto field_info = dispatch_field<Model>(field_name);

        runner.run_benchmark(test.test_name.c_str(), SelectBenchmark<Model, field_info, op_str, int>{value}, iterations);
    }

    template<typename Model, auto& test>
    static void run_insert_operation(BenchmarkRunner& runner, int iterations) {
        constexpr int batch_size = test.batch_size;

        if constexpr (batch_size <= 1) {
            runner.run_benchmark(test.test_name.c_str(), InsertBenchmark<Model>{}, iterations);
        } else {
            runner.run_benchmark(test.test_name.c_str(), InsertBatchBenchmark<Model, batch_size>{}, iterations);
        }
    }

    template<typename Model, auto& test>
    static void run_update_operation(BenchmarkRunner& runner, int iterations) {
        constexpr std::string_view field_name = test.where.field.view();
        constexpr auto op_str = test.where.op;
        constexpr int value = test.where.value_int;
        constexpr auto field_info = dispatch_field<Model>(field_name);

        runner.run_benchmark(test.test_name.c_str(), UpdateBenchmark<Model, field_info, op_str, int>{value}, iterations);
    }

    template<typename Model, auto& test>
    static void run_delete_operation(BenchmarkRunner& runner, int iterations) {
        constexpr std::string_view field_name = test.where.field.view();
        constexpr auto op_str = test.where.op;
        constexpr int value = test.where.value_int;
        constexpr auto field_info = dispatch_field<Model>(field_name);

        runner.run_benchmark(test.test_name.c_str(), DeleteBenchmark<Model, field_info, op_str, int>{value}, iterations);
    }

public:
    // Template recursion to execute tests at compile time
    template<typename Model, size_t TestIndex, size_t TotalTests>
    struct TestExecutor {
        static void execute(BenchmarkRunner& runner, int iterations, const std::string& filter = "") {
            constexpr auto& test = BENCHMARK_TESTS[TestIndex];
            constexpr std::string_view test_name = test.test_name.view();
            constexpr std::string_view operation = test.operation.view();

            // Check if test matches filter (runtime check, but minimal overhead)
            bool should_run = filter.empty() ||
                             std::string(test_name.data(), test_name.size()).find(filter) != std::string::npos;

            if (should_run) {
                // Dispatch to handler - still compile-time, just cleaner
                if constexpr (operation == "where") {
                    runner.run_where_operation<Model, test>(runner, iterations);
                } else if constexpr (operation == "insert") {
                    runner.run_insert_operation<Model, test>(runner, iterations);
                } else if constexpr (operation == "update") {
                    runner.run_update_operation<Model, test>(runner, iterations);
                } else if constexpr (operation == "delete") {
                    runner.run_delete_operation<Model, test>(runner, iterations);
                }
            }

            // Recurse to next test
            if constexpr (TestIndex + 1 < TotalTests) {
                TestExecutor<Model, TestIndex + 1, TotalTests>::execute(runner, iterations, filter);
            }
        }
    };

    // Entry point for test execution (all tests)
    template<typename Model>
    void run_all(int iterations = 1000) {
        std::cout << "=== Running All Benchmark Tests (Compile-Time Dispatch) ===\n";
        std::cout << "Total tests: " << BENCHMARK_TESTS.size() << "\n";
        std::cout << "Iterations per test: " << iterations << "\n";
        std::cout << "Using compile-time JSON parsing with nested C++ structs\n\n";

        // Start template recursion from index 0
        TestExecutor<Model, 0, BENCHMARK_TESTS.size()>::execute(*this, iterations);

        std::cout << "\n✅ All tests completed with COMPILE-TIME dispatch!\n";
        std::cout << "✅ Zero runtime string parsing overhead!\n";
        std::cout << "✅ Each test has its own specialized function!\n";
        std::cout << "✅ Tests loaded from JSON at compile time!\n";
    }

    // Entry point for filtered test execution
    template<typename Model>
    void run_filtered(const std::string& filter, int iterations = 1000) {
        std::cout << "=== Running Filtered Benchmark Tests ===\n";
        std::cout << "Filter: \"" << filter << "\"\n";
        std::cout << "Iterations per test: " << iterations << "\n";
        std::cout << "Using compile-time dispatch with runtime filtering\n\n";

        // Start template recursion with filter
        TestExecutor<Model, 0, BENCHMARK_TESTS.size()>::execute(*this, iterations, filter);

        std::cout << "\n✅ Filtered tests completed!\n";
    }
};

} // namespace storm::benchmark
