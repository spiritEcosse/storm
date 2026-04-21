#pragma once

/**
 * @file test_parser.hpp
 * @brief Compile-time JSON parser for YAML-driven unit test cases.
 *
 * Parses JSON arrays embedded via C++26 #embed into constexpr arrays of typed
 * structs. The JSON uses nested objects for where/expected/order_by/aggregation.
 *
 * Supported schemas:
 *   UnifiedTestCase — all query types (select/insert/update/erase/group_by/distinct/aggregate/chain/…)
 *
 * Usage example:
 *   #include "test_parser.hpp"
 *   inline constexpr auto UNIFIED_TESTS = storm::test::load_unified_tests();
 */

#include <array>
#include <string_view>

namespace storm::test {

    template <size_t N> using ConstexprString = storm::orm::utilities::ConstexprString<N>;

    // ============================================================================
    // Low-level JSON helpers — raw pointer access to avoid string_view bounds-check
    // assertion overhead (~13 constexpr steps per j[p] vs ~2 for ptr[p]).
    // ============================================================================

    constexpr void
    skip_ws(const char* ptr, size_t sz, size_t& p) { // NOSONAR(S6188) -- ptr+sz pattern for consteval performance
        while (p < sz) {
            if (char c = ptr[p]; c != ' ' && c != '\n' && c != '\r' && c != '\t')
                break;
            ++p;
        }
    }

    constexpr auto skip_ws(std::string_view j, size_t& p) -> void {
        skip_ws(j.data(), j.size(), p);
    }

    constexpr auto skip_char(std::string_view j, size_t& p, char c) -> void {
        skip_ws(j, p);
        if (p < j.size() && j.data()[p] == c)
            ++p;
    }

    constexpr auto parse_int(std::string_view j, size_t& p) -> int {
        skip_ws(j, p);
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        int          v   = 0;
        bool         neg = false;
        if (p < sz && ptr[p] == '-') {
            neg = true;
            ++p;
        }
        while (p < sz && ptr[p] >= '0' && ptr[p] <= '9')
            v = v * 10 + (ptr[p++] - '0');
        return neg ? -v : v;
    }

    constexpr auto parse_double(std::string_view j, size_t& p) -> double {
        skip_ws(j, p);
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        double       v   = 0.0;
        bool         neg = false;
        if (p < sz && ptr[p] == '-') {
            neg = true;
            ++p;
        }
        while (p < sz && ptr[p] >= '0' && ptr[p] <= '9')
            v = v * 10.0 + (ptr[p++] - '0');
        if (p < sz && ptr[p] == '.') {
            ++p;
            double frac = 0.1;
            while (p < sz && ptr[p] >= '0' && ptr[p] <= '9') {
                v += (ptr[p++] - '0') * frac;
                frac *= 0.1;
            }
        }
        return neg ? -v : v;
    }

    constexpr auto parse_bool(std::string_view j, size_t& p) -> bool {
        skip_ws(j, p);
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        if (p + 4 <= sz && ptr[p] == 't' && ptr[p + 1] == 'r' && ptr[p + 2] == 'u' && ptr[p + 3] == 'e') {
            p += 4;
            return true;
        }
        if (p + 5 <= sz && ptr[p] == 'f' && ptr[p + 1] == 'a' && ptr[p + 2] == 'l' && ptr[p + 3] == 's' &&
            ptr[p + 4] == 'e') {
            p += 5;
            return false;
        }
        return false;
    }

    template <size_t N> constexpr auto parse_str(std::string_view j, size_t& p) -> ConstexprString<N> {
        skip_ws(j, p);
        const char*        ptr = j.data();
        const size_t       sz  = j.size();
        ConstexprString<N> r;
        if (p < sz && ptr[p] == '"') {
            ++p;
            char*  rd = r.data.data();
            size_t i  = 0;
            while (p < sz && ptr[p] != '"' && i < N - 1)
                rd[i++] = ptr[p++];
            r.len = i;
            rd[i] = '\0';
            if (p < sz && ptr[p] == '"')
                ++p;
        }
        return r;
    }

    constexpr auto skip_object(std::string_view j, size_t& p) -> void {
        const char*  ptr   = j.data();
        const size_t sz    = j.size();
        int          depth = 0;
        while (p < sz) {
            char c = ptr[p++];
            if (c == '{')
                ++depth;
            else if (c == '}') {
                --depth;
                if (depth == 0)
                    return;
            }
        }
    }

    constexpr auto skip_array(std::string_view j, size_t& p) -> void {
        const char*  ptr   = j.data();
        const size_t sz    = j.size();
        int          depth = 0;
        while (p < sz) {
            if (char c = ptr[p++]; c == '[')
                ++depth;
            else if (c == ']') {
                --depth;
                if (depth == 0)
                    return;
            }
        }
    }

