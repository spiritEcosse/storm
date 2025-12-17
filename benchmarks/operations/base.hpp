#pragma once

/**
 * CRTP Base Class for Data-Driven Benchmarks
 *
 * Provides shared functionality for Insert and UpdateByPK benchmarks:
 * - Data storage and generation
 * - QuerySet member
 * - SQLite binding utilities
 * - Common constants (SQLite limits)
 * - Unified execute() with compile-time operation dispatch
 */

#include <sqlite3.h>
#include <vector>
#include <iostream>
#include <string_view>

import storm;

namespace storm::benchmark {

    // ========================================================================
    // OperationDispatcher: Compile-time binding of operation to method
    // ========================================================================
    enum class OperationType { Insert, UpdatePK, Delete };

    template <OperationType Op> struct OperationDispatcher;

    // INSERT operation specialization
    template <> struct OperationDispatcher<OperationType::Insert> {
        static constexpr std::string_view name() {
            return "INSERT";
        }

        template <typename QS, typename Data> static auto call(QS& qs, const Data& data) {
            return qs.insert(data);
        }
    };

    // UPDATE operation specialization
    template <> struct OperationDispatcher<OperationType::UpdatePK> {
        static constexpr std::string_view name() {
            return "UPDATE by PK";
        }

        template <typename QS, typename Data> static auto call(QS& qs, const Data& data) {
            return qs.update(data);
        }
    };

    // DELETE operation specialization (for future use)
    template <> struct OperationDispatcher<OperationType::Delete> {
        static constexpr std::string_view name() {
            return "DELETE";
        }

        template <typename QS, typename Data> static auto call(QS& qs, const Data& data) {
            return qs.remove(data);
        }
    };

    // CRTP base class for data-driven benchmarks (Insert, UpdateByPK)
    // BatchSize is now a runtime parameter for fair comparison with Storm ORM
    template <typename Derived, typename Model, size_t FieldsPerRow = 4> class DataBenchmarkBase {
      private:
        QuerySet<Model>    qs_;
        std::vector<Model> data_;
        int                batch_size_ = 1; // Runtime batch size

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
        int batch_size() const {
            return batch_size_;
        }
        void set_batch_size(int size) {
            batch_size_ = size;
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
        static int step_and_reset(sqlite3_stmt* stmt, [[maybe_unused]] sqlite3* db, int rows) {
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                sqlite3_reset(stmt);
                return rows;
            }
            sqlite3_reset(stmt);
            return 0;
        }

      public:
        // Constructor with optional batch size
        explicit DataBenchmarkBase(int batch_size = 1) : batch_size_(batch_size) {}

        // Basic prepare: generates test data only (for INSERT benchmark)
        void prepare(int iterations) {
            data().clear();
            int count = (batch_size_ == 1) ? iterations : batch_size_;
            data().reserve(count);
            for (int i = 0; i < count; i++) {
                data().push_back(Derived::create_model());
            }
        }

        // Extended prepare: clears table, generates data, inserts to get IDs
        // Used by UPDATE and DELETE benchmarks that need existing rows with valid PKs
        void prepare_with_insert(int iterations) {
            // 1. Clear table using raw SQLite
            auto&    conn = storm::QuerySet<Model>::get_default_connection();
            sqlite3* db   = conn->get();
            if (db) {
                sqlite3_exec(db, "DELETE FROM Person", nullptr, nullptr, nullptr);
            }

            // 2. Generate test data
            prepare(iterations);

            // 3. INSERT data to get valid primary keys
            auto insert_result = qs().insert(data(), storm::orm::statements::InsertOptions{.return_ids = true});
            if (!insert_result.has_value()) {
                std::cerr << "Failed to insert test data for benchmark\n";
                return;
            }

            // 4. Store returned IDs back into data
            const auto& ids = insert_result.value();
            for (size_t i = 0; i < data().size() && i < ids.size(); i++) {
                data()[i].id = ids[i];
            }
        }

        // ====================================================================
        // Unified print_info() with compile-time operation name
        // ====================================================================
        template <OperationType Op> void print_info_unified() const {
            constexpr std::string_view op_name = OperationDispatcher<Op>::name();
            if (batch_size_ == 1)
                std::cout << "Operation: " << op_name << " (single row)\n";
            else
                std::cout << "Operation: " << op_name << " (batch, " << batch_size_ << " rows per operation)\n";
        }

        // ====================================================================
        // Unified execute() with compile-time operation dispatch
        // Runtime batch size check (same as Storm ORM for fair comparison)
        // ====================================================================
        template <OperationType Op> int execute_unified(int iterations) {
            int total = 0;
            if (batch_size_ == 1) {
                for (int i = 0; i < iterations; i++) {
                    OperationDispatcher<Op>::call(qs(), data()[i]);
                    total++;
                }
            } else {
                for (int i = 0; i < iterations; i++) {
                    OperationDispatcher<Op>::call(qs(), data());
                    total += data().size();
                }
            }
            return total;
        }
    };

} // namespace storm::benchmark
