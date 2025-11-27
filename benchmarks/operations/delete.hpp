#pragma once

/**
 * DELETE Benchmark - Delete operations with WHERE clause
 */

#include <sqlite3.h>
#include <meta>
import storm;

namespace storm::benchmark {

using storm::orm::where::field;

// DELETE benchmark with WHERE clause
template<typename Model, std::meta::info FieldInfo, ConstexprString Op, typename ValueType>
class DeleteBenchmark {
    ValueType where_value_;
    QuerySet<Model> qs_;

public:
    constexpr DeleteBenchmark(ValueType where_value) : where_value_(where_value) {}

    void print_info() const {
        constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
        constexpr std::string_view op_str = Op.view();
        std::cout << "Operation: DELETE with WHERE (" << field_name << " " << op_str << " " << where_value_ << ")\n";
    }

    int execute(int iterations) {
        constexpr std::string_view op_str = Op.view();

        int total_deletes = 0;
        for (int i = 0; i < iterations; i++) {
            // Build WHERE clause based on compile-time operator
            auto where_clause = [&]() {
                if constexpr (op_str == ">") {
                    return field<FieldInfo>() > where_value_;
                } else if constexpr (op_str == ">=") {
                    return field<FieldInfo>() >= where_value_;
                } else if constexpr (op_str == "<") {
                    return field<FieldInfo>() < where_value_;
                } else if constexpr (op_str == "<=") {
                    return field<FieldInfo>() <= where_value_;
                } else if constexpr (op_str == "==") {
                    return field<FieldInfo>() == where_value_;
                }
            }();

            // Execute delete
            auto result = qs_.where(where_clause).remove();
            if (result.has_value()) {
                total_deletes += result.value();  // Number of rows deleted
            }
            qs_.reset();
        }
        return total_deletes;
    }

    // Raw SQLite execution for comparison
    int execute_raw(int iterations) {
        constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
        constexpr std::string_view op_str = Op.view();

        auto& conn = QuerySet<Model>::get_default_connection();

        // Build SQL query ONCE
        std::string sql = "DELETE FROM Person WHERE ";
        sql += std::string(field_name) + " " + std::string(op_str) + " ?";

        // Prepare statement ONCE
        auto stmt_result = conn->prepare(sql);
        if (!stmt_result.has_value()) return 0;

        auto& stmt = stmt_result.value();

        int total_deletes = 0;
        for (int i = 0; i < iterations; i++) {
            stmt.reset();

            // Bind parameter
            if constexpr (std::is_same_v<ValueType, int>) {
                if (!stmt.bind_int(1, where_value_).has_value()) continue;
            } else if constexpr (std::is_same_v<ValueType, double>) {
                if (!stmt.bind_double(1, where_value_).has_value()) continue;
            } else if constexpr (std::is_same_v<ValueType, bool>) {
                if (!stmt.bind_int(1, where_value_ ? 1 : 0).has_value()) continue;
            }

            // Execute and get number of changes
            auto step_result = stmt.step();
            if (step_result.has_value()) {
                total_deletes += sqlite3_changes(conn->get());
            }
        }
        return total_deletes;
    }
};

} // namespace storm::benchmark
