#include <cstring>
#include <iostream>
#include <iomanip>
#include <sqlite3.h>
#include "benchmark_utils.hpp"

import storm;
import <string>;
import <vector>;
import <span>;
import <expected>;
import <tuple>;

using namespace storm;
using namespace storm::benchmark;

// Test model for aggregate benchmarks
struct Person {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string name;
    int age;
    double salary;
    int years_experience;
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
            "age INTEGER NOT NULL, "
            "salary REAL NOT NULL, "
            "years_experience INTEGER NOT NULL"
            ")"
    );
    if (!create_result.has_value()) {
        throw std::runtime_error("Failed to create table");
    }

    // Insert test data
    QuerySet<Person> qs;
    std::vector<Person> people;
    people.reserve(num_people);
    for (int i = 0; i < num_people; ++i) {
        people.push_back({
            0,
            "Person" + std::to_string(i),
            20 + (i % 50),                    // age: 20-69
            50000.0 + (i % 100) * 1000.0,     // salary: 50k-149k
            (i % 20)                          // years_experience: 0-19
        });
    }

    auto insert_result = qs.insert(std::span<const Person>(people));
    if (!insert_result.has_value()) {
        throw std::runtime_error("Failed to insert people");
    }
}

void teardown_database() {
    QuerySet<Person>::clear_default_connection();
}

// ============================================================================
// Storm ORM Aggregate Benchmarks
// ============================================================================

void benchmark_storm_sum(int num_people, int iterations) {
    setup_database(num_people);
    QuerySet<Person> qs;

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = qs.sum<^^Person::age>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += num_people;
            total_time += elapsed;
        }
    }

    std::cout << "Storm SUM(age) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void benchmark_storm_count(int num_people, int iterations) {
    setup_database(num_people);
    QuerySet<Person> qs;

    BenchmarkTimer timer;
    double total_time = 0;
    int successful_queries = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = qs.count().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            successful_queries++;
            total_time += elapsed;
        }
    }

    std::cout << "Storm COUNT(*) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (successful_queries / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void benchmark_storm_avg(int num_people, int iterations) {
    setup_database(num_people);
    QuerySet<Person> qs;

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = qs.avg<^^Person::salary>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += num_people;
            total_time += elapsed;
        }
    }

    std::cout << "Storm AVG(salary) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void benchmark_storm_min(int num_people, int iterations) {
    setup_database(num_people);
    QuerySet<Person> qs;

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = qs.min<^^Person::age>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += num_people;
            total_time += elapsed;
        }
    }

    std::cout << "Storm MIN(age) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void benchmark_storm_max(int num_people, int iterations) {
    setup_database(num_people);
    QuerySet<Person> qs;

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = qs.max<^^Person::salary>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += num_people;
            total_time += elapsed;
        }
    }

    std::cout << "Storm MAX(salary) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void benchmark_storm_multi_aggregate(int num_people, int iterations) {
    setup_database(num_people);
    QuerySet<Person> qs;

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = qs.aggregate()
                          .sum<^^Person::age>()
                          .count()
                          .avg<^^Person::salary>()
                          .select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += num_people;
            total_time += elapsed;
        }
    }

    std::cout << "Storm Multi-Aggregate - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void benchmark_storm_sum_multi_field(int num_people, int iterations) {
    setup_database(num_people);
    QuerySet<Person> qs;

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = qs.sum<^^Person::age, ^^Person::years_experience>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += num_people;
            total_time += elapsed;
        }
    }

    std::cout << "Storm SUM(age+years) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// ============================================================================
// Raw SQLite Aggregate Benchmarks
// ============================================================================

