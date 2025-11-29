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

// Batch INSERT benchmark - REALISTIC pattern
// Inserts BatchSize records in ONE call, repeated N times for averaging
template<typename Model, int BatchSize = 100>
class InsertBatchBenchmark {
    QuerySet<Model> qs_;
    std::vector<Model> batch_data_;  // Single batch to insert repeatedly

public:
    void print_info() const {
        std::cout << "Operation: INSERT (batch, " << BatchSize << " rows per insert)\n";
    }

    // Prepare data BEFORE timing starts
    void prepare(int iterations) {
        // Prepare ONE batch of BatchSize records
        // This will be inserted 'iterations' times with DB cleanup between iterations
        batch_data_.clear();
        batch_data_.reserve(BatchSize);

        for (int i = 0; i < BatchSize; i++) {
            batch_data_.push_back(Model{
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

        // REALISTIC: Insert the FULL batch once per iteration
        // This tests: "How fast is ONE bulk insert of BatchSize records?"
        // repeated N times for statistical averaging
        //
        // Note: We don't delete between iterations - the DB grows naturally
        // This is realistic since users typically insert into growing databases
        for (int i = 0; i < iterations; i++) {
            qs_.insert(batch_data_);  // Insert all BatchSize records at once
            total_inserts += batch_data_.size();
        }

        return total_inserts;
    }

    // Raw SQLite batch execution for comparison - USES SAME CHUNKED BULK SQL STRATEGY AS STORM ORM
    int execute_raw(int iterations) {
        auto& conn = QuerySet<Model>::get_default_connection();
        sqlite3* db = conn->get();
        if (!db) return 0;

        int total_inserts = 0;

        // Calculate max chunk size based on SQLite's 999 variable limit
        // Person has 4 non-PK fields (name, age, is_active, salary)
        constexpr size_t fields_per_row = 4;
        constexpr size_t max_chunk_size = 999 / fields_per_row;  // 249 rows per chunk

        // REALISTIC: Insert BatchSize records per iteration using chunked bulk SQL
        for (int iter = 0; iter < iterations; iter++) {
            // Begin transaction for this batch
            sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

            // Process in chunks (same as Storm ORM does)
            size_t offset = 0;
            while (offset < batch_data_.size()) {
                size_t chunk_size = std::min(max_chunk_size, batch_data_.size() - offset);

                // Build bulk INSERT SQL: INSERT INTO ... VALUES (...), (...), ...
                std::string sql = "INSERT INTO Person (id, name, age, is_active, salary) VALUES ";
                for (size_t i = 0; i < chunk_size; i++) {
                    if (i > 0) sql += ", ";
                    sql += "(NULL, ?, ?, ?, ?)";
                }

                // Prepare statement for this chunk
                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                    return total_inserts;
                }

                // Bind all parameters for this chunk
                int param_index = 1;
                for (size_t i = 0; i < chunk_size; i++) {
                    const auto& person = batch_data_[offset + i];

                    sqlite3_bind_text(stmt, param_index++, person.name.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(stmt, param_index++, person.age);
                    sqlite3_bind_int(stmt, param_index++, person.is_active ? 1 : 0);
                    sqlite3_bind_double(stmt, param_index++, person.salary);
                }

                // Execute bulk insert for this chunk
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    // Get the last inserted row ID (same as Storm ORM does)
                    int64_t id = sqlite3_last_insert_rowid(db);
                    (void)id;  // Suppress unused warning
                    total_inserts += chunk_size;
                }

                sqlite3_finalize(stmt);
                offset += chunk_size;
            }

            // Commit transaction
            sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
        }

        return total_inserts;
    }
};

} // namespace storm::benchmark
