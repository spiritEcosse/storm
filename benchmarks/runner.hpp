#pragma once

/**
 * BenchmarkRunner - Orchestrates benchmark execution
 */

#include <iostream>
#include <chrono>
#include <iomanip>
#include <format>
#include <type_traits>
#include <vector>
#include <algorithm>
#include <cmath>
#include "schema.hpp"
#include "parser.hpp"
#include "models.hpp"
#include "operations/select.hpp" // Unified SELECT benchmark (WHERE, JOIN, WHERE+JOIN)
#include "operations/insert.hpp"
#include "operations/update.hpp"
#include "operations/delete.hpp"
#include "operations/aggregate.hpp"
#include "operations/distinct.hpp" // DISTINCT benchmarks

namespace storm::benchmark {

    // Benchmark execution mode
    enum class BenchmarkMode {
        Default, // Use JSON-defined iterations
        Quick,   // 0.3x iterations for fast validation (~3-5 min)
        Thorough // 1.5x iterations for thorough regression testing (~15-20 min)
    };

    // Calculate actual iterations based on mode
    inline auto calculate_iterations(int base_iterations, BenchmarkMode mode) -> int {
        switch (mode) {
        case BenchmarkMode::Quick:
            // 0.3x multiplier, minimum 1 iteration
            return std::max(1, static_cast<int>(base_iterations * 0.3));
        case BenchmarkMode::Thorough:
            // 1.5x multiplier, minimum 3 for very small counts
            if (base_iterations <= 2)
                return 3;
            return static_cast<int>(base_iterations * 1.5);
        case BenchmarkMode::Default:
        default:
            return base_iterations;
        }
    }

    // Number of runs per benchmark for statistical accuracy
    constexpr int NUM_RUNS = 5;

    // Statistical helper functions
    inline auto calculate_median(std::vector<double>& values) -> double {
        std::ranges::sort(values);
        size_t n = values.size();
        if (n == 0)
            return 0.0;
        return (n % 2 == 0) ? (values[n / 2 - 1] + values[n / 2]) / 2.0 : values[n / 2];
    }

    inline auto calculate_mean(const std::vector<double>& values) -> double {
        if (values.empty())
            return 0.0;
        double sum = 0.0;
        for (double v : values)
            sum += v;
        return sum / values.size();
    }

    inline auto calculate_stddev(const std::vector<double>& values, double mean) -> double {
        if (values.size() < 2)
            return 0.0;
        double sum_sq = 0.0;
        for (double v : values) {
            sum_sq += (v - mean) * (v - mean);
        }
        return std::sqrt(sum_sq / values.size());
    }

    inline auto calculate_min(const std::vector<double>& values) -> double {
        if (values.empty())
            return 0.0;
        return *std::ranges::min_element(values);
    }

    inline auto calculate_max(const std::vector<double>& values) -> double {
        if (values.empty())
            return 0.0;
        return *std::ranges::max_element(values);
    }

    // ANSI color codes for terminal output
    namespace Color {
        constexpr const char* RESET   = "\o{33}[0m";
        constexpr const char* BOLD    = "\o{33}[1m";
        constexpr const char* RED     = "\o{33}[31m";
        constexpr const char* GREEN   = "\o{33}[32m";
        constexpr const char* YELLOW  = "\o{33}[33m";
        constexpr const char* BLUE    = "\o{33}[34m";
        constexpr const char* MAGENTA = "\o{33}[35m";
        constexpr const char* CYAN    = "\o{33}[36m";
        constexpr const char* WHITE   = "\o{33}[37m";

        // Bold colors
        constexpr const char* BOLD_GREEN  = "\o{33}[1;32m";
        constexpr const char* BOLD_YELLOW = "\o{33}[1;33m";
        constexpr const char* BOLD_CYAN   = "\o{33}[1;36m";
        constexpr const char* BOLD_WHITE  = "\o{33}[1;37m";
    } // namespace Color

    // Helper: Get color based on throughput (M ops/sec)
    inline auto get_throughput_color(double throughput) -> const char* {
        if (throughput >= 5.0)
            return Color::BOLD_GREEN;
        if (throughput >= 1.0)
            return Color::GREEN;
        return Color::YELLOW;
    }

