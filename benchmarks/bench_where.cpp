#include <cstring>
#include <iostream>
#include <iomanip>
#include "benchmark_utils.hpp"

import storm;
import <string>;
import <vector>;
import <span>;
import <expected>;
import <variant>;
import <memory>;

using namespace storm;
using namespace storm::benchmark;
using namespace storm::orm::where;

// Test model for WHERE benchmarks
struct Person {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

// Setup database with test data
void setup_database(int num_people) {
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
        throw std::runtime_error("Failed to create table");
    }

    // Insert people
    QuerySet<Person> person_qs;
    std::vector<Person> people;
    people.reserve(num_people);
    for (int i = 0; i < num_people; ++i) {
        people.push_back({0, "Person" + std::to_string(i), 20 + (i % 50)});
    }

    auto insert_result = person_qs.insert(std::span<const Person>(people));
    if (!insert_result.has_value()) {
        throw std::runtime_error("Failed to insert people");
    }
}

void teardown_database() {
    QuerySet<Person>::clear_default_connection();
}

// Benchmark: SELECT without WHERE (baseline)
void benchmark_select_no_where(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<Person> person_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs.select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "SELECT (no WHERE) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Storm ORM SELECT with single WHERE condition
void benchmark_storm_where_single(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<Person> person_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs.where(field<^^Person::age>() > 30).select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM WHERE (single condition: age > 30) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite SELECT with single WHERE condition
void benchmark_raw_where_single(int num_people, int iterations = 100) {
    setup_database(num_people);

    auto& conn = QuerySet<Person>::get_default_connection();
    std::string sql = "SELECT id, name, age FROM Person WHERE age > ?";

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto stmt = std::move(stmt_result.value());
        stmt.bind_int(1, 30);

        std::vector<Person> results;
        results.reserve(num_people);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                Person p;
                p.id = stmt.extract_int(0);
                p.name = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                p.age = stmt.extract_int(2);
                results.push_back(std::move(p));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_rows += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite WHERE (single condition: age > 30) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Storm ORM SELECT with multiple WHERE conditions
void benchmark_storm_where_multiple(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<Person> person_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto expr = and_(field<^^Person::age>() > 25, field<^^Person::age>() < 50);
        auto result = person_qs.where(expr).select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM WHERE (multiple conditions: age > 25 AND age < 50) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite SELECT with multiple WHERE conditions
void benchmark_raw_where_multiple(int num_people, int iterations = 100) {
    setup_database(num_people);

    auto& conn = QuerySet<Person>::get_default_connection();
    std::string sql = "SELECT id, name, age FROM Person WHERE age > ? AND age < ?";

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto stmt = std::move(stmt_result.value());
        stmt.bind_int(1, 25);
        stmt.bind_int(2, 50);

        std::vector<Person> results;
        results.reserve(num_people);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                Person p;
                p.id = stmt.extract_int(0);
                p.name = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                p.age = stmt.extract_int(2);
                results.push_back(std::move(p));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_rows += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite WHERE (multiple conditions: age > 25 AND age < 50) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --size=N          Number of rows (default: 10000)" << std::endl;
    std::cout << "  --iterations=N    Number of iterations (default: 100)" << std::endl;
    std::cout << std::endl;
    std::cout << "Benchmark Selection (run all if none specified):" << std::endl;
    std::cout << "  --storm-single    Run Storm ORM single WHERE condition" << std::endl;
    std::cout << "  --storm-multiple  Run Storm ORM multiple WHERE conditions" << std::endl;
    std::cout << "  --raw-single      Run raw SQL single WHERE condition" << std::endl;
    std::cout << "  --raw-multiple    Run raw SQL multiple WHERE conditions" << std::endl;
    std::cout << std::endl;
    std::cout << "  --help, -h        Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    int test_size = 10000;
    int iterations = 100;

    // Benchmark selection flags
    bool run_storm_single = false;
    bool run_storm_multiple = false;
    bool run_raw_single = false;
    bool run_raw_multiple = false;
    bool run_all = true; // Default: run everything

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--size=", 7) == 0) {
            test_size = std::stoi(argv[i] + 7);
        } else if (strncmp(argv[i], "--iterations=", 13) == 0) {
            iterations = std::stoi(argv[i] + 13);
        } else if (strcmp(argv[i], "--storm-single") == 0) {
            run_storm_single = true;
            run_all = false;
        } else if (strcmp(argv[i], "--storm-multiple") == 0) {
            run_storm_multiple = true;
            run_all = false;
        } else if (strcmp(argv[i], "--raw-single") == 0) {
            run_raw_single = true;
            run_all = false;
        } else if (strcmp(argv[i], "--raw-multiple") == 0) {
            run_raw_multiple = true;
            run_all = false;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    std::cout << "====================================================" << std::endl;
    std::cout << "     Storm ORM WHERE Performance Benchmark" << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << std::endl;

    std::cout << "Testing with " << test_size << " rows (iterations: " << iterations << ")" << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    std::cout << std::endl;

    // Always run baseline for context
    benchmark_select_no_where(test_size, iterations);

    // Storm ORM WHERE benchmarks
    if (run_all || run_storm_single) {
        benchmark_storm_where_single(test_size, iterations);
    }
    if (run_all || run_storm_multiple) {
        benchmark_storm_where_multiple(test_size, iterations);
    }

    // Raw SQLite WHERE benchmarks
    if (run_all || run_raw_single) {
        benchmark_raw_where_single(test_size, iterations);
    }
    if (run_all || run_raw_multiple) {
        benchmark_raw_where_multiple(test_size, iterations);
    }

    // Calculate efficiency if running comparison
    if ((run_all || (run_storm_single && run_raw_single)) || (run_all || (run_storm_multiple && run_raw_multiple))) {
        std::cout << std::string(60, '=') << std::endl;
        std::cout << "Note: Compare Storm ORM vs Raw SQLite throughput" << std::endl;
        std::cout << "to calculate efficiency percentage." << std::endl;
        std::cout << std::string(60, '=') << std::endl;
    }

    return 0;
}
