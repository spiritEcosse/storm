// Micro-benchmark to analyze SELECT performance bottlenecks
// Isolates different components: sqlite3_step, column reading, object construction, string allocation

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

struct Person {
    int id;
    std::string name;
    int age;
};

// Benchmark 1: Just sqlite3_step() loop - no data extraction
void benchmark_step_only(sqlite3* db, int num_records) {
    std::cout << "\n=== Benchmark 1: sqlite3_step() loop only ===" << std::endl;

    sqlite3_stmt* select_stmt;
    sqlite3_prepare_v2(db, "SELECT id, name, age FROM Person", -1, &select_stmt, nullptr);

    BenchmarkTimer timer;
    int rows = 0;
    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        rows++;
    }
    double elapsed = timer.elapsed_ms();

    sqlite3_finalize(select_stmt);

    std::cout << "  Rows: " << rows << std::endl;
    std::cout << "  Time: " << std::fixed << std::setprecision(2) << elapsed << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (rows / (elapsed / 1000.0) / 1000000.0) << "M rows/sec" << std::endl;
}

// Benchmark 2: sqlite3_step() + column reading (no object construction)
void benchmark_step_and_read(sqlite3* db, int num_records) {
    std::cout << "\n=== Benchmark 2: sqlite3_step() + column reading ===" << std::endl;

    sqlite3_stmt* select_stmt;
    sqlite3_prepare_v2(db, "SELECT id, name, age FROM Person", -1, &select_stmt, nullptr);

    BenchmarkTimer timer;
    int rows = 0;
    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        // Read columns but don't store
        int id = sqlite3_column_int(select_stmt, 0);
        const unsigned char* text = sqlite3_column_text(select_stmt, 1);
        int age = sqlite3_column_int(select_stmt, 2);

        // Prevent optimization
        (void)id;
        (void)text;
        (void)age;

        rows++;
    }
    double elapsed = timer.elapsed_ms();

    sqlite3_finalize(select_stmt);

    std::cout << "  Rows: " << rows << std::endl;
    std::cout << "  Time: " << std::fixed << std::setprecision(2) << elapsed << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (rows / (elapsed / 1000.0) / 1000000.0) << "M rows/sec" << std::endl;
}

// Benchmark 3: sqlite3_step() + column reading + emplace_back (no string copy)
void benchmark_step_read_emplace_no_string(sqlite3* db, int num_records) {
    std::cout << "\n=== Benchmark 3: step + read + emplace_back (empty strings) ===" << std::endl;

    sqlite3_stmt* select_stmt;
    sqlite3_prepare_v2(db, "SELECT id, name, age FROM Person", -1, &select_stmt, nullptr);

    std::vector<Person> results;
    results.reserve(num_records);

    BenchmarkTimer timer;
    int rows = 0;
    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(select_stmt, 0);
        int age = sqlite3_column_int(select_stmt, 2);

        results.emplace_back();
        Person& obj = results.back();
        obj.id = id;
        obj.name = ""; // Don't copy string
        obj.age = age;

        rows++;
    }
    double elapsed = timer.elapsed_ms();

    sqlite3_finalize(select_stmt);

    std::cout << "  Rows: " << rows << std::endl;
    std::cout << "  Time: " << std::fixed << std::setprecision(2) << elapsed << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (rows / (elapsed / 1000.0) / 1000000.0) << "M rows/sec" << std::endl;
}

// Benchmark 4: Full Storm ORM equivalent - step + read + emplace_back + string copy
void benchmark_full_storm_equivalent(sqlite3* db, int num_records) {
    std::cout << "\n=== Benchmark 4: Full Storm ORM equivalent (with string copy) ===" << std::endl;

    sqlite3_stmt* select_stmt;
    sqlite3_prepare_v2(db, "SELECT id, name, age FROM Person", -1, &select_stmt, nullptr);

    std::vector<Person> results;
    results.reserve(num_records);

    BenchmarkTimer timer;
    int rows = 0;
    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(select_stmt, 0);
        const unsigned char* text = sqlite3_column_text(select_stmt, 1);
        int len = sqlite3_column_bytes(select_stmt, 1);
        int age = sqlite3_column_int(select_stmt, 2);

        results.emplace_back();
        Person& obj = results.back();
        obj.id = id;
        if (text) {
            obj.name.assign(reinterpret_cast<const char*>(text), len);
        } else {
            obj.name.clear();
        }
        obj.age = age;

        rows++;
    }
    double elapsed = timer.elapsed_ms();

    sqlite3_finalize(select_stmt);

    std::cout << "  Rows: " << rows << std::endl;
    std::cout << "  Time: " << std::fixed << std::setprecision(2) << elapsed << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (rows / (elapsed / 1000.0) / 1000000.0) << "M rows/sec" << std::endl;
}