    // Helper: Get color based on efficiency percentage
    inline auto get_efficiency_color(double efficiency) -> const char* {
        if (efficiency >= 90.0)
            return Color::BOLD_GREEN;
        if (efficiency >= 70.0)
            return Color::GREEN;
        if (efficiency >= 50.0)
            return Color::YELLOW;
        return Color::RED;
    }

    class BenchmarkRunner {
      public:
        // List available tests (optionally filtered by category prefix)
        auto list_tests(const std::string& category_filter = "") -> void {
            std::cout << "=== Available Benchmark Tests ===\n";

            // Count matching tests
            size_t count = 0;
            for (const auto& test : BENCHMARK_TESTS) {
                std::string cat(test.test_category.view());
                if (category_filter.empty() || cat.starts_with(category_filter)) {
                    count++;
                }
            }

            if (!category_filter.empty()) {
                std::cout << "Category filter: \"" << category_filter << "*\"\n";
            }
            std::cout << "Total: " << count << " tests\n\n";

            std::cout << "Test Name                      Category         Operation\n";
            std::cout << "────────────────────────────────────────────────────────────\n";

            for (const auto& test : BENCHMARK_TESTS) {
                std::string test_name(test.test_name.view());
                std::string category(test.test_category.view());
                std::string operation(test.operation.view());

                // Skip if doesn't match category filter
                if (!category_filter.empty() && !category.starts_with(category_filter)) {
                    continue;
                }

                // Format with padding
                std::cout << test_name;
                for (size_t j = test_name.size(); j < 31; j++)
                    std::cout << " ";
                std::cout << category;
                for (size_t j = category.size(); j < 17; j++)
                    std::cout << " ";
                std::cout << operation << "\n";
            }
            std::cout << "\n";
        }

        // Helper to detect if benchmark class has prepare() method
        template <typename T, typename = void> struct has_prepare : std::false_type {};

        template <typename T>
        struct has_prepare<T, std::void_t<decltype(std::declval<T>().prepare(int{}))>> : std::true_type {};

