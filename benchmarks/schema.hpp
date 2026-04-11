#pragma once

/**
 * Benchmark Test Schema — Nested Layout
 *
 * JSON source (human-readable, nested):
 *   {
 *     "where": { "field": "age", "op": ">", "value": 30 },
 *     "order_by": [ { "field": "salary", "direction": "DESC" } ],
 *     "group_by": { "fields": ["department"], "having": { "field": "age", "op": ">", "value": 30 } },
 *     ...
 *   }
 *
 * C++ mirrors this layout: test.where.field, test.order_by[0].field,
 * test.group_by.fields[0], test.group_by.having.field, etc.
 *
 * Each sub-spec carries a `bool enabled` flag set by the parser when the
 * corresponding JSON object is present, so consumers can dispatch at
 * compile time with `if constexpr (test.where.enabled) { ... }`.
 *
 * Values use a tagged TypedValue so the parser sniffs the JSON token type
 * (int / double / bool / string) — no more where_value_int / _double / _bool / _string
 * suffix proliferation.
 */

#include <array>
#include <cstddef>
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

        constexpr auto view() const -> std::string_view {
            return std::string_view(data.data(), len);
        }

        constexpr auto c_str() const -> const char* {
            return data.data();
        }

        constexpr auto empty() const -> bool {
            return len == 0;
        }

        constexpr bool operator==(std::string_view other) const {
            return view() == other;
        }
    };

    // ========================================================================
    // TypedValue — tagged union of supported leaf value types
    // ========================================================================
    struct TypedValue {
        enum class Kind : unsigned char { None, Int, Double, Bool, String };

        Kind                kind      = Kind::None;
        int                 as_int    = 0;
        double              as_double = 0.0;
        bool                as_bool   = false;
        ConstexprString<64> as_string;

        constexpr auto is_set() const -> bool {
            return kind != Kind::None;
        }
    };

    // ========================================================================
    // WhereSpec — a WHERE clause, optionally combined with a second clause via AND/OR
    //
    // Supports leaf operators (>, >=, <, <=, ==, !=, LIKE, BETWEEN, IN,
    // IS NULL, IS NOT NULL) plus a single optional AND/OR right-hand side.
    // Two-value operators (BETWEEN) use value2. IN uses in_values.
    // ========================================================================
    struct WhereSpec {
        bool                enabled = false;
        ConstexprString<32> field;
        ConstexprString<16> op;
        TypedValue          value;
        TypedValue          value2; // for BETWEEN

        static constexpr size_t        MAX_IN_VALUES = 10;
        std::array<int, MAX_IN_VALUES> in_values_int{};
        size_t                         in_values_count = 0;

        // Optional second clause combined with and/or
        // We only ever have depth 1 in practice, so we encode as flat second clause
        // rather than recursive (which would blow up structural-type complexity).
        bool                combine_and = false; // true if combined with AND
        bool                combine_or  = false; // true if combined with OR
        ConstexprString<32> field2;
        ConstexprString<8>  op2;
        TypedValue          value2_rhs; // right-hand value of the combined clause
    };

    // ========================================================================
    // OrderBySpec — one ORDER BY term. Tests use an array of up to 2 terms.
    // ========================================================================
    struct OrderBySpec {
        bool                enabled = false;
        ConstexprString<32> field;
        ConstexprString<8>  direction; // "ASC" / "DESC" (empty = ASC)
        ConstexprString<16> collate;   // "NOCASE" / "BINARY" / "RTRIM" (empty = none)
    };

    // ========================================================================
    // HavingSpec — HAVING clause, only valid nested inside GroupBySpec.
    // ========================================================================
    struct HavingSpec {
        bool                enabled = false;
        ConstexprString<32> field;
        ConstexprString<16> op;
        TypedValue          value;
    };

    // ========================================================================
    // GroupBySpec — one or more GROUP BY fields, optional HAVING.
    // ========================================================================
    struct GroupBySpec {
        bool                                        enabled    = false;
        static constexpr size_t                     MAX_FIELDS = 2;
        std::array<ConstexprString<32>, MAX_FIELDS> fields{};
        size_t                                      field_count = 0;
        HavingSpec                                  having;
    };

    // ========================================================================
    // DistinctSpec — SELECT DISTINCT over 1..3 fields.
    // ========================================================================
    struct DistinctSpec {
        bool                                        enabled    = false;
        static constexpr size_t                     MAX_FIELDS = 3;
        std::array<ConstexprString<32>, MAX_FIELDS> fields{};
        size_t                                      field_count = 0;
    };

    // ========================================================================
    // LimitSpec — LIMIT and/or OFFSET. `enabled` is true if either is set.
    // ========================================================================
    struct LimitSpec {
        bool enabled = false;
        int  value   = 0; // 0 = no LIMIT
        int  offset  = 0; // 0 = no OFFSET
    };

    // ========================================================================
    // AggregateSpec — aggregate function (count/sum/avg/min/max/count_distinct)
    // over an optional field. `count` with empty field means COUNT(*).
    // ========================================================================
    struct AggregateSpec {
        bool                enabled = false;
        ConstexprString<16> func;  // "count" / "sum" / "avg" / "min" / "max" / "count_distinct"
        ConstexprString<32> field; // optional — empty means COUNT(*)
    };

    // ========================================================================
    // JoinSpec — JOIN configuration. Reserved for PR2 (schema-driven JOINs).
    // In PR1 this is parsed-but-unused; existing run_*_operation methods
    // continue to hardcode JOIN types until PR2.
    // ========================================================================
    struct JoinSpec {
        bool                enabled = false;
        ConstexprString<8>  type;    // "inner" / "left" / "right"
        ConstexprString<32> related; // related model name ("User")
        ConstexprString<32> fk;      // FK field name ("sender")
        // Multi-FK support (up to 2 for now)
        static constexpr size_t                  MAX_FKS = 2;
        std::array<ConstexprString<32>, MAX_FKS> fks{};
        size_t                                   fk_count = 0;
    };

    // ========================================================================
    // BenchmarkTest — nested top-level test definition.
    // ========================================================================
    struct BenchmarkTest { // NOSONAR(cpp:S1820)
        ConstexprString<64>  test_name;
        ConstexprString<32>  test_category;
        ConstexprString<128> description;
        ConstexprString<32>  model;
        ConstexprString<32>  operation;

        // Nested specs
        WhereSpec                             where;
        static constexpr size_t               MAX_ORDER_BY = 2;
        std::array<OrderBySpec, MAX_ORDER_BY> order_by{};
        size_t                                order_by_count = 0;
        GroupBySpec                           group_by;
        DistinctSpec                          distinct;
        LimitSpec                             limit;
        AggregateSpec                         aggregate;
        JoinSpec                              join;

        // Scalar test parameters
        int iterations   = 1000;
        int dataset_size = 10000;
        int batch_size   = 1;

        // Size profile for automatic iteration over sizes
        // Values: "none", "batch_standard", "batch_insert_edge", "batch_update_edge",
        //         "dataset_standard", "dataset_small"
        ConstexprString<32> size_profile;

        consteval BenchmarkTest() = default;
    };

    // Forward declaration — defined in parser.hpp
    consteval auto load_benchmark_tests();

} // namespace storm::benchmark
