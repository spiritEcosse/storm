#include <chrono>
#include <iostream>
#include <vector>
#include <iomanip>
#include <string>
#include <sqlite3.h>

class BenchmarkTimer {
public:
    BenchmarkTimer() : start_(std::chrono::high_resolution_clock::now()) {}

    double elapsed_ms() const {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
        return duration.count() / 1000.0;
    }

    void reset() {
        start_ = std::chrono::high_resolution_clock::now();
    }

private:
    std::chrono::high_resolution_clock::time_point start_;
};

// Benchmark using pure sqlite3 (most basic approach)
void benchmark_pure_sqlite_remove(int num_records) {
    sqlite3* db;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    // Create table
    const char* create_sql = "CREATE TABLE Person (id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER NOT NULL)";
    rc = sqlite3_exec(db, create_sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return;
    }

    // Insert test data using exec
    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
    for (int i = 1; i <= num_records; ++i) {
        std::string insert_sql = "INSERT INTO Person (id, name, age) VALUES (" +
                                std::to_string(i) + ", 'Person" + std::to_string(i) + "', " +
                                std::to_string(20 + (i % 50)) + ")";
        sqlite3_exec(db, insert_sql.c_str(), nullptr, nullptr, nullptr);
    }
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

    // Benchmark removal using simple exec calls
    BenchmarkTimer timer;
    double total_time = 0;
    int successful_removes = 0;

    for (int i = 1; i <= num_records; ++i) {
        timer.reset();

        std::string delete_sql = "DELETE FROM Person WHERE id = " + std::to_string(i);
        rc = sqlite3_exec(db, delete_sql.c_str(), nullptr, nullptr, nullptr);

        double elapsed = timer.elapsed_ms();

        if (rc == SQLITE_OK) {
            successful_removes++;
            total_time += elapsed;
        }
    }

    // Report results
    std::cout << "Pure SQLite3 - Remove " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
    std::cout << "  Average per remove: " << std::fixed << std::setprecision(4)
              << (total_time / successful_removes) << " ms\n";
    std::cout << "  Successful removes: " << successful_removes << "/" << num_records << "\n";

    sqlite3_close(db);
}

// Benchmark using raw SQLite (representing Storm's approach)
void benchmark_raw_sqlite_remove(int num_records) {
    sqlite3* db;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    // Create table
    const char* create_sql = "CREATE TABLE Person (id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER NOT NULL)";
    rc = sqlite3_exec(db, create_sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return;
    }

    // Prepare data and insert
    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

    sqlite3_stmt* insert_stmt;
    sqlite3_prepare_v2(db, "INSERT INTO Person (id, name, age) VALUES (?, ?, ?)", -1, &insert_stmt, nullptr);

    for (int i = 1; i <= num_records; ++i) {
        sqlite3_bind_int(insert_stmt, 1, i);
        std::string name = "Person" + std::to_string(i);
        sqlite3_bind_text(insert_stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insert_stmt, 3, 20 + (i % 50));
        sqlite3_step(insert_stmt);
        sqlite3_reset(insert_stmt);
    }
    sqlite3_finalize(insert_stmt);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

    // Prepare remove statement
    sqlite3_stmt* remove_stmt;
    sqlite3_prepare_v2(db, "DELETE FROM Person WHERE id = ?", -1, &remove_stmt, nullptr);

    // Benchmark removal
    BenchmarkTimer timer;
    double total_time = 0;
    int successful_removes = 0;

    for (int i = 1; i <= num_records; ++i) {
        timer.reset();

        sqlite3_bind_int(remove_stmt, 1, i);
        rc = sqlite3_step(remove_stmt);

        double elapsed = timer.elapsed_ms();

        if (rc == SQLITE_DONE) {
            successful_removes++;
            total_time += elapsed;
        }

        sqlite3_reset(remove_stmt);
    }

    sqlite3_finalize(remove_stmt);

    // Report results
    std::cout << "Raw SQLite (prepared statements) - Remove " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
    std::cout << "  Average per remove: " << std::fixed << std::setprecision(4)
              << (total_time / successful_removes) << " ms\n";
    std::cout << "  Successful removes: " << successful_removes << "/" << num_records << "\n";

    sqlite3_close(db);
}

int main() {
    std::cout << "=== SQLite Benchmark ===\n\n";

    const std::vector<int> test_sizes = {1000, 5000, 10000};

    for (int size : test_sizes) {
        std::cout << "--- Testing with " << size << " records ---\n";

        benchmark_pure_sqlite_remove(size);
        std::cout << "\n";

        benchmark_raw_sqlite_remove(size);
        std::cout << "\n\n";
    }

    return 0;
}