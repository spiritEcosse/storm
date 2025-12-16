#pragma once

/**
 * DELETE Benchmark
 *
 * Tests DELETE performance for single and batch operations by primary key.
 * Inherits from DataBenchmarkBase with 1 field (id for WHERE clause).
 *
 * Workflow:
 * 1. prepare(): Clear table, insert test data
 * 2. execute(): Storm ORM remove() - uses IN clause for batch
 * 3. execute_raw(): Raw SQLite DELETE...WHERE id IN (?, ?, ...) - same strategy
 *
 * Note: DELETE is destructive. After first iteration, rows are gone.
 * Both Storm and Raw use the same IN clause strategy for fair comparison.
 */

#include "base.hpp"
#include <algorithm>
#include <unordered_map>

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

        // DELETE needs special handling: re-insert data before each batch iteration
        // because after one DELETE, all rows are gone
        int execute(int iterations) {
            int total = 0;
            if constexpr (BatchSize == 1) {
                // Single-row: delete one row per iteration (standard behavior)
                for (int i = 0; i < iterations; i++) {
                    Base::qs().remove(Base::data()[i]);
                    total++;
                }
            } else {
                // Batch: must re-insert before each DELETE iteration
                for (int i = 0; i < iterations; i++) {
                    // Re-insert data for this iteration
                    auto insert_result =
                            Base::qs().insert(Base::data(), storm::orm::statements::InsertOptions{.return_ids = true});
                    if (insert_result.has_value()) {
                        // Update IDs in data
                        const auto& ids = insert_result.value();
                        for (size_t j = 0; j < Base::data().size() && j < ids.size(); j++) {
                            Base::data()[j].id = ids[j];
                        }
                    }
                    // Now delete the batch
                    Base::qs().remove(Base::data());
                    total += Base::data().size();
                }
            }
            return total;
        }

      private:
        // Build DELETE SQL with IN clause for bulk operations
        static std::string sql_delete_batch(size_t count) {
            if (count == 1) {
                return "DELETE FROM Person WHERE id = ?";
            }
            std::string sql = "DELETE FROM Person WHERE id IN (";
            for (size_t i = 0; i < count; i++) {
                if (i > 0)
                    sql += ",";
                sql += "?";
            }
            sql += ")";
            return sql;
        }

        // Bind IDs for IN clause
        static void bind_ids(sqlite3_stmt* stmt, const Model* data, size_t count, int idx = 1) {
            for (size_t i = 0; i < count; i++) {
                sqlite3_bind_int64(stmt, idx++, data[i].id);
            }
        }

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

        // Maximum chunk size for IN clause (80% of SQLite limit = 799)
        static constexpr size_t max_chunk_size = (999 * 4) / 5; // 799

        // Helper: Execute large batch using chunked IN clauses with transaction
        // (matches Storm ORM's execute_chunked strategy)
        int execute_chunked_batch(sqlite3* db, int iterations) {
            int total = 0;

            // Pre-prepare statements for each unique chunk size we'll need
            std::unordered_map<size_t, sqlite3_stmt*> stmts;
            for (size_t off = 0; off < Base::data().size(); off += max_chunk_size) {
                size_t chunk = std::min(max_chunk_size, Base::data().size() - off);
                if (!stmts.contains(chunk)) {
                    sqlite3_stmt* stmt = nullptr;
                    if (sqlite3_prepare_v2(db, sql_delete_batch(chunk).c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                        stmts[chunk] = stmt;
                    }
                }
            }

            // Pre-prepare BEGIN/COMMIT statements (matches Storm's cached approach)
            sqlite3_stmt* begin_stmt  = nullptr;
            sqlite3_stmt* commit_stmt = nullptr;
            sqlite3_prepare_v2(db, "BEGIN TRANSACTION", -1, &begin_stmt, nullptr);
            sqlite3_prepare_v2(db, "COMMIT", -1, &commit_stmt, nullptr);

            for (int iter = 0; iter < iterations; iter++) {
                // Re-insert data before each batch delete (setup, not timed separately)
                reinsert_data();

                // Use prepared BEGIN statement
                sqlite3_step(begin_stmt);
                sqlite3_reset(begin_stmt);

                // Process in chunks using IN clause
                for (size_t off = 0; off < Base::data().size(); off += max_chunk_size) {
                    size_t chunk = std::min(max_chunk_size, Base::data().size() - off);
                    auto   it    = stmts.find(chunk);
                    if (it == stmts.end())
                        continue;

                    bind_ids(it->second, &Base::data()[off], chunk);
                    if (sqlite3_step(it->second) == SQLITE_DONE) {
                        total += chunk;
                    }
                    sqlite3_reset(it->second);
                }

                // Use prepared COMMIT statement
                sqlite3_step(commit_stmt);
                sqlite3_reset(commit_stmt);
            }

            // Cleanup
            sqlite3_finalize(begin_stmt);
            sqlite3_finalize(commit_stmt);
            for (auto& [_, stmt] : stmts) {
                sqlite3_finalize(stmt);
            }

            return total;
        }

        // Match Storm ORM's adaptive threshold calculation for fair comparison
        // Returns true if batch should use bulk IN clause, false for individual deletes
        static constexpr bool should_use_bulk_sql() {
            // max_bulk_size = 999 / fields_per_row = 999 for DELETE
            constexpr size_t max_bulk_size   = 999 / Base::fields_per_row;
            constexpr size_t bulk_sweet_spot = std::max(size_t(50), max_bulk_size / 2);
            constexpr size_t bulk_max_safe   = (max_bulk_size * 4) / 5; // 80% of max

            // Storm's adaptive logic:
            // - Batches ≤ 10: always use bulk
            // - Batches ≤ bulk_sweet_spot: use bulk
            // - Batches ≤ bulk_max_safe: use bulk
            // - Larger batches: use individual inserts
            if constexpr (BatchSize <= 10)
                return true;
            if constexpr (BatchSize <= bulk_sweet_spot)
                return true;
            if constexpr (BatchSize <= bulk_max_safe)
                return true;
            return false;
        }

        // Helper: Re-insert data using Storm ORM (not timed - just setup)
        void reinsert_data() {
            auto insert_result =
                    Base::qs().insert(Base::data(), storm::orm::statements::InsertOptions{.return_ids = true});
            if (insert_result.has_value()) {
                const auto& ids = insert_result.value();
                for (size_t j = 0; j < Base::data().size() && j < ids.size(); j++) {
                    Base::data()[j].id = ids[j];
                }
            }
        }

      public:
        int execute_raw(int iterations) {
            auto&    conn = storm::QuerySet<Model>::get_default_connection();
            sqlite3* db   = conn->get();
            if (!db)
                return 0;

            int total = 0;

            // Single-row: simple DELETE WHERE id = ?
            if constexpr (BatchSize == 1) {
                const std::string sql  = sql_delete_batch(1);
                sqlite3_stmt*     stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                    return 0;

                total = execute_single_row(stmt, iterations);
                sqlite3_finalize(stmt);
            }
            // Batch: use bulk IN clause if within Storm's adaptive threshold
            else if constexpr (should_use_bulk_sql()) {
                size_t      rows_per_stmt = std::min(Base::max_bulk, Base::data().size());
                std::string sql           = sql_delete_batch(rows_per_stmt);

                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                    return 0;

                for (int i = 0; i < iterations; i++) {
                    // Re-insert data before each batch delete (not timed in benchmark)
                    reinsert_data();
                    bind_ids(stmt, &Base::data()[0], rows_per_stmt);
                    total += Base::step_and_reset(stmt, db, rows_per_stmt);
                }
                sqlite3_finalize(stmt);
            }
            // Large batch: chunked IN clauses with transaction (matches Storm ORM strategy)
            else {
                total = execute_chunked_batch(db, iterations);
            }
            return total;
        }
    };

} // namespace storm::benchmark
