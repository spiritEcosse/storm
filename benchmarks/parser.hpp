#pragma once

/**
 * Compile-Time Nested JSON Parser for Benchmark Tests
 *
 * Parses nested JSON using C++26 #embed at compile time. Converts JSON to
 * nested C++ structs defined in schema.hpp (BenchmarkTest + WhereSpec,
 * OrderBySpec, GroupBySpec with nested HavingSpec, DistinctSpec, LimitSpec,
 * AggregateSpec, JoinSpec).
 *
 * JSON layout example:
 *   {
 *     "test_name": "...",
 *     "model": "Person",
 *     "operation": "order_by_where",
 *     "iterations": 1000,
 *     "dataset_size": 10000,
 *     "where":    { "field": "age", "op": ">", "value": 30 },
 *     "order_by": [ { "field": "salary", "direction": "DESC" } ],
 *     "group_by": { "fields": ["department"],
 *                   "having": { "field": "age", "op": ">", "value": 30 } },
 *     "distinct": { "fields": ["name", "age"] },
 *     "limit":    { "value": 10, "offset": 20 },
 *     "aggregate": { "func": "sum", "field": "salary" },
 *     "join":     { "type": "inner", "related": "User", "fk": "sender" }
 *   }
 *
 * `where.value` is sniffed from the JSON token type — int, double, bool,
 * or string — and stored in a TypedValue tagged union. Two-value operators
 * (BETWEEN) use `value2`; IN uses `in_values`. A second clause combined
 * with AND/OR is expressed via `"and": { ... }` or `"or": { ... }` inside
 * `where`, encoded as flat fields (`field2`, `op2`, `value2_rhs`) plus
 * a `combine_and`/`combine_or` flag.
 */

#include <array>
#include <string_view>
#include "schema.hpp"

namespace storm::benchmark {

