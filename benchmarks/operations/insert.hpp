#pragma once

/**
 * INSERT Benchmark
 *
 * Tests INSERT performance for single and batch operations.
 * Inherits from DataBenchmarkBase.
 *
 * FAIR COMPARISON: Both Storm ORM and raw SQLite now use RUNTIME checks
 * for batch size decisions. No compile-time advantages for raw SQLite.
 *
 * Note: Person has a UNIQUE constraint on name, so the table must be
 * cleared before each iteration to avoid constraint violations.
 */

import storm_benchmark_base;

#include <algorithm>
#include <unordered_map>
#include <iostream>

namespace storm::benchmark {

    template <typename Model> class InsertBenchmark : public DataBenchmarkBase<InsertBenchmark<Model>, Model> {
        using Base = DataBenchmarkBase<InsertBenchmark<Model>, Model>;

        // SQL from ORM: single-row INSERT with RETURNING
        static auto sql_insert_single_returning() -> std::string {
            return storm::QuerySet<Model>().insert(Model{}).sql();
        }

        // SQL from ORM: single-row INSERT without RETURNING
        static auto sql_insert_single() -> std::string {
            return storm::QuerySet<Model>().template insert<storm::orm::statements::ReturnId::No>(Model{}).sql();
        }

        // SQL from ORM: multi-row INSERT for bulk operations
        static auto sql_insert_batch(size_t count) -> std::string {
            std::vector<Model> batch(count);
            return storm::QuerySet<Model>().insert(batch).sql();
        }

        // Bind a range of models starting at parameter index `idx`
        static auto bind_rows(sqlite3_stmt* stmt, const Model* data, size_t count, int idx = 1) -> void {
            for (size_t i = 0; i < count; i++) {
                bind_non_pk_fields(stmt, data[i], idx);
            }
        }

        // Clear table — needed before each insert iteration due to UNIQUE constraint on name
        static auto clear_table(sqlite3* db) -> void {
            auto sql = std::format("DELETE FROM {}", std::meta::identifier_of(^^Model));
            sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        }

      public:
        // Constructor with runtime batch size
        explicit InsertBenchmark(int batch_size = 1) : Base(batch_size) {}

        // Use unified print_info with compile-time operation name
        auto print_info() const -> void {
            Base::template print_info_unified<OperationType::Insert>();
        }

        // ORM execute — clear table before each iteration for fair comparison
        auto execute(int iterations) -> int {
            sqlite3* db    = get_db<Model>();
            int      total = 0;
            if (Base::batch_size() == 1) {
                for (int i = 0; i < iterations; i++) {
                    OperationDispatcher<OperationType::Insert>::call(Base::qs(), Base::data()[i]).execute();
                    total++;
                }
            } else {
                for (int i = 0; i < iterations; i++) {
                    if (db)
                        clear_table(db);
                    OperationDispatcher<OperationType::Insert>::call(Base::qs(), Base::data()).execute();
                    total += Base::data().size();
                }
            }
            return total;
        }

        auto print_info_no_return() const -> void {
            std::cout << "Operation: INSERT (no return, single row)\n";
        }

        auto execute_no_return(int iterations) -> int {
            int total = 0;
            for (int i = 0; i < iterations; i++) {
                Base::qs().template insert<storm::orm::statements::ReturnId::No>(Base::data()[i]).execute();
                total++;
            }
            return total;
        }

        // Helper: Prepare statements for unique chunk sizes (reduces nesting)
        auto prepare_chunk_statements(sqlite3* db, std::unordered_map<size_t, sqlite3_stmt*>& stmts) -> void {
            for (size_t off = 0; off < Base::data().size(); off += Base::max_bulk) {
                size_t chunk = std::min(Base::max_bulk, Base::data().size() - off);
                if (stmts.contains(chunk))
                    continue;

                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql_insert_batch(chunk).c_str(), -1, &stmt, nullptr) == SQLITE_OK)
                    stmts[chunk] = stmt;
            }
        }

        // Helper: Execute one batch iteration (reduces nesting)
        auto execute_batch_iteration(sqlite3* db, const std::unordered_map<size_t, sqlite3_stmt*>& stmts) -> int {
            int total = 0;
            for (size_t off = 0; off < Base::data().size(); off += Base::max_bulk) {
                size_t chunk = std::min(Base::max_bulk, Base::data().size() - off);
                auto   it    = stmts.find(chunk);
                if (it == stmts.end())
                    continue;

                bind_rows(it->second, &Base::data()[off], chunk);
                total += Base::step_and_reset(it->second, db, chunk);
            }
            return total;
        }

        auto execute_raw(int iterations) -> int {
            sqlite3* db = get_db<Model>();
            if (!db)
                return 0;

            int total = 0;

            // Runtime check - FAIR comparison with Storm ORM
            if (Base::batch_size() == 1) {
                // Single-row: clear once, each row has unique name
                clear_table(db);
                std::string sql = sql_insert_single_returning();

                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                    return 0;

                for (int i = 0; i < iterations; i++) {
                    int idx = 1;
                    bind_non_pk_fields(stmt, Base::data()[i], idx);
                    if (sqlite3_step(stmt) == SQLITE_ROW) {
                        [[maybe_unused]] int64_t id = sqlite3_column_int64(stmt, 0);
                        total++;
                    }
                    sqlite3_reset(stmt);
                }
                sqlite3_finalize(stmt);
            } else if (static_cast<size_t>(Base::batch_size()) <= Base::bulk_threshold) {
                // Small batch: clear before each iteration
                size_t      rows_per_stmt = std::min(Base::max_bulk, Base::data().size());
                std::string sql           = sql_insert_batch(rows_per_stmt);

                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                    return 0;

                for (int i = 0; i < iterations; i++) {
                    clear_table(db);
                    bind_rows(stmt, &Base::data()[0], rows_per_stmt);
                    total += Base::step_and_reset(stmt, db, rows_per_stmt);
                }
                sqlite3_finalize(stmt);
            }
            // Large batch: chunked with transaction — clear before each iteration
            else {
                std::unordered_map<size_t, sqlite3_stmt*> stmts;
                prepare_chunk_statements(db, stmts);

                for (int iter = 0; iter < iterations; iter++) {
                    clear_table(db);
                    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
                    total += execute_batch_iteration(db, stmts);
                    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
                }

                for (const auto& [_, stmt] : stmts)
                    sqlite3_finalize(stmt);
            }
            return total;
        }

        // Raw SQLite INSERT without RETURNING — fair comparison for insert_no_return
        auto execute_raw_no_return(int iterations) -> int {
            sqlite3* db = get_db<Model>();
            if (!db)
                return 0;

            clear_table(db);

            int         total = 0;
            std::string sql   = sql_insert_single();

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                return 0;

            for (int i = 0; i < iterations; i++) {
                int idx = 1;
                bind_non_pk_fields(stmt, Base::data()[i], idx);
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    total++;
                }
                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
            return total;
        }
    };

    // Wrapper for INSERT without RETURNING benchmarks — routes to no-return methods
    template <typename Model> class InsertNoReturnBenchmark : public InsertBenchmark<Model> {
        using Base = InsertBenchmark<Model>;

      public:
        explicit InsertNoReturnBenchmark(int batch_size = 1) : Base(batch_size) {}

        auto print_info() const -> void {
            Base::print_info_no_return();
        }

        auto execute(int iterations) -> int {
            return Base::execute_no_return(iterations);
        }

        auto execute_raw(int iterations) -> int {
            return Base::execute_raw_no_return(iterations);
        }
    };

} // namespace storm::benchmark
