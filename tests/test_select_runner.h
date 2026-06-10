#pragma once

/**
 * @file test_select_runner.h
 * @brief Field dispatch, WHERE builder, QueryRunnerBase, and SELECT runners.
 *
 * Provides:
 * - dispatch_field<Model>(name) — via storm::orm::query_builder
 * - build_where_expr<FieldInfo>(op, value) — ORM expression builder
 * - QueryRunnerBase<Model, ConnType> — shared base with seed + WHERE/JOIN apply
 * - SelectRunner, AggregateRunner, ChainAggRunner, DistinctRunner, GroupByRunner
 *
 * Include AFTER `import storm;` and test_models.h.
 */

#include <algorithm>
#include <meta>
#include <string_view>
#include <utility>
#include <vector>

#include "src/orm/query_builder.hpp" // NOLINT(misc-header-include-cycle)

namespace storm::test {

// Re-use the shared field dispatcher — no duplication with bench side.
using storm::orm::query_builder::dispatch_field;

// ============================================================================
// WHERE expression builder — dispatches on operator string at compile time
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
    return field<FieldInfo>() == value;
}

// FK resolver for test models (Message::sender → Person).
struct TestFkResolver {
    consteval auto operator()(std::string_view name) const -> std::meta::info {
        if (name == "sender")
            return ^^Message::sender;
        std::unreachable();
    }
};
inline constexpr TestFkResolver test_fk{};

// Stateless no-op FK resolver for tests (JOIN is never enabled in test cases).
struct NoOpFkResolver {
    consteval auto operator()(std::string_view) const -> std::meta::info { std::unreachable(); }
};
inline constexpr NoOpFkResolver no_op_fk{};

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

template <typename Model, typename ConnType> class QueryRunnerBase {
  public:
    storm::QuerySet<Model, ConnType, true> qs_;

    template <const auto &Tc> auto seed_and_insert() -> void {
        std::vector<Model> initial;
        initial.reserve(static_cast<std::size_t>(Tc.bench.dataset_size));
        for (int i = 0; i < Tc.bench.dataset_size; ++i)
            initial.push_back(make_record<Model>(i));
        auto ins = this->qs_.insert(std::span<const Model>(initial)).execute();
        ASSERT_TRUE(ins.has_value()) << ins.error().message();
    }

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

    template <const auto &Tc> auto qs_with_modifiers() -> decltype(auto) { return (qs_); }
};

template <typename Model, typename ConnType> class SelectQueryRunnerBase : public QueryRunnerBase<Model, ConnType> {};

// ---------------------------------------------------------------------------
// Shared group-by result verification helpers
// ---------------------------------------------------------------------------

template <const auto &Tc, typename V>
auto check_group_keys_and_agg(const std::vector<std::pair<int, V>> &groups, bool is_int) -> void {
    ASSERT_EQ(groups.size(), Tc.expected.groups_count);
    for (std::size_t g = 0; g < Tc.expected.groups_count; ++g) {
        EXPECT_EQ(groups[g].first, Tc.expected.group_keys[g]) << "Group key mismatch at index " << g;
        if (is_int)
            EXPECT_EQ(static_cast<std::int64_t>(groups[g].second), Tc.expected.group_agg_int[g])
                << "Group agg mismatch at index " << g;
        else
            EXPECT_NEAR(groups[g].second, Tc.expected.group_agg_dbl[g], 0.01) << "Group agg mismatch at index " << g;
    }
}

template <const auto &Tc> auto verify_group_int_results(const auto &result) -> void {
    std::vector<std::pair<int, std::int64_t>> groups;
    for (const auto &[key, agg] : result)
        groups.emplace_back(static_cast<int>(key), agg);
    std::ranges::sort(groups);
    check_group_keys_and_agg<Tc>(groups, true);
}

template <const auto &Tc> auto verify_group_dbl_results(const auto &result) -> void {
    std::vector<std::pair<int, double>> groups;
    for (const auto &[key, agg] : result)
        groups.emplace_back(static_cast<int>(key), agg);
    std::ranges::sort(groups);
    check_group_keys_and_agg<Tc>(groups, false);
}

