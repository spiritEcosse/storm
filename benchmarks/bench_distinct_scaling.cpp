#include <cstring>
import <memory>;
#include <iostream>
#include <iomanip>
#include <cmath>
#include "benchmark_utils.hpp"

import storm;
import <string>;
import <vector>;
import <span>;
import <expected>;
import <tuple>;
import <meta>;

using namespace storm;
using namespace storm::benchmark;

// Test model for DISTINCT benchmarks
struct Person {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

// Setup database with controlled unique value counts
void setup_database(int num_records, int num_unique_combos) {
    auto result = QuerySet<Person>::set_default_connection(":memory:");
    if (!result.has_value()) {
        throw std::runtime_error("Failed to open database");
    }

    auto& conn = QuerySet<Person>::get_default_connection();

    // Create table
    auto create_result = conn->execute(
            "CREATE TABLE Person ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "age INTEGER NOT NULL"
            ")"
    );
    if (!create_result.has_value()) {
        throw std::runtime_error("Failed to create Person table");
    }

    // Insert records with controlled number of unique (name, age) combinations
    // Strategy: cycle through unique_combos different combinations
    QuerySet<Person>    person_qs;
    std::vector<Person> people;
    people.reserve(num_records);

    for (int i = 0; i < num_records; ++i) {
        int combo_idx = i % num_unique_combos;
        // Use square root distribution for balanced cardinality
        int name_factor = static_cast<int>(std::sqrt(num_unique_combos));
        int age_factor  = (num_unique_combos + name_factor - 1) / name_factor;
        int name_idx    = combo_idx % name_factor;
        int age_idx     = combo_idx / name_factor;
        people.push_back({0, "Person" + std::to_string(name_idx), 20 + age_idx});
    }

    auto insert_result = person_qs.insert(std::span<const Person>(people));
    if (!insert_result.has_value()) {
        throw std::runtime_error("Failed to insert people");
    }
}

void teardown_database() {
    QuerySet<Person>::clear_default_connection();
}

// Benchmark Storm ORM DISTINCT with specific field(s)
template <std::meta::info... FieldInfos>
void benchmark_storm_distinct(const char* field_desc, int num_records, int num_unique_combos, int iterations = 100) {
    setup_database(num_records, num_unique_combos);

    QuerySet<Person> person_qs;
    BenchmarkTimer   timer;
    double           total_time    = 0;
    int              total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        auto result = [&]() {
            if constexpr (sizeof...(FieldInfos) == 0) {
                // Default to PK
                return person_qs.distinct().select();
            } else {
                return person_qs.distinct<FieldInfos...>().select();
            }
        }();

        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_results += result.value().size();
            total_time += elapsed;
        }
    }

    double avg_time_ms     = total_time / iterations;
    double throughput      = total_results / (total_time / 1000.0);
    int    avg_result_size = total_results / iterations;

    std::cout << "  " << std::setw(25) << std::left << field_desc << " | Avg: " << std::fixed << std::setprecision(2)
              << std::setw(6) << avg_time_ms << " ms"
              << " | Results: " << std::setw(6) << avg_result_size << " | Throughput: " << std::setprecision(2)
              << std::setw(8) << (throughput / 1000000.0) << " M/s"
              << " | Efficiency: ";

    teardown_database();
}

// Benchmark Raw SQLite DISTINCT
void benchmark_raw_distinct(
        const char* field_desc,
        const char* sql,
        int         num_records,
        int         num_unique_combos,
        int         num_fields,
        int         iterations = 100
) {
    setup_database(num_records, num_unique_combos);

    auto&          conn = QuerySet<Person>::get_default_connection();
    BenchmarkTimer timer;
    double         total_time    = 0;
    int            total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto stmt_result = conn->prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto stmt      = std::move(stmt_result.value());
        int  row_count = 0;

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                // Extract based on field count (just for side effects)
                if (num_fields == 1) {
                    volatile auto val = stmt.extract_int(0);
                } else if (num_fields == 2) {
                    volatile auto val1 = stmt.extract_text_ptr(0);
                    volatile auto val2 = stmt.extract_int(1);
                } else {
                    volatile auto val1 = stmt.extract_int(0);
                    volatile auto val2 = stmt.extract_text_ptr(1);
                    volatile auto val3 = stmt.extract_int(2);
                }
                row_count++;
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_results += row_count;
        total_time += elapsed;
    }

    double avg_time_ms = total_time / iterations;
    double throughput  = total_results / (total_time / 1000.0);

    std::cout << std::fixed << std::setprecision(2) << std::setw(8) << (throughput / 1000000.0) << " M/s" << std::endl;

    teardown_database();
}

void run_scaling_test(int num_records, int num_unique_combos, int iterations) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Result Size: ~" << num_unique_combos << " unique rows (" << num_records << " total records)"
              << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Single-field DISTINCT tests
    std::cout << "Single-Field DISTINCT:" << std::endl;

    benchmark_storm_distinct<^^Person::name>("Storm: name", num_records, num_unique_combos, iterations);
    benchmark_raw_distinct(
            "Raw: name", "SELECT DISTINCT name FROM Person", num_records, num_unique_combos, 1, iterations
    );

    benchmark_storm_distinct<^^Person::age>("Storm: age", num_records, num_unique_combos, iterations);
    benchmark_raw_distinct(
            "Raw: age", "SELECT DISTINCT age FROM Person", num_records, num_unique_combos, 1, iterations
    );

    benchmark_storm_distinct<>("Storm: id (PK)", num_records, num_unique_combos, iterations);
    benchmark_raw_distinct("Raw: id", "SELECT DISTINCT id FROM Person", num_records, num_unique_combos, 1, iterations);

    // Multi-field DISTINCT tests
    std::cout << "\nMulti-Field DISTINCT:" << std::endl;

    benchmark_storm_distinct<^^Person::name, ^^Person::age>(
            "Storm: name, age", num_records, num_unique_combos, iterations
    );
    benchmark_raw_distinct(
            "Raw: name, age", "SELECT DISTINCT name, age FROM Person", num_records, num_unique_combos, 2, iterations
    );

    benchmark_storm_distinct<^^Person::id, ^^Person::name, ^^Person::age>(
            "Storm: id, name, age", num_records, num_unique_combos, iterations
    );
    benchmark_raw_distinct(
            "Raw: id, name, age",
            "SELECT DISTINCT id, name, age FROM Person",
            num_records,
            num_unique_combos,
            3,
            iterations
    );
}

int main(int argc, char* argv[]) {
    int num_records = 10000;
    int iterations  = 100;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--records=", 10) == 0) {
            num_records = std::stoi(argv[i] + 10);
        } else if (strncmp(argv[i], "--iterations=", 13) == 0) {
            iterations = std::stoi(argv[i] + 13);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --records=N        Number of records to insert (default: 10000)" << std::endl;
            std::cout << "  --iterations=N     Number of iterations per test (default: 100)" << std::endl;
            std::cout << "  --help, -h         Show this help message" << std::endl;
            return 0;
        }
    }

    std::cout << "=====================================================" << std::endl;
    std::cout << "  Storm ORM DISTINCT Scaling Performance Benchmark" << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "\nTotal records: " << num_records << ", Iterations: " << iterations << std::endl;

    // Test with different result sizes to show scaling behavior
    run_scaling_test(num_records, 100, iterations);         // Small result set
    run_scaling_test(num_records, 1000, iterations);        // Medium result set
    run_scaling_test(num_records, num_records, iterations); // Large result set (all unique)

    std::cout << "\n=====================================================" << std::endl;

    return 0;
}
