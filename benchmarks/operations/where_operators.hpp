#pragma once

/**
 * WHERE Operator Benchmarks - LIKE, BETWEEN, IN, AND/OR
 *
 * Dedicated benchmark classes for advanced WHERE operators that don't fit
 * the standard WhereConfig pattern.
 */

#include "base.hpp"
#include <format>
#include <meta>
#include <plf_hive/plf_hive.h>
#include <vector>

namespace storm::benchmark {

    using storm::orm::where::field;

    // ========================================================================
    // LIKE Operator Benchmark
    // ========================================================================
    template <typename Model, std::meta::info FieldInfo>
    class WhereLikeBenchmark : public DataBenchmarkBase<WhereLikeBenchmark<Model, FieldInfo>, Model, 1> {
        using Base = DataBenchmarkBase<WhereLikeBenchmark<Model, FieldInfo>, Model, 1>;

        std::string pattern_;

      public:
        explicit constexpr WhereLikeBenchmark(std::string pattern, int dataset_size = 1000)
            : Base(dataset_size), pattern_(std::move(pattern)) {}

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

        void prepare(int iterations) {
            Base::prepare_with_insert(iterations);
        }

        void print_info() const {
            constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
            std::cout << "SELECT (WHERE LIKE): " << field_name << " LIKE '" << pattern_ << "'\n";
            std::cout << "  Dataset: " << Base::batch_size() << " rows\n";
        }

        int execute(int iterations) {
            auto where_clause = field<FieldInfo>().like(pattern_);
            Base::qs().where(where_clause);

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                auto result = Base::qs().select();
                if (result.has_value()) {
                    total += result.value().size();
                }
            }
            Base::qs().reset();
            return total;
        }