    constexpr auto skip_value(std::string_view j, size_t& p) -> void { // NOSONAR(S3776) -- consteval JSON parser
        skip_ws(j, p);
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        if (p >= sz)
            return;
        if (char first = ptr[p]; first == '"') {
            ++p;
            while (p < sz && ptr[p] != '"') {
                if (ptr[p] == '\\')
                    ++p;
                ++p;
            }
            if (p < sz)
                ++p;
        } else if (first == '[') {
            skip_array(j, p);
        } else if (first == '{') {
            skip_object(j, p);
        } else {
            while (p < sz) {
                if (char c = ptr[p];
                    c == ',' || c == '}' || c == ']' || c == ' ' || c == '\n' || c == '\r' || c == '\t')
                    break;
                ++p;
            }
        }
    }

    constexpr auto count_objects(std::string_view j) -> size_t {
        size_t n = 0;
        size_t p = 0;
        skip_char(j, p, '[');
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        while (p < sz) {
            skip_ws(ptr, sz, p);
            if (p >= sz || ptr[p] == ']')
                break;
            if (ptr[p] == '{') {
                ++n;
                skip_object(j, p);
            }
            skip_ws(ptr, sz, p);
            if (p < sz && ptr[p] == ',')
                ++p;
        }
        return n;
    }

    // Zero-alloc key comparison directly from JSON buffer.
    template <size_t M>
    constexpr auto jkey_eq(const char* json_ptr, size_t key_start, size_t key_len, const char (&lit)[M]) noexcept
            -> bool {
        constexpr size_t len = M - 1;
        if (key_len != len)
            return false;
        for (size_t i = 0; i < len; ++i)
            if (json_ptr[key_start + i] != lit[i])
                return false;
        return true;
    }

    struct JsonKeyRef {
        size_t start = 0;
        size_t len   = 0;
    };

    constexpr auto parse_key_ref(const char* ptr, size_t sz, size_t& p)
            -> JsonKeyRef { // NOSONAR — ptr+sz needed for consteval
        skip_ws(ptr, sz, p);
        JsonKeyRef ref;
        if (p < sz && ptr[p] == '"') {
            ++p;
            ref.start = p;
            while (p < sz && ptr[p] != '"')
                ++p;
            ref.len = p - ref.start;
            if (p < sz)
                ++p;
        }
        return ref;
    }

    // Detect JSON value type and parse into the appropriate where.value_* field.
    // Checks the first non-whitespace character: " = string, t/f = bool, [ = array, else number.
    constexpr auto is_number_with_dot(std::string_view j, size_t p) -> bool {
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        while (p < sz) {
            char c = ptr[p];
            if (c == '.')
                return true;
            if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\n')
                return false;
            ++p;
        }
        return false;
    }

    template <size_t MaxLen>
    constexpr auto parse_int_array(std::string_view j, size_t& p, std::array<int, MaxLen>& out) -> size_t {
        skip_ws(j, p);
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        if (p >= sz || ptr[p] != '[')
            return 0;
        ++p;
        size_t n  = 0;
        int*   od = out.data();
        while (p < sz && n < MaxLen) {
            skip_ws(ptr, sz, p);
            if (ptr[p] == ']') {
                ++p;
                break;
            }
            od[n++] = parse_int(j, p);
            skip_ws(ptr, sz, p);
            if (p < sz && ptr[p] == ',')
                ++p;
        }
        return n;
    }

    template <size_t MaxLen>
    constexpr auto parse_double_array(std::string_view j, size_t& p, std::array<double, MaxLen>& out) -> size_t {
        skip_ws(j, p);
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        if (p >= sz || ptr[p] != '[')
            return 0;
        ++p;
        size_t  n  = 0;
        double* od = out.data();
        while (p < sz && n < MaxLen) {
            skip_ws(ptr, sz, p);
            if (ptr[p] == ']') {
                ++p;
                break;
            }
            od[n++] = parse_double(j, p);
            skip_ws(ptr, sz, p);
            if (p < sz && ptr[p] == ',')
                ++p;
        }
        return n;
    }

    // In-place array parser — calls parse_fn(j, p, arr[i]) to avoid copy overhead.
    template <typename T, size_t N, typename ParseIntoFn>
    constexpr auto parse_array_into(std::string_view j, ParseIntoFn parse_fn) -> std::array<T, N> {
        std::array<T, N> arr{};
        size_t           p   = 0;
        const char*      ptr = j.data();
        const size_t     sz  = j.size();
        skip_char(j, p, '[');
        for (size_t i = 0; i < N; ++i) {
            skip_ws(ptr, sz, p);
            if (p >= sz || ptr[p] == ']')
                break;
            parse_fn(j, p, arr.data()[i]);
            skip_ws(ptr, sz, p);
            if (p < sz && ptr[p] == ',')
                ++p;
        }
        return arr;
    }

