#pragma once

/**
 * @file test_select_runner.h
 * @brief SelectRunner, DistinctValuesRunner, AggregateRunner, GroupByRunner.
 *
 * Runners for UnifiedTestCase-driven SELECT, DISTINCT, VALUES, aggregate, and
 * GROUP BY queries.  Include AFTER `import storm;`, test_models.h,
 * test_query_dispatch.h, and test_query_runner_base.h.
 */

#include "test_query_runner_base.h"

#include <algorithm>
#include <vector>

namespace storm::test {

// ---------------------------------------------------------------------------
// SelectRunner -- plain SELECT queries (select / first / one)
// ---------------------------------------------------------------------------
template <typename Model, typename ConnType> class SelectRunner : public SelectQueryRunnerBase<Model, ConnType> {
  public:
    template <const auto &Tc> void run() { // NOSONAR(S3776) -- inherent constexpr dispatch
        this->template apply_query_filters<Tc>();
        if constexpr (Tc.query_type == "first") {
            auto r = this->qs_.first().execute();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(r.value().has_value() ? 1 : 0, Tc.expected.count);
            if constexpr (Tc.expected.count == 1) {
                if constexpr (!Tc.expected.first_name.empty()) {
                    ASSERT_TRUE(r.value().has_value());
                    EXPECT_EQ(r.value().value().name, Tc.expected.first_name.view());
                }
                if constexpr (Tc.expected.first_age != -1) {
                    ASSERT_TRUE(r.value().has_value());
                    EXPECT_EQ(r.value().value().age, Tc.expected.first_age);
                }
            }
        } else if constexpr (Tc.query_type == "one") {
            auto r = this->qs_.get().execute();
            if constexpr (Tc.expected.count == 1) {
                ASSERT_TRUE(r.has_value()) << r.error().message();
                if constexpr (!Tc.expected.first_name.empty())
                    EXPECT_EQ(r.value().name, Tc.expected.first_name.view());
                if constexpr (Tc.expected.first_age != -1)
                    EXPECT_EQ(r.value().age, Tc.expected.first_age);
            } else {
                EXPECT_FALSE(r.has_value()); // expected error
            }
        } else {
            auto result = this->qs_.select().execute();
            ASSERT_TRUE(result.has_value()) << result.error().message();
            EXPECT_EQ(static_cast<int>(result.value().size()), Tc.expected.count);
            if constexpr (Tc.expected.first_age != -1) {
                ASSERT_FALSE(result.value().empty());
                EXPECT_EQ(result.value().begin()->age, Tc.expected.first_age);
            }
            if constexpr (!Tc.expected.first_name.empty()) {
                ASSERT_FALSE(result.value().empty());
                EXPECT_EQ(result.value().begin()->name, std::string(Tc.expected.first_name.view()));
            }
        }
    }
};

// ---------------------------------------------------------------------------
// AggregateRunner -- scalar aggregates (count, sum, avg, min, max, count_distinct)
// ---------------------------------------------------------------------------
template <typename Model, typename ConnType> class AggregateRunner : public SelectQueryRunnerBase<Model, ConnType> {
  public:
    template <const auto &Tc> void run() { // NOSONAR(S3776) -- inherent constexpr dispatch
        this->template apply_query_filters<Tc>();

        if constexpr (Tc.query_type == "count") {
            auto r = this->qs_.count().get();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(r.value(), Tc.expected.int_val);

        } else if constexpr (Tc.query_type == "count_distinct") {
            constexpr auto fi = dispatch_field<Model>(Tc.agg_field.view());
            auto r = this->qs_.template count_distinct<fi>().get();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(r.value(), Tc.expected.int_val);

        } else if constexpr (Tc.query_type == "sum") {
            constexpr auto fi = dispatch_field<Model>(Tc.agg_field.view());
            if constexpr (Tc.expected.int_val != -1) {
                auto r = this->qs_.template sum<fi>().get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                EXPECT_EQ(r.value(), Tc.expected.int_val);
            } else {
                auto r = this->qs_.template sum<fi>().get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                EXPECT_NEAR(r.value(), Tc.expected.dbl_val, 0.01);
            }

        } else if constexpr (Tc.query_type == "avg") {
            constexpr auto fi = dispatch_field<Model>(Tc.agg_field.view());
            auto r = this->qs_.template avg<fi>().get();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_NEAR(r.value(), Tc.expected.dbl_val, 0.01);

        } else if constexpr (Tc.query_type == "min") {
            constexpr auto fi = dispatch_field<Model>(Tc.agg_field.view());
            auto r = this->qs_.template min<fi>().get();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_NEAR(r.value(), Tc.expected.dbl_val, 0.01);

        } else if constexpr (Tc.query_type == "max") {
            constexpr auto fi = dispatch_field<Model>(Tc.agg_field.view());
            auto r = this->qs_.template max<fi>().get();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_NEAR(r.value(), Tc.expected.dbl_val, 0.01);

        } else if constexpr (Tc.query_type == "count_field") {
            constexpr auto fi = dispatch_field<Model>(Tc.agg_field.view());
            auto r = this->qs_.template count<fi>().get();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(r.value(), Tc.expected.int_val);
        }
    }
};

// ---------------------------------------------------------------------------
// ChainAggRunner -- multi-aggregate chain queries (query_type == "chain")
// ---------------------------------------------------------------------------
template <typename Model, typename ConnType> class ChainAggRunner : public SelectQueryRunnerBase<Model, ConnType> {
  public:
    template <const auto &Tc>
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void run() { // NOSONAR(S3776) -- inherent constexpr dispatch
        this->template apply_query_filters<Tc>();

        if constexpr (Tc.chain_len == 2) {
            if constexpr (Tc.aggregations[0].func == "sum" && Tc.aggregations[1].func == "count") {
                constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
                auto r = this->qs_.template sum<fi0>().count().get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                auto [v0, v1] = r.value();
                EXPECT_EQ(v0, static_cast<int64_t>(Tc.aggregations[0].res_value));
                EXPECT_EQ(v1, static_cast<int64_t>(Tc.aggregations[1].res_value));
            } else if constexpr (Tc.aggregations[0].func == "count") {
                constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
                auto r = this->qs_.count().template sum<fi1>().get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                auto [v0, v1] = r.value();
                EXPECT_EQ(v0, static_cast<int64_t>(Tc.aggregations[0].res_value));
                EXPECT_EQ(v1, static_cast<int64_t>(Tc.aggregations[1].res_value));
            } else if constexpr (Tc.aggregations[0].func == "avg" && Tc.aggregations[1].func == "count") {
                constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
                auto r = this->qs_.template avg<fi0>().count().get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                auto [v0, v1] = r.value();
                EXPECT_NEAR(v0, Tc.aggregations[0].res_value, 0.01);
                EXPECT_EQ(v1, static_cast<int64_t>(Tc.aggregations[1].res_value));
            } else if constexpr (Tc.aggregations[0].func == "avg" && Tc.aggregations[1].func == "min") {
                constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
                constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
                auto r = this->qs_.template avg<fi0>().template min<fi1>().get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                auto [v0, v1] = r.value();
                EXPECT_NEAR(v0, Tc.aggregations[0].res_value, 0.01);
                EXPECT_NEAR(v1, Tc.aggregations[1].res_value, 0.01);
            } else if constexpr (Tc.aggregations[0].func == "avg" && Tc.aggregations[1].func == "max") {
                constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
                constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
                auto r = this->qs_.template avg<fi0>().template max<fi1>().get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                auto [v0, v1] = r.value();
                EXPECT_NEAR(v0, Tc.aggregations[0].res_value, 0.01);
                EXPECT_NEAR(v1, Tc.aggregations[1].res_value, 0.01);
            } else if constexpr (Tc.aggregations[0].func == "sum" && Tc.aggregations[1].func == "avg") {
                constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
                constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
                auto r = this->qs_.template sum<fi0>().template avg<fi1>().get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                auto [v0, v1] = r.value();
                EXPECT_EQ(v0, static_cast<int64_t>(Tc.aggregations[0].res_value));
                EXPECT_NEAR(v1, Tc.aggregations[1].res_value, 0.01);
            } else if constexpr (Tc.aggregations[0].func == "min" && Tc.aggregations[1].func == "max") {
                constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
                constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
                auto r = this->qs_.template min<fi0>().template max<fi1>().get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                auto [v0, v1] = r.value();
                EXPECT_NEAR(v0, Tc.aggregations[0].res_value, 0.01);
                EXPECT_NEAR(v1, Tc.aggregations[1].res_value, 0.01);
            } else if constexpr (Tc.aggregations[0].func == "min" && Tc.aggregations[1].func == "sum") {
                constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
                constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
                auto r = this->qs_.template min<fi0>().template sum<fi1>().get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                auto [v0, v1] = r.value();
                EXPECT_NEAR(v0, Tc.aggregations[0].res_value, 0.01);
                EXPECT_EQ(v1, static_cast<int64_t>(Tc.aggregations[1].res_value));
            } else if constexpr (Tc.aggregations[0].func == "max" && Tc.aggregations[1].func == "avg") {
                constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
                constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
                auto r = this->qs_.template max<fi0>().template avg<fi1>().get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                auto [v0, v1] = r.value();
                EXPECT_NEAR(v0, Tc.aggregations[0].res_value, 0.01);
                EXPECT_NEAR(v1, Tc.aggregations[1].res_value, 0.01);
            } else if constexpr (Tc.aggregations[0].func == "max" && Tc.aggregations[1].func == "min") {
                constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
                constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
                auto r = this->qs_.template max<fi0>().template min<fi1>().get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                auto [v0, v1] = r.value();
                EXPECT_NEAR(v0, Tc.aggregations[0].res_value, 0.01);
                EXPECT_NEAR(v1, Tc.aggregations[1].res_value, 0.01);
            }
        } else if constexpr (Tc.chain_len == 3) {
            if constexpr (Tc.aggregations[0].func == "sum") {
                constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
                constexpr auto fi2 = dispatch_field<Model>(Tc.aggregations[2].field.view());
                auto r = this->qs_.template sum<fi0>().count().template avg<fi2>().get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                auto [v0, v1, v2] = r.value();
                EXPECT_EQ(v0, static_cast<int64_t>(Tc.aggregations[0].res_value));
                EXPECT_EQ(v1, static_cast<int64_t>(Tc.aggregations[1].res_value));
                EXPECT_NEAR(v2, Tc.aggregations[2].res_value, 0.01);
            } else if constexpr (Tc.aggregations[0].func == "count") {
                constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
                constexpr auto fi2 = dispatch_field<Model>(Tc.aggregations[2].field.view());
                auto r = this->qs_.count().template sum<fi1>().template avg<fi2>().get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                auto [v0, v1, v2] = r.value();
                EXPECT_EQ(v0, static_cast<int64_t>(Tc.aggregations[0].res_value));
                EXPECT_EQ(v1, static_cast<int64_t>(Tc.aggregations[1].res_value));
                EXPECT_NEAR(v2, Tc.aggregations[2].res_value, 0.01);
            }
        } else if constexpr (Tc.chain_len == 4) {
            constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
            constexpr auto fi2 = dispatch_field<Model>(Tc.aggregations[2].field.view());
            constexpr auto fi3 = dispatch_field<Model>(Tc.aggregations[3].field.view());
            auto r = this->qs_.count().template sum<fi1>().template min<fi2>().template max<fi3>().get();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            auto [v0, v1, v2, v3] = r.value();
            EXPECT_EQ(v0, static_cast<int64_t>(Tc.aggregations[0].res_value));
            EXPECT_EQ(v1, static_cast<int64_t>(Tc.aggregations[1].res_value));
            EXPECT_NEAR(v2, Tc.aggregations[2].res_value, 0.01);
            EXPECT_NEAR(v3, Tc.aggregations[3].res_value, 0.01);
        } else if constexpr (Tc.chain_len == 5) {
            if constexpr (Tc.aggregations[0].func == "sum") {
                constexpr auto fi0 = dispatch_field<Model>(Tc.aggregations[0].field.view());
                constexpr auto fi2 = dispatch_field<Model>(Tc.aggregations[2].field.view());
                constexpr auto fi3 = dispatch_field<Model>(Tc.aggregations[3].field.view());
                constexpr auto fi4 = dispatch_field<Model>(Tc.aggregations[4].field.view());
                auto r = this->qs_.template sum<fi0>()
                             .count()
                             .template avg<fi2>()
                             .template min<fi3>()
                             .template max<fi4>()
                             .get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                auto [v0, v1, v2, v3, v4] = r.value();
                EXPECT_EQ(v0, static_cast<int64_t>(Tc.aggregations[0].res_value));
                EXPECT_EQ(v1, static_cast<int64_t>(Tc.aggregations[1].res_value));
                EXPECT_NEAR(v2, Tc.aggregations[2].res_value, 0.01);
                EXPECT_NEAR(v3, Tc.aggregations[3].res_value, 0.01);
                EXPECT_NEAR(v4, Tc.aggregations[4].res_value, 0.01);
            } else if constexpr (Tc.aggregations[0].func == "count") {
                constexpr auto fi1 = dispatch_field<Model>(Tc.aggregations[1].field.view());
                constexpr auto fi2 = dispatch_field<Model>(Tc.aggregations[2].field.view());
                constexpr auto fi3 = dispatch_field<Model>(Tc.aggregations[3].field.view());
                constexpr auto fi4 = dispatch_field<Model>(Tc.aggregations[4].field.view());
                auto r = this->qs_.count()
                             .template sum<fi1>()
                             .template avg<fi2>()
                             .template min<fi3>()
                             .template max<fi4>()
                             .get();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                auto [v0, v1, v2, v3, v4] = r.value();
                EXPECT_EQ(v0, static_cast<int64_t>(Tc.aggregations[0].res_value));
                EXPECT_EQ(v1, static_cast<int64_t>(Tc.aggregations[1].res_value));
                EXPECT_NEAR(v2, Tc.aggregations[2].res_value, 0.01);
                EXPECT_NEAR(v3, Tc.aggregations[3].res_value, 0.01);
                EXPECT_NEAR(v4, Tc.aggregations[4].res_value, 0.01);
            }
        }
    }
};

// ---------------------------------------------------------------------------
// DistinctRunner -- DISTINCT projection (returns plf::hive<FieldType>)
// ---------------------------------------------------------------------------
template <typename Model, typename ConnType> class DistinctRunner : public SelectQueryRunnerBase<Model, ConnType> {
  public:
    template <const auto &Tc> void run() {
        this->template apply_query_filters<Tc>();
        if constexpr (!Tc.distinct_field2.empty()) {
            constexpr auto fi1 = dispatch_field<Model>(Tc.distinct_field.view());
            constexpr auto fi2 = dispatch_field<Model>(Tc.distinct_field2.view());
            auto r = this->qs_.template distinct<fi1, fi2>().select();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
        } else {
            constexpr auto fi = dispatch_field<Model>(Tc.distinct_field.view());
            auto r = this->qs_.template distinct<fi>().select();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
        }
    }
};

// ---------------------------------------------------------------------------
// GroupByRunner -- GROUP BY + aggregate (group_count, group_sum, etc.)
// ---------------------------------------------------------------------------
template <typename Model, typename ConnType> class GroupByRunner : public SelectQueryRunnerBase<Model, ConnType> {
  public:
    template <const auto &Tc> void run() { // NOSONAR(S3776) -- inherent constexpr dispatch
        this->template apply_query_filters<Tc>();

        constexpr auto gb_fi = dispatch_field<Model>(Tc.group_by_field.view());

        if constexpr (Tc.query_type == "group_count") {
            if constexpr (!Tc.group_by_field2.empty()) {
                constexpr auto gb_fi2 = dispatch_field<Model>(Tc.group_by_field2.view());
                auto r = this->qs_.template group_by<gb_fi, gb_fi2>().count().select();
                ASSERT_TRUE(r.has_value()) << r.error().message();
                EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
            } else {
                if constexpr (!Tc.having_field.empty()) {
                    constexpr auto hv_fi = dispatch_field<Model>(Tc.having_field.view());
                    auto r = this->qs_.template group_by<gb_fi>()
                                 .having(build_where_expr<hv_fi>(Tc.having_op.view(), Tc.having_value_int))
                                 .count()
                                 .select();
                    ASSERT_TRUE(r.has_value()) << r.error().message();
                    EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
                } else {
                    auto r = this->qs_.template group_by<gb_fi>().count().select();
                    ASSERT_TRUE(r.has_value()) << r.error().message();
                    EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
                    if constexpr (Tc.expected.groups_count > 0) { // NOSONAR(S134) -- constexpr nesting
                        std::vector<std::pair<int, int64_t>> groups;
                        for (const auto &[key, agg] : r.value())
                            groups.emplace_back(static_cast<int>(key), agg);
                        std::ranges::sort(groups);
                        ASSERT_EQ(groups.size(), Tc.expected.groups_count);
                        for (size_t g = 0; g < Tc.expected.groups_count; ++g) {
                            EXPECT_EQ(groups[g].first, Tc.expected.group_keys[g])
                                << "Group key mismatch at index " << g;
                            EXPECT_EQ(groups[g].second, Tc.expected.group_agg_int[g])
                                << "Group agg mismatch at index " << g;
                        }
                    }
                } // end else (!having_field.empty())
            } // end else (single group_by field)

        } else if constexpr (Tc.query_type == "group_sum") {
            constexpr auto agg_fi = dispatch_field<Model>(Tc.agg_field.view());
            auto r = this->qs_.template group_by<gb_fi>().template sum<agg_fi>().select();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
            if constexpr (Tc.expected.groups_count > 0) {
                std::vector<std::pair<int, int64_t>> groups;
                for (const auto &[key, agg] : r.value())
                    groups.emplace_back(static_cast<int>(key), agg);
                std::ranges::sort(groups);
                ASSERT_EQ(groups.size(), Tc.expected.groups_count);
                for (size_t g = 0; g < Tc.expected.groups_count; ++g) {
                    EXPECT_EQ(groups[g].first, Tc.expected.group_keys[g]) << "Group key mismatch at index " << g;
                    EXPECT_EQ(groups[g].second, Tc.expected.group_agg_int[g]) << "Group sum mismatch at index " << g;
                }
            }

        } else if constexpr (Tc.query_type == "group_avg") {
            constexpr auto agg_fi = dispatch_field<Model>(Tc.agg_field.view());
            auto r = this->qs_.template group_by<gb_fi>().template avg<agg_fi>().select();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
            if constexpr (Tc.expected.groups_count > 0) {
                std::vector<std::pair<int, double>> groups;
                for (const auto &[key, agg] : r.value())
                    groups.emplace_back(static_cast<int>(key), agg);
                std::ranges::sort(groups);
                ASSERT_EQ(groups.size(), Tc.expected.groups_count);
                for (size_t g = 0; g < Tc.expected.groups_count; ++g) {
                    EXPECT_EQ(groups[g].first, Tc.expected.group_keys[g]) << "Group key mismatch at index " << g;
                    EXPECT_NEAR(groups[g].second, Tc.expected.group_agg_dbl[g], 0.01)
                        << "Group avg mismatch at index " << g;
                }
            }

        } else if constexpr (Tc.query_type == "group_min") {
            constexpr auto agg_fi = dispatch_field<Model>(Tc.agg_field.view());
            auto r = this->qs_.template group_by<gb_fi>().template min<agg_fi>().select();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
            if constexpr (Tc.expected.groups_count > 0) {
                std::vector<std::pair<int, double>> groups;
                for (const auto &[key, agg] : r.value())
                    groups.emplace_back(static_cast<int>(key), agg);
                std::ranges::sort(groups);
                ASSERT_EQ(groups.size(), Tc.expected.groups_count);
                for (size_t g = 0; g < Tc.expected.groups_count; ++g) {
                    EXPECT_EQ(groups[g].first, Tc.expected.group_keys[g]) << "Group key mismatch at index " << g;
                    EXPECT_NEAR(groups[g].second, Tc.expected.group_agg_dbl[g], 0.01)
                        << "Group min mismatch at index " << g;
                }
            }

        } else if constexpr (Tc.query_type == "group_max") {
            constexpr auto agg_fi = dispatch_field<Model>(Tc.agg_field.view());
            auto r = this->qs_.template group_by<gb_fi>().template max<agg_fi>().select();
            ASSERT_TRUE(r.has_value()) << r.error().message();
            EXPECT_EQ(static_cast<int>(r.value().size()), Tc.expected.count);
            if constexpr (Tc.expected.groups_count > 0) {
                std::vector<std::pair<int, double>> groups;
                for (const auto &[key, agg] : r.value())
                    groups.emplace_back(static_cast<int>(key), agg);
                std::ranges::sort(groups);
                ASSERT_EQ(groups.size(), Tc.expected.groups_count);
                for (size_t g = 0; g < Tc.expected.groups_count; ++g) {
                    EXPECT_EQ(groups[g].first, Tc.expected.group_keys[g]) << "Group key mismatch at index " << g;
                    EXPECT_NEAR(groups[g].second, Tc.expected.group_agg_dbl[g], 0.01)
                        << "Group max mismatch at index " << g;
                }
            }
        }
    }
};

} // namespace storm::test
