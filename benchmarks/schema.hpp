#pragma once

/**
 * Benchmark Test Schema with Nested Structs
 *
 * JSON is FLAT (for constexpr parsing):
 *   "where_field": "age", "where_op": ">", "where_value_int": 30
 *
 * C++ is NESTED (for clean organization):
 *   test.where.field, test.where.op, test.where.value_int
 */

#include <array>
#include <string_view>

namespace storm::benchmark {

    // Fixed-size constexpr string
    template <size_t N> struct ConstexprString {
        std::array<char, N> data{};
        size_t              len = 0;

        consteval ConstexprString() = default;

        explicit consteval ConstexprString(const char* str) {
            size_t i = 0;
            while (str[i] != '\0' && i < N - 1) {
                data[i] = str[i];
                ++i;
            }
            len       = i;
            data[len] = '\0';
        }

        constexpr std::string_view view() const {
            return std::string_view(data.data(), len);
        }

        constexpr const char* c_str() const {
            return data.data();
        }

        constexpr bool operator==(std::string_view other) const {
            return view() == other;
        }
    };

    // Nested WHERE clause structure
    struct WhereClause {
        ConstexprString<32> field;
        ConstexprString<16> op; // Increased for operators like "BETWEEN", "LIKE", "IN", "AND_OR"

        // Union-like for different value types
        int                 value_int    = 0;
        double              value_double = 0.0;
        bool                value_bool   = false;
        ConstexprString<64> value_string;

        // Second value for BETWEEN operator
        int    value_int2    = 0;
        double value_double2 = 0.0;

        // Array values for IN operator (fixed size for compile-time)
        static constexpr size_t        MAX_IN_VALUES = 10;
        std::array<int, MAX_IN_VALUES> in_values_int{};
        size_t                         in_values_count = 0;

        // Second field for complex AND/OR expressions
        ConstexprString<32> field2;
        ConstexprString<8>  op2;
        int                 value2_int = 0;

        enum class ValueType { None, Int, Double, Bool, String };
        ValueType value_type = ValueType::None;

        consteval WhereClause() = default;
    };

    // Main benchmark test with nested structure
    struct BenchmarkTest { // NOSONAR(cpp:S1820)
        ConstexprString<64>  test_name;
        ConstexprString<32>  test_category;
        ConstexprString<128> description;
        ConstexprString<32>  model;
        ConstexprString<32>  operation;

        // Nested WHERE clause
        WhereClause where;

        // Aggregate field (for SUM, AVG, MIN, MAX, COUNT(field), COUNT(DISTINCT))
        ConstexprString<32> aggregate_field;

        // Distinct field (for SELECT DISTINCT operations)
        ConstexprString<32> distinct_field;
        ConstexprString<32> distinct_field2; // Second field for multi-field DISTINCT
        ConstexprString<32> distinct_field3; // Third field for multi-field DISTINCT

        // Test parameters
        int iterations   = 1000;
        int dataset_size = 10000;
        int batch_size   = 1; // For batch operations (1 = single operation)

        // LIMIT/OFFSET clause parameters
        int limit_value  = 0; // 0 = no LIMIT, > 0 = LIMIT n
        int offset_value = 0; // 0 = no OFFSET, > 0 = OFFSET n

        // ORDER BY clause parameters
        ConstexprString<32> order_by_field;      // Field name to order by (empty = no ORDER BY)
        ConstexprString<8>  order_by_direction;  // "ASC" or "DESC" (empty = default ASC)
        ConstexprString<16> order_by_collate;    // "NOCASE", "BINARY", "RTRIM" (empty = no COLLATE)
        ConstexprString<32> order_by_field2;     // Second field for multi-field ORDER BY
        ConstexprString<8>  order_by_direction2; // "ASC" or "DESC" for second field

        // GROUP BY clause parameters
        ConstexprString<32> group_by_field;  // Field name to group by (empty = no GROUP BY)
        ConstexprString<32> group_by_field2; // Second field for multi-field GROUP BY

        // HAVING clause parameters
        ConstexprString<32> having_field;         // Field name for HAVING condition
        int                 having_value_int = 0; // Integer value for HAVING comparison

        // Size profile for automatic iteration over sizes
        // Values: "none", "batch_standard", "batch_insert_edge", "batch_update_edge",
        //         "dataset_standard", "dataset_small"
        ConstexprString<32> size_profile; // Empty or "none" = non-scaled test (run once)

        consteval BenchmarkTest() = default;
    };

    // Note: BENCHMARK_TESTS will be defined after including benchmark_json_parser.hpp
    // Forward declaration only
    consteval auto load_benchmark_tests();

} // namespace storm::benchmark
