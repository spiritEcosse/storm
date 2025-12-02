#pragma once

/**
 * INSERT Benchmark - Single and batch row insertions
 */

#include <sqlite3.h>
#include <vector>

import storm;

using namespace storm;

template<typename Model, int BatchSize = 1>
class InsertBenchmark {
    QuerySet<Model> qs_;
    std::vector<Model> data_;

    // Single-row SQL (literal values, no prepared statement)
    static std::string sql_insert_single(const Model& person) {
        return "INSERT INTO Person (id, name, age, is_active, salary) VALUES (NULL, '" +
            person.name + "', " + std::to_string(person.age) + ", " +
            std::to_string(person.is_active ? 1 : 0) + ", " +
            std::to_string(person.salary) + ")";
    }

    // Batch SQL (placeholders)
    static std::string sql_insert_batch(size_t count) {
        std::string sql = "INSERT INTO Person (id, name, age, is_active, salary) VALUES ";
        for (size_t i = 0; i < count; i++) {
            if (i > 0) sql += ", ";
            sql += "(NULL, ?, ?, ?, ?)";
        }
        return sql;
    }

public:
    void print_info() const {
        if constexpr (BatchSize == 1)
            std::cout << "Operation: INSERT (single row)\n";
        else
            std::cout << "Operation: INSERT (batch, " << BatchSize << " rows per insert)\n";
    }

    void prepare(int iterations) {
        data_.clear();

        if constexpr (BatchSize == 1) {
            data_.reserve(iterations);
            for (int i = 0; i < iterations; i++) {
                data_.push_back(Model{.id = 0, .name = "BenchmarkPerson",
                    .age = 30, .is_active = true, .salary = 50000.0});
            }
        } else {
            data_.reserve(BatchSize);
            for (int i = 0; i < BatchSize; i++) {
                data_.push_back(Model{.id = 0, .name = "BatchPerson",
                    .age = 25 + (i % 45), .is_active = (i % 2 == 0),
                    .salary = 40000.0 + (i * 500.0)});
            }
        }
    }

    int execute(int iterations) {
        int total = 0;
        if constexpr (BatchSize == 1) {
            for (int i = 0; i < iterations; i++) {
                qs_.insert(data_[i]);
                total++;
            }
        } else {
            for (int i = 0; i < iterations; i++) {
                qs_.insert(data_);  // Insert full batch
                total += data_.size();
            }
        }
        return total;
    }

    int execute_raw(int iterations) {
        auto& conn = QuerySet<Model>::get_default_connection();
        sqlite3* db = conn->get();
        if (!db) return 0;

        int total = 0;

        if constexpr (BatchSize == 1) {
            for (int i = 0; i < iterations; i++) {
                std::string sql = sql_insert_single(data_[i]);
                if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK) {
                    (void)sqlite3_last_insert_rowid(db);
                    total++;
                }
            }
        } else {
            constexpr size_t fields_per_row = 4;
            constexpr size_t max_chunk = 999 / fields_per_row;

            for (int iter = 0; iter < iterations; iter++) {
                sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

                size_t offset = 0;
                while (offset < data_.size()) {
                    size_t chunk = std::min(max_chunk, data_.size() - offset);
                    std::string sql = sql_insert_batch(chunk);

                    sqlite3_stmt* stmt = nullptr;
                    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
                        return total;
                    }

                    int idx = 1;
                    for (size_t i = 0; i < chunk; i++) {
                        const auto& p = data_[offset + i];
                        sqlite3_bind_text(stmt, idx++, p.name.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int(stmt, idx++, p.age);
                        sqlite3_bind_int(stmt, idx++, p.is_active ? 1 : 0);
                        sqlite3_bind_double(stmt, idx++, p.salary);
                    }

                    if (sqlite3_step(stmt) == SQLITE_DONE) {
                        (void)sqlite3_last_insert_rowid(db);
                        total += chunk;
                    }
                    sqlite3_finalize(stmt);
                    offset += chunk;
                }
                sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
            }
        }
        return total;
    }
};
