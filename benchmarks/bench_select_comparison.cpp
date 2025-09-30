// Direct comparison between raw SQLite and Storm ORM SELECT
// Using the exact same test setup to find performance gap

#include <sqlite3.h>
#include "benchmark_utils.hpp"

import storm;
import <vector>;
import <expected>;

struct Person {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string name;
    int age;
};

using namespace storm::benchmark;

void benchmark_raw_sqlite(int num_records) {
    std::cout << "\n=== Raw SQLite SELECT ===" << std::endl;

    sqlite3* db;
    sqlite3_open(":memory:", &db);

    const char* create_sql = "CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER NOT NULL)";
    sqlite3_exec(db, create_sql, nullptr, nullptr, nullptr);

    // Insert test data
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

    // Benchmark SELECT
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
        if (text) {
            obj.name = std::string(reinterpret_cast<const char*>(text));
        } else {
            obj.name.clear();
        }
        obj.age = sqlite3_column_int(select_stmt, 2);
        rows++;
    }
    results.resize(rows);
    double elapsed = timer.elapsed_ms();

    sqlite3_finalize(select_stmt);
    sqlite3_close(db);

    std::cout << "  Rows: " << rows << std::endl;
    std::cout << "  Time: " << std::fixed << std::setprecision(2) << elapsed << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (rows / (elapsed / 1000.0) / 1000000.0) << "M rows/sec" << std::endl;
}

void benchmark_storm_orm(int num_records) {
    std::cout << "\n=== Storm ORM SELECT ===" << std::endl;

    // Setup Storm ORM
    auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
    if (!result.has_value()) {
        std::cerr << "Failed to set connection" << std::endl;
        return;
    }

    auto& conn = storm::QuerySet<Person>::get_default_connection();
    auto create_result = conn.execute(
        "CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER NOT NULL)"
    );

    // Insert test data
    std::vector<Person> persons = data_utils::generate_simple_test_data<Person>(num_records);
    auto queryset = storm::QuerySet<Person>{};

    for (const auto& person : persons) {
        queryset.insert(person);
    }

    // Benchmark SELECT
    BenchmarkTimer timer;
    auto select_result = queryset.select();
    double elapsed = timer.elapsed_ms();

    if (select_result.has_value()) {
        const auto& selected = select_result.value();
        std::cout << "  Rows: " << selected.size() << std::endl;
        std::cout << "  Time: " << std::fixed << std::setprecision(2) << elapsed << " ms" << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
                  << (selected.size() / (elapsed / 1000.0) / 1000000.0) << "M rows/sec" << std::endl;
    } else {
        std::cerr << "SELECT failed: " << select_result.error().message() << std::endl;
    }

    storm::QuerySet<Person>::clear_default_connection();
}

int main() {
    std::cout << "=== Direct SELECT Performance Comparison ===" << std::endl;

    const std::vector<int> test_sizes = {1000, 10000};

    for (int size : test_sizes) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Testing with " << size << " records" << std::endl;
        std::cout << "========================================" << std::endl;

        benchmark_raw_sqlite(size);
        benchmark_storm_orm(size);
    }

    return 0;
}