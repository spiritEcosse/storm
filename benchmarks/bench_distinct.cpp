#include <cstring>
#include <iostream>
#include <iomanip>
#include "benchmark_utils.hpp"

import storm;
import <string>;
import <vector>;
import <span>;
import <expected>;

using namespace storm;
using namespace storm::benchmark;

// Test model for DISTINCT benchmarks
struct Person {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

// Setup database with test data containing duplicates
void setup_database(int num_records, int num_unique_names = 100, int num_unique_ages = 50) {
    auto result = QuerySet<Person>::set_default_connection(":memory:");
    if (!result.has_value()) {
        throw std::runtime_error("Failed to open database");
    }

    auto& conn = QuerySet<Person>::get_default_connection();

    // Create table
    auto create_result = conn.execute(
            "CREATE TABLE Person ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "age INTEGER NOT NULL"
            ")"
    );
    if (!create_result.has_value()) {
        throw std::runtime_error("Failed to create Person table");
    }

    // Insert records with controlled duplicates
    QuerySet<Person> person_qs;
    std::vector<Person> people;
    people.reserve(num_records);
    for (int i = 0; i < num_records; ++i) {
        int name_idx = i % num_unique_names;
        int age_idx = i % num_unique_ages;
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

// Benchmark: Storm ORM DISTINCT on name field
void benchmark_storm_distinct_name(int num_records, int iterations = 100) {
    setup_database(num_records);

    QuerySet<Person> person_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs.distinct<&Person::name>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_results += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM DISTINCT (name) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_results / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Storm ORM DISTINCT on age field
void benchmark_storm_distinct_age(int num_records, int iterations = 100) {
    setup_database(num_records);

    QuerySet<Person> person_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs.distinct<&Person::age>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_results += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM DISTINCT (age) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_results / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Storm ORM DISTINCT on id field (primary key)
void benchmark_storm_distinct_id(int num_records, int iterations = 100) {
    setup_database(num_records);

    QuerySet<Person> person_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs.distinct().select();  // Defaults to PK
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_results += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM DISTINCT (id/PK) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_results / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite DISTINCT on name field
void benchmark_raw_distinct_name(int num_records, int iterations = 100) {
    setup_database(num_records);

    auto& conn = QuerySet<Person>::get_default_connection();
    std::string sql = "SELECT DISTINCT name FROM Person";

    BenchmarkTimer timer;
    double total_time = 0;
    int total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto stmt = std::move(stmt_result.value());
        std::vector<std::string> results;
        results.reserve(100);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                results.push_back(std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(0))));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_results += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite DISTINCT (name) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_results / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite DISTINCT on age field
void benchmark_raw_distinct_age(int num_records, int iterations = 100) {
    setup_database(num_records);

    auto& conn = QuerySet<Person>::get_default_connection();
    std::string sql = "SELECT DISTINCT age FROM Person";

    BenchmarkTimer timer;
    double total_time = 0;
    int total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto stmt = std::move(stmt_result.value());
        std::vector<int> results;
        results.reserve(50);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                results.push_back(stmt.extract_int(0));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_results += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite DISTINCT (age) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_results / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite DISTINCT on id field
void benchmark_raw_distinct_id(int num_records, int iterations = 100) {
    setup_database(num_records);

    auto& conn = QuerySet<Person>::get_default_connection();
    std::string sql = "SELECT DISTINCT id FROM Person";

    BenchmarkTimer timer;
    double total_time = 0;
    int total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto stmt = std::move(stmt_result.value());
        std::vector<int> results;
        results.reserve(num_records);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                results.push_back(stmt.extract_int(0));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_results += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite DISTINCT (id) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_results / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --size=N          Number of records (default: 10000)" << std::endl;
    std::cout << "  --iterations=N    Number of iterations (default: 100)" << std::endl;
    std::cout << std::endl;
    std::cout << "Benchmark Selection (run all if none specified):" << std::endl;
    std::cout << "  --storm-name      Run Storm ORM DISTINCT on name field" << std::endl;
    std::cout << "  --storm-age       Run Storm ORM DISTINCT on age field" << std::endl;
    std::cout << "  --storm-id        Run Storm ORM DISTINCT on id field (PK)" << std::endl;
    std::cout << "  --raw-name        Run raw SQL DISTINCT on name field" << std::endl;
    std::cout << "  --raw-age         Run raw SQL DISTINCT on age field" << std::endl;
    std::cout << "  --raw-id          Run raw SQL DISTINCT on id field" << std::endl;
    std::cout << std::endl;
    std::cout << "  --help, -h        Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    int test_size = 10000;
    int iterations = 100;

    // Benchmark selection flags
    bool run_storm_name = false;
    bool run_storm_age = false;
    bool run_storm_id = false;
    bool run_raw_name = false;
    bool run_raw_age = false;
    bool run_raw_id = false;
    bool run_all = true; // Default: run everything

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--size=", 7) == 0) {
            test_size = std::stoi(argv[i] + 7);
        } else if (strncmp(argv[i], "--iterations=", 13) == 0) {
            iterations = std::stoi(argv[i] + 13);
        } else if (strcmp(argv[i], "--storm-name") == 0) {
            run_storm_name = true;
            run_all = false;
        } else if (strcmp(argv[i], "--storm-age") == 0) {
            run_storm_age = true;
            run_all = false;
        } else if (strcmp(argv[i], "--storm-id") == 0) {
            run_storm_id = true;
            run_all = false;
        } else if (strcmp(argv[i], "--raw-name") == 0) {
            run_raw_name = true;
            run_all = false;
        } else if (strcmp(argv[i], "--raw-age") == 0) {
            run_raw_age = true;
            run_all = false;
        } else if (strcmp(argv[i], "--raw-id") == 0) {
            run_raw_id = true;
            run_all = false;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    std::cout << "====================================================" << std::endl;
    std::cout << "    Storm ORM DISTINCT Performance Benchmark" << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << std::endl;

    std::cout << "Testing with " << test_size << " records (iterations: " << iterations << ")" << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    std::cout << std::endl;

    // Run Storm ORM benchmarks
    if (run_all || run_storm_name || run_storm_age || run_storm_id) {
        std::cout << "--- Storm ORM DISTINCT Operations ---" << std::endl;
        if (run_all || run_storm_name) {
            benchmark_storm_distinct_name(test_size, iterations);
        }
        if (run_all || run_storm_age) {
            benchmark_storm_distinct_age(test_size, iterations);
        }
        if (run_all || run_storm_id) {
            benchmark_storm_distinct_id(test_size, iterations);
        }
        std::cout << std::endl;
    }

    // Run Raw SQLite benchmarks
    if (run_all || run_raw_name || run_raw_age || run_raw_id) {
        std::cout << "--- Raw SQLite DISTINCT Operations ---" << std::endl;
        if (run_all || run_raw_name) {
            benchmark_raw_distinct_name(test_size, iterations);
        }
        if (run_all || run_raw_age) {
            benchmark_raw_distinct_age(test_size, iterations);
        }
        if (run_all || run_raw_id) {
            benchmark_raw_distinct_id(test_size, iterations);
        }
        std::cout << std::endl;
    }

    std::cout << std::string(60, '=') << std::endl;
    std::cout << std::endl;

    return 0;
}