    // Helper: check for end-of-object '}'.  Returns true (and advances p) if found.
    constexpr auto at_object_end(const char* ptr, size_t sz, size_t& p) -> bool {
        skip_ws(ptr, sz, p);
        if (p < sz && ptr[p] == '}') {
            ++p;
            return true;
        }
        return false;
    }

    // Helper: parse key and skip colon.  Returns the key reference.
    constexpr auto parse_key_and_colon(const char* ptr, size_t sz, size_t& p) -> JsonKeyRef {
        auto kr = parse_key_ref(ptr, sz, p);
        skip_ws(ptr, sz, p);
        if (p < sz && ptr[p] == ':')
            ++p;
        return kr;
    }

    // Helper: skip trailing comma after a value inside an object/array.
    constexpr auto skip_comma(const char* ptr, size_t sz, size_t& p) -> void {
        skip_ws(ptr, sz, p);
        if (p < sz && ptr[p] == ',')
            ++p;
    }

    // ============================================================================
    // UnifiedTestCase schema
    // ============================================================================

    struct WhereNode {
        ConstexprString<32> field;
        ConstexprString<8>  op;
        int                 value_int = 0;
        double              value_dbl = 0.0;
        int                 left      = -1;
        int                 right     = -1;
    };

    struct WhereSpec {
        ConstexprString<32>     field;
        ConstexprString<16>     op;
        int                     value_int  = 0;
        double                  value_dbl  = 0.0;
        bool                    value_bool = false;
        ConstexprString<64>     value_str;
        int                     value_int2 = 0; // BETWEEN upper bound
        static constexpr size_t MAX_IN     = 10;
        std::array<int, MAX_IN> value_list{};
        size_t                  value_list_count = 0;
        ConstexprString<32>     field2;
        ConstexprString<16>     op2;
        int                     value_int_2 = 0;
        double                  value_dbl2  = 0.0;
        bool                    value_bool2 = false;
        ConstexprString<32>     field3;
        bool                    value_bool3 = false;
        ConstexprString<8>      logic;
        constexpr WhereSpec() = default;
    };

    struct OrderBySpec {
        ConstexprString<32> field;
        bool                asc = true;
        constexpr OrderBySpec() = default;
    };

    struct FirstRowSpec {
        ConstexprString<64> name;
        bool                has_name             = false;
        int                 age                  = 0;
        bool                has_age              = false;
        double              salary               = 0.0;
        bool                has_salary           = false;
        bool                is_active            = false;
        bool                has_is_active        = false;
        int                 years_experience     = 0;
        bool                has_years_experience = false;
        ConstexprString<64> department;
        bool                has_department = false;
        ConstexprString<64> content;
        bool                has_content = false;
        int                 value       = 0;
        bool                has_value   = false;
        constexpr FirstRowSpec()        = default;
    };

    struct ExpectedSpec {
        int                            count     = 0;
        int                            int_val   = -1;
        double                         dbl_val   = -1.0;
        int                            unchanged = -1;
        int                            remaining = -1;
        FirstRowSpec                   first;
        static constexpr size_t        MAX_GROUPS = 20;
        std::array<int, MAX_GROUPS>    group_keys{};
        std::array<int, MAX_GROUPS>    group_agg_int{};
        std::array<double, MAX_GROUPS> group_agg_dbl{};
        size_t                         groups_count = 0;
        constexpr ExpectedSpec()                    = default;
    };

    struct ChainAggSpec {
        ConstexprString<16> func;
        ConstexprString<32> field;
        ConstexprString<8>  res_type;
        double              res_value = 0.0;
        constexpr ChainAggSpec()      = default;
    };

    // ============================================================================
    // Nested object parsers
    // ============================================================================

    // Auto-detect JSON value type for polymorphic "value" fields.
    // Sets the appropriate typed field in the where spec.
    enum class TypedValueKind { Int, Dbl, Bool, Str };

    struct TypedValue {
        int                 i = 0;
        double              d = 0.0;
        bool                b = false;
        ConstexprString<64> s;
        TypedValueKind      kind = TypedValueKind::Int;
    };

    constexpr auto parse_typed_value(std::string_view j, size_t& p) -> TypedValue {
        using enum TypedValueKind;
        skip_ws(j, p);
        const char* ptr = j.data();
        TypedValue  tv;
        if (ptr[p] == '"') {
            tv.s    = parse_str<64>(j, p);
            tv.kind = Str;
        } else if (ptr[p] == 't' || ptr[p] == 'f') {
            tv.b    = parse_bool(j, p);
            tv.kind = Bool;
        } else if (is_number_with_dot(j, p)) {
            tv.d    = parse_double(j, p);
            tv.kind = Dbl;
        } else {
            tv.i    = parse_int(j, p);
            tv.kind = Int;
        }
        return tv;
    }

