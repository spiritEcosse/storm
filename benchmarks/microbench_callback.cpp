/**
 * Microbenchmark: SQL building overhead analysis
 * Measures: 1) Full SQL build, 2) to_sql() call, 3) String compare
 */

#include <sqlite3.h>
#include <iostream>
#include <chrono>
#include <format>
#include <expected>
#include <memory>
#include <string>
#include <unordered_map>

import storm;

struct CallbackPerson {
    [[= storm::meta::FieldAttr::primary]] int64_t id;
    std::string                                   name;
    int                                           age;
    bool                                          is_active;
    double                                        salary;
};

constexpr int ITERATIONS = 100000;

// Base SQL (static, computed once)
static const std::string BASE_SQL = "SELECT id, name, age, is_active, salary FROM CallbackPerson";

int main() {
    std::cout << "=== SQL Building Overhead Benchmark ===\n";
    std::cout << "Iterations: " << ITERATIONS << "\n\n";

    // Initialize connection for WHERE expression
    auto result = storm::QuerySet<CallbackPerson>::set_default_connection(":memory:");
    if (!result) {
        std::cerr << "Failed\n";
        return 1;
    }

    // Create WHERE expression - convert to ExpressionVariantPtr
    using storm::orm::where::ExpressionVariantPtr;
    using storm::orm::where::field;
    ExpressionVariantPtr where_expr = field<^^CallbackPerson::age>() > 30;

    // Warmup
    for (int i = 0; i < 1000; i++) {
        std::string sql = BASE_SQL + " WHERE " + storm::orm::where::to_sql(*where_expr);
        (void)sql;
    }

    // Test 1: Just to_sql() call
    {
        auto            start    = std::chrono::high_resolution_clock::now();
        volatile size_t checksum = 0;
        for (int i = 0; i < ITERATIONS; i++) {
            std::string where_clause = storm::orm::where::to_sql(*where_expr);
            checksum += where_clause.size();
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto us  = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << std::format(
                "to_sql() only:        {:>10} us ({:.1f} ns/call)\n", us, (double)us * 1000 / ITERATIONS
        );
    }

    // Test 2: Full SQL build (BASE + " WHERE " + to_sql)
    {
        auto            start    = std::chrono::high_resolution_clock::now();
        volatile size_t checksum = 0;
        for (int i = 0; i < ITERATIONS; i++) {
            std::string sql;
            sql.reserve(BASE_SQL.size() + 7 + 50);
            sql = BASE_SQL;
            sql += " WHERE ";
            sql += storm::orm::where::to_sql(*where_expr);
            checksum += sql.size();
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto us  = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << std::format(
                "Full SQL build:       {:>10} us ({:.1f} ns/call)\n", us, (double)us * 1000 / ITERATIONS
        );
    }

    // Test 3: String comparison
    std::string cached_sql = BASE_SQL + " WHERE " + storm::orm::where::to_sql(*where_expr);
    {
        auto         start    = std::chrono::high_resolution_clock::now();
        volatile int checksum = 0;
        for (int i = 0; i < ITERATIONS; i++) {
            std::string sql = BASE_SQL + " WHERE " + storm::orm::where::to_sql(*where_expr);
            if (cached_sql == sql) {
                checksum++;
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto us  = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << std::format(
                "Build + compare:      {:>10} us ({:.1f} ns/call)\n", us, (double)us * 1000 / ITERATIONS
        );
    }

    // Test 4: Pointer comparison (what we could do instead)
    {
        const void*  cached_ptr = &where_expr;
        auto         start      = std::chrono::high_resolution_clock::now();
        volatile int checksum   = 0;
        for (int i = 0; i < ITERATIONS; i++) {
            if (cached_ptr == &where_expr) {
                checksum++;
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto us  = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << std::format(
                "Pointer compare:      {:>10} us ({:.1f} ns/call)\n", us, (double)us * 1000 / ITERATIONS
        );
    }

    // Test 5: Hash-based cache lookup (prepare_cached simulation)
    {
        std::unordered_map<std::string, int> cache;
        cache[cached_sql] = 42;

        auto         start    = std::chrono::high_resolution_clock::now();
        volatile int checksum = 0;
        for (int i = 0; i < ITERATIONS; i++) {
            std::string sql = BASE_SQL + " WHERE " + storm::orm::where::to_sql(*where_expr);
            auto        it  = cache.find(sql);
            if (it != cache.end()) {
                checksum += it->second;
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto us  = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << std::format(
                "Build + hash lookup:  {:>10} us ({:.1f} ns/call)\n", us, (double)us * 1000 / ITERATIONS
        );
    }

    std::cout << "\n=== Analysis ===\n";
    std::cout << "If SELECT takes ~900 us/iter with WHERE:\n";
    std::cout << "  SQL building overhead = (build_ns / 1000) / 900 * 100 %\n";

    return 0;
}
