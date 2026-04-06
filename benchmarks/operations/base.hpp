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

#include "../raw_helpers.hpp"
#include <format>
#include <plf_hive/plf_hive.h>
#include <iostream>
#include <string_view>
#include <vector>

namespace storm::benchmark {

    // ========================================================================
    // get_db: Helper to get raw sqlite3* from default connection
    // ========================================================================
    template <typename Model> auto get_db() -> sqlite3* {
        auto& conn = storm::QuerySet<Model>::get_default_connection();
        return conn->get();
    }

    // ========================================================================
    // OperationDispatcher: Compile-time binding of operation to method
    // ========================================================================
    enum class OperationType { Insert, UpdatePK, Delete, Select };

    template <OperationType Op> struct OperationDispatcher;

    // INSERT operation specialization
    template <> struct OperationDispatcher<OperationType::Insert> {
        static constexpr auto name() -> std::string_view {
            return "INSERT";
        }

        template <typename QS, typename Data> static auto call(QS& qs, const Data& data) {
            return qs.insert(data);
        }
    };

    // UPDATE operation specialization
    template <> struct OperationDispatcher<OperationType::UpdatePK> {
        static constexpr auto name() -> std::string_view {
            return "UPDATE by PK";
        }

        template <typename QS, typename Data> static auto call(QS& qs, const Data& data) {
            return qs.update(data);
        }
    };

    // DELETE operation specialization (for future use)
    template <> struct OperationDispatcher<OperationType::Delete> {
        static constexpr auto name() -> std::string_view {
            return "DELETE";
        }

        template <typename QS, typename Data> static auto call(QS& qs, const Data& data) {
            return qs.remove(data);
        }
    };

    // SELECT operation specialization
    template <> struct OperationDispatcher<OperationType::Select> {
        static constexpr auto name() -> std::string_view {
            return "SELECT WHERE";
        }
    };

    // CRTP base class for data-driven benchmarks (Insert, UpdateByPK)
    // BatchSize is now a runtime parameter for fair comparison with Storm ORM
    template <typename Derived, typename Model, size_t FieldsPerRow = non_pk_field_count<Model>()>
    class DataBenchmarkBase {
      private:
        QuerySet<Model>    qs_;
        std::vector<Model> data_;
        int                batch_size_ = 1; // Runtime batch size

      protected:
        // Accessor methods for derived classes
        auto qs() -> QuerySet<Model>& {
            return qs_;
        }
        auto qs() const -> const QuerySet<Model>& {
            return qs_;
        }
        auto data() -> std::vector<Model>& {
            return data_;
        }
        auto data() const -> const std::vector<Model>& {
            return data_;
        }
        auto batch_size() const -> int {
            return batch_size_;
        }
        auto set_batch_size(int size) -> void {
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
        // index parameter allows generating varied data (useful for SELECT WHERE benchmarks)
        static auto create_model(int index = 0) -> Model {
            return Model{
                    .name      = std::format("Person{}", index),
                    .age       = 20 + (index % 50),
                    .salary    = 30000.0 + (index * 1000.0),
                    .is_active = (index % 2 == 0)
            };
        }

        // Execute statement, reset, return success count
        static auto step_and_reset(sqlite3_stmt* stmt, [[maybe_unused]] sqlite3* db, int rows) -> int {
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
        auto prepare(int iterations) -> void {
            data().clear();
            int count = (batch_size_ == 1) ? iterations : batch_size_;
            data().reserve(count);
            for (int i = 0; i < count; i++) {
                data().push_back(Derived::create_model(i));
            }
        }

        // Extended prepare: clears table, generates data, inserts and retrieves IDs
        // Used by UPDATE and DELETE benchmarks that need existing rows with valid PKs
        auto prepare_with_insert(int iterations) -> void {
            // 1. Clear table using raw SQLite
            if (sqlite3* db = get_db<Model>()) {
                auto delete_sql = std::format("DELETE FROM {}", std::meta::identifier_of(^^Model));
                sqlite3_exec(db, delete_sql.c_str(), nullptr, nullptr, nullptr);
            }

            // 2. Generate test data
            prepare(iterations);

            // 3. INSERT data
            auto insert_result = qs().insert(data()).execute();
            if (!insert_result.has_value()) {
                std::cerr << "Failed to insert test data for benchmark\n";
                return;
            }

            // 4. SELECT back to get the auto-generated IDs
            auto select_result = qs().select().execute();
            if (!select_result.has_value()) {
                std::cerr << "Failed to select test data for benchmark\n";
                return;
            }

            // 5. Store retrieved IDs back into data
            const auto& selected = select_result.value();
            size_t      i        = 0;
            for (const auto& row : selected) {
                if (i >= data().size())
                    break;
                data()[i].id = row.id;
                i++;
            }
        }

        // ====================================================================
        // Unified print_info() with compile-time operation name
        // ====================================================================
        template <OperationType Op> auto print_info_unified() const -> void {
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
        template <OperationType Op> auto execute_unified(int iterations) -> int {
            int total = 0;
            if (batch_size_ == 1) {
                for (int i = 0; i < iterations; i++) {
                    OperationDispatcher<Op>::call(qs(), data()[i]).execute();
                    total++;
                }
            } else {
                for (int i = 0; i < iterations; i++) {
                    OperationDispatcher<Op>::call(qs(), data()).execute();
                    total += data().size();
                }
            }
            return total;
        }
    };

} // namespace storm::benchmark
