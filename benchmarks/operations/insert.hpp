#pragma once

/**
 * INSERT Benchmark
 *
 * Tests INSERT performance for single and batch operations.
 * Inherits from DataBenchmarkBase with 4 fields (id is auto-increment).
 *
 * FAIR COMPARISON: Both Storm ORM and raw SQLite now use RUNTIME checks
 * for batch size decisions. No compile-time advantages for raw SQLite.
 */

#include "base.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <unordered_map>
#include <iostream>

namespace storm::benchmark {

    template <typename Model> class InsertBenchmark : public DataBenchmarkBase<InsertBenchmark<Model>, Model, 4> {
        using Base = DataBenchmarkBase<InsertBenchmark<Model>, Model, 4>;

        // Build multi-row INSERT SQL for bulk operations
        static std::string sql_insert_batch(size_t count) {
            std::string sql = "INSERT INTO Person (id, name, age, is_active, salary) VALUES ";
            for (size_t i = 0; i < count; i++) {
                if (i > 0)
                    sql += ", ";
                sql += "(NULL, ?, ?, ?, ?)";
            }
            return sql;
        }

        // Bind a range of models starting at parameter index `idx`
        static void bind_rows(sqlite3_stmt* stmt, const Model* data, size_t count, int idx = 1) {
            for (size_t i = 0; i < count; i++) {
                int local_idx = idx;
                Base::bind_model_fields(stmt, data[i], local_idx);
                idx = local_idx;
            }
        }

      public:
        // Constructor with runtime batch size
        explicit InsertBenchmark(int batch_size = 1) : Base(batch_size) {}

        // Use unified print_info with compile-time operation name
        void print_info() const {
            Base::template print_info_unified<OperationType::Insert>();
        }

        // Use unified execute with compile-time operation binding
        int execute(int iterations) {
            return Base::template execute_unified<OperationType::Insert>(iterations);
        }

        // Helper: Prepare statements for unique chunk sizes (reduces nesting)
        void prepare_chunk_statements(sqlite3* db, std::unordered_map<size_t, sqlite3_stmt*>& stmts) {
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
        int execute_batch_iteration(sqlite3* db, const std::unordered_map<size_t, sqlite3_stmt*>& stmts) {
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

        int execute_raw(int iterations) {
            sqlite3* db = get_db<Model>();
            if (!db)
                return 0;

            int total = 0;

            // Runtime check - FAIR comparison with Storm ORM
            // Single-row or small batch: one prepared statement
            if (Base::batch_size() == 1 || static_cast<size_t>(Base::batch_size()) <= Base::bulk_threshold) {
                size_t rows_per_stmt = (Base::batch_size() == 1) ? 1 : std::min(Base::max_bulk, Base::data().size());
                std::string sql      = sql_insert_batch(rows_per_stmt);

                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                    return 0;

                for (int i = 0; i < iterations; i++) {
                    bind_rows(stmt, &Base::data()[(Base::batch_size() == 1) ? i : 0], rows_per_stmt);
                    total += Base::step_and_reset(stmt, db, rows_per_stmt);
                }
                sqlite3_finalize(stmt);
            }
            // Large batch: chunked with transaction
            else {
                // Prepare statements for each unique chunk size
                std::unordered_map<size_t, sqlite3_stmt*> stmts;
                prepare_chunk_statements(db, stmts);

                for (int iter = 0; iter < iterations; iter++) {
                    sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
                    total += execute_batch_iteration(db, stmts);
                    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
                }

                for (const auto& [_, stmt] : stmts)
                    sqlite3_finalize(stmt);
            }
            return total;
        }
    };

} // namespace storm::benchmark
