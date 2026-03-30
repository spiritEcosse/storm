#pragma once

/**
 * Compile-Time Flat JSON Parser for Benchmark Tests
 *
 * Parses flat JSON structure at compile time using C++26 #embed.
 * Converts JSON to nested C++ structs (BenchmarkTest with WhereClause).
 */

#include <array>
#include <string_view>
#include "schema.hpp"

namespace storm::benchmark {

    // Helper: Skip whitespace
    constexpr auto skip_whitespace(std::string_view json, size_t& pos) -> void {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) {
            pos++;
        }
    }

    // Helper: Skip specific character with whitespace
    constexpr auto skip_char(std::string_view json, size_t& pos, char c) -> void {
        skip_whitespace(json, pos);
        if (pos < json.size() && json[pos] == c) {
            pos++;
        }
    }

    // Parse int
    constexpr auto parse_int(std::string_view json, size_t& pos) -> int {
        skip_whitespace(json, pos);

        int  result   = 0;
        bool negative = false;

        if (pos < json.size() && json[pos] == '-') {
            negative = true;
            pos++;
        }

        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
            result = result * 10 + (json[pos] - '0');
            pos++;
        }

        return negative ? -result : result;
    }

    // Parse double
    constexpr auto parse_double(std::string_view json, size_t& pos) -> double {
        skip_whitespace(json, pos);

        double result   = 0.0;
        bool   negative = false;

        if (pos < json.size() && json[pos] == '-') {
            negative = true;
            pos++;
        }

        // Integer part
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
            result = result * 10.0 + (json[pos] - '0');
            pos++;
        }

        // Decimal part
        if (pos < json.size() && json[pos] == '.') {
            pos++;
            double fraction = 0.1;
            while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
                result += (json[pos] - '0') * fraction;
                fraction *= 0.1;
                pos++;
            }
        }

        return negative ? -result : result;
    }

    // Parse bool
    constexpr auto parse_bool(std::string_view json, size_t& pos) -> bool {
        skip_whitespace(json, pos);

        if (pos + 4 <= json.size() && json.substr(pos, 4) == "true") {
            pos += 4;
            return true;
        }
        if (pos + 5 <= json.size() && json.substr(pos, 5) == "false") {
            pos += 5;
            return false;
        }

        return false;
    }

    // Parse string into ConstexprString
    template <size_t N> constexpr auto parse_string(std::string_view json, size_t& pos) -> ConstexprString<N> {
        skip_whitespace(json, pos);

        ConstexprString<N> result;

        if (pos < json.size() && json[pos] == '"') {
            pos++; // Skip opening quote

            size_t i = 0;
            while (pos < json.size() && json[pos] != '"' && i < N - 1) {
                result.data[i] = json[pos];
                i++;
                pos++;
            }
            result.len     = i;
            result.data[i] = '\0';

            if (pos < json.size() && json[pos] == '"') {
                pos++; // Skip closing quote
            }
        }

        return result;
    }

    // Skip a JSON value (string, number, bool, null)
    constexpr auto skip_value(std::string_view json, size_t& pos) -> void {
        skip_whitespace(json, pos);

        if (pos >= json.size())
            return;

        // String
        if (json[pos] == '"') {
            pos++; // Skip opening quote
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\')
                    pos++; // Skip escape
                pos++;
            }
            if (pos < json.size())
                pos++; // Skip closing quote
            return;
        }

        // Number, bool, null
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']' && json[pos] != ' ' &&
               json[pos] != '\n' && json[pos] != '\r' && json[pos] != '\t') {
            pos++;
        }
    }

    // Parse key name from JSON (returns the key string)
    constexpr auto parse_key(std::string_view json, size_t& pos) -> ConstexprString<64> {
        return parse_string<64>(json, pos);
    }

    // Helper: Parse JSON array of integers into WhereClause (reduces nesting in main parser)
    constexpr auto parse_int_array_into_where(WhereClause& where, std::string_view json, size_t& pos) -> void {
        skip_whitespace(json, pos);
        if (pos >= json.size() || json[pos] != '[') {
            return;
        }

        pos++; // Skip '['
        size_t count = 0;

        while (pos < json.size() && count < WhereClause::MAX_IN_VALUES) {
            skip_whitespace(json, pos);
            if (json[pos] == ']') {
                pos++; // Skip ']'
                break;
            }

            where.in_values_int[count] = parse_int(json, pos);
            count++;
            skip_whitespace(json, pos);

            if (json[pos] == ',') {
                pos++; // Skip ','
            }
        }

        where.in_values_count = count;
        where.value_type      = WhereClause::ValueType::Int;
    }

    // Helper: Parse and assign key-value pair to BenchmarkTest
    constexpr void parse_and_assign_field( // NOSONAR(cpp:S3776) - consteval JSON parser dispatches on 20+ field names; complexity is inherent
            BenchmarkTest& test, std::string_view key, std::string_view json, size_t& pos
    ) { // NOSONAR(cpp:S3776)
        if (key == "test_name") {
            test.test_name = parse_string<64>(json, pos);
        } else if (key == "test_category") {
            test.test_category = parse_string<32>(json, pos);
        } else if (key == "description") {
            test.description = parse_string<128>(json, pos);
        } else if (key == "model") {
            test.model = parse_string<32>(json, pos);
        } else if (key == "operation") {
            test.operation = parse_string<32>(json, pos);
        } else if (key == "where_field") {
            test.where.field = parse_string<32>(json, pos);
        } else if (key == "where_op") {
            test.where.op = parse_string<16>(json, pos);
        } else if (key == "where_value_int") {
            test.where.value_int  = parse_int(json, pos);
            test.where.value_type = WhereClause::ValueType::Int;
        } else if (key == "where_value_double") {
            test.where.value_double = parse_double(json, pos);
            test.where.value_type   = WhereClause::ValueType::Double;
        } else if (key == "where_value_bool") {
            test.where.value_bool = parse_bool(json, pos);
            test.where.value_type = WhereClause::ValueType::Bool;
        } else if (key == "where_value_string") {
            test.where.value_string = parse_string<64>(json, pos);
            test.where.value_type   = WhereClause::ValueType::String;
        } else if (key == "where_value_int2") {
            test.where.value_int2 = parse_int(json, pos);
        } else if (key == "where_value_double2") {
            test.where.value_double2 = parse_double(json, pos);
        } else if (key == "where_in_values") {
            // Parse JSON array of integers using helper (reduces nesting depth)
            parse_int_array_into_where(test.where, json, pos);
        } else if (key == "where_field2") {
            test.where.field2 = parse_string<32>(json, pos);
        } else if (key == "where_op2") {
            test.where.op2 = parse_string<8>(json, pos);
        } else if (key == "where_value2_int") {
            test.where.value2_int = parse_int(json, pos);
        } else if (key == "iterations") {
            test.iterations = parse_int(json, pos);
        } else if (key == "dataset_size") {
            test.dataset_size = parse_int(json, pos);
        } else if (key == "batch_size") {
            test.batch_size = parse_int(json, pos);
        } else if (key == "aggregate_field") {
            test.aggregate_field = parse_string<32>(json, pos);
        } else if (key == "distinct_field") {
            test.distinct_field = parse_string<32>(json, pos);
        } else if (key == "distinct_field2") {
            test.distinct_field2 = parse_string<32>(json, pos);
        } else if (key == "distinct_field3") {
            test.distinct_field3 = parse_string<32>(json, pos);
        } else if (key == "limit_value") {
            test.limit_value = parse_int(json, pos);
        } else if (key == "offset_value") {
            test.offset_value = parse_int(json, pos);
        } else if (key == "order_by_field") {
            test.order_by_field = parse_string<32>(json, pos);
        } else if (key == "order_by_direction") {
            test.order_by_direction = parse_string<8>(json, pos);
        } else if (key == "order_by_collate") {
            test.order_by_collate = parse_string<16>(json, pos);
        } else if (key == "order_by_field2") {
            test.order_by_field2 = parse_string<32>(json, pos);
        } else if (key == "order_by_direction2") {
            test.order_by_direction2 = parse_string<8>(json, pos);
        } else if (key == "group_by_field") {
            test.group_by_field = parse_string<32>(json, pos);
        } else if (key == "group_by_field2") {
            test.group_by_field2 = parse_string<32>(json, pos);
        } else if (key == "having_field") {
            test.having_field = parse_string<32>(json, pos);
        } else if (key == "having_value_int") {
            test.having_value_int = parse_int(json, pos);
        } else if (key == "size_profile") {
            test.size_profile = parse_string<32>(json, pos);
        } else {
            // Unknown key - skip value
            skip_value(json, pos);
        }
    }

    // Parse single benchmark test object from flat JSON
    constexpr auto parse_test_object(std::string_view json, size_t& pos) -> BenchmarkTest {
        BenchmarkTest test;
        skip_char(json, pos, '{');

        while (pos < json.size()) {
            skip_whitespace(json, pos);

            if (json[pos] == '}') {
                pos++; // Skip closing brace
                break;
            }

            // Parse key-value pair and assign to test object
            auto key = parse_key(json, pos);
            skip_char(json, pos, ':');
            parse_and_assign_field(test, key.view(), json, pos);

            skip_whitespace(json, pos);
            if (pos < json.size() && json[pos] == ',') {
                pos++; // Skip comma
            }
        }

        return test;
    }

    // Helper: Skip to end of JSON object (reduces nesting depth)
    constexpr auto skip_json_object(std::string_view json, size_t& pos) -> void {
        int brace_depth = 0;
        while (pos < json.size()) {
            if (json[pos] == '{')
                brace_depth++;
            if (json[pos] == '}') {
                brace_depth--;
                if (brace_depth == 0) {
                    pos++;
                    break;
                }
            }
            pos++;
        }
    }

    // Count tests in JSON array (needed for std::array size)
    constexpr auto count_tests(std::string_view json) -> size_t {
        size_t count = 0;
        size_t pos   = 0;

        skip_char(json, pos, '[');

        while (pos < json.size()) {
            skip_whitespace(json, pos);

            if (json[pos] == ']') {
                break;
            }

            if (json[pos] == '{') {
                count++;
                skip_json_object(json, pos);
            }

            skip_whitespace(json, pos);
            if (pos < json.size() && json[pos] == ',') {
                pos++;
            }
        }

        return count;
    }

    // Parse entire JSON array
    template <size_t N> constexpr auto parse_tests(std::string_view json) -> std::array<BenchmarkTest, N> {
        std::array<BenchmarkTest, N> tests{};

        size_t pos = 0;
        skip_char(json, pos, '[');

        for (size_t i = 0; i < N; i++) {
            skip_whitespace(json, pos);

            if (pos >= json.size() || json[pos] == ']') {
                break;
            }

            tests[i] = parse_test_object(json, pos);

            skip_whitespace(json, pos);
            if (pos < json.size() && json[pos] == ',') {
                pos++; // Skip comma
            }
        }

        return tests;
    }

    // Load and parse JSON from embedded file
    consteval auto load_benchmark_tests() {
        // Embed JSON file at compile time
        static constexpr const char json_data[] = {
#embed "tests/benchmark_tests.json"
                , '\0'
        };

        constexpr std::string_view json_str(json_data);

        // Count tests to determine array size
        constexpr size_t test_count = count_tests(json_str);

        // Parse tests
        return parse_tests<test_count>(json_str);
    }

    // Compile-time constant tests loaded from JSON
    // Define this AFTER load_benchmark_tests() is defined
    inline constexpr auto BENCHMARK_TESTS = load_benchmark_tests();

} // namespace storm::benchmark
