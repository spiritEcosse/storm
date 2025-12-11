#pragma once

/**
 * DELETE Benchmark
 *
 * Tests DELETE performance for single and batch operations by primary key.
 * Inherits from DataBenchmarkBase with 1 field (id for WHERE clause).
 *
 * Workflow:
 * 1. prepare(): Clear table, insert test data
 * 2. execute(): Storm ORM remove() - single or batch by PK
 * 3. execute_raw(): Raw SQLite DELETE...WHERE id=?
 *
 * Note: DELETE is destructive. After first iteration, rows are gone.
 * Both Storm and Raw have same behavior, so comparison is fair.
 */

#include "base.hpp"

namespace storm::benchmark {

    template <typename Model, int BatchSize = 1>
    class DeleteBenchmark : public DataBenchmarkBase<DeleteBenchmark<Model, BatchSize>, Model, BatchSize, 1> {
        using Base = DataBenchmarkBase<DeleteBenchmark<Model, BatchSize>, Model, BatchSize, 1>;

      public:
        // Use unified print_info with compile-time operation name
        void print_info() const {
            Base::template print_info_unified<OperationType::Delete>();
        }

        void prepare(int iterations) {
            // Clear table, generate data, insert to get IDs
            Base::prepare_with_insert(iterations);
        }

        // Use unified execute with compile-time operation dispatch
        int execute(int iterations) {
            return Base::template execute_unified<OperationType::Delete>(iterations);
        }

      private:
        // Helper: Execute single-row deletes
        int execute_single_row(sqlite3_stmt* stmt, int iterations) {
            int total = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_bind_int64(stmt, 1, Base::data()[i].id);
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    total++;
                }
                sqlite3_reset(stmt);
            }
            return total;
        }

        // Helper: Execute batch deletes with transaction
        int execute_batch(sqlite3_stmt* stmt, int iterations) {
            int total = 0;
            for (int iter = 0; iter < iterations; iter++) {
                sqlite3_exec(sqlite3_db_handle(stmt), "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
                for (const auto& p : Base::data()) {
                    sqlite3_bind_int64(stmt, 1, p.id);
                    if (sqlite3_step(stmt) == SQLITE_DONE) {
                        total++;
                    }
                    sqlite3_reset(stmt);
                }
                sqlite3_exec(sqlite3_db_handle(stmt), "COMMIT", nullptr, nullptr, nullptr);
            }
            return total;
        }

      public:
        int execute_raw(int iterations) {
            auto&    conn = storm::QuerySet<Model>::get_default_connection();
            sqlite3* db   = conn->get();
            if (!db)
                return 0;

            // Raw SQLite DELETE SQL: "DELETE FROM Person WHERE id = ?"
            const std::string sql = "DELETE FROM Person WHERE id = ?";

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                return 0;

            // Execute single-row or batch deletes based on BatchSize
            int total;
            if constexpr (BatchSize == 1) {
                total = execute_single_row(stmt, iterations);
            } else {
                total = execute_batch(stmt, iterations);
            }

            sqlite3_finalize(stmt);
            return total;
        }
    };

} // namespace storm::benchmark