    // Assign typed value into primary where fields.
    constexpr auto assign_where_value(WhereSpec& w, const TypedValue& tv) -> void {
        using enum TypedValueKind;
        if (tv.kind == Bool)
            w.value_bool = tv.b;
        else if (tv.kind == Str)
            w.value_str = tv.s;
        else if (tv.kind == Dbl)
            w.value_dbl = tv.d;
        else
            w.value_int = tv.i;
    }

    // Assign typed value into secondary where fields (value2).
    constexpr auto assign_where_value2(WhereSpec& w, const TypedValue& tv) -> void {
        using enum TypedValueKind;
        if (tv.kind == Bool)
            w.value_bool2 = tv.b;
        else if (tv.kind == Dbl)
            w.value_dbl2 = tv.d;
        else
            w.value_int_2 = tv.i;
    }

    // Parse len-5 keys inside "where" object.
    constexpr void
    parse_where_key5(std::string_view j, size_t& p, const char* ptr, JsonKeyRef kr, WhereSpec& w) { // NOSONAR(S3776)
        if (jkey_eq(ptr, kr.start, kr.len, "field"))
            w.field = parse_str<32>(j, p);
        else if (jkey_eq(ptr, kr.start, kr.len, "upper"))
            w.value_int2 = parse_int(j, p);
        else if (jkey_eq(ptr, kr.start, kr.len, "logic"))
            w.logic = parse_str<8>(j, p);
        else if (jkey_eq(ptr, kr.start, kr.len, "value"))
            assign_where_value(w, parse_typed_value(j, p));
        else
            skip_value(j, p);
    }

    // Parse len-6 keys inside "where" object.
    constexpr auto parse_where_key6(std::string_view j, size_t& p, const char* ptr, JsonKeyRef kr, WhereSpec& w)
            -> void {
        if (jkey_eq(ptr, kr.start, kr.len, "field2"))
            w.field2 = parse_str<32>(j, p);
        else if (jkey_eq(ptr, kr.start, kr.len, "field3"))
            w.field3 = parse_str<32>(j, p);
        else if (jkey_eq(ptr, kr.start, kr.len, "value2"))
            assign_where_value2(w, parse_typed_value(j, p));
        else if (jkey_eq(ptr, kr.start, kr.len, "value3")) {
            auto tv = parse_typed_value(j, p);
            if (tv.kind == TypedValueKind::Bool)
                w.value_bool3 = tv.b;
            else
                skip_value(j, p);
        } else if (jkey_eq(ptr, kr.start, kr.len, "values"))
            w.value_list_count = parse_int_array<WhereSpec::MAX_IN>(j, p, w.value_list);
        else
            skip_value(j, p);
    }

    // Parse "where": { field, op, value, upper, values, field2, op2, value2, field3, value3, logic }
    constexpr auto parse_where_into(std::string_view j, size_t& p, WhereSpec& w) -> void { // NOSONAR(S3776)
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        skip_char(j, p, '{');
        while (p < sz) {
            if (at_object_end(ptr, sz, p))
                break;
            auto kr = parse_key_and_colon(ptr, sz, p);

            switch (kr.len) {
            case 2:
                if (jkey_eq(ptr, kr.start, kr.len, "op"))
                    w.op = parse_str<16>(j, p);
                else
                    skip_value(j, p);
                break;
            case 3:
                if (jkey_eq(ptr, kr.start, kr.len, "op2"))
                    w.op2 = parse_str<16>(j, p);
                else
                    skip_value(j, p);
                break;
            case 5:
                parse_where_key5(j, p, ptr, kr, w);
                break;
            case 6:
                parse_where_key6(j, p, ptr, kr, w);
                break;
            default:
                skip_value(j, p);
                break;
            }
            skip_comma(ptr, sz, p);
        }
    }

    // Parse "order_by": { field, asc }
    constexpr auto parse_order_by_into(std::string_view j, size_t& p, OrderBySpec& ob) -> void {
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        skip_char(j, p, '{');
        while (p < sz) {
            if (at_object_end(ptr, sz, p))
                break;
            auto kr = parse_key_and_colon(ptr, sz, p);
            if (jkey_eq(ptr, kr.start, kr.len, "field"))
                ob.field = parse_str<32>(j, p);
            else if (jkey_eq(ptr, kr.start, kr.len, "asc"))
                ob.asc = parse_bool(j, p);
            else
                skip_value(j, p);
            skip_comma(ptr, sz, p);
        }
    }

