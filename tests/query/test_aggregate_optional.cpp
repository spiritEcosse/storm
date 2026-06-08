#include <gtest/gtest.h>
#include "test_db_helpers.h"

import storm;
import std;

using storm::QuerySet;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"
#include "test_aggregate_fixture.h"

// =============================================================================
// NULL/Optional Tests
// =============================================================================

template <typename ConnType> class OptionalAggregateTest : public StormTestFixture<Person, ConnType> {
  public:
    auto on_after_setup(const std::shared_ptr<ConnType>&) -> void override {
        qs = std::make_unique<QuerySet<Person, ConnType>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        StormTestFixture<Person, ConnType>::TearDown();
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    std::unique_ptr<QuerySet<Person, ConnType>> qs; // NOSONAR cpp:S3656
};

TYPED_TEST_SUITE(OptionalAggregateTest, DatabaseTypes);

TYPED_TEST(OptionalAggregateTest, CountWithNullValues) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .salary = 50000.0, .score = 25},
            {.name = "Bob", .salary = 60000.0, .score = std::nullopt},
            {.name = "Charlie", .salary = 70000.0, .score = 35},
            {.name = "Dave", .salary = 80000.0, .score = std::nullopt},
    })));

    auto count_all = this->qs->count().execute();
    ASSERT_TRUE(count_all.has_value());
    EXPECT_EQ(count_all.value(), 4);

    auto count_age = this->qs->template count<^^Person::score>().execute();
    ASSERT_TRUE(count_age.has_value());
    EXPECT_EQ(count_age.value(), 2);
}

TYPED_TEST(OptionalAggregateTest, SumWithNullValues) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .salary = 50000.0, .score = 25},
            {.name = "Bob", .salary = 60000.0, .score = std::nullopt},
            {.name = "Charlie", .salary = 70000.0, .score = 35},
    })));

    auto sum = this->qs->template sum<^^Person::score>().execute();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 60);
}

TYPED_TEST(OptionalAggregateTest, AvgWithNullValues) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .salary = 50000.0, .score = 20},
            {.name = "Bob", .salary = 60000.0, .score = std::nullopt},
            {.name = "Charlie", .salary = 70000.0, .score = 40},
    })));

    auto avg = this->qs->template avg<^^Person::score>().execute();
    ASSERT_TRUE(avg.has_value());
    EXPECT_NEAR(avg.value(), 30.0, 0.01);
}

TYPED_TEST(OptionalAggregateTest, MinMaxWithNullValues) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .salary = 50000.0, .score = 25},
            {.name = "Bob", .salary = 60000.0, .score = std::nullopt},
            {.name = "Charlie", .salary = 70000.0, .score = 45},
            {.name = "Dave", .salary = 80000.0, .score = std::nullopt},
    })));

    auto min_val = this->qs->template min<^^Person::score>().execute();
    ASSERT_TRUE(min_val.has_value());
    EXPECT_NEAR(min_val.value(), 25.0, 0.01);

    auto max_val = this->qs->template max<^^Person::score>().execute();
    ASSERT_TRUE(max_val.has_value());
    EXPECT_NEAR(max_val.value(), 45.0, 0.01);
}

TYPED_TEST(OptionalAggregateTest, CountDistinctWithNullValues) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .salary = 50000.0, .score = 30},
            {.name = "Bob", .salary = 60000.0, .score = std::nullopt},
            {.name = "Charlie", .salary = 70000.0, .score = 30},
            {.name = "Dave", .salary = 80000.0, .score = std::nullopt},
            {.name = "Eve", .salary = 90000.0, .score = 40},
    })));

    auto result = this->qs->template count_distinct<^^Person::score>().execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 2);
}

TYPED_TEST(OptionalAggregateTest, GroupByWithAllNullValuesInGroupColumn) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .salary = 50000.0, .score = std::nullopt},
            {.name = "Bob", .salary = 60000.0, .score = std::nullopt},
            {.name = "Charlie", .salary = 70000.0, .score = std::nullopt},
    })));

    auto result = this->qs->template group_by<^^Person::score>().count().execute();
    ASSERT_TRUE(result.has_value()) << "GROUP BY with all NULL values failed";
    EXPECT_EQ(result.value().size(), 1);

    auto& groups = result.value();
    auto  it     = groups.begin();
    ASSERT_NE(it, groups.end());
    auto [score_key, count_val] = *it;
    EXPECT_FALSE(score_key.has_value()) << "Expected NULL group key";
    EXPECT_EQ(count_val, 3) << "Expected count of 3 in NULL group";
}

