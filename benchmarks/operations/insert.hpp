#pragma once

/**
 * INSERT Benchmark - Single and batch row insertions
 */

#include <sqlite3.h>
#include <vector>
#include <span>

import storm;

namespace storm::benchmark {

// Single INSERT benchmark
template<typename Model>
class InsertBenchmark {
    QuerySet<Model> qs_;
    std::vector<Model> data_;

public:
    void print_info() const {
        std::cout << "Operation: INSERT (single row)\n";
    }

    // Prepare data BEFORE timing starts
    void prepare(int iterations) {
        data_.clear();
        data_.reserve(iterations);

        for (int i = 0; i < iterations; i++) {
            data_.push_back(Model{
                .id = 0,  // Auto-increment
                .name = "BenchmarkPerson",
                .age = 30,
                .is_active = true,
                .salary = 50000.0
            });
        }
    }

    int execute(int iterations) {
        int total_inserts = 0;
        for (int i = 0; i < iterations; i++) {
            qs_.insert(data_[i]);
            total_inserts++;
        }
        return total_inserts;
    }

    // Raw SQLite execution for comparison using pure sqlite3 API
    int execute_raw(int iterations) {
        auto& conn = QuerySet<Model>::get_default_connection();

        // Get raw sqlite3* handle from the connection
        sqlite3* db = conn->get();
        if (!db) return 0;

        int total_inserts = 0;

        for (int i = 0; i < iterations; i++) {
            const auto& person = data_[i];

            // Build SQL string with literal values
            std::string insert_sql = "INSERT INTO Person (id, name, age, is_active, salary) VALUES (NULL, '" +
                person.name + "', " + std::to_string(person.age) + ", " +
                std::to_string(person.is_active ? 1 : 0) + ", " +
                std::to_string(person.salary) + ")";

            // Execute using sqlite3_exec (no prepared statements)
            int rc = sqlite3_exec(db, insert_sql.c_str(), nullptr, nullptr, nullptr);

            if (rc == SQLITE_OK) {
                // Get the inserted row ID (same as Storm ORM does)
                int64_t id = sqlite3_last_insert_rowid(db);
                (void)id;  // Suppress unused warning
                total_inserts++;
            }
        }

        return total_inserts;
    }
};

// Batch INSERT benchmark
template<typename Model, int BatchSize = 100>
class InsertBatchBenchmark {
    QuerySet<Model> qs_;
    std::vector<Model> all_data_;

public:
    void print_info() const {
        std::cout << "Operation: INSERT (batch, " << BatchSize << " rows per batch)\n";
    }

    // Prepare data BEFORE timing starts
    void prepare(int iterations) {
        // Calculate total records to insert
        int total_records = iterations * BatchSize;

        // Prepare ALL data upfront
        all_data_.clear();
        all_data_.reserve(total_records);

        for (int i = 0; i < total_records; i++) {
            all_data_.push_back(Model{
                .id = 0,  // Auto-increment
                .name = "BatchPerson",
                .age = 25 + (i % 45),
                .is_active = (i % 2 == 0),
                .salary = 40000.0 + (i * 500.0)
            });
        }
    }

    int execute(int iterations) {
        int total_inserts = 0;

        // Execute batch inserts by slicing the pre-prepared vector
        for (int i = 0; i < iterations; i++) {
            size_t start_idx = i * BatchSize;
            size_t end_idx = std::min(start_idx + BatchSize, all_data_.size());
            std::span<const Model> batch(all_data_.data() + start_idx, end_idx - start_idx);

            qs_.insert(batch);
            total_inserts += batch.size();
        }

        return total_inserts;
    }

    // Raw SQLite batch execution for comparison
    int execute_raw(int iterations) {
        auto& conn = QuerySet<Model>::get_default_connection();
        sqlite3* db = conn->get();
        if (!db) return 0;

        int total_inserts = 0;

        // Prepare statement once (fair comparison - Storm also uses prepared statements)
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT INTO Person (id, name, age, is_active, salary) VALUES (NULL, ?, ?, ?, ?)";

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return 0;
        }

        // Execute batch inserts by slicing the pre-prepared data
        for (int iter = 0; iter < iterations; iter++) {
            size_t start_idx = iter * BatchSize;
            size_t end_idx = std::min(start_idx + BatchSize, all_data_.size());

            // Begin transaction for batch
            sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

            // Insert BatchSize rows from the prepared data
            for (size_t i = start_idx; i < end_idx; i++) {
                const auto& person = all_data_[i];

                // Bind parameters
                sqlite3_bind_text(stmt, 1, person.name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 2, person.age);
                sqlite3_bind_int(stmt, 3, person.is_active ? 1 : 0);
                sqlite3_bind_double(stmt, 4, person.salary);

                // Execute
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    total_inserts++;

                    // Get the inserted row ID (same as Storm ORM does)
                    int64_t id = sqlite3_last_insert_rowid(db);
                    (void)id;  // Suppress unused warning
                }

                // Reset statement for next iteration
                sqlite3_reset(stmt);
            }

            // Commit transaction
            sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
        }

        sqlite3_finalize(stmt);
        return total_inserts;
    }
};

} // namespace storm::benchmark
