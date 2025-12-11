#pragma once

/**
 * CRTP Base Class for Data-Driven Benchmarks
 *
 * Provides shared functionality for Insert and UpdateByPK benchmarks:
 * - Data storage and generation
 * - QuerySet member
 * - SQLite binding utilities
 * - Common constants (SQLite limits)
 */

#include <sqlite3.h>
#include <vector>
#include <iostream>

import storm;

namespace storm::benchmark {

    // CRTP base class for data-driven benchmarks (Insert, UpdateByPK)
    template <typename Derived, typename Model, int BatchSize = 1, size_t FieldsPerRow = 4> class DataBenchmarkBase {
      private:
        QuerySet<Model>    qs_;
        std::vector<Model> data_;

      protected:
        // Accessor methods for derived classes
        QuerySet<Model>& qs() {
            return qs_;
        }
        const QuerySet<Model>& qs() const {
            return qs_;
        }
        std::vector<Model>& data() {
            return data_;
        }
        const std::vector<Model>& data() const {
            return data_;
        }

        // SQLite binding limit constants
        // 999 is SQLite's default SQLITE_MAX_VARIABLE_NUMBER
        // FieldsPerRow: 4 for INSERT (name, age, is_active, salary - id is auto-increment)
        //               5 for UPDATE (name, age, is_active, salary, id - need id for WHERE clause)
        static constexpr size_t fields_per_row = FieldsPerRow;
        static constexpr size_t max_bulk       = 999 / fields_per_row; // ~249 for INSERT, ~199 for UPDATE
        static constexpr size_t bulk_threshold = (max_bulk > 100) ? (max_bulk / 2) : 50;

        // Default model creation - derived classes can override via static method hiding
        static Model create_model() {
            return Model{.id = 0, .name = "BenchmarkPerson", .age = 30, .is_active = true, .salary = 50000.0};
        }

        // Bind model fields to statement (name, age, is_active, salary)
        // idx is 1-based SQLite parameter index, incremented after each bind
        static void bind_model_fields(sqlite3_stmt* stmt, const Model& p, int& idx) {
            sqlite3_bind_text(stmt, idx++, p.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, idx++, p.age);
            sqlite3_bind_int(stmt, idx++, p.is_active ? 1 : 0);
            sqlite3_bind_double(stmt, idx++, p.salary);
        }

        // Execute statement, reset, return success count
        static int step_and_reset(sqlite3_stmt* stmt, sqlite3* db, int rows) {
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                sqlite3_reset(stmt);
                return rows;
            }
            sqlite3_reset(stmt);
            return 0;
        }

      public:
        // Prepare test data before timing
        // For INSERT: creates fresh data
        // For UPDATE: derived class overrides to INSERT first, then modify
        void prepare(int iterations) {
            data().clear();
            int count = (BatchSize == 1) ? iterations : BatchSize;
            data().reserve(count);
            for (int i = 0; i < count; i++) {
                data().push_back(Derived::create_model());
            }
        }
    };

} // namespace storm::benchmark