void benchmark_raw_sum(int num_people, int iterations) {
    setup_database(num_people);
    auto& conn = QuerySet<Person>::get_default_connection();

    auto stmt_result = conn.prepare_cached("SELECT SUM(age) FROM Person");
    if (!stmt_result.has_value()) {
        throw std::runtime_error("Prepare failed");
    }
    auto stmt = stmt_result.value();

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        auto step_result = stmt->step();
        if (!step_result.has_value() || !step_result.value()) {
            throw std::runtime_error("Step failed");
        }

        int64_t sum = sqlite3_column_int64(stmt->handle(), 0);
        stmt->reset();
        double elapsed = timer.elapsed_ms();

        total_rows += num_people;
        total_time += elapsed;
    }

    std::cout << "Raw SQLite SUM(age) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void benchmark_raw_count(int num_people, int iterations) {
    setup_database(num_people);
    auto& conn = QuerySet<Person>::get_default_connection();

    auto stmt_result = conn.prepare_cached("SELECT COUNT(*) FROM Person");
    if (!stmt_result.has_value()) {
        throw std::runtime_error("Prepare failed");
    }
    auto stmt = stmt_result.value();

    BenchmarkTimer timer;
    double total_time = 0;
    int successful_queries = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        auto step_result = stmt->step();
        if (!step_result.has_value() || !step_result.value()) {
            throw std::runtime_error("Step failed");
        }

        int64_t count = sqlite3_column_int64(stmt->handle(), 0);
        stmt->reset();
        double elapsed = timer.elapsed_ms();

        successful_queries++;
        total_time += elapsed;
    }

    std::cout << "Raw SQLite COUNT(*) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (successful_queries / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void benchmark_raw_avg(int num_people, int iterations) {
    setup_database(num_people);
    auto& conn = QuerySet<Person>::get_default_connection();

    auto stmt_result = conn.prepare_cached("SELECT AVG(salary) FROM Person");
    if (!stmt_result.has_value()) {
        throw std::runtime_error("Prepare failed");
    }
    auto stmt = stmt_result.value();

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        auto step_result = stmt->step();
        if (!step_result.has_value() || !step_result.value()) {
            throw std::runtime_error("Step failed");
        }

        double avg = sqlite3_column_double(stmt->handle(), 0);
        stmt->reset();
        double elapsed = timer.elapsed_ms();

        total_rows += num_people;
        total_time += elapsed;
    }

    std::cout << "Raw SQLite AVG(salary) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void benchmark_raw_min(int num_people, int iterations) {
    setup_database(num_people);
    auto& conn = QuerySet<Person>::get_default_connection();

    auto stmt_result = conn.prepare_cached("SELECT MIN(age) FROM Person");
    if (!stmt_result.has_value()) {
        throw std::runtime_error("Prepare failed");
    }
    auto stmt = stmt_result.value();

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        auto step_result = stmt->step();
        if (!step_result.has_value() || !step_result.value()) {
            throw std::runtime_error("Step failed");
        }

        int min_age = sqlite3_column_int(stmt->handle(), 0);
        stmt->reset();
        double elapsed = timer.elapsed_ms();

        total_rows += num_people;
        total_time += elapsed;
    }

    std::cout << "Raw SQLite MIN(age) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void benchmark_raw_max(int num_people, int iterations) {
    setup_database(num_people);
    auto& conn = QuerySet<Person>::get_default_connection();

    auto stmt_result = conn.prepare_cached("SELECT MAX(salary) FROM Person");
    if (!stmt_result.has_value()) {
        throw std::runtime_error("Prepare failed");
    }
    auto stmt = stmt_result.value();

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        auto step_result = stmt->step();
        if (!step_result.has_value() || !step_result.value()) {
            throw std::runtime_error("Step failed");
        }

        double max_salary = sqlite3_column_double(stmt->handle(), 0);
        stmt->reset();
        double elapsed = timer.elapsed_ms();

        total_rows += num_people;
        total_time += elapsed;
    }

    std::cout << "Raw SQLite MAX(salary) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void benchmark_raw_multi_aggregate(int num_people, int iterations) {
    setup_database(num_people);
    auto& conn = QuerySet<Person>::get_default_connection();

    auto stmt_result = conn.prepare_cached("SELECT SUM(age), COUNT(*), AVG(salary) FROM Person");
    if (!stmt_result.has_value()) {
        throw std::runtime_error("Prepare failed");
    }
    auto stmt = stmt_result.value();

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        auto step_result = stmt->step();
        if (!step_result.has_value() || !step_result.value()) {
            throw std::runtime_error("Step failed");
        }

        int64_t sum = sqlite3_column_int64(stmt->handle(), 0);
        int64_t count = sqlite3_column_int64(stmt->handle(), 1);
        double avg = sqlite3_column_double(stmt->handle(), 2);
        stmt->reset();
        double elapsed = timer.elapsed_ms();

        total_rows += num_people;
        total_time += elapsed;
    }

    std::cout << "Raw SQLite Multi-Aggregate - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void benchmark_raw_sum_multi_field(int num_people, int iterations) {
    setup_database(num_people);
    auto& conn = QuerySet<Person>::get_default_connection();

    auto stmt_result = conn.prepare_cached("SELECT SUM(age), SUM(years_experience) FROM Person");
    if (!stmt_result.has_value()) {
        throw std::runtime_error("Prepare failed");
    }
    auto stmt = stmt_result.value();

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        auto step_result = stmt->step();
        if (!step_result.has_value() || !step_result.value()) {
            throw std::runtime_error("Step failed");
        }

        int64_t sum_age = sqlite3_column_int64(stmt->handle(), 0);
        int64_t sum_years = sqlite3_column_int64(stmt->handle(), 1);
        stmt->reset();
        double elapsed = timer.elapsed_ms();

        total_rows += num_people;
        total_time += elapsed;
    }

    std::cout << "Raw SQLite SUM(age+years) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void print_help() {
    std::cout << "Usage: bench_aggregate [options]\n"
              << "Options:\n"
              << "  --size=N         Number of rows (default: 10000)\n"
              << "  --iterations=N   Number of iterations (default: 100)\n"
              << "  --storm-sum      Run Storm SUM benchmark\n"
              << "  --storm-count    Run Storm COUNT benchmark\n"
              << "  --storm-avg      Run Storm AVG benchmark\n"
              << "  --storm-min      Run Storm MIN benchmark\n"
              << "  --storm-max      Run Storm MAX benchmark\n"
              << "  --storm-multi    Run Storm multi-aggregate benchmark\n"
              << "  --storm-sum-multi Run Storm multi-field SUM benchmark\n"
              << "  --raw-sum        Run raw SQLite SUM benchmark\n"
              << "  --raw-count      Run raw SQLite COUNT benchmark\n"
              << "  --raw-avg        Run raw SQLite AVG benchmark\n"
              << "  --raw-min        Run raw SQLite MIN benchmark\n"
              << "  --raw-max        Run raw SQLite MAX benchmark\n"
              << "  --raw-multi      Run raw SQLite multi-aggregate benchmark\n"
              << "  --raw-sum-multi  Run raw SQLite multi-field SUM benchmark\n"
              << "  --all            Run all benchmarks\n"
              << "  --compare        Run comparison (Storm vs Raw)\n"
              << "  --help           Show this help message\n";
}