    // Parse "first": { name, age, salary, is_active, years_experience, department, content, value }
    constexpr auto parse_first_row_into(std::string_view j, size_t& p, FirstRowSpec& f) -> void { // NOSONAR(S3776)
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        skip_char(j, p, '{');
        while (p < sz) {
            if (at_object_end(ptr, sz, p))
                break;
            auto kr = parse_key_and_colon(ptr, sz, p);

            switch (kr.len) {
            case 3: // age
                if (jkey_eq(ptr, kr.start, kr.len, "age")) {
                    f.age     = parse_int(j, p);
                    f.has_age = true;
                } else
                    skip_value(j, p);
                break;
            case 4: // name
                if (jkey_eq(ptr, kr.start, kr.len, "name")) {
                    f.name     = parse_str<64>(j, p);
                    f.has_name = true;
                } else
                    skip_value(j, p);
                break;
            case 5: // value
                if (jkey_eq(ptr, kr.start, kr.len, "value")) {
                    f.value     = parse_int(j, p);
                    f.has_value = true;
                } else
                    skip_value(j, p);
                break;
            case 6: // salary
                if (jkey_eq(ptr, kr.start, kr.len, "salary")) {
                    f.salary     = parse_double(j, p);
                    f.has_salary = true;
                } else
                    skip_value(j, p);
                break;
            case 7: // content
                if (jkey_eq(ptr, kr.start, kr.len, "content")) {
                    f.content     = parse_str<64>(j, p);
                    f.has_content = true;
                } else
                    skip_value(j, p);
                break;
            case 9: // is_active
                if (jkey_eq(ptr, kr.start, kr.len, "is_active")) {
                    f.is_active     = parse_bool(j, p);
                    f.has_is_active = true;
                } else
                    skip_value(j, p);
                break;
            case 10: // department
                if (jkey_eq(ptr, kr.start, kr.len, "department")) {
                    f.department     = parse_str<64>(j, p);
                    f.has_department = true;
                } else
                    skip_value(j, p);
                break;
            case 16: // years_experience
                if (jkey_eq(ptr, kr.start, kr.len, "years_experience")) {
                    f.years_experience     = parse_int(j, p);
                    f.has_years_experience = true;
                } else
                    skip_value(j, p);
                break;
            default:
                skip_value(j, p);
                break;
            }
            skip_comma(ptr, sz, p);
        }
    }

    // Parse "expected": { count, int_val, dbl_val, unchanged, remaining, first,
    //                     groups_count, group_keys, group_agg_int, group_agg_dbl }
    constexpr auto parse_expected_into(std::string_view j, size_t& p, ExpectedSpec& e) -> void { // NOSONAR(S3776)
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        skip_char(j, p, '{');
        while (p < sz) {
            if (at_object_end(ptr, sz, p))
                break;
            auto kr = parse_key_and_colon(ptr, sz, p);

            switch (kr.len) {
            case 5:
                if (jkey_eq(ptr, kr.start, kr.len, "count"))
                    e.count = parse_int(j, p);
                else if (jkey_eq(ptr, kr.start, kr.len, "first"))
                    parse_first_row_into(j, p, e.first);
                else
                    skip_value(j, p);
                break;
            case 7:
                if (jkey_eq(ptr, kr.start, kr.len, "int_val"))
                    e.int_val = parse_int(j, p);
                else if (jkey_eq(ptr, kr.start, kr.len, "dbl_val"))
                    e.dbl_val = parse_double(j, p);
                else
                    skip_value(j, p);
                break;
            case 9:
                if (jkey_eq(ptr, kr.start, kr.len, "unchanged"))
                    e.unchanged = parse_int(j, p);
                else if (jkey_eq(ptr, kr.start, kr.len, "remaining"))
                    e.remaining = parse_int(j, p);
                else
                    skip_value(j, p);
                break;
            case 10:
                if (jkey_eq(ptr, kr.start, kr.len, "group_keys"))
                    parse_int_array<ExpectedSpec::MAX_GROUPS>(j, p, e.group_keys);
                else
                    skip_value(j, p);
                break;
            case 12:
                if (jkey_eq(ptr, kr.start, kr.len, "groups_count"))
                    e.groups_count = static_cast<size_t>(parse_int(j, p));
                else
                    skip_value(j, p);
                break;
            case 13:
                if (jkey_eq(ptr, kr.start, kr.len, "group_agg_int"))
                    parse_int_array<ExpectedSpec::MAX_GROUPS>(j, p, e.group_agg_int);
                else if (jkey_eq(ptr, kr.start, kr.len, "group_agg_dbl"))
                    parse_double_array<ExpectedSpec::MAX_GROUPS>(j, p, e.group_agg_dbl);
                else
                    skip_value(j, p);
                break;
            default:
                skip_value(j, p);
                break;
            }
            skip_comma(ptr, sz, p);
        }
    }

