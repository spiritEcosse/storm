#pragma once

#include <sqlite3.h>
#include <vector>
#include <algorithm>
#include <unordered_map>

import storm;

using namespace storm;

template<typename Model, int BatchSize = 1>
class InsertBenchmark {
    QuerySet<Model> qs_;
    std::vector<Model> data_;

    static std::string sql_insert_batch(size_t count) {
        std::string sql = "INSERT INTO Person (id, name, age, is_active, salary) VALUES ";
        for (size_t i = 0; i < count; i++) {
            if (i > 0) sql += ", ";
            sql += "(NULL, ?, ?, ?, ?)";
        }
        return sql;
    }

    static Model create_model() {
        return Model{.id = 0, .name = "BenchmarkPerson", .age = 30, .is_active = true, .salary = 50000.0};
    }

    // Bind a range of models starting at parameter index `idx`
    static void bind_rows(sqlite3_stmt* stmt, const Model* data, size_t count, int idx = 1) {
        for (size_t i = 0; i < count; i++) {
            const auto& p = data[i];
            sqlite3_bind_text(stmt, idx++, p.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, idx++, p.age);
            sqlite3_bind_int(stmt, idx++, p.is_active ? 1 : 0);
            sqlite3_bind_double(stmt, idx++, p.salary);
        }
    }

    // Execute statement, reset, return row count on success
    static int step_and_reset(sqlite3_stmt* stmt, sqlite3* db, int rows) {
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            (void)sqlite3_last_insert_rowid(db);
            sqlite3_reset(stmt);
            return rows;
        }
        sqlite3_reset(stmt);
        return 0;
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
        int count = (BatchSize == 1) ? iterations : BatchSize;
        data_.reserve(count);
        for (int i = 0; i < count; i++)
            data_.push_back(create_model());
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
                qs_.insert(data_);
                total += data_.size();
            }
        }
        return total;
    }

    int execute_raw(int iterations) {
        auto& conn = QuerySet<Model>::get_default_connection();
        sqlite3* db = conn->get();
        if (!db) return 0;

        constexpr size_t fields_per_row = 4;
        constexpr size_t max_bulk = 999 / fields_per_row;
        constexpr size_t bulk_threshold = (max_bulk > 100) ? (max_bulk / 2) : 50;

        int total = 0;

        // Single-row or small batch: one prepared statement
        if constexpr (BatchSize == 1 || BatchSize <= bulk_threshold) {
            size_t rows_per_stmt = (BatchSize == 1) ? 1 : std::min(max_bulk, data_.size());
            std::string sql = sql_insert_batch(rows_per_stmt);

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                return 0;

            for (int i = 0; i < iterations; i++) {
                bind_rows(stmt, &data_[(BatchSize == 1) ? i : 0], rows_per_stmt);
                total += step_and_reset(stmt, db, rows_per_stmt);
            }
            sqlite3_finalize(stmt);
        }
        // Large batch: chunked with transaction
        else {
            // Prepare statements for each unique chunk size
            std::unordered_map<size_t, sqlite3_stmt*> stmts;
            for (size_t off = 0; off < data_.size(); off += max_bulk) {
                size_t chunk = std::min(max_bulk, data_.size() - off);
                if (!stmts.contains(chunk)) {
                    sqlite3_stmt* stmt = nullptr;
                    if (sqlite3_prepare_v2(db, sql_insert_batch(chunk).c_str(), -1, &stmt, nullptr) == SQLITE_OK)
                        stmts[chunk] = stmt;
                }
            }

            for (int iter = 0; iter < iterations; iter++) {
                sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

                for (size_t off = 0; off < data_.size(); off += max_bulk) {
                    size_t chunk = std::min(max_bulk, data_.size() - off);
                    if (auto it = stmts.find(chunk); it != stmts.end()) {
                        bind_rows(it->second, &data_[off], chunk);
                        total += step_and_reset(it->second, db, chunk);
                    }
                }

                sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
            }

            for (auto& [_, stmt] : stmts)
                sqlite3_finalize(stmt);
        }
        return total;
    }
};