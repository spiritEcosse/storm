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

    // Factory function for creating Model instances
    static Model create_model() {
        return Model{.id = 0, .name = "BenchmarkPerson", .age = 30, .is_active = true, .salary = 50000.0};
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
                data_.push_back(create_model());
            }
        } else {
            data_.reserve(BatchSize);
            for (int i = 0; i < BatchSize; i++) {
                data_.push_back(create_model());
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
            // Prepare statement ONCE (fair comparison with Storm's statement caching)
            std::string sql = "INSERT INTO Person (id, name, age, is_active, salary) VALUES (NULL, ?, ?, ?, ?)";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return 0;
            }

            for (int i = 0; i < iterations; i++) {
                const auto& p = data_[i];
                sqlite3_bind_text(stmt, 1, p.name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 2, p.age);
                sqlite3_bind_int(stmt, 3, p.is_active ? 1 : 0);
                sqlite3_bind_double(stmt, 4, p.salary);

                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    (void)sqlite3_last_insert_rowid(db);
                    total++;
                }
                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
        } else {
            constexpr size_t fields_per_row = 4;
            constexpr size_t max_bulk_size = 999 / fields_per_row;  // 249 for Person

            // Storm ORM's adaptive threshold logic:
            // bulk_sweet_spot = max(50, 249/2) = 124
            constexpr size_t bulk_sweet_spot = (max_bulk_size > 100) ? (max_bulk_size / 2) : 50;

            // Match Storm's adaptive strategy:
            // - BatchSize <= bulk_sweet_spot: Use bulk SQL (INSERT VALUES (...), (...))
            // - BatchSize > bulk_sweet_spot: Use individual INSERTs with transaction
            if constexpr (BatchSize <= bulk_sweet_spot) {
                // Strategy: BULK SQL (same as Storm for small/medium batches)
                size_t chunk = std::min(max_bulk_size, data_.size());
                std::string sql = sql_insert_batch(chunk);

                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                    return 0;
                }

                for (int iter = 0; iter < iterations; iter++) {
                    int idx = 1;
                    for (size_t i = 0; i < data_.size(); i++) {
                        const auto& p = data_[i];
                        sqlite3_bind_text(stmt, idx++, p.name.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int(stmt, idx++, p.age);
                        sqlite3_bind_int(stmt, idx++, p.is_active ? 1 : 0);
                        sqlite3_bind_double(stmt, idx++, p.salary);
                    }

                    if (sqlite3_step(stmt) == SQLITE_DONE) {
                        (void)sqlite3_last_insert_rowid(db);
                        total += data_.size();
                    }
                    sqlite3_reset(stmt);
                }
                sqlite3_finalize(stmt);
            } else {
                // Strategy: INDIVIDUAL INSERTs with TRANSACTION (same as Storm for large batches)
                std::string sql = "INSERT INTO Person (id, name, age, is_active, salary) VALUES (NULL, ?, ?, ?, ?)";
                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                    return 0;
                }

                for (int iter = 0; iter < iterations; iter++) {
                    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

                    for (size_t i = 0; i < data_.size(); i++) {
                        const auto& p = data_[i];
                        sqlite3_bind_text(stmt, 1, p.name.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int(stmt, 2, p.age);
                        sqlite3_bind_int(stmt, 3, p.is_active ? 1 : 0);
                        sqlite3_bind_double(stmt, 4, p.salary);

                        if (sqlite3_step(stmt) == SQLITE_DONE) {
                            (void)sqlite3_last_insert_rowid(db);
                            total++;
                        }
                        sqlite3_reset(stmt);
                    }

                    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
                }
                sqlite3_finalize(stmt);
            }
        }
        return total;
    }
};