        // Common timing and reporting for all benchmarks (with statistical analysis)
        template <typename BenchmarkClass>
        auto run_benchmark(const char* test_name, BenchmarkClass bench, int iterations) -> void {
            std::cout << "\n" << Color::BOLD_CYAN << "=== " << test_name << " ===" << Color::RESET << "\n";

            bench.print_info();

            // Prepare data BEFORE timing (if benchmark has prepare() method)
            if constexpr (has_prepare<BenchmarkClass>::value) {
                bench.prepare(iterations);
            }

            // Collect throughput samples from multiple runs
            std::vector<double> storm_throughputs;
            std::vector<double> raw_throughputs;
            storm_throughputs.reserve(NUM_RUNS);
            raw_throughputs.reserve(NUM_RUNS);

            int operations_storm = 0;
            int operations_raw   = 0;

            for (int run = 0; run < NUM_RUNS; run++) {
                // ===== Storm ORM Execution =====
                auto start_storm = std::chrono::steady_clock::now();
                operations_storm = bench.execute(iterations);
                auto end_storm   = std::chrono::steady_clock::now();

                auto   duration_storm = std::chrono::duration_cast<std::chrono::nanoseconds>(end_storm - start_storm);
                double storm_ops_per_sec = (operations_storm / (duration_storm.count() / 1e9));
                storm_throughputs.push_back(storm_ops_per_sec / 1e6);

                // ===== Raw SQLite Execution =====
                auto start_raw = std::chrono::steady_clock::now();
                operations_raw = bench.execute_raw(iterations);
                auto end_raw   = std::chrono::steady_clock::now();

                auto   duration_raw    = std::chrono::duration_cast<std::chrono::nanoseconds>(end_raw - start_raw);
                double raw_ops_per_sec = (operations_raw / (duration_raw.count() / 1e9));
                raw_throughputs.push_back(raw_ops_per_sec / 1e6);
            }

            // Calculate statistics for Storm ORM
            double storm_median = calculate_median(storm_throughputs);
            double storm_mean   = calculate_mean(storm_throughputs);
            double storm_stddev = calculate_stddev(storm_throughputs, storm_mean);
            double storm_min    = calculate_min(storm_throughputs);
            double storm_max    = calculate_max(storm_throughputs);

            // Calculate statistics for Raw SQLite
            double raw_median = calculate_median(raw_throughputs);
            double raw_mean   = calculate_mean(raw_throughputs);
            double raw_stddev = calculate_stddev(raw_throughputs, raw_mean);
            double raw_min    = calculate_min(raw_throughputs);
            double raw_max    = calculate_max(raw_throughputs);

            // Calculate efficiency based on median (most stable metric)
            double efficiency = (storm_median / raw_median) * 100.0;

            // Format output with colors
            std::cout << "Iterations: " << Color::YELLOW << iterations << Color::RESET;
            std::cout << " | Runs: " << Color::YELLOW << NUM_RUNS << Color::RESET << "\n\n";

            // Storm ORM results
            std::cout << Color::BOLD << "Storm ORM:" << Color::RESET << "\n";
            std::cout << "  Operations: " << Color::YELLOW << operations_storm << Color::RESET << "\n";

            const char* storm_color = get_throughput_color(storm_median);
            std::cout << "  Median:  " << storm_color << std::format("{:.2f}", storm_median) << " M ops/sec"
                      << Color::RESET << "\n";
            std::cout << "  Mean:    " << storm_color << std::format("{:.2f}", storm_mean) << " M ops/sec"
                      << Color::RESET << " (±" << std::format("{:.2f}", storm_stddev) << ")\n";
            std::cout << "  Range:   [" << std::format("{:.2f}", storm_min) << " - " << std::format("{:.2f}", storm_max)
                      << "]\n";

            // Raw SQLite results
            std::cout << "\n" << Color::BOLD << "Raw SQLite:" << Color::RESET << "\n";
            std::cout << "  Operations: " << Color::YELLOW << operations_raw << Color::RESET << "\n";

            const char* raw_color = get_throughput_color(raw_median);
            std::cout << "  Median:  " << raw_color << std::format("{:.2f}", raw_median) << " M ops/sec" << Color::RESET
                      << "\n";
            std::cout << "  Mean:    " << raw_color << std::format("{:.2f}", raw_mean) << " M ops/sec" << Color::RESET
                      << " (±" << std::format("{:.2f}", raw_stddev) << ")\n";
            std::cout << "  Range:   [" << std::format("{:.2f}", raw_min) << " - " << std::format("{:.2f}", raw_max)
                      << "]\n";

            // Efficiency comparison
            std::cout << "\n" << Color::BOLD << "Efficiency: " << Color::RESET;
            const char* eff_color = get_efficiency_color(efficiency);
            std::cout << eff_color << std::format("{:.1f}", efficiency) << "%" << Color::RESET;

            const char* comparison_color = (efficiency >= 100.0) ? Color::BOLD_GREEN : Color::GREEN;
            const char* comparison_text  = (efficiency >= 100.0) ? "FASTER" : "slower";
            std::cout << " (" << comparison_color << comparison_text << Color::RESET << " than raw SQLite)\n";

            std::cout << Color::GREEN << "✅ Benchmark complete!" << Color::RESET << "\n";
        }

      private:
        // Operation handlers - one function per operation type
        // Clean, simple, easy to add new operations

        template <typename Model, auto& test> static void run_where_operation(BenchmarkRunner& runner, int iterations) {
            constexpr std::string_view field_name   = test.where.field.view();
            constexpr auto             op_str       = test.where.op;
            constexpr int              value        = test.where.value_int;
            constexpr auto             field_info   = dispatch_field<Model>(field_name);
            constexpr int              dataset_size = test.dataset_size;

            // Use new unified SelectBenchmark with WhereConfig
            runner.run_benchmark(
                    test.test_name.c_str(),
                    SelectWhereBenchmark<Model, field_info, op_str, int>{value, dataset_size},
                    iterations
            );
        }

        template <typename Model, auto& test>
        static void run_insert_operation(BenchmarkRunner& runner, int iterations) {
            // Runtime batch_size - fair comparison with Storm ORM
            runner.run_benchmark(test.test_name.c_str(), InsertBenchmark<Model>{test.batch_size}, iterations);
        }

        template <typename Model, auto& test>
        static void run_delete_pk_operation(BenchmarkRunner& runner, int iterations) {
            // Runtime batch_size - fair comparison with Storm ORM
            runner.run_benchmark(test.test_name.c_str(), DeleteBenchmark<Model>{test.batch_size}, iterations);
        }

