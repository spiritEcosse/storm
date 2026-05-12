#pragma once

/**
 * @file test_query_dispatch.h
 * @brief Compile-time field dispatch and WHERE clause builder for YAML-driven tests.
 *
 * Provides:
 * - dispatch_field<Model>(name) — via storm::orm::query_builder (shared with bench)
 * - build_where_expr<FieldInfo>(op, values...) — builds the ORM expression for a given op string
 *
 * Include AFTER `import storm;` and after query_builder.hpp has been included.
 */

#include <meta>
#include <string_view>

#include "src/orm/query_builder.hpp" // NOLINT(misc-header-include-cycle)

namespace storm::test {

// Re-use the shared field dispatcher — no duplication with bench side.
using storm::orm::query_builder::dispatch_field;

// ============================================================================
// WHERE expression builder — dispatches on operator string at compile time
//
// Returns an ORM expression object suitable for QuerySet::where().
// The operator string must be one of: ==, !=, >, >=, <, <=
// ============================================================================

template <std::meta::info FieldInfo, typename ValueType>
constexpr auto build_where_expr(std::string_view op, ValueType value) {
    using storm::orm::where::field;
    if constexpr (requires { field<FieldInfo>() == value; }) {
        if (op == "==")
            return field<FieldInfo>() == value;
        if (op == "!=")
            return field<FieldInfo>() != value;
        if (op == ">")
            return field<FieldInfo>() > value;
        if (op == ">=")
            return field<FieldInfo>() >= value;
        if (op == "<")
            return field<FieldInfo>() < value;
        if (op == "<=")
            return field<FieldInfo>() <= value;
    }
    // Fallback — unreachable for valid operators but satisfies constexpr path
    return field<FieldInfo>() == value;
}

} // namespace storm::test
