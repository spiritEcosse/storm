/**
 * Microbenchmark to trace SELECT performance overhead
 *
 * Instruments Storm ORM vs Raw SQLite to find where the ~8% overhead comes from.
 *
 * Build: cmake --build --preset ninja-release
 * Run:   ./build/release/benchmarks/microbench_select_trace
 */

#define ENABLE_TIMING_TRACE true
#include "timing_trace.hpp"

#include <sqlite3.h>
#include <iostream>
#include <vector>
#include <format>
#include <expected>
#include <plf_hive/plf_hive.h>

import storm;

struct TracePerson {
    [[= storm::meta::FieldAttr::primary]] int64_t id;
    std::string                                   name;
    int                                           age;
    bool                                          is_active;
    double                                        salary;
};

constexpr int DATASET_SIZE = 10000;
constexpr int ITERATIONS   = 1000;
constexpr int WHERE_VALUE  = 30;

// Setup database with test data
void setup_database() {
    auto& conn = *storm::QuerySet<TracePerson>::get_default_connection();

    conn.execute("DROP TABLE IF EXISTS TracePerson");
    conn.execute(R"(
        CREATE TABLE TracePerson (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            age INTEGER NOT NULL,
            is_active INTEGER NOT NULL,
            salary REAL NOT NULL
        )
    )");

    // Insert test data in batch
    conn.execute("BEGIN TRANSACTION");
    for (int i = 0; i < DATASET_SIZE; i++) {
        conn.execute(
                std::format(
                        "INSERT INTO TracePerson (name, age, is_active, salary) VALUES ('Person{}', {}, {}, {})",
                        i,
                        20 + (i % 50),
                        (i % 2),
                        30000.0 + (i * 100.0)
                )
        );
    }
    conn.execute("COMMIT");
}

// ============================================================================
// Storm ORM SELECT - instrumented
// ============================================================================
int storm_select_instrumented(storm::QuerySet<TracePerson>& qs, int iterations) {
    using storm::orm::where::field;

    TRACE_INIT();

    int  total_rows = 0;
    auto where_expr = field<^^TracePerson::age>() > WHERE_VALUE;

    for (int i = 0; i < iterations; i++) {
        TRACE_START("storm_where");
        qs.where(where_expr);
        TRACE_END("storm_where");

        TRACE_START("storm_select");
        auto results = qs.select();
        TRACE_END("storm_select");

        TRACE_START("storm_count");
        total_rows += results.value().size();
        TRACE_END("storm_count");

        TRACE_START("storm_reset");
        qs.reset();
        TRACE_END("storm_reset");
    }

    TRACE_REPORT_N(iterations);
    return total_rows;
}

// ============================================================================
// Raw SQLite SELECT - instrumented
// ============================================================================
int raw_select_instrumented(sqlite3* db, int iterations) {
    TRACE_INIT();

    const char* sql = "SELECT id, name, age, is_active, salary FROM TracePerson WHERE age > ?";

    TRACE_START("raw_prepare");
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    TRACE_END("raw_prepare");

    int total_rows = 0;

    for (int i = 0; i < iterations; i++) {
        TRACE_START("raw_reset");
        sqlite3_reset(stmt);
        TRACE_END("raw_reset");

        TRACE_START("raw_bind");
        sqlite3_bind_int(stmt, 1, WHERE_VALUE);
        TRACE_END("raw_bind");

        TRACE_START("raw_loop");
        plf::hive<TracePerson> results;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            TracePerson p;
            p.id        = sqlite3_column_int64(stmt, 0);
            p.name      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            p.age       = sqlite3_column_int(stmt, 2);
            p.is_active = sqlite3_column_int(stmt, 3) != 0;
            p.salary    = sqlite3_column_double(stmt, 4);
            results.insert(std::move(p));
        }
        TRACE_END("raw_loop");

        TRACE_START("raw_count");
        total_rows += results.size();
        TRACE_END("raw_count");
    }

    TRACE_START("raw_finalize");
    sqlite3_finalize(stmt);
    TRACE_END("raw_finalize");

    TRACE_REPORT_N(iterations);
    return total_rows;
}

int main() {
    std::cout << "=== SELECT Performance Trace ===\n";
    std::cout << "Dataset: " << DATASET_SIZE << " rows\n";
    std::cout << "Iterations: " << ITERATIONS << "\n";
    std::cout << "WHERE: age > " << WHERE_VALUE << "\n\n";

    // Setup
    auto result = storm::QuerySet<TracePerson>::set_default_connection(":memory:");
    if (!result) {
        std::cerr << "Failed to create connection\n";
        return 1;
    }
    setup_database();

    storm::QuerySet<TracePerson> qs;
    sqlite3*                     db = storm::QuerySet<TracePerson>::get_default_connection()->get();

    // Run traces
    std::cout << "\n========== RAW SQLITE ==========\n";
    int raw_rows = raw_select_instrumented(db, ITERATIONS);
    std::cout << "Total rows: " << raw_rows << "\n";

    std::cout << "\n========== STORM ORM ==========\n";
    int storm_rows = storm_select_instrumented(qs, ITERATIONS);
    std::cout << "Total rows: " << storm_rows << "\n";

    return 0;
}