    // Parse "aggregation": { func, field, join }
    // Also sets query_type and agg_field on the parent test case.
    constexpr void parse_aggregation_into(
            std::string_view     j,
            size_t&              p,
            ConstexprString<32>& query_type,
            ConstexprString<32>& agg_field,
            ConstexprString<16>& join_name
    ) {
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        skip_char(j, p, '{');
        while (p < sz) {
            if (at_object_end(ptr, sz, p))
                break;
            auto kr = parse_key_and_colon(ptr, sz, p);
            if (jkey_eq(ptr, kr.start, kr.len, "func")) {
                auto v = parse_str<32>(j, p);
                if (query_type.len == 0)
                    query_type = v;
            } else if (jkey_eq(ptr, kr.start, kr.len, "field")) {
                agg_field = parse_str<32>(j, p);
            } else if (jkey_eq(ptr, kr.start, kr.len, "join")) {
                join_name = parse_str<16>(j, p);
            } else
                skip_value(j, p);
            skip_comma(ptr, sz, p);
        }
    }

    // Parse nested "result": { type, value } inside a ChainAggSpec.
    constexpr auto parse_chain_result_into(std::string_view j, size_t& p, ChainAggSpec& spec) -> void {
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        skip_char(j, p, '{');
        while (p < sz) {
            if (at_object_end(ptr, sz, p))
                break;
            auto kr = parse_key_and_colon(ptr, sz, p);
            if (jkey_eq(ptr, kr.start, kr.len, "type"))
                spec.res_type = parse_str<8>(j, p);
            else if (jkey_eq(ptr, kr.start, kr.len, "value"))
                spec.res_value = parse_double(j, p);
            else
                skip_value(j, p);
            skip_comma(ptr, sz, p);
        }
    }

    // Parse a single ChainAggSpec: { func, field, result: { type, value } }
    constexpr auto parse_chain_agg_spec(std::string_view j, size_t& p) -> ChainAggSpec {
        ChainAggSpec spec;
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        skip_char(j, p, '{');
        while (p < sz) {
            if (at_object_end(ptr, sz, p))
                break;
            auto kr = parse_key_and_colon(ptr, sz, p);
            if (jkey_eq(ptr, kr.start, kr.len, "func"))
                spec.func = parse_str<16>(j, p);
            else if (jkey_eq(ptr, kr.start, kr.len, "field"))
                spec.field = parse_str<32>(j, p);
            else if (jkey_eq(ptr, kr.start, kr.len, "result"))
                parse_chain_result_into(j, p, spec);
            else
                skip_value(j, p);
            skip_comma(ptr, sz, p);
        }
        return spec;
    }

    template <size_t MaxLen>
    constexpr size_t
    parse_chain_agg_array(std::string_view j, size_t& p, std::array<ChainAggSpec, MaxLen>& out) { // NOSONAR(S3776)
        skip_ws(j, p);
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        if (p >= sz || ptr[p] != '[')
            return 0;
        ++p;
        size_t n = 0;
        while (p < sz) {
            skip_ws(ptr, sz, p);
            if (p >= sz || ptr[p] == ']') {
                if (p < sz)
                    ++p;
                break;
            }
            if (ptr[p] == '{') {
                if (n < MaxLen)
                    out.data()[n++] = parse_chain_agg_spec(j, p);
                else
                    skip_object(j, p);
            }
            skip_ws(ptr, sz, p);
            if (p < sz && ptr[p] == ',')
                ++p;
        }
        return n;
    }

