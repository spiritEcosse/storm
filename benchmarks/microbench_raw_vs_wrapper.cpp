/**
 * Microbenchmark: Raw sqlite3 calls vs Statement wrapper
 * Isolates the overhead of going through wrapper methods
 */

#include <sqlite3.h>
#include <iostream>
#include <chrono>
#include <format>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

import storm;

struct WrapperPerson {
    [[= storm::meta::FieldAttr::primary]] int64_t id;
    std::string                                   name;
    int                                           age;
    bool                                          is_active;
    double                                        salary;
};

constexpr int DATASET_SIZE = 10000;
constexpr int ITERATIONS   = 5000;

void setup_database() {
    auto conn = storm::QuerySet<WrapperPerson>::get_default_connection();
    conn->execute("DROP TABLE IF EXISTS WrapperPerson");
    conn->execute(R"(
        CREATE TABLE WrapperPerson (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL, age INTEGER NOT NULL,
            is_active INTEGER NOT NULL, salary REAL NOT NULL
        )
    )");
    conn->execute("BEGIN TRANSACTION");
    for (int i = 0; i < DATASET_SIZE; i++) {
        conn->execute(
                std::format(
                        "INSERT INTO WrapperPerson (name, age, is_active, salary) VALUES ('Person{}', {}, {}, {})",
                        i,
                        20 + (i % 50),
                        (i % 2),
                        30000.0 + (i * 100.0)
                )
        );
    }
    conn->execute("COMMIT");
}

// Test 1: Direct sqlite3 calls (baseline)
int64_t test_direct_sqlite(sqlite3* db, int iterations) {
    const char*   sql  = "SELECT id, name, age, is_active, salary FROM WrapperPerson";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

    auto    start    = std::chrono::high_resolution_clock::now();
    int64_t checksum = 0;
    for (int i = 0; i < iterations; i++) {
        sqlite3_reset(stmt);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t     id        = sqlite3_column_int64(stmt, 0);
            const char* name      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            int         age       = sqlite3_column_int(stmt, 2);
            int         is_active = sqlite3_column_int(stmt, 3);
            double      salary    = sqlite3_column_double(stmt, 4);
            checksum += id + age + is_active + (int64_t)salary + (name ? name[0] : 0);
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    sqlite3_finalize(stmt);
    std::cout << "Direct checksum: " << checksum << "\n";
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

// Test 2: Using Statement wrapper's handle() + direct calls
int64_t test_wrapper_handle(storm::db::sqlite::Connection* conn, int iterations) {
    auto          stmt_result = conn->prepare_cached("SELECT id, name, age, is_active, salary FROM WrapperPerson");
    auto*         stmt        = *stmt_result;
    sqlite3_stmt* raw         = stmt->handle(); // Get raw pointer once

    auto    start    = std::chrono::high_resolution_clock::now();
    int64_t checksum = 0;
    for (int i = 0; i < iterations; i++) {
        sqlite3_reset(raw); // Use raw pointer
        while (sqlite3_step(raw) == SQLITE_ROW) {
            int64_t     id        = sqlite3_column_int64(raw, 0);
            const char* name      = reinterpret_cast<const char*>(sqlite3_column_text(raw, 1));
            int         age       = sqlite3_column_int(raw, 2);
            int         is_active = sqlite3_column_int(raw, 3);
            double      salary    = sqlite3_column_double(raw, 4);
            checksum += id + age + is_active + (int64_t)salary + (name ? name[0] : 0);
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "Handle checksum: " << checksum << "\n";
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

// Test 3: Using Statement wrapper methods
int64_t test_wrapper_methods(storm::db::sqlite::Connection* conn, int iterations) {
    auto  stmt_result = conn->prepare_cached("SELECT id, name, age, is_active, salary FROM WrapperPerson");
    auto* stmt        = *stmt_result;

    auto    start    = std::chrono::high_resolution_clock::now();
    int64_t checksum = 0;
    for (int i = 0; i < iterations; i++) {
        stmt->reset();
        while (stmt->step_raw() == storm::db::sqlite::Statement::ROW_AVAILABLE) {
            int64_t     id        = stmt->extract_int64(0);
            const char* name      = reinterpret_cast<const char*>(stmt->extract_text_ptr(1));
            int         age       = stmt->extract_int(2);
            bool        is_active = stmt->extract_bool(3);
            double      salary    = stmt->extract_double(4);
            checksum += id + age + (is_active ? 1 : 0) + (int64_t)salary + (name ? name[0] : 0);
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "Methods checksum: " << checksum << "\n";
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

int main() {
    std::cout << "=== Wrapper Overhead Benchmark ===\n";
    std::cout << "Dataset: " << DATASET_SIZE << ", Iterations: " << ITERATIONS << "\n\n";

    auto result = storm::QuerySet<WrapperPerson>::set_default_connection(":memory:");
    if (!result) {
        std::cerr << "Failed\n";
        return 1;
    }
    setup_database();

    auto     conn = storm::QuerySet<WrapperPerson>::get_default_connection();
    sqlite3* db   = conn->get();

    int64_t direct_us  = test_direct_sqlite(db, ITERATIONS);
    int64_t handle_us  = test_wrapper_handle(conn.get(), ITERATIONS);
    int64_t methods_us = test_wrapper_methods(conn.get(), ITERATIONS);

    std::cout << "\n=== Results ===\n";
    std::cout << std::format("Direct sqlite3:      {:>10} us (100.0%)\n", direct_us);
    std::cout << std::format("handle() + direct:   {:>10} us ({:.1f}%)\n", handle_us, 100.0 * direct_us / handle_us);
    std::cout << std::format("Wrapper methods:     {:>10} us ({:.1f}%)\n", methods_us, 100.0 * direct_us / methods_us);

    std::cout << "\n=== Overhead per iteration ===\n";
    std::cout << std::format("handle() overhead:   {:.1f} us/iter\n", (double)(handle_us - direct_us) / ITERATIONS);
    std::cout << std::format("Methods overhead:    {:.1f} us/iter\n", (double)(methods_us - direct_us) / ITERATIONS);

    return 0;
}
