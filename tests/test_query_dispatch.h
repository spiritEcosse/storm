#pragma once

/**
 * @file test_query_dispatch.h
 * @brief Compile-time field dispatch and WHERE clause builder for YAML-driven tests.
 *
 * Provides:
 * - dispatch_field<Model>(name) — resolves a runtime string to a consteval std::meta::info
 * - build_where_expr<FieldInfo>(op, values...) — builds the ORM expression for a given op string
 *
 * These are adapted from benchmarks/operations/select_query_base.hpp so that the
 * YAML-driven unit test pipeline can reuse the same compile-time dispatch mechanism.
 *
 * Include AFTER `import storm;` (requires storm::orm::where::field to be in scope).
 *
 * Usage:
 *   constexpr auto fi = dispatch_field<Person>("age");
 *   auto expr = build_where_expr<fi>(">=", 30);
 *   qs.where(expr);
 */

#include <meta>
#include <string_view>

namespace storm::test {

// ============================================================================
// Field dispatcher — resolves a field name string to its consteval meta::info
// ============================================================================

template <typename Model> consteval auto dispatch_field(std::string_view field_name) -> std::meta::info {
    for (std::meta::info member :
         std::meta::nonstatic_data_members_of(^^Model, std::meta::access_context::unchecked())) {
        if (std::meta::identifier_of(member) == field_name) {
            return member;
        }
    }
    throw std::invalid_argument("dispatch_field: field not found");
}

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