        template <typename Model, auto& test>
        static void run_update_pk_operation(BenchmarkRunner& runner, int iterations) {
            // Runtime batch_size - fair comparison with Storm ORM
            runner.run_benchmark(test.test_name.c_str(), UpdateBenchmark<Model>{test.batch_size}, iterations);
        }

        template <typename Model, auto& test>
        static void run_select_operation(BenchmarkRunner& runner, int iterations) {
            // Simple SELECT benchmark (no WHERE, no JOIN)
            constexpr int dataset_size = test.dataset_size;
            runner.run_benchmark(test.test_name.c_str(), SelectBenchmark<Model>{dataset_size}, iterations);
        }

        template <typename Model, auto& test>
        static void run_select_join_operation(BenchmarkRunner& runner, int iterations) {
            // SELECT JOIN benchmark using FKMessage and User models
            // FKFieldPtr is &FKMessage::sender for single JOIN
            constexpr int dataset_size = test.dataset_size;
            runner.run_benchmark(
                    test.test_name.c_str(),
                    SelectJoinBenchmark<FKMessage, User, &FKMessage::sender>{dataset_size},
                    iterations
            );
        }

        template <typename Model, auto& test>
        static void run_select_where_join_operation(BenchmarkRunner& runner, int iterations) {
            // SELECT WHERE + JOIN benchmark using FKMessage and User models
            // WHERE clause filters on User.age (joined model field)
            constexpr std::string_view field_name   = test.where.field.view();
            constexpr auto             op_str       = test.where.op;
            constexpr int              value        = test.where.value_int;
            constexpr auto             field_info   = dispatch_field<User>(field_name);
            constexpr int              dataset_size = test.dataset_size;
            runner.run_benchmark(
                    test.test_name.c_str(),
                    SelectWhereJoinBenchmark<FKMessage, User, &FKMessage::sender, field_info, op_str, int>{
                            value, dataset_size
                    },
                    iterations
            );
        }

        // ====================================================================
        // Aggregate operation handlers
        // ====================================================================

        template <typename Model, auto& test>
        static void run_aggregate_count_operation(BenchmarkRunner& runner, int iterations) {
            constexpr int dataset_size = test.dataset_size;
            runner.run_benchmark(test.test_name.c_str(), CountBenchmark<Model>{dataset_size}, iterations);
        }

        template <typename Model, auto& test>
        static void run_aggregate_count_field_operation(BenchmarkRunner& runner, int iterations) {
            constexpr std::string_view field_name   = test.aggregate_field.view();
            constexpr auto             field_info   = dispatch_field<Model>(field_name);
            constexpr int              dataset_size = test.dataset_size;
            runner.run_benchmark(
                    test.test_name.c_str(), CountFieldBenchmark<Model, field_info>{dataset_size}, iterations
            );
        }

        template <typename Model, auto& test>
        static void run_aggregate_count_distinct_operation(BenchmarkRunner& runner, int iterations) {
            constexpr std::string_view field_name   = test.aggregate_field.view();
            constexpr auto             field_info   = dispatch_field<Model>(field_name);
            constexpr int              dataset_size = test.dataset_size;
            runner.run_benchmark(
                    test.test_name.c_str(), CountDistinctBenchmark<Model, field_info>{dataset_size}, iterations
            );
        }

        template <typename Model, auto& test>
        static void run_aggregate_sum_operation(BenchmarkRunner& runner, int iterations) {
            constexpr std::string_view field_name   = test.aggregate_field.view();
            constexpr auto             field_info   = dispatch_field<Model>(field_name);
            constexpr int              dataset_size = test.dataset_size;
            runner.run_benchmark(test.test_name.c_str(), SumBenchmark<Model, field_info>{dataset_size}, iterations);
        }

        template <typename Model, auto& test>
        static void run_aggregate_avg_operation(BenchmarkRunner& runner, int iterations) {
            constexpr std::string_view field_name   = test.aggregate_field.view();
            constexpr auto             field_info   = dispatch_field<Model>(field_name);
            constexpr int              dataset_size = test.dataset_size;
            runner.run_benchmark(test.test_name.c_str(), AvgBenchmark<Model, field_info>{dataset_size}, iterations);
        }

