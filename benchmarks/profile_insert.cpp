/**
 * Detailed profiling of INSERT operations to identify bottlenecks
 */

import <chrono>;
import <iostream>;
import <iomanip>;
import <vector>;
import <string>;
import <expected>;
import <memory>;
import <span>;

#include <sqlite3.h>

import storm;

using namespace storm;

struct Person {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string name;
    int age;
    bool is_active;
    double salary;
};

// High-resolution timer
class Timer {
    std::chrono::high_resolution_clock::time_point start_;
public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}

    double elapsed_us() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count() / 1000.0;
    }

    void reset() {
        start_ = std::chrono::high_resolution_clock::now();
    }
};

void profile_storm_insert_batch_100() {
    std::cout << "\n=== Profiling Storm ORM Batch INSERT (100 rows) ===\n\n";

    // Setup
    auto result = QuerySet<Person>::set_default_connection(":memory:");
    if (!result) {
        std::cerr << "Failed to create connection\n";
        return;
    }

    auto& conn = QuerySet<Person>::get_default_connection();
    (void)conn->execute("CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                        "name TEXT, age INTEGER, is_active INTEGER, salary REAL)");

    // Prepare data
    std::vector<Person> people;
    people.reserve(100);
    for (int i = 0; i < 100; i++) {
        people.push_back({0, "BenchPerson", 30, true, 50000.0});
    }

    QuerySet<Person> qs;

    // Warmup
    for (int i = 0; i < 10; i++) {
        qs.insert(std::span<const Person>{people});
    }

    // Profile multiple iterations
    std::cout << "Running 1000 iterations...\n\n";

    double total_time = 0;
    Timer overall_timer;

    for (int iter = 0; iter < 1000; iter++) {
        Timer iter_timer;
        auto insert_result = qs.insert(std::span<const Person>{people});
        double elapsed = iter_timer.elapsed_us();
        total_time += elapsed;

        if (!insert_result) {
            std::cerr << "Insert failed at iteration " << iter << "\n";
            break;
        }
    }

    double total_ms = overall_timer.elapsed_us() / 1000.0;

    std::cout << "Results:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_ms << " ms\n";
    std::cout << "  Average per batch: " << std::fixed << std::setprecision(3)
              << (total_time / 1000.0 / 1000.0) << " ms\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (100000.0 / (total_ms / 1000.0)) << " rows/sec\n";

    QuerySet<Person>::clear_default_connection();
}

void profile_raw_sqlite_batch_100() {
    std::cout << "\n=== Profiling Raw SQLite Batch INSERT (100 rows) ===\n\n";

    sqlite3* db;
    sqlite3_open(":memory:", &db);

    sqlite3_exec(db, "CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                     "name TEXT, age INTEGER, is_active INTEGER, salary REAL)",
                 nullptr, nullptr, nullptr);

    // Build bulk SQL
    std::string sql = "INSERT INTO Person (id, name, age, is_active, salary) VALUES ";
    for (int i = 0; i < 100; i++) {
        if (i > 0) sql += ", ";
        sql += "(NULL, ?, ?, ?, ?)";
    }

    // Prepare statement
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);

    // Warmup
    for (int i = 0; i < 10; i++) {
        int idx = 1;
        for (int j = 0; j < 100; j++) {
            sqlite3_bind_text(stmt, idx++, "BenchPerson", -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, idx++, 30);
            sqlite3_bind_int(stmt, idx++, 1);
            sqlite3_bind_double(stmt, idx++, 50000.0);
        }
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    // Profile
    std::cout << "Running 1000 iterations...\n\n";

    double total_time = 0;
    Timer overall_timer;

    for (int iter = 0; iter < 1000; iter++) {
        Timer iter_timer;

        int idx = 1;
        for (int j = 0; j < 100; j++) {
            sqlite3_bind_text(stmt, idx++, "BenchPerson", -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, idx++, 30);
            sqlite3_bind_int(stmt, idx++, 1);
            sqlite3_bind_double(stmt, idx++, 50000.0);
        }
        sqlite3_step(stmt);
        sqlite3_reset(stmt);

        double elapsed = iter_timer.elapsed_us();
        total_time += elapsed;
    }

    double total_ms = overall_timer.elapsed_us() / 1000.0;

    std::cout << "Results:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_ms << " ms\n";
    std::cout << "  Average per batch: " << std::fixed << std::setprecision(3)
              << (total_time / 1000.0 / 1000.0) << " ms\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (100000.0 / (total_ms / 1000.0)) << " rows/sec\n";

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

// Break down Storm ORM overhead by measuring individual components
void profile_storm_components() {
    std::cout << "\n=== Breaking Down Storm ORM Overhead ===\n\n";

    // Setup
    auto result = QuerySet<Person>::set_default_connection(":memory:");
    auto& conn = QuerySet<Person>::get_default_connection();
    (void)conn->execute("CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                        "name TEXT, age INTEGER, is_active INTEGER, salary REAL)");

    std::vector<Person> people;
    people.reserve(100);
    for (int i = 0; i < 100; i++) {
        people.push_back({0, "BenchPerson", 30, true, 50000.0});
    }

    // We'll measure by comparing execution times with different data sizes
    // to isolate SQL generation vs binding vs execution overhead

    std::cout << "Testing different batch sizes to isolate overhead sources:\n\n";

    QuerySet<Person> qs;

    for (int size : {10, 50, 100}) {
        std::vector<Person> batch(people.begin(), people.begin() + size);

        // Warmup
        for (int i = 0; i < 10; i++) {
            qs.insert(std::span<const Person>{batch});
        }

        Timer timer;
        for (int i = 0; i < 100; i++) {
            qs.insert(std::span<const Person>{batch});
        }
        double elapsed = timer.elapsed_us();

        std::cout << "  Batch size " << std::setw(3) << size << ": "
                  << std::fixed << std::setprecision(3)
                  << (elapsed / 100.0 / 1000.0) << " ms/batch, "
                  << std::setprecision(2)
                  << (size * 100.0 / (elapsed / 1000000.0)) << " rows/sec\n";
    }

    QuerySet<Person>::clear_default_connection();
}

int main() {
    std::cout << "Storm ORM INSERT Profiling Tool\n";
    std::cout << "================================\n";

    profile_storm_insert_batch_100();
    profile_raw_sqlite_batch_100();
    profile_storm_components();

    return 0;
}