// ---------------------------------------------------------------------------
// SelectRunner -- plain SELECT queries (select / first / one)
// ---------------------------------------------------------------------------
template <typename Model, typename ConnType> class SelectRunner : public SelectQueryRunnerBase<Model, ConnType> {
    template <const auto &Tc, typename Row> auto verify_basic_fields(const Row &row) -> void {
        if constexpr (Tc.expected.first.has_name && requires { row.name; })
            EXPECT_EQ(row.name, std::string(Tc.expected.first.name.view()));
        if constexpr (Tc.expected.first.has_age && requires { row.age; })
            EXPECT_EQ(row.age, Tc.expected.first.age);
        if constexpr (Tc.expected.first.has_salary && requires { row.salary; })
            EXPECT_NEAR(row.salary, Tc.expected.first.salary, 0.01);
        if constexpr (Tc.expected.first.has_is_active && requires { row.is_active; })
            EXPECT_EQ(row.is_active, Tc.expected.first.is_active);
    }

    template <const auto &Tc, typename Row> auto verify_extended_fields(const Row &row) -> void {
        if constexpr (Tc.expected.first.has_years_experience && requires { row.years_experience; })
            EXPECT_EQ(row.years_experience, Tc.expected.first.years_experience);
        if constexpr (Tc.expected.first.has_department && requires { row.department; })
            EXPECT_EQ(row.department, std::string(Tc.expected.first.department.view()));
        if constexpr (Tc.expected.first.has_content && requires { row.content; })
            EXPECT_EQ(row.content, std::string(Tc.expected.first.content.view()));
        if constexpr (Tc.expected.first.has_value && requires { row.value; })
            EXPECT_EQ(row.value, Tc.expected.first.value);
    }

  public:
    template <const auto &Tc, typename Row> auto verify_first_row(const Row &row) -> void {
        verify_basic_fields<Tc>(row);
        verify_extended_fields<Tc>(row);
    }

    template <const auto &Tc> auto run() -> void { // NOSONAR(S3776) -- inherent constexpr dispatch
        this->template apply_where_and_join<Tc>();
        if constexpr (Tc.query_type == "first") {
            auto r = this->qs_.first().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(r.value().has_value() ? 1 : 0, Tc.expected.count);
            if constexpr (Tc.expected.count == 1) {
                ASSERT_TRUE(r.value().has_value());
                verify_first_row<Tc>(r.value().value());
            }
        } else if constexpr (Tc.query_type == "one") {
            auto r = this->qs_.get().execute();
            if constexpr (Tc.expected.count == 1) {
                ASSERT_TRUE(r.has_value()) << r.error().message();
                verify_first_row<Tc>(r.value());
            } else {
                EXPECT_FALSE(r.has_value());
            }
        } else {
            auto result = this->qs_.select().execute();
            ASSERT_TRUE(result.has_value()) << result.error().message();
            EXPECT_EQ(static_cast<int>(result.value().size()), Tc.expected.count);
            if constexpr (Tc.expected.count > 0) {
                ASSERT_FALSE(result.value().empty());
                verify_first_row<Tc>(*result.value().begin());
            }
        }
    }
};

// ---------------------------------------------------------------------------
// AggregateRunner -- scalar aggregates (count, sum, avg, min, max, count_distinct)
// ---------------------------------------------------------------------------
template <typename Model, typename ConnType> class AggregateRunner : public SelectQueryRunnerBase<Model, ConnType> {
  public:
    template <const auto &Tc> auto run() -> void { // NOSONAR(S3776) -- inherent constexpr dispatch
        this->template apply_where_and_join<Tc>();

        if constexpr (Tc.query_type == "count") {
            auto r = this->qs_.count().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(r.value(), Tc.expected.int_val);

        } else if constexpr (Tc.query_type == "count_distinct") {
            constexpr auto fi = dispatch_field<Model>(Tc.bench.aggregate.field.view());
            auto r = this->qs_.template count_distinct<fi>().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(r.value(), Tc.expected.int_val);

        } else if constexpr (Tc.query_type == "sum") {
            constexpr auto fi = dispatch_field<Model>(Tc.bench.aggregate.field.view());
            auto r = this->qs_.template sum<fi>().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            if constexpr (Tc.expected.int_val != -1)
                EXPECT_EQ(r.value(), Tc.expected.int_val);
            else
                EXPECT_NEAR(r.value(), Tc.expected.dbl_val, 0.01);

        } else if constexpr (Tc.query_type == "avg") {
            constexpr auto fi = dispatch_field<Model>(Tc.bench.aggregate.field.view());
            auto r = this->qs_.template avg<fi>().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_NEAR(r.value(), Tc.expected.dbl_val, 0.01);

        } else if constexpr (Tc.query_type == "min") {
            constexpr auto fi = dispatch_field<Model>(Tc.bench.aggregate.field.view());
            auto r = this->qs_.template min<fi>().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_NEAR(r.value(), Tc.expected.dbl_val, 0.01);

        } else if constexpr (Tc.query_type == "max") {
            constexpr auto fi = dispatch_field<Model>(Tc.bench.aggregate.field.view());
            auto r = this->qs_.template max<fi>().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_NEAR(r.value(), Tc.expected.dbl_val, 0.01);

        } else if constexpr (Tc.query_type == "count_field") {
            constexpr auto fi = dispatch_field<Model>(Tc.bench.aggregate.field.view());
            auto r = this->qs_.template count<fi>().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(r.value(), Tc.expected.int_val);
        }
    }
};

