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
 *
 * FAIR COMPARISON: Both Storm ORM and raw SQLite now use RUNTIME checks
 * for batch size decisions. No compile-time advantages for raw SQLite.
 */

#include "base.hpp"

namespace storm::benchmark {

    template <typename Model> class UpdateBenchmark : public DataBenchmarkBase<UpdateBenchmark<Model>, Model, 5> {
        using Base = DataBenchmarkBase<UpdateBenchmark<Model>, Model, 5>;

      public:
        // Constructor with runtime batch size
        explicit UpdateBenchmark(int batch_size = 1) : Base(batch_size) {}

        // Use unified print_info with compile-time operation name
        void print_info() const {
            Base::template print_info_unified<OperationType::UpdatePK>();
        }

        void prepare(int iterations) {
            // 1. Clear table, generate data, insert to get IDs
            Base::prepare_with_insert(iterations);

            // 2. Modify data fields for update test (change values so UPDATE actually does work)
            for (auto& obj : Base::data()) {
                obj.name      = "UpdatedPerson";
                obj.age       = obj.age + 5;
                obj.salary    = obj.salary * 1.1;
                obj.is_active = !obj.is_active;
            }
        }

        // Use unified execute with compile-time operation binding
        int execute(int iterations) {
            return Base::template execute_unified<OperationType::UpdatePK>(iterations);
        }

      private:
        // Helper: Bind model fields for UPDATE (name, age, is_active, salary, id)
        static void bind_update_fields(sqlite3_stmt* stmt, const Model& p) {
            int idx = 1;
            sqlite3_bind_text(stmt, idx++, p.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, idx++, p.age);
            sqlite3_bind_int(stmt, idx++, p.is_active ? 1 : 0);
            sqlite3_bind_double(stmt, idx++, p.salary);
            sqlite3_bind_int64(stmt, idx++, p.id);
        }

        // Helper: Execute single-row updates
        int execute_single_row(sqlite3_stmt* stmt, int iterations) {
            int total = 0;
            for (int i = 0; i < iterations; i++) {
                bind_update_fields(stmt, Base::data()[i]);
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    total++;
                }
                sqlite3_reset(stmt);
            }
            return total;
        }

        // Helper: Execute batch updates with transaction
        int execute_batch(sqlite3_stmt* stmt, int iterations) {
            int total = 0;
            for (int iter = 0; iter < iterations; iter++) {
                sqlite3_exec(sqlite3_db_handle(stmt), "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
                for (const auto& p : Base::data()) {
                    bind_update_fields(stmt, p);
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

            // Raw SQLite UPDATE SQL: "UPDATE Person SET name=?, age=?, is_active=?, salary=? WHERE id=?"
            const std::string sql = "UPDATE Person SET name=?, age=?, is_active=?, salary=? WHERE id=?";

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                return 0;

            // Runtime check - FAIR comparison with Storm ORM
            // Execute single-row or batch updates based on batch_size
            int total;
            if (Base::batch_size() == 1) {
                total = execute_single_row(stmt, iterations);
            } else {
                total = execute_batch(stmt, iterations);
            }

            sqlite3_finalize(stmt);
            return total;
        }
    };

} // namespace storm::benchmark
