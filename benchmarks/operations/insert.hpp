#pragma once

/**
 * INSERT Benchmark
 *
 * Tests INSERT performance for single and batch operations.
 * Inherits from DataBenchmarkBase with 4 fields (id is auto-increment).
 */

#include "base.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <unordered_map>
#include <iostream>

namespace storm::benchmark {

    template <typename Model, int BatchSize = 1>
    class InsertBenchmark : public DataBenchmarkBase<InsertBenchmark<Model, BatchSize>, Model, BatchSize, 4> {
        using Base = DataBenchmarkBase<InsertBenchmark<Model, BatchSize>, Model, BatchSize, 4>;

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
        void print_info() const {
            if constexpr (BatchSize == 1)
                std::cout << "Operation: INSERT (single row)\n";
            else
                std::cout << "Operation: INSERT (batch, " << BatchSize << " rows per insert)\n";
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
            auto&    conn = storm::QuerySet<Model>::get_default_connection();
            sqlite3* db   = conn->get();
            if (!db)
                return 0;

            int total = 0;

            // Single-row or small batch: one prepared statement
            if constexpr (BatchSize == 1 || BatchSize <= Base::bulk_threshold) {
                size_t      rows_per_stmt = (BatchSize == 1) ? 1 : std::min(Base::max_bulk, Base::data().size());
                std::string sql           = sql_insert_batch(rows_per_stmt);

                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                    return 0;

                for (int i = 0; i < iterations; i++) {
                    bind_rows(stmt, &Base::data()[(BatchSize == 1) ? i : 0], rows_per_stmt);
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

                for (auto& [_, stmt] : stmts)
                    sqlite3_finalize(stmt);
            }
            return total;
        }
    };

} // namespace storm::benchmark