#pragma once

/**
 * UPDATE-by-PK Benchmark
 *
 * Tests UPDATE performance using primary key WHERE clause.
 * Inherits from DataBenchmarkBase with 5 fields (4 data + 1 PK for WHERE).
 *
 * Workflow:
 * 1. prepare(): Clear table, insert test data, modify fields for update
 * 2. execute(): Storm ORM update() - single or batch
 * 3. execute_raw(): Raw SQLite UPDATE...WHERE id=?
 *
 * Raw SQLite uses transaction for batch (no multi-row UPDATE syntax).
 */

#include "base.hpp"
#include <iostream>
#include <chrono>

namespace storm::benchmark {

    template <typename Model, int BatchSize = 1>
    class UpdateBenchmark : public DataBenchmarkBase<UpdateBenchmark<Model, BatchSize>, Model, BatchSize, 5> {
        using Base = DataBenchmarkBase<UpdateBenchmark<Model, BatchSize>, Model, BatchSize, 5>;

      public:
        void print_info() const {
            if constexpr (BatchSize == 1)
                std::cout << "Operation: UPDATE by PK (single row)\n";
            else
                std::cout << "Operation: UPDATE by PK (batch, " << BatchSize << " rows per update)\n";
        }

        void prepare(int iterations) {
            // 1. Clear table first using raw SQLite
            auto&    conn = storm::QuerySet<Model>::get_default_connection();
            sqlite3* db   = conn->get();
            if (db) {
                sqlite3_exec(db, "DELETE FROM Person", nullptr, nullptr, nullptr);
            }

            // 2. Generate test data using base class method
            Base::prepare(iterations);

            // 3. INSERT data to get valid primary keys
            // Use InsertOptions to get IDs back
            auto insert_result = Base::qs_.insert(Base::data_, storm::orm::statements::InsertOptions{.return_ids = true});
            if (!insert_result.has_value()) {
                std::cerr << "Failed to insert test data for UPDATE benchmark\n";
                return;
            }

            // Store returned IDs back into data
            const auto& ids = insert_result.value();
            for (size_t i = 0; i < Base::data_.size() && i < ids.size(); i++) {
                Base::data_[i].id = ids[i];
            }

            // 4. Modify data fields for update test (change values so UPDATE actually does work)
            for (auto& obj : Base::data_) {
                obj.name      = "UpdatedPerson";
                obj.age       = obj.age + 5;
                obj.salary    = obj.salary * 1.1;
                obj.is_active = !obj.is_active;
            }
        }

        int execute(int iterations) {
            int total = 0;
            if constexpr (BatchSize == 1) {
                for (int i = 0; i < iterations; i++) {
                    Base::qs_.update(Base::data_[i]);
                    total++;
                }
            } else {
                for (int i = 0; i < iterations; i++) {
                    Base::qs_.update(Base::data_);
                    total += Base::data_.size();
                }
            }
            return total;
        }

        int execute_raw(int iterations) {
            auto&    conn = storm::QuerySet<Model>::get_default_connection();
            sqlite3* db   = conn->get();
            if (!db)
                return 0;

            int total = 0;

            // Raw SQLite UPDATE SQL: "UPDATE Person SET name=?, age=?, is_active=?, salary=? WHERE id=?"
            const std::string sql = "UPDATE Person SET name=?, age=?, is_active=?, salary=? WHERE id=?";

            // Single-row or small batch: one prepared statement, use transaction for batch
            if constexpr (BatchSize == 1) {
                // Single row - no transaction needed
                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                    return 0;

                for (int i = 0; i < iterations; i++) {
                    const auto& p   = Base::data_[i];
                    int         idx = 1;
                    sqlite3_bind_text(stmt, idx++, p.name.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(stmt, idx++, p.age);
                    sqlite3_bind_int(stmt, idx++, p.is_active ? 1 : 0);
                    sqlite3_bind_double(stmt, idx++, p.salary);
                    sqlite3_bind_int64(stmt, idx++, p.id);

                    if (sqlite3_step(stmt) == SQLITE_DONE) {
                        total++;
                    }
                    sqlite3_reset(stmt);
                }
                sqlite3_finalize(stmt);
            } else {
                // Batch update - use transaction wrapper
                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                    return 0;

                for (int iter = 0; iter < iterations; iter++) {
                    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

                    for (const auto& p : Base::data_) {
                        int idx = 1;
                        sqlite3_bind_text(stmt, idx++, p.name.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int(stmt, idx++, p.age);
                        sqlite3_bind_int(stmt, idx++, p.is_active ? 1 : 0);
                        sqlite3_bind_double(stmt, idx++, p.salary);
                        sqlite3_bind_int64(stmt, idx++, p.id);

                        if (sqlite3_step(stmt) == SQLITE_DONE) {
                            total++;
                        }
                        sqlite3_reset(stmt);
                    }

                    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
                }

                sqlite3_finalize(stmt);
            }
            return total;
        }
    };

} // namespace storm::benchmark