    // Recursively parse a "where_expr" nested tree and linearize into a flat
    // WhereNode array using pre-order DFS.  Each node reserves its slot first,
    // then recurses into children so that indices are assigned in pre-order.
    //
    // JSON leaf:      {"field":"age","op":">","value_int":30}
    // JSON composite: {"op":"AND","left":{...},"right":{...}}
    template <int MaxNodes>
    constexpr int parse_where_expr_dfs(
            std::string_view j, size_t& p, std::array<WhereNode, MaxNodes>& nodes, int& count
    ) { // NOSONAR — consteval recursive DFS
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        if (count >= MaxNodes) {
            skip_object(j, p);
            return -1;
        }

        int idx = count;
        ++count;
        nodes[idx] = WhereNode{};

        // Parse flat keys immediately and remember positions of sub-objects
        // for later recursive parsing (left/right may appear before op/field).
        size_t left_pos  = 0;
        size_t right_pos = 0;
        bool   has_left  = false;
        bool   has_right = false;

        skip_char(j, p, '{');
        while (p < sz) {
            if (at_object_end(ptr, sz, p))
                break;
            auto kr = parse_key_and_colon(ptr, sz, p);
            skip_ws(j, p);

            if (jkey_eq(ptr, kr.start, kr.len, "field"))
                nodes[idx].field = parse_str<32>(j, p);
            else if (jkey_eq(ptr, kr.start, kr.len, "op"))
                nodes[idx].op = parse_str<8>(j, p);
            else if (jkey_eq(ptr, kr.start, kr.len, "value_int"))
                nodes[idx].value_int = parse_int(j, p);
            else if (jkey_eq(ptr, kr.start, kr.len, "value_dbl"))
                nodes[idx].value_dbl = parse_double(j, p);
            else if (jkey_eq(ptr, kr.start, kr.len, "left")) {
                if (p < sz && ptr[p] == '{') {
                    has_left = true;
                    left_pos = p;
                    skip_object(j, p);
                } else
                    skip_value(j, p);
            } else if (jkey_eq(ptr, kr.start, kr.len, "right")) {
                if (p < sz && ptr[p] == '{') {
                    has_right = true;
                    right_pos = p;
                    skip_object(j, p);
                } else
                    skip_value(j, p);
            } else
                skip_value(j, p);
            skip_comma(ptr, sz, p);
        }

        // Now recurse into saved sub-object positions (pre-order: left then right).
        if (has_left) {
            size_t lp       = left_pos;
            nodes[idx].left = parse_where_expr_dfs<MaxNodes>(j, lp, nodes, count);
        }
        if (has_right) {
            size_t rp        = right_pos;
            nodes[idx].right = parse_where_expr_dfs<MaxNodes>(j, rp, nodes, count);
        }

        return idx;
    }

    // ============================================================================
    // UnifiedTestCase struct + top-level parser
    // ============================================================================

    struct UnifiedTestCase { // NOSONAR(S1820) -- flat struct for consteval JSON parsing
        ConstexprString<64> name;
        ConstexprString<16> model;
        ConstexprString<32> query_type;
        ConstexprString<16> dataset;
        int                 dataset_size = 0;

        WhereSpec where;

        static constexpr int                   MAX_WHERE_NODES = 7;
        std::array<WhereNode, MAX_WHERE_NODES> where_nodes{};
        int                                    where_node_count = 0;

        ConstexprString<16> join_name;
        int                 limit_value  = -1;
        int                 offset_value = -1;

        OrderBySpec order_by;

        ConstexprString<32> distinct_field;
        ConstexprString<32> distinct_field2;

        ConstexprString<32> group_by_field;
        ConstexprString<32> group_by_field2;
        ConstexprString<32> having_field;
        ConstexprString<16> having_op;
        int                 having_value_int = 0;
        ConstexprString<32> agg_field;

        size_t                              chain_len = 0;
        static constexpr size_t             MAX_CHAIN = 5;
        std::array<ChainAggSpec, MAX_CHAIN> aggregations{};

        int insert_count = 0;
        int update_count = 0;
        int erase_count  = 0;

        ExpectedSpec expected;

        constexpr UnifiedTestCase() = default;
    };

