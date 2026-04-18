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

    // Extract numeric value with correct type (int or double).
    // TypedValue must be passed as NTTP so if constexpr can dispatch on kind.
    template <TypedValue tv> consteval auto numeric_value() {
        if constexpr (tv.kind == TypedValue::Kind::Double) {
            return tv.as_double;
        } else {
            return tv.as_int;
        }
    }

    // ========================================================================
    // WhereCondition — a single WHERE clause term (field + operator + values)
    //
    // values[] holds all operand values:
    //   Comparison (>, <, ==, ...): values[0]        (value_count=1)
    //   LIKE:                       values[0] string  (value_count=1)
    //   BETWEEN:                    values[0], [1]    (value_count=2)
    //   IN:                         values[0..N]      (value_count=N)
    //   IS NULL / IS NOT NULL:      (value_count=0)
    // ========================================================================
    struct WhereCondition {
        ConstexprString<32> field;
        ConstexprString<16> op;

        static constexpr size_t            MAX_VALUES = 10;
        std::array<TypedValue, MAX_VALUES> values{};
        size_t                             value_count = 0;
    };

    // ========================================================================
    // WhereSpec — one or more WHERE conditions, optionally combined with AND/OR
    //
    // Single condition: conditions[0] holds the clause.
    // Combined (AND/OR): conditions[0..1] hold two clauses, combine_and/combine_or set.
    // ========================================================================
    struct WhereSpec {
        bool enabled = false;

        static constexpr size_t                    MAX_CONDITIONS = 2;
        std::array<WhereCondition, MAX_CONDITIONS> conditions{};
        size_t                                     condition_count = 0;

        bool combine_and = false;
        bool combine_or  = false;
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
        HavingSpec                                  having;

        consteval auto field_count() const -> size_t {
            size_t n = 0;
            for (size_t i = 0; i < MAX_FIELDS; ++i) {
                if (!fields[i].empty())
                    ++n;
            }
            return n;
        }
    };

    // ========================================================================
    // DistinctSpec — SELECT DISTINCT over 1..3 fields.
    // ========================================================================
    struct DistinctSpec {
        bool                                        enabled    = false;
        static constexpr size_t                     MAX_FIELDS = 3;
        std::array<ConstexprString<32>, MAX_FIELDS> fields{};

        consteval auto field_count() const -> size_t {
            size_t n = 0;
            for (size_t i = 0; i < MAX_FIELDS; ++i) {
                if (!fields[i].empty())
                    ++n;
            }
            return n;
        }
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
    // SetOpSpec — set operation type (union/union_all/except/intersect)
    // ========================================================================
    struct SetOpSpec {
        bool                enabled = false;
        ConstexprString<16> type; // "union" / "union_all" / "except" / "intersect"
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
        SetOpSpec                             setop;

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