// ---------------------------------------------------------------------------
// ChainAggRunner -- multi-aggregate chain queries (query_type == "chain")
// ---------------------------------------------------------------------------
template <typename Model, typename ConnType> class ChainAggRunner : public SelectQueryRunnerBase<Model, ConnType> {
    template <const auto &Tc> auto check_chain5(const auto &r) -> void {
        ASSERT_TRUE(r.has_value()) << r.error().message();
        auto [v0, v1, v2, v3, v4] = r.value();
        EXPECT_EQ(v0, static_cast<std::int64_t>(Tc.aggregations[0].res_value));
        EXPECT_EQ(v1, static_cast<std::int64_t>(Tc.aggregations[1].res_value));
        EXPECT_NEAR(v2, Tc.aggregations[2].res_value, 0.01);
        EXPECT_NEAR(v3, Tc.aggregations[3].res_value, 0.01);
        EXPECT_NEAR(v4, Tc.aggregations[4].res_value, 0.01);
    }

    template <const auto &Tc> auto run_chain2_sum_or_count() -> void {
        if constexpr (Tc.aggregations[0].func == "sum" && Tc.aggregations[1].func == "count") {
            constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
            auto [v0, v1] = this->qs_.template sum<fi0>().count().execute().value();
            EXPECT_EQ(v0, static_cast<std::int64_t>(Tc.aggregations[0].res_value));
            EXPECT_EQ(v1, static_cast<std::int64_t>(Tc.aggregations[1].res_value));
        } else if constexpr (Tc.aggregations[0].func == "count") {
            constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
            auto [v0, v1] = this->qs_.count().template sum<fi1>().execute().value();
            EXPECT_EQ(v0, static_cast<std::int64_t>(Tc.aggregations[0].res_value));
            EXPECT_EQ(v1, static_cast<std::int64_t>(Tc.aggregations[1].res_value));
        } else if constexpr (Tc.aggregations[0].func == "sum") {
            constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
            constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
            auto [v0, v1] = this->qs_.template sum<fi0>().template avg<fi1>().execute().value();
            EXPECT_EQ(v0, static_cast<std::int64_t>(Tc.aggregations[0].res_value));
            EXPECT_NEAR(v1, Tc.aggregations[1].res_value, 0.01);
        }
    }

    template <const auto &Tc> auto run_chain2_avg() -> void {
        constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
        if constexpr (Tc.aggregations[1].func == "count") {
            auto [v0, v1] = this->qs_.template avg<fi0>().count().execute().value();
            EXPECT_NEAR(v0, Tc.aggregations[0].res_value, 0.01);
            EXPECT_EQ(v1, static_cast<std::int64_t>(Tc.aggregations[1].res_value));
        } else {
            constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
            if constexpr (Tc.aggregations[1].func == "min") {
                auto [v0, v1] = this->qs_.template avg<fi0>().template min<fi1>().execute().value();
                EXPECT_NEAR(v0, Tc.aggregations[0].res_value, 0.01);
                EXPECT_NEAR(v1, Tc.aggregations[1].res_value, 0.01);
            } else {
                auto [v0, v1] = this->qs_.template avg<fi0>().template max<fi1>().execute().value();
                EXPECT_NEAR(v0, Tc.aggregations[0].res_value, 0.01);
                EXPECT_NEAR(v1, Tc.aggregations[1].res_value, 0.01);
            }
        }
    }

    template <const auto &Tc> auto run_chain2_min_or_max() -> void {
        constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
        constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
        if constexpr (Tc.aggregations[0].func == "min" && Tc.aggregations[1].func == "max") {
            auto [v0, v1] = this->qs_.template min<fi0>().template max<fi1>().execute().value();
            EXPECT_NEAR(v0, Tc.aggregations[0].res_value, 0.01);
            EXPECT_NEAR(v1, Tc.aggregations[1].res_value, 0.01);
        } else if constexpr (Tc.aggregations[0].func == "min") {
            auto [v0, v1] = this->qs_.template min<fi0>().template sum<fi1>().execute().value();
            EXPECT_NEAR(v0, Tc.aggregations[0].res_value, 0.01);
            EXPECT_EQ(v1, static_cast<std::int64_t>(Tc.aggregations[1].res_value));
        } else if constexpr (Tc.aggregations[0].func == "max" && Tc.aggregations[1].func == "avg") {
            auto [v0, v1] = this->qs_.template max<fi0>().template avg<fi1>().execute().value();
            EXPECT_NEAR(v0, Tc.aggregations[0].res_value, 0.01);
            EXPECT_NEAR(v1, Tc.aggregations[1].res_value, 0.01);
        } else if constexpr (Tc.aggregations[0].func == "max") {
            auto [v0, v1] = this->qs_.template max<fi0>().template min<fi1>().execute().value();
            EXPECT_NEAR(v0, Tc.aggregations[0].res_value, 0.01);
            EXPECT_NEAR(v1, Tc.aggregations[1].res_value, 0.01);
        }
    }

    template <const auto &Tc> auto run_chain2() -> void {
        if constexpr (Tc.aggregations[0].func == "avg")
            run_chain2_avg<Tc>();
        else if constexpr (Tc.aggregations[0].func == "min" || Tc.aggregations[0].func == "max")
            run_chain2_min_or_max<Tc>();
        else
            run_chain2_sum_or_count<Tc>();
    }

    template <const auto &Tc> auto run_chain3() -> void {
        if constexpr (Tc.aggregations[0].func == "sum") {
            constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
            constexpr auto fi2 = dispatch_field<Model>(Tc.aggregations[2].field.view());
            auto r = this->qs_.template sum<fi0>().count().template avg<fi2>().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            auto [v0, v1, v2] = r.value();
            EXPECT_EQ(v0, static_cast<std::int64_t>(Tc.aggregations[0].res_value));
            EXPECT_EQ(v1, static_cast<std::int64_t>(Tc.aggregations[1].res_value));
            EXPECT_NEAR(v2, Tc.aggregations[2].res_value, 0.01);
        } else if constexpr (Tc.aggregations[0].func == "count") {
            constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
            constexpr auto fi2 = dispatch_field<Model>(Tc.aggregations[2].field.view());
            auto r = this->qs_.count().template sum<fi1>().template avg<fi2>().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            auto [v0, v1, v2] = r.value();
            EXPECT_EQ(v0, static_cast<std::int64_t>(Tc.aggregations[0].res_value));
            EXPECT_EQ(v1, static_cast<std::int64_t>(Tc.aggregations[1].res_value));
            EXPECT_NEAR(v2, Tc.aggregations[2].res_value, 0.01);
        }
    }

    template <const auto &Tc> auto run_chain4() -> void {
        constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
        constexpr auto fi2 = dispatch_field<Model>(Tc.aggregations[2].field.view());
        constexpr auto fi3 = dispatch_field<Model>(Tc.aggregations[3].field.view());
        auto r = this->qs_.count().template sum<fi1>().template min<fi2>().template max<fi3>().execute();
        ASSERT_TRUE(r.has_value()) << r.error().message();
        auto [v0, v1, v2, v3] = r.value();
        EXPECT_EQ(v0, static_cast<std::int64_t>(Tc.aggregations[0].res_value));
        EXPECT_EQ(v1, static_cast<std::int64_t>(Tc.aggregations[1].res_value));
        EXPECT_NEAR(v2, Tc.aggregations[2].res_value, 0.01);
        EXPECT_NEAR(v3, Tc.aggregations[3].res_value, 0.01);
    }

    template <const auto &Tc> auto chain5_tail(auto &&head) -> void {
        constexpr auto fi2 = dispatch_field<Model>(Tc.aggregations[2].field.view());
        constexpr auto fi3 = dispatch_field<Model>(Tc.aggregations[3].field.view());
        constexpr auto fi4 = dispatch_field<Model>(Tc.aggregations[4].field.view());
        check_chain5<Tc>(
            std::forward<decltype(head)>(head).template avg<fi2>().template min<fi3>().template max<fi4>().execute());
    }

    template <const auto &Tc> auto run_chain5_sum() -> void {
        constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
        chain5_tail<Tc>(this->qs_.template sum<fi0>().count());
    }

    template <const auto &Tc> auto run_chain5_count() -> void {
        constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
        chain5_tail<Tc>(this->qs_.count().template sum<fi1>());
    }

  public:
    template <const auto &Tc> auto run() -> void {
        this->template apply_where_and_join<Tc>();
        if constexpr (Tc.chain_len == 2)
            run_chain2<Tc>();
        else if constexpr (Tc.chain_len == 3)
            run_chain3<Tc>();
        else if constexpr (Tc.chain_len == 4)
            run_chain4<Tc>();
        else if constexpr (Tc.chain_len == 5 && Tc.aggregations[0].func == "sum")
            run_chain5_sum<Tc>();
        else if constexpr (Tc.chain_len == 5)
            run_chain5_count<Tc>();
    }
};