        template <typename Model, auto& test>
        static void run_aggregate_min_operation(BenchmarkRunner& runner, int iterations) {
            constexpr std::string_view field_name   = test.aggregate_field.view();
            constexpr auto             field_info   = dispatch_field<Model>(field_name);
            constexpr int              dataset_size = test.dataset_size;
            runner.run_benchmark(test.test_name.c_str(), MinBenchmark<Model, field_info>{dataset_size}, iterations);
        }

        template <typename Model, auto& test>
        static void run_aggregate_max_operation(BenchmarkRunner& runner, int iterations) {
            constexpr std::string_view field_name   = test.aggregate_field.view();
            constexpr auto             field_info   = dispatch_field<Model>(field_name);
            constexpr int              dataset_size = test.dataset_size;
            runner.run_benchmark(test.test_name.c_str(), MaxBenchmark<Model, field_info>{dataset_size}, iterations);
        }

        // ====================================================================
        // DISTINCT operation handlers
        // ====================================================================

        template <typename Model, auto& test>
        static void run_distinct_operation(BenchmarkRunner& runner, int iterations) {
            constexpr std::string_view field_name   = test.distinct_field.view();
            constexpr auto             field_info   = dispatch_field<Model>(field_name);
            constexpr int              dataset_size = test.dataset_size;
            runner.run_benchmark(
                    test.test_name.c_str(), SimpleDistinctBenchmark<Model, field_info>{dataset_size}, iterations
            );
        }

        template <typename Model, auto& test>
        static void run_distinct_where_operation(BenchmarkRunner& runner, int iterations) {
            constexpr std::string_view distinct_field_name = test.distinct_field.view();
            constexpr auto             distinct_field_info = dispatch_field<Model>(distinct_field_name);
            constexpr std::string_view where_field_name    = test.where.field.view();
            constexpr auto             where_field_info    = dispatch_field<Model>(where_field_name);
            constexpr auto             op_str              = test.where.op;
            constexpr double           value               = test.where.value_double;
            constexpr int              dataset_size        = test.dataset_size;
            runner.run_benchmark(
                    test.test_name.c_str(),
                    DistinctWhereBenchmark<Model, distinct_field_info, where_field_info, op_str, double>{
                            value, dataset_size
                    },
                    iterations
            );
        }

        template <typename Model, auto& test>
        static void run_distinct_join_operation(BenchmarkRunner& runner, int iterations) {
            // DISTINCT JOIN on FKMessage.sender with User model
            constexpr std::string_view distinct_field_name = test.distinct_field.view();
            constexpr auto             distinct_field_info = dispatch_field<FKMessage>(distinct_field_name);
            constexpr int              dataset_size        = test.dataset_size;
            runner.run_benchmark(
                    test.test_name.c_str(),
                    DistinctJoinBenchmark<FKMessage, User, &FKMessage::sender, distinct_field_info>{dataset_size},
                    iterations
            );
        }

        template <typename Model, auto& test>
        static void run_distinct_where_join_operation(BenchmarkRunner& runner, int iterations) {
            // DISTINCT WHERE + JOIN on FKMessage.sender with User model
            // WHERE clause filters on User.age
            constexpr std::string_view distinct_field_name = test.distinct_field.view();
            constexpr auto             distinct_field_info = dispatch_field<FKMessage>(distinct_field_name);
            constexpr std::string_view where_field_name    = test.where.field.view();
            constexpr auto             where_field_info    = dispatch_field<User>(where_field_name);
            constexpr auto             op_str              = test.where.op;
            constexpr int              value               = test.where.value_int;
            constexpr int              dataset_size        = test.dataset_size;
            runner.run_benchmark(
                    test.test_name.c_str(),
                    DistinctWhereJoinBenchmark<
                            FKMessage,
                            User,
                            &FKMessage::sender,
                            distinct_field_info,
                            where_field_info,
                            op_str,
                            int>{value, dataset_size},
                    iterations
            );
        }

        // ====================================================================
        // LIMIT/OFFSET operation handlers
        // ====================================================================

        template <typename Model, auto& test>
        static void run_select_limit_operation(BenchmarkRunner& runner, int iterations) {
            constexpr int dataset_size = test.dataset_size;
            constexpr int limit_value  = test.limit_value;
            runner.run_benchmark(
                    test.test_name.c_str(), SelectLimitBenchmark<Model, limit_value>{dataset_size}, iterations
            );
        }

