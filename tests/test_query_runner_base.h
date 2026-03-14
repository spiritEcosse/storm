#pragma once

/**
 * @file test_query_runner_base.h
 * @brief QueryRunnerBase and SelectQueryRunnerBase -- shared foundation for YAML-driven test runners.
 *
 * Mirrors DataBenchmarkBase and SelectQueryBenchmarkBase from the benchmark hierarchy.
 * Include AFTER `import storm;`, after test_models.h, after test_query_dispatch.h.
 */

#include "test_query_dispatch.h"
#include <utility>

namespace storm::test {

// Recursively build a WHERE expression from a compile-time WhereNode tree.
// N    — index of the current node in Tc.where_nodes[]
// Tc   — the UnifiedTestCase NTTP (all fields are constexpr)
// Model — ORM model type for field dispatch
template <int N, const auto &Tc, typename Model> auto build_where_node_expr() {
    using storm::orm::where::field;
    if constexpr (Tc.where_nodes[N].left < 0) {
        // Leaf node — dispatch field and build comparison expression
        constexpr auto fi = dispatch_field<Model>(Tc.where_nodes[N].field.view());
        if constexpr (Tc.where_nodes[N].value_dbl != 0.0)
            return build_where_expr<fi>(Tc.where_nodes[N].op.view(), Tc.where_nodes[N].value_dbl);
        else
            return build_where_expr<fi>(Tc.where_nodes[N].op.view(), Tc.where_nodes[N].value_int);
    } else if constexpr (Tc.where_nodes[N].op == "AND") {
        return build_where_node_expr<Tc.where_nodes[N].left, Tc, Model>() &&
               build_where_node_expr<Tc.where_nodes[N].right, Tc, Model>();
    } else {
        return build_where_node_expr<Tc.where_nodes[N].left, Tc, Model>() ||
               build_where_node_expr<Tc.where_nodes[N].right, Tc, Model>();
    }
}

template <typename Model, typename ConnType> class QueryRunnerBase {
  public:
    storm::QuerySet<Model, ConnType> qs_;

    // NTTP twin of SelectQueryBenchmarkBase::build_where_clause()
    // Tc must have a WhereSpec-compatible `where` member.
    template <const auto &Tc> void apply_where() {
        using storm::orm::where::field;

        if constexpr (Tc.where.op == "LIKE") {
            constexpr auto fi = dispatch_field<Model>(Tc.where.field.view());
            qs_.where(field<fi>().like(Tc.where.value_str.view()));

        } else if constexpr (Tc.where.op == "BETWEEN") {
            constexpr auto fi = dispatch_field<Model>(Tc.where.field.view());
            qs_.where(field<fi>().between(Tc.where.value_int, Tc.where.value_int2));

        } else if constexpr (Tc.where.op == "IN") {
            constexpr auto fi = dispatch_field<Model>(Tc.where.field.view());
            constexpr size_t n = Tc.where.value_list_count;
            [&]<size_t... J>(std::index_sequence<J...>) {
                qs_.where(field<fi>().in(Tc.where.value_list[J]...));
            }(std::make_index_sequence<n>{});

        } else if constexpr (!Tc.where.field3.empty()) {
            constexpr auto fi1 = dispatch_field<Model>(Tc.where.field.view());
            constexpr auto fi2 = dispatch_field<Model>(Tc.where.field2.view());
            constexpr auto fi3 = dispatch_field<Model>(Tc.where.field3.view());
            qs_.where(field<fi1>() > Tc.where.value_int && field<fi2>() > Tc.where.value_dbl2 &&
                      field<fi3>() == Tc.where.value_bool3);

        } else if constexpr (!Tc.where.field2.empty() && Tc.where.logic == "AND") {
            constexpr auto fi1 = dispatch_field<Model>(Tc.where.field.view());
            constexpr auto fi2 = dispatch_field<Model>(Tc.where.field2.view());
            qs_.where(field<fi1>() > Tc.where.value_int && field<fi2>() == Tc.where.value_bool2);

        } else if constexpr (!Tc.where.field2.empty() && Tc.where.logic == "OR") {
            constexpr auto fi1 = dispatch_field<Model>(Tc.where.field.view());
            constexpr auto fi2 = dispatch_field<Model>(Tc.where.field2.view());
            qs_.where(field<fi1>() < Tc.where.value_int || field<fi2>() > Tc.where.value_int_2);

        } else if constexpr (!Tc.where.value_str.empty()) {
            constexpr auto fi = dispatch_field<Model>(Tc.where.field.view());
            qs_.where(build_where_expr<fi>(Tc.where.op.view(), Tc.where.value_str.view()));

        } else if constexpr (Tc.where.value_dbl != 0.0) {
            constexpr auto fi = dispatch_field<Model>(Tc.where.field.view());
            qs_.where(build_where_expr<fi>(Tc.where.op.view(), Tc.where.value_dbl));

        } else if constexpr (Tc.where.value_bool) {
            constexpr auto fi = dispatch_field<Model>(Tc.where.field.view());
            qs_.where(build_where_expr<fi>(Tc.where.op.view(), Tc.where.value_bool));

        } else {
            constexpr auto fi = dispatch_field<Model>(Tc.where.field.view());
            qs_.where(build_where_expr<fi>(Tc.where.op.view(), Tc.where.value_int));
        }
    }

    // Apply WHERE and JOIN filters (mutates qs_ in place)
    template <const auto &Tc> void apply_where_and_join() {
        if constexpr (!Tc.join_name.empty()) {
            qs_.template join<&Model::sender>();
        }
        if constexpr (Tc.where_node_count > 0)
            qs_.where(build_where_node_expr<0, Tc, Model>());
        else if constexpr (!Tc.where.field.empty())
            apply_where<Tc>();
    }

    // Return a QuerySet with ORDER BY / LIMIT / OFFSET applied
    // order_by/limit/offset return finalized QuerySet<T,C,true> by value
    // When no modifiers: returns qs_ by reference (avoids copy — QuerySet has unique_ptr members)
    template <const auto &Tc> decltype(auto) qs_with_modifiers() { // NOSONAR(S3776) -- inherent constexpr dispatch
        if constexpr (!Tc.order_by.field.empty()) {
            constexpr auto fi = dispatch_field<Model>(Tc.order_by.field.view());
            if constexpr (Tc.order_by.asc) {
                if constexpr (Tc.limit_value >= 0 && Tc.offset_value >= 0)
                    return qs_.template order_by<fi, true>().limit(Tc.limit_value).offset(Tc.offset_value);
                else if constexpr (Tc.limit_value >= 0)
                    return qs_.template order_by<fi, true>().limit(Tc.limit_value);
                else if constexpr (Tc.offset_value >= 0)
                    return qs_.template order_by<fi, true>().offset(Tc.offset_value);
                else
                    return qs_.template order_by<fi, true>();
            } else {
                if constexpr (Tc.limit_value >= 0 && Tc.offset_value >= 0)
                    return qs_.template order_by<fi, false>().limit(Tc.limit_value).offset(Tc.offset_value);
                else if constexpr (Tc.limit_value >= 0)
                    return qs_.template order_by<fi, false>().limit(Tc.limit_value);
                else if constexpr (Tc.offset_value >= 0)
                    return qs_.template order_by<fi, false>().offset(Tc.offset_value);
                else
                    return qs_.template order_by<fi, false>();
            }
        } else if constexpr (Tc.limit_value >= 0 && Tc.offset_value >= 0) {
            return qs_.limit(Tc.limit_value).offset(Tc.offset_value);
        } else if constexpr (Tc.limit_value >= 0) {
            return qs_.limit(Tc.limit_value);
        } else if constexpr (Tc.offset_value >= 0) {
            return qs_.offset(Tc.offset_value);
        } else {
            return (qs_); // parentheses → returns by reference (no copy)
        }
    }
};

template <typename Model, typename ConnType> class SelectQueryRunnerBase : public QueryRunnerBase<Model, ConnType> {};

} // namespace storm::test