// ---------------------------------------------------------------------------
// DistinctRunner -- DISTINCT projection (returns plf::hive<FieldType>)
// ---------------------------------------------------------------------------
template <typename Model, typename ConnType> class DistinctRunner : public SelectQueryRunnerBase<Model, ConnType> {
  public:
    template <const auto &Tc> auto run() -> void {
        this->template apply_where_and_join<Tc>();
        constexpr std::size_t fc = Tc.bench.distinct.field_count();
        if constexpr (fc >= 2) {
            constexpr auto fi1 = dispatch_field<Model>(Tc.bench.distinct.fields[0].view());
            constexpr auto fi2 = dispatch_field<Model>(Tc.bench.distinct.fields[1].view());
            auto r = this->qs_.template distinct<fi1, fi2>().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
        } else {
            constexpr auto fi = dispatch_field<Model>(Tc.bench.distinct.fields[0].view());
            auto r = this->qs_.template distinct<fi>().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
        }
    }
};

// ---------------------------------------------------------------------------
// GroupByRunner -- GROUP BY + aggregate (group_count, group_sum, etc.)
// ---------------------------------------------------------------------------
template <typename Model, typename ConnType> class GroupByRunner : public SelectQueryRunnerBase<Model, ConnType> {
    template <const auto &Tc> auto run_group_count() -> void { // NOSONAR(S3776)
        constexpr auto gb_fi = dispatch_field<Model>(Tc.bench.group_by.fields[0].view());
        if constexpr (Tc.bench.group_by.field_count() >= 2) {
            constexpr auto gb_fi2 = dispatch_field<Model>(Tc.bench.group_by.fields[1].view());
            auto r = this->qs_.template group_by<gb_fi, gb_fi2>().count().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
        } else if constexpr (Tc.bench.group_by.having.enabled) {
            constexpr auto hv_fi = dispatch_field<Model>(Tc.bench.group_by.having.field.view());
            auto r = this->qs_.template group_by<gb_fi>()
                         .having(build_where_expr<hv_fi>(Tc.bench.group_by.having.op.view(),
                                                         Tc.bench.group_by.having.value.as_int))
                         .count()
                         .execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
        } else {
            auto r = this->qs_.template group_by<gb_fi>().count().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
            if constexpr (Tc.expected.groups_count > 0)
                verify_group_int_results<Tc>(r.value());
        }
    }

  public:
    template <const auto &Tc> auto run() -> void { // NOSONAR(S3776) -- inherent constexpr dispatch
        this->template apply_where_and_join<Tc>();
        constexpr auto gb_fi = dispatch_field<Model>(Tc.bench.group_by.fields[0].view());

        if constexpr (Tc.query_type == "group_count") {
            run_group_count<Tc>();
        } else if constexpr (Tc.query_type == "group_sum") {
            constexpr auto agg_fi = dispatch_field<Model>(Tc.bench.aggregate.field.view());
            auto r = this->qs_.template group_by<gb_fi>().template sum<agg_fi>().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
            if constexpr (Tc.expected.groups_count > 0)
                verify_group_int_results<Tc>(r.value());
        } else if constexpr (Tc.query_type == "group_avg") {
            constexpr auto agg_fi = dispatch_field<Model>(Tc.bench.aggregate.field.view());
            auto r = this->qs_.template group_by<gb_fi>().template avg<agg_fi>().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
            if constexpr (Tc.expected.groups_count > 0)
                verify_group_dbl_results<Tc>(r.value());
        } else if constexpr (Tc.query_type == "group_min") {
            constexpr auto agg_fi = dispatch_field<Model>(Tc.bench.aggregate.field.view());
            auto r = this->qs_.template group_by<gb_fi>().template min<agg_fi>().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
            if constexpr (Tc.expected.groups_count > 0)
                verify_group_dbl_results<Tc>(r.value());
        } else if constexpr (Tc.query_type == "group_max") {
            constexpr auto agg_fi = dispatch_field<Model>(Tc.bench.aggregate.field.view());
            auto r = this->qs_.template group_by<gb_fi>().template max<agg_fi>().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
            if constexpr (Tc.expected.groups_count > 0)
                verify_group_dbl_results<Tc>(r.value());
        }
    }
};

} // namespace storm::test