        template <typename Model, auto& test>
        static void run_select_offset_operation(BenchmarkRunner& runner, int iterations) {
            constexpr int dataset_size = test.dataset_size;
            constexpr int offset_value = test.offset_value;
            runner.run_benchmark(
                    test.test_name.c_str(), SelectOffsetBenchmark<Model, offset_value>{dataset_size}, iterations
            );
        }

        template <typename Model, auto& test>
        static void run_select_limit_offset_operation(BenchmarkRunner& runner, int iterations) {
            constexpr int dataset_size = test.dataset_size;
            constexpr int limit_value  = test.limit_value;
            constexpr int offset_value = test.offset_value;
            runner.run_benchmark(
                    test.test_name.c_str(),
                    SelectLimitOffsetBenchmark<Model, limit_value, offset_value>{dataset_size},
                    iterations
            );
        }

        template <typename Model, auto& test>
        static void run_select_where_limit_operation(BenchmarkRunner& runner, int iterations) {
            constexpr std::string_view field_name   = test.where.field.view();
            constexpr auto             op_str       = test.where.op;
            constexpr int              value        = test.where.value_int;
            constexpr auto             field_info   = dispatch_field<Model>(field_name);
            constexpr int              dataset_size = test.dataset_size;
            constexpr int              limit_value  = test.limit_value;
            runner.run_benchmark(
                    test.test_name.c_str(),
                    SelectWhereLimitBenchmark<Model, field_info, op_str, int, limit_value>{value, dataset_size},
                    iterations
            );
        }

        template <typename Model, auto& test>
        static void run_select_join_limit_operation(BenchmarkRunner& runner, int iterations) {
            constexpr int dataset_size = test.dataset_size;
            constexpr int limit_value  = test.limit_value;
            runner.run_benchmark(
                    test.test_name.c_str(),
                    SelectJoinLimitBenchmark<FKMessage, User, &FKMessage::sender, limit_value>{dataset_size},
                    iterations
            );
        }

        template <typename Model, auto& test>
        static void run_select_join_limit_offset_operation(BenchmarkRunner& runner, int iterations) {
            constexpr int dataset_size = test.dataset_size;
            constexpr int limit_value  = test.limit_value;
            constexpr int offset_value = test.offset_value;
            runner.run_benchmark(
                    test.test_name.c_str(),
                    SelectJoinLimitOffsetBenchmark<FKMessage, User, &FKMessage::sender, limit_value, offset_value>{
                            dataset_size
                    },
                    iterations
            );
        }

        // ====================================================================
        // ORDER BY operation handlers
        // ====================================================================

        template <typename Model, auto& test>
        static void run_select_order_by_asc_operation(BenchmarkRunner& runner, int iterations) {
            constexpr std::string_view field_name   = test.order_by_field.view();
            constexpr auto             field_info   = dispatch_field<Model>(field_name);
            constexpr int              dataset_size = test.dataset_size;
            runner.run_benchmark(
                    test.test_name.c_str(), SelectOrderByAscBenchmark<Model, field_info>{dataset_size}, iterations
            );
        }

        template <typename Model, auto& test>
        static void run_select_order_by_desc_operation(BenchmarkRunner& runner, int iterations) {
            constexpr std::string_view field_name   = test.order_by_field.view();
            constexpr auto             field_info   = dispatch_field<Model>(field_name);
            constexpr int              dataset_size = test.dataset_size;
            runner.run_benchmark(
                    test.test_name.c_str(), SelectOrderByDescBenchmark<Model, field_info>{dataset_size}, iterations
            );
        }