// Benchmark 5: Optimized with resize instead of emplace_back
void benchmark_optimized_resize(sqlite3* db, int num_records) {
    std::cout << "\n=== Benchmark 5: Optimized with resize (pre-construct objects) ===" << std::endl;

    sqlite3_stmt* select_stmt;
    sqlite3_prepare_v2(db, "SELECT id, name, age FROM Person", -1, &select_stmt, nullptr);

    std::vector<Person> results;
    results.resize(num_records);

    BenchmarkTimer timer;
    int rows = 0;
    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        Person& obj = results[rows];

        obj.id = sqlite3_column_int(select_stmt, 0);
        const unsigned char* text = sqlite3_column_text(select_stmt, 1);
        int len = sqlite3_column_bytes(select_stmt, 1);
        if (text) {
            obj.name.assign(reinterpret_cast<const char*>(text), len);
        } else {
            obj.name.clear();
        }
        obj.age = sqlite3_column_int(select_stmt, 2);

        rows++;
    }
    double elapsed = timer.elapsed_ms();

    results.resize(rows); // Trim to actual size
    sqlite3_finalize(select_stmt);

    std::cout << "  Rows: " << rows << std::endl;
    std::cout << "  Time: " << std::fixed << std::setprecision(2) << elapsed << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (rows / (elapsed / 1000.0) / 1000000.0) << "M rows/sec" << std::endl;
}

// Benchmark 6: Test string allocation overhead
void benchmark_string_allocation_cost(sqlite3* db, int num_records) {
    std::cout << "\n=== Benchmark 6: String allocation cost analysis ===" << std::endl;

    sqlite3_stmt* select_stmt;
    sqlite3_prepare_v2(db, "SELECT id, name, age FROM Person", -1, &select_stmt, nullptr);

    // First pass: measure with std::string construction
    std::vector<std::string> strings1;
    strings1.reserve(num_records);

    BenchmarkTimer timer1;
    int rows1 = 0;
    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(select_stmt, 1);
        if (text) {
            strings1.emplace_back(reinterpret_cast<const char*>(text));
        }
        rows1++;
    }
    double elapsed1 = timer1.elapsed_ms();

    sqlite3_reset(select_stmt);

    // Second pass: measure with string.assign()
    std::vector<std::string> strings2;
    strings2.reserve(num_records);

    BenchmarkTimer timer2;
    int rows2 = 0;
    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(select_stmt, 1);
        int len = sqlite3_column_bytes(select_stmt, 1);
        if (text) {
            strings2.emplace_back();
            strings2.back().assign(reinterpret_cast<const char*>(text), len);
        }
        rows2++;
    }
    double elapsed2 = timer2.elapsed_ms();

    sqlite3_finalize(select_stmt);

    std::cout << "  std::string(const char*):       " << std::fixed << std::setprecision(2)
              << elapsed1 << " ms (" << (rows1 / (elapsed1 / 1000.0) / 1000000.0) << "M rows/sec)" << std::endl;
    std::cout << "  string.assign(const char*, len): " << std::fixed << std::setprecision(2)
              << elapsed2 << " ms (" << (rows2 / (elapsed2 / 1000.0) / 1000000.0) << "M rows/sec)" << std::endl;
}

int main() {
    std::cout << "=== Storm ORM SELECT Performance Analysis ===" << std::endl;
    std::cout << "Isolating bottlenecks in SELECT operations\n" << std::endl;

    const int num_records = 10000;

    // Setup database
    sqlite3* db;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database" << std::endl;
        return 1;
    }

    // Create table
    const char* create_sql = "CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER NOT NULL)";
    sqlite3_exec(db, create_sql, nullptr, nullptr, nullptr);

    // Insert test data
    std::cout << "Inserting " << num_records << " test records..." << std::endl;
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
    std::cout << "Test data inserted.\n" << std::endl;

    // Run benchmarks
    std::cout << "========================================" << std::endl;
    std::cout << "BOTTLENECK ANALYSIS" << std::endl;
    std::cout << "========================================" << std::endl;

    benchmark_step_only(db, num_records);
    benchmark_step_and_read(db, num_records);
    benchmark_step_read_emplace_no_string(db, num_records);
    benchmark_full_storm_equivalent(db, num_records);
    benchmark_optimized_resize(db, num_records);
    benchmark_string_allocation_cost(db, num_records);

    std::cout << "\n========================================" << std::endl;
    std::cout << "ANALYSIS SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "This benchmark isolates the overhead of each component:" << std::endl;
    std::cout << "1. sqlite3_step() loop baseline" << std::endl;
    std::cout << "2. Column reading overhead" << std::endl;
    std::cout << "3. Object construction overhead (without strings)" << std::endl;
    std::cout << "4. String allocation/copy overhead" << std::endl;
    std::cout << "5. resize() vs emplace_back() strategy" << std::endl;
    std::cout << "6. String construction strategy comparison" << std::endl;

    sqlite3_close(db);
    return 0;
}