        int execute_raw(int iterations) {
            sqlite3* db = get_db<Model>();
            if (!db)
                return 0;

            constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
            std::string                sql        = std::format("SELECT * FROM Person WHERE {} LIKE ?", field_name);

            sqlite3_stmt* stmt = nullptr;
            sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
            sqlite3_bind_text(stmt, 1, pattern_.c_str(), -1, SQLITE_TRANSIENT);

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    total++;
                }
            }

            sqlite3_finalize(stmt);
            return total;
        }
    };

    // ========================================================================
    // BETWEEN Operator Benchmark
    // ========================================================================
    template <typename Model, std::meta::info FieldInfo, typename ValueType>
    class WhereBetweenBenchmark
        : public DataBenchmarkBase<WhereBetweenBenchmark<Model, FieldInfo, ValueType>, Model, 1> {
        using Base = DataBenchmarkBase<WhereBetweenBenchmark<Model, FieldInfo, ValueType>, Model, 1>;

        ValueType min_value_;
        ValueType max_value_;

      public:
        explicit constexpr WhereBetweenBenchmark(ValueType min_val, ValueType max_val, int dataset_size = 1000)
            : Base(dataset_size), min_value_(min_val), max_value_(max_val) {}

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

        void prepare(int iterations) {
            Base::prepare_with_insert(iterations);
        }

        void print_info() const {
            constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
            std::cout << "SELECT (WHERE BETWEEN): " << field_name << " BETWEEN " << min_value_ << " AND " << max_value_
                      << "\n";
            std::cout << "  Dataset: " << Base::batch_size() << " rows\n";
        }

        int execute(int iterations) {
            auto where_clause = field<FieldInfo>().between(min_value_, max_value_);
            Base::qs().where(where_clause);

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                auto result = Base::qs().select();
                if (result.has_value()) {
                    total += result.value().size();
                }
            }
            Base::qs().reset();
            return total;
        }

        int execute_raw(int iterations) {
            sqlite3* db = get_db<Model>();
            if (!db)
                return 0;

            constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
            std::string                sql = std::format("SELECT * FROM Person WHERE {} BETWEEN ? AND ?", field_name);

            sqlite3_stmt* stmt = nullptr;
            sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);

            // Bind values based on type
            if constexpr (std::is_same_v<ValueType, int>) {
                sqlite3_bind_int(stmt, 1, min_value_);
                sqlite3_bind_int(stmt, 2, max_value_);
            } else if constexpr (std::is_same_v<ValueType, double>) {
                sqlite3_bind_double(stmt, 1, min_value_);
                sqlite3_bind_double(stmt, 2, max_value_);
            }

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    total++;
                }
            }

            sqlite3_finalize(stmt);
            return total;
        }
    };

    // ========================================================================
    // IN Operator Benchmark
    // ========================================================================
    template <typename Model, std::meta::info FieldInfo, typename ValueType>
    class WhereInBenchmark : public DataBenchmarkBase<WhereInBenchmark<Model, FieldInfo, ValueType>, Model, 1> {
        using Base = DataBenchmarkBase<WhereInBenchmark<Model, FieldInfo, ValueType>, Model, 1>;

        std::vector<ValueType> values_;

      public:
        explicit WhereInBenchmark(std::vector<ValueType> values, int dataset_size = 1000)
            : Base(dataset_size), values_(std::move(values)) {}

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

        void prepare(int iterations) {
            Base::prepare_with_insert(iterations);
        }

        void print_info() const {
            constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
            std::cout << "SELECT (WHERE IN): " << field_name << " IN (";
            for (size_t i = 0; i < values_.size(); i++) {
                if (i > 0)
                    std::cout << ", ";
                std::cout << values_[i];
            }
            std::cout << ")\n";
            std::cout << "  Dataset: " << Base::batch_size() << " rows\n";
        }

        int execute(int iterations) {
            // Build IN expression using field<>().in(values...)
            // We need to use the runtime API since we have a vector
            auto where_clause = build_in_clause();
            Base::qs().where(where_clause);

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                auto result = Base::qs().select();
                if (result.has_value()) {
                    total += result.value().size();
                }
            }
            Base::qs().reset();
            return total;
        }

        int execute_raw(int iterations) {
            sqlite3* db = get_db<Model>();
            if (!db)
                return 0;

            constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);

            // Build SQL with placeholders
            std::string sql = std::format("SELECT * FROM Person WHERE {} IN (", field_name);
            for (size_t i = 0; i < values_.size(); i++) {
                if (i > 0)
                    sql += ", ";
                sql += "?";
            }
            sql += ")";

            sqlite3_stmt* stmt = nullptr;
            sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);

            // Bind all values
            for (size_t i = 0; i < values_.size(); i++) {
                if constexpr (std::is_same_v<ValueType, int>) {
                    sqlite3_bind_int(stmt, static_cast<int>(i + 1), values_[i]);
                } else if constexpr (std::is_same_v<ValueType, double>) {
                    sqlite3_bind_double(stmt, static_cast<int>(i + 1), values_[i]);
                }
            }

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    total++;
                }
            }

            sqlite3_finalize(stmt);
            return total;
        }

      private:
        // Helper to build IN clause from vector
        auto build_in_clause() const {
            // Use InExpression directly since we have a runtime vector
            return storm::orm::where::Expr(
                    std::make_shared<storm::orm::where::ExpressionVariant>(storm::orm::where::InExpression<ValueType>{
                            std::string(std::meta::identifier_of(FieldInfo)), values_
                    })
            );
        }
    };

    // ========================================================================
    // Complex AND/OR Operator Benchmark
    // ========================================================================
    template <
            typename Model,
            std::meta::info FieldInfo1,
            auto            Op1,
            typename ValueType1,
            std::meta::info FieldInfo2,
            auto            Op2,
            typename ValueType2,
            bool IsAnd = true>
    class WhereAndOrBenchmark
        : public DataBenchmarkBase<
                  WhereAndOrBenchmark<Model, FieldInfo1, Op1, ValueType1, FieldInfo2, Op2, ValueType2, IsAnd>,
                  Model,
                  1> {
        using Base = DataBenchmarkBase<
                WhereAndOrBenchmark<Model, FieldInfo1, Op1, ValueType1, FieldInfo2, Op2, ValueType2, IsAnd>,
                Model,
                1>;

        ValueType1 value1_;
        ValueType2 value2_;

      public:
        explicit constexpr WhereAndOrBenchmark(ValueType1 value1, ValueType2 value2, int dataset_size = 1000)
            : Base(dataset_size), value1_(value1), value2_(value2) {}

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

        void prepare(int iterations) {
            Base::prepare_with_insert(iterations);
        }

        void print_info() const {
            constexpr std::string_view field_name1 = std::meta::identifier_of(FieldInfo1);
            constexpr std::string_view field_name2 = std::meta::identifier_of(FieldInfo2);
            constexpr std::string_view op_str1     = Op1.view();
            constexpr std::string_view op_str2     = Op2.view();
            constexpr const char*      logic_op    = IsAnd ? "AND" : "OR";

            std::cout << "SELECT (WHERE " << logic_op << "): " << field_name1 << " " << op_str1 << " " << value1_ << " "
                      << logic_op << " " << field_name2 << " " << op_str2 << " " << value2_ << "\n";
            std::cout << "  Dataset: " << Base::batch_size() << " rows\n";
        }

        int execute(int iterations) {
            auto where_clause = build_where_clause();
            Base::qs().where(where_clause);

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                auto result = Base::qs().select();
                if (result.has_value()) {
                    total += result.value().size();
                }
            }
            Base::qs().reset();
            return total;
        }

        int execute_raw(int iterations) {
            sqlite3* db = get_db<Model>();
            if (!db)
                return 0;

            constexpr std::string_view field_name1 = std::meta::identifier_of(FieldInfo1);
            constexpr std::string_view field_name2 = std::meta::identifier_of(FieldInfo2);
            constexpr std::string_view op_sql1     = get_sql_op(Op1.view());
            constexpr std::string_view op_sql2     = get_sql_op(Op2.view());
            constexpr const char*      logic_op    = IsAnd ? "AND" : "OR";

            std::string sql = std::format(
                    "SELECT * FROM Person WHERE {} {} ? {} {} {} ?",
                    field_name1,
                    op_sql1,
                    logic_op,
                    field_name2,
                    op_sql2
            );

            sqlite3_stmt* stmt = nullptr;
            sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);

            // Bind first value
            if constexpr (std::is_same_v<ValueType1, int>) {
                sqlite3_bind_int(stmt, 1, value1_);
            } else if constexpr (std::is_same_v<ValueType1, double>) {
                sqlite3_bind_double(stmt, 1, value1_);
            } else if constexpr (std::is_same_v<ValueType1, bool>) {
                sqlite3_bind_int(stmt, 1, value1_ ? 1 : 0);
            }

            // Bind second value
            if constexpr (std::is_same_v<ValueType2, int>) {
                sqlite3_bind_int(stmt, 2, value2_);
            } else if constexpr (std::is_same_v<ValueType2, double>) {
                sqlite3_bind_double(stmt, 2, value2_);
            } else if constexpr (std::is_same_v<ValueType2, bool>) {
                sqlite3_bind_int(stmt, 2, value2_ ? 1 : 0);
            }

            int total = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    total++;
                }
            }

            sqlite3_finalize(stmt);
            return total;
        }

      private:
        static constexpr std::string_view get_sql_op(std::string_view op) {
            if (op == ">")
                return ">";
            if (op == ">=")
                return ">=";
            if (op == "<")
                return "<";
            if (op == "<=")
                return "<=";
            if (op == "==")
                return "=";
            if (op == "!=")
                return "!=";
            return "=";
        }

        auto build_where_clause() const {
            constexpr std::string_view op_str1 = Op1.view();
            constexpr std::string_view op_str2 = Op2.view();

            auto cond1 = build_condition1();
            auto cond2 = build_condition2();

            if constexpr (IsAnd) {
                return cond1 && cond2;
            } else {
                return cond1 || cond2;
            }
        }

        auto build_condition1() const {
            constexpr std::string_view op_str = Op1.view();
            if constexpr (op_str == ">") {
                return field<FieldInfo1>() > value1_;
            } else if constexpr (op_str == ">=") {
                return field<FieldInfo1>() >= value1_;
            } else if constexpr (op_str == "<") {
                return field<FieldInfo1>() < value1_;
            } else if constexpr (op_str == "<=") {
                return field<FieldInfo1>() <= value1_;
            } else if constexpr (op_str == "==") {
                return field<FieldInfo1>() == value1_;
            } else if constexpr (op_str == "!=") {
                return field<FieldInfo1>() != value1_;
            }
        }

        auto build_condition2() const {
            constexpr std::string_view op_str = Op2.view();
            if constexpr (op_str == ">") {
                return field<FieldInfo2>() > value2_;
            } else if constexpr (op_str == ">=") {
                return field<FieldInfo2>() >= value2_;
            } else if constexpr (op_str == "<") {
                return field<FieldInfo2>() < value2_;
            } else if constexpr (op_str == "<=") {
                return field<FieldInfo2>() <= value2_;
            } else if constexpr (op_str == "==") {
                return field<FieldInfo2>() == value2_;
            } else if constexpr (op_str == "!=") {
                return field<FieldInfo2>() != value2_;
            }
        }
    };

} // namespace storm::benchmark