        template <typename Model, auto& test>
        static void run_select_order_by_where_operation(BenchmarkRunner& runner, int iterations) {
            constexpr std::string_view order_field_name = test.order_by_field.view();
            constexpr auto             order_field_info = dispatch_field<Model>(order_field_name);
            constexpr std::string_view where_field_name = test.where.field.view();
            constexpr auto             where_field_info = dispatch_field<Model>(where_field_name);
            constexpr auto             op_str           = test.where.op;
            constexpr int              value            = test.where.value_int;
            constexpr int              dataset_size     = test.dataset_size;
            constexpr std::string_view dir_str          = test.order_by_direction.view();
            // Default to ASC if direction not specified
            if constexpr (dir_str == "DESC") {
                runner.run_benchmark(
                        test.test_name.c_str(),
                        SelectOrderByWhereBenchmark<
                                Model,
                                order_field_info,
                                OrderDirection::DESC,
                                where_field_info,
                                op_str,
                                int>{value, dataset_size},
                        iterations
                );
            } else {
                runner.run_benchmark(
                        test.test_name.c_str(),
                        SelectOrderByWhereBenchmark<
                                Model,
                                order_field_info,
                                OrderDirection::ASC,
                                where_field_info,
                                op_str,
                                int>{value, dataset_size},
                        iterations
                );
            }
        }

        template <typename Model, auto& test>
        static void run_select_order_by_limit_operation(BenchmarkRunner& runner, int iterations) {
            constexpr std::string_view field_name   = test.order_by_field.view();
            constexpr auto             field_info   = dispatch_field<Model>(field_name);
            constexpr int              dataset_size = test.dataset_size;
            constexpr int              limit_value  = test.limit_value;
            constexpr std::string_view dir_str      = test.order_by_direction.view();
            // Default to ASC if direction not specified
            if constexpr (dir_str == "DESC") {
                runner.run_benchmark(
                        test.test_name.c_str(),
                        SelectOrderByLimitBenchmark<Model, field_info, OrderDirection::DESC, limit_value>{dataset_size},
                        iterations
                );
            } else {
                runner.run_benchmark(
                        test.test_name.c_str(),
                        SelectOrderByLimitBenchmark<Model, field_info, OrderDirection::ASC, limit_value>{dataset_size},
                        iterations
                );
            }
        }

      public:
        // Template recursion to execute tests at compile time
        template <typename Model, size_t TestIndex, size_t TotalTests> struct TestExecutor {
            static void
            execute(BenchmarkRunner&   runner,
                    int                iterations_override, // 0 = use JSON value
                    const std::string& filter     = "",
                    const std::string& category   = "",
                    bool               scale_test = false,
                    BenchmarkMode      mode       = BenchmarkMode::Default) {
                constexpr auto&            test          = BENCHMARK_TESTS[TestIndex];
                constexpr std::string_view test_name     = test.test_name.view();
                constexpr std::string_view test_category = test.test_category.view();
                constexpr std::string_view operation     = test.operation.view();

                // Calculate actual iterations: explicit override > mode-adjusted JSON value
                int actual_iterations;
                if (iterations_override > 0) {
                    actual_iterations = iterations_override; // Explicit --iterations wins
                } else {
                    // Use JSON-defined iterations with mode multiplier
                    actual_iterations = calculate_iterations(test.iterations, mode);
                }

                // Check if test matches category (empty = all categories, prefix match supported)
                std::string category_str(test_category.data(), test_category.size());
                bool        category_match = category.empty() || category_str.starts_with(category);

                // Check if test matches filter (runtime check, but minimal overhead)
                // - Empty filter: run all tests
                // - scale_test=true: substring match (e.g., "insert_batch" matches "insert_batch_100")
                // - scale_test=false: exact match (e.g., "insert_batch_100" only matches "insert_batch_100")
                std::string test_name_str(test_name.data(), test_name.size());
                bool        filter_match =
                        filter.empty() || (scale_test ? test_name_str.contains(filter) : (test_name_str == filter));

                bool should_run = category_match && filter_match;

                if (should_run) {
                    // Dispatch to handler - still compile-time, just cleaner
                    if constexpr (operation == "where") {
                        runner.run_where_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "insert") {
                        runner.run_insert_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "update_pk") {
                        runner.run_update_pk_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "delete_pk") {
                        runner.run_delete_pk_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "select") {
                        runner.run_select_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "select_join") {
                        runner.run_select_join_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "select_where_join") {
                        runner.run_select_where_join_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "aggregate_count") {
                        runner.run_aggregate_count_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "aggregate_count_field") {
                        runner.run_aggregate_count_field_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "aggregate_count_distinct") {
                        runner.run_aggregate_count_distinct_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "aggregate_sum") {
                        runner.run_aggregate_sum_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "aggregate_avg") {
                        runner.run_aggregate_avg_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "aggregate_min") {
                        runner.run_aggregate_min_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "aggregate_max") {
                        runner.run_aggregate_max_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "distinct") {
                        runner.run_distinct_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "distinct_where") {
                        runner.run_distinct_where_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "distinct_join") {
                        runner.run_distinct_join_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "distinct_where_join") {
                        runner.run_distinct_where_join_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "select_limit") {
                        runner.run_select_limit_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "select_offset") {
                        runner.run_select_offset_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "select_limit_offset") {
                        runner.run_select_limit_offset_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "select_where_limit") {
                        runner.run_select_where_limit_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "select_join_limit") {
                        runner.run_select_join_limit_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "select_join_limit_offset") {
                        runner.run_select_join_limit_offset_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "order_by_asc") {
                        runner.run_select_order_by_asc_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "order_by_desc") {
                        runner.run_select_order_by_desc_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "order_by_where") {
                        runner.run_select_order_by_where_operation<Model, test>(runner, actual_iterations);
                    } else if constexpr (operation == "order_by_limit") {
                        runner.run_select_order_by_limit_operation<Model, test>(runner, actual_iterations);
                    }
                }

                // Recurse to next test
                if constexpr (TestIndex + 1 < TotalTests) {
                    TestExecutor<Model, TestIndex + 1, TotalTests>::execute(
                            runner, iterations_override, filter, category, scale_test, mode
                    );
                }
            }
        };