int main(int argc, char* argv[]) {
    int num_people = 10000;
    int iterations = 100;
    bool run_all = false;
    bool run_compare = false;
    bool run_storm_sum = false;
    bool run_storm_count = false;
    bool run_storm_avg = false;
    bool run_storm_min = false;
    bool run_storm_max = false;
    bool run_storm_multi = false;
    bool run_storm_sum_multi = false;
    bool run_raw_sum = false;
    bool run_raw_count = false;
    bool run_raw_avg = false;
    bool run_raw_min = false;
    bool run_raw_max = false;
    bool run_raw_multi = false;
    bool run_raw_sum_multi = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.find("--size=") == 0) {
            num_people = std::stoi(arg.substr(7));
        } else if (arg.find("--iterations=") == 0) {
            iterations = std::stoi(arg.substr(13));
        } else if (arg == "--all") {
            run_all = true;
        } else if (arg == "--compare") {
            run_compare = true;
        } else if (arg == "--storm-sum") {
            run_storm_sum = true;
        } else if (arg == "--storm-count") {
            run_storm_count = true;
        } else if (arg == "--storm-avg") {
            run_storm_avg = true;
        } else if (arg == "--storm-min") {
            run_storm_min = true;
        } else if (arg == "--storm-max") {
            run_storm_max = true;
        } else if (arg == "--storm-multi") {
            run_storm_multi = true;
        } else if (arg == "--storm-sum-multi") {
            run_storm_sum_multi = true;
        } else if (arg == "--raw-sum") {
            run_raw_sum = true;
        } else if (arg == "--raw-count") {
            run_raw_count = true;
        } else if (arg == "--raw-avg") {
            run_raw_avg = true;
        } else if (arg == "--raw-min") {
            run_raw_min = true;
        } else if (arg == "--raw-max") {
            run_raw_max = true;
        } else if (arg == "--raw-multi") {
            run_raw_multi = true;
        } else if (arg == "--raw-sum-multi") {
            run_raw_sum_multi = true;
        } else if (arg == "--help") {
            print_help();
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_help();
            return 1;
        }
    }

    // Default to running all if no specific benchmarks selected
    if (!run_all && !run_compare &&
        !run_storm_sum && !run_storm_count && !run_storm_avg &&
        !run_storm_min && !run_storm_max && !run_storm_multi && !run_storm_sum_multi &&
        !run_raw_sum && !run_raw_count && !run_raw_avg &&
        !run_raw_min && !run_raw_max && !run_raw_multi && !run_raw_sum_multi) {
        run_all = true;
    }

    std::cout << "Aggregate Function Benchmarks\n";
    std::cout << "==============================\n";
    std::cout << "Rows: " << num_people << ", Iterations: " << iterations << "\n\n";

    // Run benchmarks
    if (run_all || run_compare || run_storm_sum) {
        benchmark_storm_sum(num_people, iterations);
    }

    if (run_all || run_compare || run_raw_sum) {
        benchmark_raw_sum(num_people, iterations);
    }

    if (run_all || run_compare || run_storm_count) {
        benchmark_storm_count(num_people, iterations);
    }

    if (run_all || run_compare || run_raw_count) {
        benchmark_raw_count(num_people, iterations);
    }

    if (run_all || run_compare || run_storm_avg) {
        benchmark_storm_avg(num_people, iterations);
    }

    if (run_all || run_compare || run_raw_avg) {
        benchmark_raw_avg(num_people, iterations);
    }

    if (run_all || run_compare || run_storm_min) {
        benchmark_storm_min(num_people, iterations);
    }

    if (run_all || run_compare || run_raw_min) {
        benchmark_raw_min(num_people, iterations);
    }

    if (run_all || run_compare || run_storm_max) {
        benchmark_storm_max(num_people, iterations);
    }

    if (run_all || run_compare || run_raw_max) {
        benchmark_raw_max(num_people, iterations);
    }

    if (run_all || run_compare || run_storm_multi) {
        benchmark_storm_multi_aggregate(num_people, iterations);
    }

    if (run_all || run_compare || run_raw_multi) {
        benchmark_raw_multi_aggregate(num_people, iterations);
    }

    if (run_all || run_storm_sum_multi) {
        benchmark_storm_sum_multi_field(num_people, iterations);
    }

    if (run_all || run_raw_sum_multi) {
        benchmark_raw_sum_multi_field(num_people, iterations);
    }

    return 0;
}
