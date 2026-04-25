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

import storm_benchmark_base;

namespace storm::benchmark {

    // FieldsPerRow for UPDATE = non-PK fields + 1 (PK in WHERE clause)
    template <typename Model>
    class UpdateBenchmark : public DataBenchmarkBase<UpdateBenchmark<Model>, Model, non_pk_field_count<Model>() + 1> {
        using Base = DataBenchmarkBase<UpdateBenchmark<Model>, Model, non_pk_field_count<Model>() + 1>;

      public:
        // Constructor with runtime batch size
        explicit UpdateBenchmark(int batch_size = 1) : Base(batch_size) {}

        // Use unified print_info with compile-time operation name
        auto print_info() const -> void {
            Base::template print_info_unified<OperationType::UpdatePK>();
        }

        auto prepare(int iterations) -> void {
            // 1. Clear table, generate data, insert to get IDs
            Base::prepare_with_insert(iterations);

            // 2. Modify data fields for update test (change values so UPDATE actually does work)
            for (size_t i = 0; i < Base::data().size(); i++) {
                auto& obj     = Base::data()[i];
                obj.name      = std::format("Updated{}", i);
                obj.age       = obj.age + 5;
                obj.salary    = obj.salary * 1.1;
                obj.is_active = !obj.is_active;
            }
        }

        // Use unified execute with compile-time operation binding
        auto execute(int iterations) -> int {
            return Base::template execute_unified<OperationType::UpdatePK>(iterations);
        }

      private:
        // Bind model fields for UPDATE: non-PK fields + PK at end (WHERE id=?)
        static auto bind_update_fields_raw(sqlite3_stmt* stmt, const Model& p) -> void {
            int idx = 1;
            storm::benchmark::bind_update_fields(stmt, p, idx);
        }

        // Helper: Execute single-row updates
        auto execute_single_row(sqlite3_stmt* stmt, int iterations) -> int {
            int total = 0;
            for (int i = 0; i < iterations; i++) {
                bind_update_fields_raw(stmt, Base::data()[i]);
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    total++;
                }
                sqlite3_reset(stmt);
            }
            return total;
        }

        // Helper: Execute batch updates with transaction
        auto execute_batch(sqlite3_stmt* stmt, int iterations) -> int {
            int total = 0;
            for (int iter = 0; iter < iterations; iter++) {
                sqlite3_exec(sqlite3_db_handle(stmt), "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
                for (const auto& p : Base::data()) {
                    bind_update_fields_raw(stmt, p);
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
        auto execute_raw(int iterations) -> int {
            sqlite3* db = get_db<Model>();
            if (!db)
                return 0;

            // SQL from ORM — single source of truth
            const std::string sql = storm::QuerySet<Model>().update(Model{}).sql();

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