TYPED_TEST(OptionalAggregateTest, GroupByWithMixedNullAndNonNullValues) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .salary = 50000.0, .score = 25},
            {.name = "Bob", .salary = 60000.0, .score = std::nullopt},
            {.name = "Charlie", .salary = 70000.0, .score = 25},
            {.name = "Dave", .salary = 80000.0, .score = std::nullopt},
            {.name = "Eve", .salary = 90000.0, .score = 30},
    })));

    auto result = this->qs->template group_by<^^Person::score>().count().execute();
    ASSERT_TRUE(result.has_value()) << "GROUP BY with mixed NULL values failed";
    EXPECT_EQ(result.value().size(), 3) << "Expected 3 groups (NULL, 25, 30)";

    std::int64_t null_count   = 0; // NOLINT(misc-const-correctness) - modified in loop
    std::int64_t age_25_count = 0; // NOLINT(misc-const-correctness) - modified in loop
    std::int64_t age_30_count = 0; // NOLINT(misc-const-correctness) - modified in loop

    for (const auto& [score_key, count_val] : result.value()) {
        if (!score_key.has_value()) {
            null_count = count_val;
        } else if (score_key.value() == 25) {
            age_25_count = count_val;
        } else if (score_key.value() == 30) {
            age_30_count = count_val;
        }
    }

    EXPECT_EQ(null_count, 2) << "Expected 2 rows in NULL group";
    EXPECT_EQ(age_25_count, 2) << "Expected 2 rows in age=25 group";
    EXPECT_EQ(age_30_count, 1) << "Expected 1 row in age=30 group";
}

// =============================================================================
// Negative Number Tests
// =============================================================================

TYPED_TEST_SUITE(AggregateTest, DatabaseTypes);

TYPED_TEST(AggregateTest, NegativeNumbersInSum) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3},
            {.name = "Bob", .age = 5, .salary = 60000.0, .years_experience = 5},
            {.name = "Charlie", .age = -3, .salary = 70000.0, .years_experience = 7},
    })));

    auto sum = this->qs->template sum<^^Person::age>().execute();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), -8);
}

TYPED_TEST(AggregateTest, NegativeNumbersInAvg) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = -12, .salary = 50000.0, .years_experience = 3},
            {.name = "Bob", .age = 6, .salary = 60000.0, .years_experience = 5},
            {.name = "Charlie", .age = 0, .salary = 70000.0, .years_experience = 7},
    })));

    auto avg = this->qs->template avg<^^Person::age>().execute();
    ASSERT_TRUE(avg.has_value());
    EXPECT_NEAR(avg.value(), -2.0, 0.01);
}

TYPED_TEST(AggregateTest, NegativeNumbersInMinMax) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3},
            {.name = "Bob", .age = 5, .salary = 60000.0, .years_experience = 5},
            {.name = "Charlie", .age = -20, .salary = 70000.0, .years_experience = 7},
            {.name = "Dave", .age = 15, .salary = 80000.0, .years_experience = 10},
    })));

    auto min_val = this->qs->template min<^^Person::age>().execute();
    ASSERT_TRUE(min_val.has_value());
    EXPECT_NEAR(min_val.value(), -20.0, 0.01);

    auto max_val = this->qs->template max<^^Person::age>().execute();
    ASSERT_TRUE(max_val.has_value());
    EXPECT_NEAR(max_val.value(), 15.0, 0.01);
}

TYPED_TEST(AggregateTest, NegativeNumbersInCount) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3},
            {.name = "Bob", .age = -5, .salary = 60000.0, .years_experience = 5},
            {.name = "Charlie", .age = 0, .salary = 70000.0, .years_experience = 7},
    })));

    auto count = this->qs->count().execute();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 3);

    auto count_age = this->qs->template count<^^Person::age>().execute();
    ASSERT_TRUE(count_age.has_value());
    EXPECT_EQ(count_age.value(), 3);
}

TYPED_TEST(AggregateTest, NegativeNumbersInWhere) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3},
            {.name = "Bob", .age = -5, .salary = 60000.0, .years_experience = 5},
            {.name = "Charlie", .age = 0, .salary = 70000.0, .years_experience = 7},
            {.name = "Dave", .age = 5, .salary = 80000.0, .years_experience = 10},
            {.name = "Eve", .age = 10, .salary = 90000.0, .years_experience = 15},
    })));

    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() < 0).count().execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 2);

    auto sum_neg =
            this->qs->where(storm::orm::where::field<^^Person::age>() < 0).template sum<^^Person::age>().execute();
    ASSERT_TRUE(sum_neg.has_value());
    EXPECT_EQ(sum_neg.value(), -15);
}
