#pragma once

/**
 * @file test_query_runner_base.h
 * @brief QueryRunnerBase and SelectQueryRunnerBase -- shared foundation for YAML-driven test runners.
 *
 * Include AFTER `import storm;`, after test_models.h, after test_query_dispatch.h.
 */

#include "test_query_dispatch.h"
#include <utility>

namespace storm::test {

// Recursively build a WHERE expression from a compile-time WhereNode tree.
template <int N, const auto &Tc, typename Model> auto build_where_node_expr() {
    using storm::orm::where::field;
    if constexpr (Tc.where_nodes[N].left < 0) {
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

// Stateless no-op FK resolver for tests (JOIN is never enabled in test cases).
struct NoOpFkResolver {
    consteval auto operator()(std::string_view) const -> std::meta::info { std::unreachable(); }
};
inline constexpr NoOpFkResolver no_op_fk{};

template <typename Model, typename ConnType> class QueryRunnerBase {
  public:
    storm::QuerySet<Model, ConnType, true> qs_;

    template <const auto &Tc> auto seed_and_insert() -> void {
        std::vector<Model> initial;
        initial.reserve(static_cast<size_t>(Tc.bench.dataset_size));
        for (int i = 0; i < Tc.bench.dataset_size; ++i)
            initial.push_back(make_record<Model>(i));
        auto ins = this->qs_.insert(std::span<const Model>(initial)).execute();
        ASSERT_TRUE(ins.has_value()) << ins.error().message();
    }

    // Apply WHERE + JOIN + ORDER BY + LIMIT using QueryBuilder.
    // For where_expr tree cases, apply the WHERE tree manually after build_qs().
    template <const auto &Tc> auto apply_where_and_join() -> void { // NOSONAR(S3776)
        if constexpr (Tc.bench.join.enabled) {
            using QB = storm::orm::query_builder::QueryBuilder<Model, Tc.bench, test_fk, ConnType>;
            qs_ = QB::build_qs();
        } else {
            using QB = storm::orm::query_builder::QueryBuilder<Model, Tc.bench, no_op_fk, ConnType>;
            qs_ = QB::build_qs();
        }
        if constexpr (Tc.where_node_count > 0)
            qs_ = qs_.where(build_where_node_expr<0, Tc, Model>());
    }

    // Modifiers already applied by QueryBuilder inside build_qs(); return by reference.
    template <const auto &Tc> auto qs_with_modifiers() -> decltype(auto) {
        return (qs_); // parentheses → by reference
    }
};

template <typename Model, typename ConnType> class SelectQueryRunnerBase : public QueryRunnerBase<Model, ConnType> {};

} // namespace storm::test
