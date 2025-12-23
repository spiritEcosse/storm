/**
 * Clean microbenchmark for SELECT performance comparison
 * No trace overhead - just raw timing comparison
 */

#include <sqlite3.h>
#include <iostream>
#include <chrono>
#include <expected>
#include <format>
#include <plf_hive/plf_hive.h>

import storm;

struct CleanPerson {
    [[= storm::meta::FieldAttr::primary]] int64_t id;
    std::string                                   name;
    int                                           age;
    bool                                          is_active;
    double                                        salary;
};

constexpr int DATASET_SIZE = 10000;
constexpr int ITERATIONS   = 5000;
constexpr int WHERE_VALUE  = 30;
constexpr int WARMUP       = 100;

void setup_database() {
    auto& conn = *storm::QuerySet<CleanPerson>::get_default_connection();
    conn.execute("DROP TABLE IF EXISTS CleanPerson");
    conn.execute(R"(
        CREATE TABLE CleanPerson (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL, age INTEGER NOT NULL,
            is_active INTEGER NOT NULL, salary REAL NOT NULL
        )
    )");
    conn.execute("BEGIN TRANSACTION");
    for (int i = 0; i < DATASET_SIZE; i++) {
        conn.execute(
                std::format(
                        "INSERT INTO CleanPerson (name, age, is_active, salary) VALUES ('Person{}', {}, {}, {})",
                        i,
                        20 + (i % 50),
                        (i % 2),
                        30000.0 + (i * 100.0)
                )
        );
    }
    conn.execute("COMMIT");
}

// Storm ORM SELECT
int64_t benchmark_storm(storm::QuerySet<CleanPerson>& qs, int iterations) {
    using storm::orm::where::field;
    auto where_expr = field<^^CleanPerson::age>() > WHERE_VALUE;

    // Warmup
    for (int i = 0; i < WARMUP; i++) {
        qs.where(where_expr);
        auto r = qs.select();
        qs.reset();
    }

    auto start = std::chrono::high_resolution_clock::now();
    int  total = 0;
    for (int i = 0; i < iterations; i++) {
        qs.where(where_expr);
        auto results = qs.select();
        total += results.value().size();
        qs.reset();
    }
    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "Storm rows: " << total << "\n";
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

// Raw SQLite SELECT
int64_t benchmark_raw(sqlite3* db, int iterations) {
    const char*   sql  = "SELECT id, name, age, is_active, salary FROM CleanPerson WHERE age > ?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

    // Warmup
    for (int i = 0; i < WARMUP; i++) {
        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, WHERE_VALUE);
        plf::hive<CleanPerson> r;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            CleanPerson p;
            p.id        = sqlite3_column_int64(stmt, 0);
            p.name      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            p.age       = sqlite3_column_int(stmt, 2);
            p.is_active = sqlite3_column_int(stmt, 3) != 0;
            p.salary    = sqlite3_column_double(stmt, 4);
            r.insert(std::move(p));
        }
    }

    auto start = std::chrono::high_resolution_clock::now();
    int  total = 0;
    for (int i = 0; i < iterations; i++) {
        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, WHERE_VALUE);
        plf::hive<CleanPerson> results;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            CleanPerson p;
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
    std::cout << "Raw rows: " << total << "\n";
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

int main() {
    std::cout << "=== Clean SELECT Benchmark ===\n";
    std::cout << "Dataset: " << DATASET_SIZE << ", Iterations: " << ITERATIONS << "\n\n";

    auto result = storm::QuerySet<CleanPerson>::set_default_connection(":memory:");
    if (!result) {
        std::cerr << "Failed\n";
        return 1;
    }
    setup_database();

    storm::QuerySet<CleanPerson> qs;
    sqlite3*                     db = storm::QuerySet<CleanPerson>::get_default_connection()->get();

    // Run multiple times
    for (int run = 1; run <= 3; run++) {
        std::cout << "=== Run " << run << " ===\n";

        int64_t raw_us   = benchmark_raw(db, ITERATIONS);
        int64_t storm_us = benchmark_storm(qs, ITERATIONS);

        double raw_ops    = (double)ITERATIONS / raw_us * 1000000.0;
        double storm_ops  = (double)ITERATIONS / storm_us * 1000000.0;
        double efficiency = storm_ops / raw_ops * 100.0;

        std::cout << std::format("Raw:   {:>8} us ({:.2f} K ops/sec)\n", raw_us, raw_ops / 1000);
        std::cout << std::format("Storm: {:>8} us ({:.2f} K ops/sec)\n", storm_us, storm_ops / 1000);
        std::cout << std::format("Efficiency: {:.1f}%\n\n", efficiency);
    }
    return 0;
}
