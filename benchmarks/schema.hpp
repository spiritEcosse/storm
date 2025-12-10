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
        ConstexprString<8>  op;

        // Union-like for different value types
        int                 value_int    = 0;
        double              value_double = 0.0;
        bool                value_bool   = false;
        ConstexprString<64> value_string;

        enum class ValueType { None, Int, Double, Bool, String };
        ValueType value_type = ValueType::None;

        consteval WhereClause() = default;
    };

    // Main benchmark test with nested structure
    struct BenchmarkTest {
        ConstexprString<64>  test_name;
        ConstexprString<32>  test_category;
        ConstexprString<128> description;
        ConstexprString<32>  model;
        ConstexprString<32>  operation;

        // Nested WHERE clause
        WhereClause where;

        // Test parameters
        int iterations   = 1000;
        int dataset_size = 10000;
        int batch_size   = 1; // For batch operations (1 = single operation)

        consteval BenchmarkTest() = default;
    };

    // Note: BENCHMARK_TESTS will be defined after including benchmark_json_parser.hpp
    // Forward declaration only
    consteval auto load_benchmark_tests();

} // namespace storm::benchmark
