/**
 * Microbenchmark to compare row extraction methods
 * Tests: Raw sqlite3 vs Storm Statement wrapper extraction
 */

#include <sqlite3.h>
#include <iostream>
#include <chrono>
#include <format>
#include <expected>
#include <plf_hive/plf_hive.h>

import storm;

struct ExtractPerson {
    [[= storm::meta::FieldAttr::primary]] int64_t id;
    std::string                                   name;
    int                                           age;
    bool                                          is_active;
    double                                        salary;
};

constexpr int DATASET_SIZE = 10000;
constexpr int ITERATIONS   = 5000;

void setup_database() {
    auto& conn = *storm::QuerySet<ExtractPerson>::get_default_connection();
    conn.execute("DROP TABLE IF EXISTS ExtractPerson");
    conn.execute(R"(
        CREATE TABLE ExtractPerson (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL, age INTEGER NOT NULL,
            is_active INTEGER NOT NULL, salary REAL NOT NULL
        )
    )");
    conn.execute("BEGIN TRANSACTION");
    for (int i = 0; i < DATASET_SIZE; i++) {
        conn.execute(
                std::format(
                        "INSERT INTO ExtractPerson (name, age, is_active, salary) VALUES ('Person{}', {}, {}, {})",
                        i,
                        20 + (i % 50),
                        (i % 2),
                        30000.0 + (i * 100.0)
                )
        );
    }
    conn.execute("COMMIT");
}

// Test 1: Raw sqlite3 extraction (baseline)
int64_t test_raw_extraction(sqlite3* db, int iterations) {
    const char*   sql  = "SELECT id, name, age, is_active, salary FROM ExtractPerson";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

    auto start = std::chrono::high_resolution_clock::now();
    int  total = 0;
    for (int i = 0; i < iterations; i++) {
        sqlite3_reset(stmt);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ExtractPerson p;
            p.id        = sqlite3_column_int64(stmt, 0);
            p.name      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            p.age       = sqlite3_column_int(stmt, 2);
            p.is_active = sqlite3_column_int(stmt, 3) != 0;
            p.salary    = sqlite3_column_double(stmt, 4);
            total++;
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    sqlite3_finalize(stmt);
    std::cout << "Raw extraction rows: " << total << "\n";
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

// Test 2: Raw sqlite3 extraction with hive insert
int64_t test_raw_with_hive(sqlite3* db, int iterations) {
    const char*   sql  = "SELECT id, name, age, is_active, salary FROM ExtractPerson";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

    auto start = std::chrono::high_resolution_clock::now();
    int  total = 0;
    for (int i = 0; i < iterations; i++) {
        sqlite3_reset(stmt);
        plf::hive<ExtractPerson> results;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ExtractPerson p;
            p.id        = sqlite3_column_int64(stmt, 0);
            p.name      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            p.age       = sqlite3_column_int(stmt, 2);
            p.is_active = sqlite3_column_int(stmt, 3) != 0;
            p.salary    = sqlite3_column_double(stmt, 4);
            results.insert(std::move(p));
        }
        total += results.size();
    }
    auto end = std::chrono::high_resolution_clock::now();

    sqlite3_finalize(stmt);
    std::cout << "Raw+hive rows: " << total << "\n";
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

// Test 3: Storm ORM full select (no WHERE for fair comparison)
int64_t test_storm_select(storm::QuerySet<ExtractPerson>& qs, int iterations) {
    auto start = std::chrono::high_resolution_clock::now();
    int  total = 0;
    for (int i = 0; i < iterations; i++) {
        auto results = qs.select();
        total += results.value().size();
    }
    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "Storm select rows: " << total << "\n";
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

int main() {
    std::cout << "=== Extraction Overhead Benchmark ===\n";
    std::cout << "Dataset: " << DATASET_SIZE << ", Iterations: " << ITERATIONS << "\n\n";

    auto result = storm::QuerySet<ExtractPerson>::set_default_connection(":memory:");
    if (!result) {
        std::cerr << "Failed\n";
        return 1;
    }
    setup_database();

    storm::QuerySet<ExtractPerson> qs;
    sqlite3*                       db = storm::QuerySet<ExtractPerson>::get_default_connection()->get();

    // Warmup
    for (int i = 0; i < 100; i++) {
        qs.select();
    }

    int64_t raw_us   = test_raw_extraction(db, ITERATIONS);
    int64_t hive_us  = test_raw_with_hive(db, ITERATIONS);
    int64_t storm_us = test_storm_select(qs, ITERATIONS);

    std::cout << "\n=== Results ===\n";
    std::cout << std::format("Raw extraction:    {:>10} us (baseline)\n", raw_us);
    std::cout << std::format("Raw + hive insert: {:>10} us ({:.1f}% of raw)\n", hive_us, 100.0 * raw_us / hive_us);
    std::cout << std::format(
            "Storm ORM select:  {:>10} us ({:.1f}% of raw+hive)\n", storm_us, 100.0 * hive_us / storm_us
    );

    std::cout << "\n=== Overhead Analysis ===\n";
    int64_t hive_overhead  = hive_us - raw_us;
    int64_t storm_overhead = storm_us - hive_us;
    std::cout << std::format(
            "Hive insert overhead:  {:>10} us ({:.1f} us/iter)\n", hive_overhead, (double)hive_overhead / ITERATIONS
    );
    std::cout << std::format(
            "Storm ORM overhead:    {:>10} us ({:.1f} us/iter)\n", storm_overhead, (double)storm_overhead / ITERATIONS
    );

    return 0;
}