    // ========================================================================
    // Primitive parsers
    // ========================================================================
    constexpr auto skip_whitespace(std::string_view json, size_t& pos) -> void {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) {
            pos++;
        }
    }

    constexpr auto skip_char(std::string_view json, size_t& pos, char c) -> void {
        skip_whitespace(json, pos);
        if (pos < json.size() && json[pos] == c) {
            pos++;
        }
    }

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

    constexpr auto parse_double(std::string_view json, size_t& pos) -> double {
        skip_whitespace(json, pos);
        double result   = 0.0;
        bool   negative = false;
        if (pos < json.size() && json[pos] == '-') {
            negative = true;
            pos++;
        }
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
            result = result * 10.0 + (json[pos] - '0');
            pos++;
        }
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

    template <size_t N> constexpr auto parse_string(std::string_view json, size_t& pos) -> ConstexprString<N> {
        skip_whitespace(json, pos);
        ConstexprString<N> result;
        if (pos < json.size() && json[pos] == '"') {
            pos++; // opening quote
            size_t i = 0;
            while (pos < json.size() && json[pos] != '"' && i < N - 1) {
                result.data[i] = json[pos];
                i++;
                pos++;
            }
            result.len     = i;
            result.data[i] = '\0';
            if (pos < json.size() && json[pos] == '"') {
                pos++; // closing quote
            }
        }
        return result;
    }

    constexpr auto parse_key(std::string_view json, size_t& pos) -> ConstexprString<64> {
        return parse_string<64>(json, pos);
    }

    // ========================================================================
    // Value skipping (for unknown keys and recursive object/array skip)
    // ========================================================================
    constexpr auto skip_value(std::string_view json, size_t& pos) -> void { // NOSONAR(cpp:S3776)
        skip_whitespace(json, pos);
        if (pos >= json.size()) {
            return;
        }

        if (json[pos] == '"') {
            pos++;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\') {
                    pos++;
                }
                pos++;
            }
            if (pos < json.size()) {
                pos++;
            }
            return;
        }

        if (json[pos] == '{' || json[pos] == '[') {
            char open  = json[pos];
            char close = (open == '{') ? '}' : ']';
            int  depth = 0;
            while (pos < json.size()) {
                if (json[pos] == '"') {
                    pos++;
                    while (pos < json.size() && json[pos] != '"') {
                        if (json[pos] == '\\') {
                            pos++;
                        }
                        pos++;
                    }
                    if (pos < json.size()) {
                        pos++;
                    }
                    continue;
                }
                if (json[pos] == open) {
                    depth++;
                } else if (json[pos] == close) {
                    depth--;
                    if (depth == 0) {
                        pos++;
                        return;
                    }
                }
                pos++;
            }
            return;
        }

        // number, bool, null
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']' && json[pos] != ' ' &&
               json[pos] != '\n' && json[pos] != '\r' && json[pos] != '\t') {
            pos++;
        }
    }

    // ========================================================================
    // TypedValue — sniff the next JSON token and populate a tagged union.
    //
    // Lookahead: " → string, t/f → bool, digit/-  → scan ahead for '.' → double else int.
    // ========================================================================
    constexpr auto parse_typed_value(std::string_view json, size_t& pos) -> TypedValue {
        skip_whitespace(json, pos);
        TypedValue tv;
        if (pos >= json.size()) {
            return tv;
        }

        char c = json[pos];

        if (c == '"') {
            tv.kind      = TypedValue::Kind::String;
            tv.as_string = parse_string<64>(json, pos);
            return tv;
        }
        if (c == 't' || c == 'f') {
            tv.kind    = TypedValue::Kind::Bool;
            tv.as_bool = parse_bool(json, pos);
            return tv;
        }
        // Number — peek ahead for '.' to decide int vs double
        bool has_dot = false;
        for (size_t q = pos; q < json.size(); q++) {
            char qc = json[q];
            if (qc == '.') {
                has_dot = true;
                break;
            }
            if (qc == ',' || qc == '}' || qc == ']' || qc == ' ' || qc == '\n' || qc == '\r' || qc == '\t') {
                break;
            }
        }
        if (has_dot) {
            tv.kind      = TypedValue::Kind::Double;
            tv.as_double = parse_double(json, pos);
        } else {
            tv.kind   = TypedValue::Kind::Int;
            tv.as_int = parse_int(json, pos);
        }
        return tv;
    }

    // ========================================================================
    // Array of integers — used by where.in_values
    // ========================================================================
    constexpr auto parse_int_array_into_where(WhereSpec& where, std::string_view json, size_t& pos) -> void {
        skip_whitespace(json, pos);
        if (pos >= json.size() || json[pos] != '[') {
            return;
        }
        pos++; // '['
        size_t count = 0;
        while (pos < json.size() && count < WhereSpec::MAX_IN_VALUES) {
            skip_whitespace(json, pos);
            if (json[pos] == ']') {
                pos++;
                break;
            }
            where.in_values_int[count] = parse_int(json, pos);
            count++;
            skip_whitespace(json, pos);
            if (json[pos] == ',') {
                pos++;
            }
        }
        where.in_values_count = count;
    }

    // ========================================================================
    // Array of short strings — used by group_by.fields, distinct.fields, join.fks
    // ========================================================================
    template <size_t MaxItems, typename AssignFn>
    constexpr auto parse_string_array(std::string_view json, size_t& pos, AssignFn assign) -> size_t {
        skip_whitespace(json, pos);
        if (pos >= json.size() || json[pos] != '[') {
            return 0;
        }
        pos++; // '['
        size_t count = 0;
        while (pos < json.size() && count < MaxItems) {
            skip_whitespace(json, pos);
            if (json[pos] == ']') {
                pos++;
                break;
            }
            auto s = parse_string<32>(json, pos);
            assign(count, s);
            count++;
            skip_whitespace(json, pos);
            if (json[pos] == ',') {
                pos++;
            }
        }
        // Handle possible trailing whitespace + ']'
        skip_whitespace(json, pos);
        if (pos < json.size() && json[pos] == ']') {
            pos++;
        }
        return count;
    }

    // ========================================================================
    // Nested object parsers
    // ========================================================================

    // Generic object-walker: iterate key/value pairs and dispatch via callback.
    // Callback receives (key_view, json, pos) and consumes exactly one value.
    template <typename Dispatch>
    constexpr auto parse_object_keys(std::string_view json, size_t& pos, Dispatch dispatch) -> void {
        skip_char(json, pos, '{');
        while (pos < json.size()) {
            skip_whitespace(json, pos);
            if (pos < json.size() && json[pos] == '}') {
                pos++;
                return;
            }
            auto key = parse_key(json, pos);
            skip_char(json, pos, ':');
            dispatch(key.view(), json, pos);
            skip_whitespace(json, pos);
            if (pos < json.size() && json[pos] == ',') {
                pos++;
            }
        }
    }

    // --- where leaf fields (shared by WhereSpec top level and inner and/or) ---
    constexpr auto parse_where_leaf_field( // NOSONAR(cpp:S3776)
            std::string_view key, std::string_view json, size_t& pos, ConstexprString<32>& field_out,
            ConstexprString<16>& op_out, TypedValue& value_out
    ) -> bool {
        if (key == "field") {
            field_out = parse_string<32>(json, pos);
            return true;
        }
        if (key == "op") {
            op_out = parse_string<16>(json, pos);
            return true;
        }
        if (key == "value") {
            value_out = parse_typed_value(json, pos);
            return true;
        }
        return false;
    }

    // --- parse_where_into ---
    constexpr auto parse_where_into(WhereSpec& w, std::string_view json, size_t& pos) -> void { // NOSONAR(cpp:S3776)
        w.enabled = true;
        parse_object_keys(json, pos, [&](std::string_view key, std::string_view j, size_t& p) {
            if (parse_where_leaf_field(key, j, p, w.field, w.op, w.value)) {
                return;
            }
            if (key == "value2") {
                w.value2 = parse_typed_value(j, p);
                return;
            }
            if (key == "in_values") {
                parse_int_array_into_where(w, j, p);
                return;
            }
            if (key == "field2") {
                w.field2 = parse_string<32>(j, p);
                return;
            }
            if (key == "op2") {
                w.op2 = parse_string<8>(j, p);
                return;
            }
            if (key == "value2_rhs") {
                w.value2_rhs = parse_typed_value(j, p);
                return;
            }
            if (key == "combine") {
                auto s = parse_string<8>(j, p);
                if (s == std::string_view("and"))
                    w.combine_and = true;
                else if (s == std::string_view("or"))
                    w.combine_or = true;
                return;
            }
            if (key == "and" || key == "or") {
                if (key == "and") {
                    w.combine_and = true;
                }
                if (key == "or") {
                    w.combine_or = true;
                }
                // Parse nested {field, op, value} into the _rhs fields
                parse_object_keys(j, p, [&](std::string_view k2, std::string_view j2, size_t& p2) {
                    if (k2 == "field") {
                        w.field2 = parse_string<32>(j2, p2);
                    } else if (k2 == "op") {
                        w.op2 = parse_string<8>(j2, p2);
                    } else if (k2 == "value") {
                        w.value2_rhs = parse_typed_value(j2, p2);
                    } else {
                        skip_value(j2, p2);
                    }
                });
                return;
            }
            skip_value(j, p);
        });
    }

    // --- parse_order_by_entry_into (one OrderBySpec) ---
    constexpr auto parse_order_by_entry_into(OrderBySpec& o, std::string_view json, size_t& pos) -> void {
        o.enabled = true;
        parse_object_keys(json, pos, [&](std::string_view key, std::string_view j, size_t& p) {
            if (key == "field") {
                o.field = parse_string<32>(j, p);
            } else if (key == "direction") {
                o.direction = parse_string<8>(j, p);
            } else if (key == "collate") {
                o.collate = parse_string<16>(j, p);
            } else {
                skip_value(j, p);
            }
        });
    }

    // --- parse_order_by_into (array of entries) ---
    constexpr auto parse_order_by_array_into(BenchmarkTest& test, std::string_view json, size_t& pos) -> void {
        skip_whitespace(json, pos);
        if (pos >= json.size() || json[pos] != '[') {
            return;
        }
        pos++; // '['
        size_t count = 0;
        while (pos < json.size() && count < BenchmarkTest::MAX_ORDER_BY) {
            skip_whitespace(json, pos);
            if (json[pos] == ']') {
                pos++;
                break;
            }
            parse_order_by_entry_into(test.order_by[count], json, pos);
            count++;
            skip_whitespace(json, pos);
            if (json[pos] == ',') {
                pos++;
            }
        }
        skip_whitespace(json, pos);
        if (pos < json.size() && json[pos] == ']') {
            pos++;
        }
        test.order_by_count = count;
    }

    // --- parse_having_into ---
    constexpr auto parse_having_into(HavingSpec& h, std::string_view json, size_t& pos) -> void {
        h.enabled = true;
        parse_object_keys(json, pos, [&](std::string_view key, std::string_view j, size_t& p) {
            if (key == "field") {
                h.field = parse_string<32>(j, p);
            } else if (key == "op") {
                h.op = parse_string<16>(j, p);
            } else if (key == "value") {
                h.value = parse_typed_value(j, p);
            } else {
                skip_value(j, p);
            }
        });
    }

    // --- parse_group_by_into ---
    constexpr auto parse_group_by_into(GroupBySpec& g, std::string_view json, size_t& pos) -> void {
        g.enabled = true;
        parse_object_keys(json, pos, [&](std::string_view key, std::string_view j, size_t& p) {
            if (key == "fields") {
                g.field_count =
                        parse_string_array<GroupBySpec::MAX_FIELDS>(j, p, [&](size_t i, const ConstexprString<32>& s) {
                            g.fields[i] = s;
                        });
            } else if (key == "having") {
                parse_having_into(g.having, j, p);
            } else {
                skip_value(j, p);
            }
        });
    }

    // --- parse_distinct_into ---
    constexpr auto parse_distinct_into(DistinctSpec& d, std::string_view json, size_t& pos) -> void {
        d.enabled = true;
        parse_object_keys(json, pos, [&](std::string_view key, std::string_view j, size_t& p) {
            if (key == "fields") {
                d.field_count =
                        parse_string_array<DistinctSpec::MAX_FIELDS>(j, p, [&](size_t i, const ConstexprString<32>& s) {
                            d.fields[i] = s;
                        });
            } else {
                skip_value(j, p);
            }
        });
    }

    // --- parse_limit_into ---
    constexpr auto parse_limit_into(LimitSpec& l, std::string_view json, size_t& pos) -> void {
        l.enabled = true;
        parse_object_keys(json, pos, [&](std::string_view key, std::string_view j, size_t& p) {
            if (key == "value") {
                l.value = parse_int(j, p);
            } else if (key == "offset") {
                l.offset = parse_int(j, p);
            } else {
                skip_value(j, p);
            }
        });
    }

    // --- parse_aggregate_into ---
    constexpr auto parse_aggregate_into(AggregateSpec& a, std::string_view json, size_t& pos) -> void {
        a.enabled = true;
        parse_object_keys(json, pos, [&](std::string_view key, std::string_view j, size_t& p) {
            if (key == "func") {
                a.func = parse_string<16>(j, p);
            } else if (key == "field") {
                a.field = parse_string<32>(j, p);
            } else {
                skip_value(j, p);
            }
        });
    }

    // --- parse_join_into ---
    constexpr auto parse_join_into(JoinSpec& jn, std::string_view json, size_t& pos) -> void {
        jn.enabled = true;
        parse_object_keys(json, pos, [&](std::string_view key, std::string_view j, size_t& p) {
            if (key == "type") {
                jn.type = parse_string<8>(j, p);
            } else if (key == "related") {
                jn.related = parse_string<32>(j, p);
            } else if (key == "fk") {
                jn.fk = parse_string<32>(j, p);
            } else if (key == "fks") {
                jn.fk_count = parse_string_array<JoinSpec::MAX_FKS>(j, p, [&](size_t i, const ConstexprString<32>& s) {
                    jn.fks[i] = s;
                });
            } else {
                skip_value(j, p);
            }
        });
    }

    // ========================================================================
    // Top-level BenchmarkTest field dispatcher
    // ========================================================================
    constexpr auto parse_test_field( // NOSONAR(cpp:S3776)
            BenchmarkTest& test, std::string_view key, std::string_view json, size_t& pos
    ) -> void {
        if (key == "test_name") {
            test.test_name = parse_string<64>(json, pos);
        } else if (key == "test_category" || key == "category") {
            test.test_category = parse_string<32>(json, pos);
        } else if (key == "description") {
            test.description = parse_string<128>(json, pos);
        } else if (key == "model") {
            test.model = parse_string<32>(json, pos);
        } else if (key == "operation") {
            test.operation = parse_string<32>(json, pos);
        } else if (key == "iterations") {
            test.iterations = parse_int(json, pos);
        } else if (key == "dataset_size") {
            test.dataset_size = parse_int(json, pos);
        } else if (key == "batch_size") {
            test.batch_size = parse_int(json, pos);
        } else if (key == "size_profile") {
            test.size_profile = parse_string<32>(json, pos);
        } else if (key == "where") {
            parse_where_into(test.where, json, pos);
        } else if (key == "order_by") {
            parse_order_by_array_into(test, json, pos);
        } else if (key == "group_by") {
            parse_group_by_into(test.group_by, json, pos);
        } else if (key == "distinct") {
            parse_distinct_into(test.distinct, json, pos);
        } else if (key == "limit") {
            parse_limit_into(test.limit, json, pos);
        } else if (key == "aggregate") {
            parse_aggregate_into(test.aggregate, json, pos);
        } else if (key == "join") {
            parse_join_into(test.join, json, pos);
        } else {
            skip_value(json, pos);
        }
    }

    constexpr auto parse_test_object(std::string_view json, size_t& pos) -> BenchmarkTest {
        BenchmarkTest test;
        parse_object_keys(json, pos, [&](std::string_view key, std::string_view j, size_t& p) {
            parse_test_field(test, key, j, p);
        });
        return test;
    }

    // ========================================================================
    // Test array parsing
    // ========================================================================
    constexpr auto skip_json_object(std::string_view json, size_t& pos) -> void {
        int brace_depth = 0;
        while (pos < json.size()) {
            if (json[pos] == '"') {
                pos++;
                while (pos < json.size() && json[pos] != '"') {
                    if (json[pos] == '\\') {
                        pos++;
                    }
                    pos++;
                }
                if (pos < json.size()) {
                    pos++;
                }
                continue;
            }
            if (json[pos] == '{') {
                brace_depth++;
            }
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

    template <size_t N> constexpr auto parse_tests(std::string_view json) -> std::array<BenchmarkTest, N> {
        std::array<BenchmarkTest, N> tests{};
        size_t                       pos = 0;
        skip_char(json, pos, '[');
        for (size_t i = 0; i < N; i++) {
            skip_whitespace(json, pos);
            if (pos >= json.size() || json[pos] == ']') {
                break;
            }
            tests[i] = parse_test_object(json, pos);
            skip_whitespace(json, pos);
            if (pos < json.size() && json[pos] == ',') {
                pos++;
            }
        }
        return tests;
    }

    // ========================================================================
    // Load embedded JSON at compile time
    // ========================================================================
    consteval auto load_benchmark_tests() {
        static constexpr const char json_data[] = {
#embed "tests/benchmark_tests.json"
                , '\0'
        };
        constexpr std::string_view json_str(json_data);
        constexpr size_t           test_count = count_tests(json_str);
        return parse_tests<test_count>(json_str);
    }

    inline constexpr auto BENCHMARK_TESTS = load_benchmark_tests();

} // namespace storm::benchmark
