#pragma once

/**
 * SELECT Benchmark - WHERE clause queries
 */

#include <meta>
import storm;

namespace storm::benchmark {

using storm::orm::where::field;

// Forward declare field dispatcher
template<typename Model>
consteval std::meta::info dispatch_field(std::string_view field_name) {
    constexpr auto model_info = ^^Model;

    for (std::meta::info member :
         std::meta::nonstatic_data_members_of(model_info, std::meta::access_context::unchecked())) {
        if (std::meta::identifier_of(member) == field_name) {
            return member;
        }
    }

    throw "Field not found";
}

// SELECT benchmark with WHERE clause
template<typename Model, std::meta::info FieldInfo, ConstexprString Op, typename ValueType>
class SelectBenchmark {
    ValueType value_;
    QuerySet<Model> qs_;

public:
    constexpr SelectBenchmark(ValueType value) : value_(value) {}

    void print_info() const {
        constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
        constexpr std::string_view op_str = Op.view();
        std::cout << "Field: " << field_name << ", Operator: " << op_str << ", Value: " << value_ << "\n";
    }

    int execute(int iterations) {
        constexpr std::string_view op_str = Op.view();

        int total_rows = 0;
        for (int i = 0; i < iterations; i++) {
            // Execute query based on compile-time operator
            if constexpr (op_str == ">") {
                auto results = qs_.where(field<FieldInfo>() > value_).select();
                total_rows += results.value().size();
            } else if constexpr (op_str == ">=") {
                auto results = qs_.where(field<FieldInfo>() >= value_).select();
                total_rows += results.value().size();
            } else if constexpr (op_str == "<") {
                auto results = qs_.where(field<FieldInfo>() < value_).select();
                total_rows += results.value().size();
            } else if constexpr (op_str == "<=") {
                auto results = qs_.where(field<FieldInfo>() <= value_).select();
                total_rows += results.value().size();
            } else if constexpr (op_str == "==") {
                auto results = qs_.where(field<FieldInfo>() == value_).select();
                total_rows += results.value().size();
            }
            qs_.reset();
        }
        return total_rows;
    }

    // Raw SQLite execution for comparison
    int execute_raw(int iterations) {
        constexpr std::string_view field_name = std::meta::identifier_of(FieldInfo);
        constexpr std::string_view op_str = Op.view();

        auto& conn = QuerySet<Model>::get_default_connection();

        // Build SQL query ONCE
        std::string sql = "SELECT id, name, age, is_active, salary FROM Person WHERE ";
        sql += std::string(field_name) + " " + std::string(op_str) + " ?";

        // Prepare statement ONCE (outside loop - realistic usage)
        auto stmt_result = conn->prepare(sql);
        if (!stmt_result.has_value()) return 0;

        auto& stmt = stmt_result.value();

        int total_rows = 0;
        for (int i = 0; i < iterations; i++) {
            // Reset statement for reuse
            stmt.reset();

            // Bind parameter
            if constexpr (std::is_same_v<ValueType, int>) {
                if (!stmt.bind_int(1, value_).has_value()) continue;
            } else if constexpr (std::is_same_v<ValueType, double>) {
                if (!stmt.bind_double(1, value_).has_value()) continue;
            } else if constexpr (std::is_same_v<ValueType, bool>) {
                if (!stmt.bind_int(1, value_ ? 1 : 0).has_value()) continue;
            }

            // Execute and count rows
            while (true) {
                auto step_result = stmt.step();
                if (!step_result.has_value() || !step_result.value()) break;

                // Manually construct object from row (fair comparison)
                Model obj;
                obj.id = stmt.extract_int(0);
                obj.name = std::string(stmt.extract_text_view(1));
                obj.age = stmt.extract_int(2);
                obj.is_active = stmt.extract_bool(3);
                obj.salary = stmt.extract_double(4);
                total_rows++;
            }
        }
        return total_rows;
    }
};

} // namespace storm::benchmark
