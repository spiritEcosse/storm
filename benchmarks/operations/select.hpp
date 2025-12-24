#pragma once

/**
 * SELECT Benchmark - WHERE clause queries
 *
 * Tests SELECT performance with various WHERE clause patterns.
 * Inherits from DataBenchmarkBase for consistent data management.
 *
 * Workflow:
 * 1. prepare(): Clear table, insert test data (using base class)
 * 2. execute(): Storm ORM where().select()
 * 3. execute_raw(): Raw SQLite SELECT...WHERE - native sqlite3 API only
 *
 * FAIR COMPARISON: Both Storm ORM and raw SQLite use prepared statements
 * with parameter binding for WHERE clause values.
 */

#include "base.hpp"
#include <format>
#include <meta>
#include <plf_hive/plf_hive.h>

namespace storm::benchmark {

    using storm::orm::where::field;

    // Forward declare field dispatcher - compile-time field lookup by name
    template <typename Model> consteval std::meta::info dispatch_field(std::string_view field_name) {
        constexpr auto model_info = ^^Model;

        for (std::meta::info member :
             std::meta::nonstatic_data_members_of(model_info, std::meta::access_context::unchecked())) {
            if (std::meta::identifier_of(member) == field_name) {
                return member;
            }
        }

        // Field not found - compile error at consteval time
        throw "Field not found";
    }

    // SELECT benchmark with WHERE clause
    // FieldInfo: compile-time field reflection info
    // Op: comparison operator as ConstexprString (">", ">=", "<", "<=", "==") - use auto for NTTP deduction
    // ValueType: type of WHERE clause value (int, double, bool)
    template <typename Model, std::meta::info FieldInfo, auto Op, typename ValueType>
    class SelectBenchmark : public DataBenchmarkBase<SelectBenchmark<Model, FieldInfo, Op, ValueType>, Model, 1> {
        using Base = DataBenchmarkBase<SelectBenchmark<Model, FieldInfo, Op, ValueType>, Model, 1>;

        ValueType value_;

      public:
        // dataset_size: number of records to insert for querying
        explicit constexpr SelectBenchmark(ValueType value, int dataset_size = 1000)
            : Base(dataset_size), value_(value) {}

        // Override create_model to generate varied data for WHERE clause testing
        static Model create_model(int index = 0) {
            int i = index + 1;
            return Model{
                    .id        = 0,
                    .name      = std::format("Person{}", i),
                    .age       = 20 + (i % 50),
                    .is_active = (i % 2 == 0),
                    .salary    = 30000.0 + (i * 1000.0)
            };
        }

        // ====================================================================
        // print_info - SELECT-specific info with field, operator, value
        // ====================================================================
        void print_info() const {
            constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
            constexpr std::string_view op_str     = Op.view();
            constexpr std::string_view op_name    = OperationDispatcher<OperationType::Select>::name();

            std::cout << "Operation: " << op_name << "\n";
            std::cout << "  Field: " << field_name << ", Operator: " << op_str << ", Value: " << value_ << "\n";
            std::cout << "  Dataset: " << Base::batch_size() << " rows\n";
        }

        // ====================================================================
        // prepare - clear table, generate varied data, insert for querying
        // Uses create_model(index) override for varied data (age, is_active)
        // ====================================================================
        void prepare(int iterations) {
            Base::prepare_with_insert(iterations);
        }

        // ====================================================================
        // build_where_clause - extract WHERE clause building to separate function
        // Returns the WHERE expression based on compile-time operator
        // ====================================================================
      private:
        auto build_where_clause() const {
            constexpr std::string_view op_str = Op.view();

            if constexpr (op_str == ">") {
                return field<FieldInfo>() > value_;
            } else if constexpr (op_str == ">=") {
                return field<FieldInfo>() >= value_;
            } else if constexpr (op_str == "<") {
                return field<FieldInfo>() < value_;
            } else if constexpr (op_str == "<=") {
                return field<FieldInfo>() <= value_;
            } else if constexpr (op_str == "==") {
                return field<FieldInfo>() == value_;
            } else if constexpr (op_str == "!=") {
                return field<FieldInfo>() != value_;
            }
        }

      public:
        // ====================================================================
        // execute - Storm ORM SELECT with WHERE clause
        // OPTIMIZATION: where_clause is created once, and SelectStatement caches by expression pointer.
        // When the same expression object is reused, SQL string building is skipped entirely.
        // NOTE: reset() moved outside loop to enable expression address caching optimization.
        // ====================================================================
        int execute(int iterations) {
            auto where_clause = build_where_clause();

            int total_rows = 0;
            Base::qs().where(where_clause); // Set WHERE once
            for (int i = 0; i < iterations; i++) {
                auto results = Base::qs().select();
                total_rows += results.value().size();
            }
            Base::qs().reset(); // Reset after loop
            return total_rows;
        }

        // ====================================================================
        // execute_raw - Raw SQLite using ONLY native sqlite3 API
        // No Storm features - pure sqlite3_* functions
        // ====================================================================
      private:
        // Helper: Build SQL query string
        static std::string build_select_sql() {
            constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
            constexpr std::string_view op_str     = Op.view();

            std::string sql = "SELECT id, name, age, is_active, salary FROM Person WHERE ";
            sql += std::string(field_name);
            sql += " ";
            sql += std::string(op_str);
            sql += " ?";
            return sql;
        }

        // Helper: Bind WHERE clause value using native sqlite3 API
        static void bind_where_value(sqlite3_stmt* stmt, const ValueType& value) {
            if constexpr (std::is_same_v<ValueType, int>) {
                sqlite3_bind_int(stmt, 1, value);
            } else if constexpr (std::is_same_v<ValueType, double>) {
                sqlite3_bind_double(stmt, 1, value);
            } else if constexpr (std::is_same_v<ValueType, bool>) {
                sqlite3_bind_int(stmt, 1, value ? 1 : 0);
            } else if constexpr (std::is_same_v<ValueType, std::string> || std::is_same_v<ValueType, const char*>) {
                if constexpr (std::is_same_v<ValueType, std::string>) {
                    sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_TRANSIENT);
                } else {
                    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
                }
            }
        }

        // Row extraction - manual hardcoded extraction (fastest approach)
        __attribute__((always_inline)) static Model extract_row(sqlite3_stmt* stmt) {
            Model obj;
            obj.id        = sqlite3_column_int64(stmt, 0);
            obj.name      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            obj.age       = sqlite3_column_int(stmt, 2);
            obj.is_active = sqlite3_column_int(stmt, 3) != 0;
            obj.salary    = sqlite3_column_double(stmt, 4);
            return obj;
        }

      public:
        // execute_raw - Raw SQLite benchmark
        int execute_raw(int iterations) {
            auto&    conn = storm::QuerySet<Model>::get_default_connection();
            sqlite3* db   = conn->get();
            if (!db)
                return 0;

            const std::string sql = build_select_sql();

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return 0;
            }

            // Bind WHERE value once (same value for all iterations)
            bind_where_value(stmt, value_);

            int total_rows = 0;
            for (int i = 0; i < iterations; i++) {
                // sqlite3_reset() is REQUIRED to re-execute a statement after sqlite3_step() returns SQLITE_DONE
                // Note: reset does NOT clear bindings, so we can bind once before the loop
                sqlite3_reset(stmt);

                plf::hive<Model> results;
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    results.insert(extract_row(stmt));
                }
                total_rows += results.size();
            }

            sqlite3_finalize(stmt);
            return total_rows;
        }
    };

} // namespace storm::benchmark