        // Entry point for test execution (all tests)
        template <typename Model>
        auto run_all(int iterations_override = 0, BenchmarkMode mode = BenchmarkMode::Default) -> void {
            std::cout << "=== Running All Benchmark Tests (Compile-Time Dispatch) ===\n";
            std::cout << "Total tests: " << BENCHMARK_TESTS.size() << "\n";

            // Print mode/iterations info
            if (iterations_override > 0) {
                std::cout << "Iterations per test: " << iterations_override << " (override)\n";
            } else if (mode == BenchmarkMode::Quick) {
                std::cout << "Mode: Quick (0.3x JSON iterations)\n";
            } else if (mode == BenchmarkMode::Thorough) {
                std::cout << "Mode: Thorough (1.5x JSON iterations)\n";
            } else {
                std::cout << "Iterations: per-test from JSON\n";
            }
            std::cout << "Using compile-time JSON parsing with nested C++ structs\n\n";

            // Start template recursion from index 0
            TestExecutor<Model, 0, BENCHMARK_TESTS.size()>::execute(*this, iterations_override, "", "", false, mode);

            std::cout << "\n✅ All tests completed with COMPILE-TIME dispatch!\n";
            std::cout << "✅ Zero runtime string parsing overhead!\n";
            std::cout << "✅ Each test has its own specialized function!\n";
            std::cout << "✅ Tests loaded from JSON at compile time!\n";
        }

        // Entry point for filtered test execution
        template <typename Model>
        auto run_filtered(
                const std::string& filter,
                const std::string& category,
                int                iterations_override = 0,
                bool               scale_test          = false,
                BenchmarkMode      mode                = BenchmarkMode::Default
        ) -> void {
            std::cout << "=== Running Filtered Benchmark Tests ===\n";
            if (!category.empty()) {
                std::cout << "Category: \"" << category << "\"\n";
            }
            if (!filter.empty()) {
                std::cout << "Filter: \"" << filter << "\" (";
                std::cout << (scale_test ? "substring match" : "exact match") << ")\n";
            }

            // Print mode/iterations info
            if (iterations_override > 0) {
                std::cout << "Iterations override: " << iterations_override << "\n";
            } else if (mode == BenchmarkMode::Quick) {
                std::cout << "Mode: Quick (0.3x JSON iterations)\n";
            } else if (mode == BenchmarkMode::Thorough) {
                std::cout << "Mode: Thorough (1.5x JSON iterations)\n";
            }
            std::cout << "Using compile-time dispatch with runtime filtering\n\n";

            // Start template recursion with filter and category
            TestExecutor<Model, 0, BENCHMARK_TESTS.size()>::execute(
                    *this, iterations_override, filter, category, scale_test, mode
            );

            std::cout << "\n✅ Filtered tests completed!\n";
        }
    };

} // namespace storm::benchmark