    // Top-level keys (26 unique):
    //  4: name  5: model,where  7: dataset  8: expected,order_by
    //  9: agg_field,chain_len,having_op,join_name
    // 10: query_type  11: aggregation,erase_count,limit_value,where_nodes
    // 12: aggregations,dataset_size,having_field,insert_count,offset_value,
    //     update_count
    // 14: distinct_field,group_by_field
    // 15: distinct_field2,group_by_field2,having_value_int
    constexpr auto parse_unified_case_into(std::string_view j, size_t& p, UnifiedTestCase& tc)
            -> void { // NOSONAR(S3776)
        const char*  ptr = j.data();
        const size_t sz  = j.size();
        skip_char(j, p, '{');
        while (p < sz) {
            while (p < sz) {
                char c = ptr[p];
                if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
                    break;
                ++p;
            }
            if (ptr[p] == '}') {
                ++p;
                break;
            }
            auto kr = parse_key_ref(ptr, sz, p);
            while (p < sz) {
                char c = ptr[p];
                if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
                    break;
                ++p;
            }
            if (p < sz && ptr[p] == ':')
                ++p;

            switch (kr.len) {
            case 4: // name
                if (jkey_eq(ptr, kr.start, kr.len, "name"))
                    tc.name = parse_str<64>(j, p);
                else
                    skip_value(j, p);
                break;
            case 5: // model, where
                if (jkey_eq(ptr, kr.start, kr.len, "model"))
                    tc.model = parse_str<16>(j, p);
                else if (jkey_eq(ptr, kr.start, kr.len, "where"))
                    parse_where_into(j, p, tc.where);
                else
                    skip_value(j, p);
                break;
            case 7: // dataset
                if (jkey_eq(ptr, kr.start, kr.len, "dataset"))
                    tc.dataset = parse_str<16>(j, p);
                else
                    skip_value(j, p);
                break;
            case 8: // expected, order_by
                if (jkey_eq(ptr, kr.start, kr.len, "expected"))
                    parse_expected_into(j, p, tc.expected);
                else if (jkey_eq(ptr, kr.start, kr.len, "order_by"))
                    parse_order_by_into(j, p, tc.order_by);
                else
                    skip_value(j, p);
                break;
            case 9: // agg_field, chain_len, having_op, join_name
                if (jkey_eq(ptr, kr.start, kr.len, "agg_field"))
                    tc.agg_field = parse_str<32>(j, p);
                else if (jkey_eq(ptr, kr.start, kr.len, "chain_len"))
                    tc.chain_len = static_cast<size_t>(parse_int(j, p));
                else if (jkey_eq(ptr, kr.start, kr.len, "having_op"))
                    tc.having_op = parse_str<16>(j, p);
                else if (jkey_eq(ptr, kr.start, kr.len, "join_name"))
                    tc.join_name = parse_str<16>(j, p);
                else
                    skip_value(j, p);
                break;
            case 10: // query_type, where_expr
                if (jkey_eq(ptr, kr.start, kr.len, "query_type"))
                    tc.query_type = parse_str<32>(j, p);
                else if (jkey_eq(ptr, kr.start, kr.len, "where_expr")) {
                    int cnt = 0;
                    parse_where_expr_dfs<UnifiedTestCase::MAX_WHERE_NODES>(j, p, tc.where_nodes, cnt);
                    tc.where_node_count = cnt;
                } else
                    skip_value(j, p);
                break;
            case 11: // aggregation, limit_value, erase_count
                if (jkey_eq(ptr, kr.start, kr.len, "aggregation"))
                    parse_aggregation_into(j, p, tc.query_type, tc.agg_field, tc.join_name);
                else if (jkey_eq(ptr, kr.start, kr.len, "limit_value"))
                    tc.limit_value = parse_int(j, p);
                else if (jkey_eq(ptr, kr.start, kr.len, "erase_count"))
                    tc.erase_count = parse_int(j, p);
                else
                    skip_value(j, p);
                break;
            case 12: // aggregations, dataset_size, having_field, insert/update_count, offset_value
                if (jkey_eq(ptr, kr.start, kr.len, "aggregations"))
                    tc.chain_len = parse_chain_agg_array<UnifiedTestCase::MAX_CHAIN>(j, p, tc.aggregations);
                else if (jkey_eq(ptr, kr.start, kr.len, "dataset_size"))
                    tc.dataset_size = parse_int(j, p);
                else if (jkey_eq(ptr, kr.start, kr.len, "having_field"))
                    tc.having_field = parse_str<32>(j, p);
                else if (jkey_eq(ptr, kr.start, kr.len, "insert_count"))
                    tc.insert_count = parse_int(j, p);
                else if (jkey_eq(ptr, kr.start, kr.len, "update_count"))
                    tc.update_count = parse_int(j, p);
                else if (jkey_eq(ptr, kr.start, kr.len, "offset_value"))
                    tc.offset_value = parse_int(j, p);
                else
                    skip_value(j, p);
                break;
            case 14: // distinct_field, group_by_field
                if (jkey_eq(ptr, kr.start, kr.len, "distinct_field"))
                    tc.distinct_field = parse_str<32>(j, p);
                else if (jkey_eq(ptr, kr.start, kr.len, "group_by_field"))
                    tc.group_by_field = parse_str<32>(j, p);
                else
                    skip_value(j, p);
                break;
            case 15: // distinct_field2, group_by_field2
                if (jkey_eq(ptr, kr.start, kr.len, "distinct_field2"))
                    tc.distinct_field2 = parse_str<32>(j, p);
                else if (jkey_eq(ptr, kr.start, kr.len, "group_by_field2"))
                    tc.group_by_field2 = parse_str<32>(j, p);
                else
                    skip_value(j, p);
                break;
            case 16: // having_value_int
                if (jkey_eq(ptr, kr.start, kr.len, "having_value_int"))
                    tc.having_value_int = parse_int(j, p);
                else
                    skip_value(j, p);
                break;
            default:
                skip_value(j, p);
                break;
            }
            while (p < sz) {
                char c = ptr[p];
                if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
                    break;
                ++p;
            }
            if (p < sz && ptr[p] == ',')
                ++p;
        }
    }

    consteval auto load_unified_tests() {
        static constexpr const char raw[] = {
#embed "test_cases/unified_cases.json"
                , '\0'
        };
        constexpr std::string_view json(raw, sizeof(raw) - 1);
        constexpr size_t           n = count_objects(json);
        return parse_array_into<UnifiedTestCase, n>(json, parse_unified_case_into);
    }

} // namespace storm::test
