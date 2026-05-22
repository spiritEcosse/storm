#include <gtest/gtest.h>
#include "test_db_helpers.h"

import storm;

import <expected>;
import <format>;
import <span>;
import <string>;
import <vector>;

using namespace storm;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"
#include "test_aggregate_fixture.h"

TYPED_TEST_SUITE(AggregateTest, DatabaseTypes);

// ============================================================================
// Single Aggregate Function Tests
// ============================================================================

TYPED_TEST(AggregateTest, SumMultipleFields) {
    this->insert_test_data();

    auto result = this->qs->template sum<^^Person::age, ^^Person::years_experience>().execute();
    ASSERT_TRUE(result.has_value()) << "SUM multi-field failed: " << result.error().message();
    EXPECT_EQ(result.value(), 1064);
}

TYPED_TEST(AggregateTest, AvgMultipleFields) {
    this->insert_test_data();

    auto result = this->qs->template avg<^^Person::age, ^^Person::years_experience>().execute();
    ASSERT_TRUE(result.has_value()) << "AVG multi-field failed: " << result.error().message();
    EXPECT_NEAR(result.value(), 42.56, 0.01);
}

TYPED_TEST(AggregateTest, MinMultipleFields) {
    this->insert_test_data();

    auto result = this->qs->template min<^^Person::age, ^^Person::years_experience>().execute();
    ASSERT_TRUE(result.has_value()) << "MIN multi-field failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 27.0);
}

TYPED_TEST(AggregateTest, MaxMultipleFields) {
    this->insert_test_data();

    auto result = this->qs->template max<^^Person::age, ^^Person::years_experience>().execute();
    ASSERT_TRUE(result.has_value()) << "MAX multi-field failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 60.0);
}

// ============================================================================
// Multiple Aggregate Functions in One Query (direct chaining)
// ============================================================================

TYPED_TEST(AggregateTest, DirectChain_TypeSafety) {
    this->insert_test_data();

    auto result = this->qs->template sum<^^Person::age>().count().execute();
    ASSERT_TRUE(result.has_value());
    static_assert(
            std::is_same_v<std::remove_reference_t<decltype(result.value())>, std::tuple<int64_t, int64_t>>,
            "Multiple aggregates should return tuple"
    );
}

TYPED_TEST(AggregateTest, SingleAggregates_WithWhereAndJoin) {
    this->insert_join_test_data();

    auto sum_result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 25)
                              .template join<&Message::sender>()
                              .template sum<^^Message::value>()
                              .execute();
    ASSERT_TRUE(sum_result.has_value()) << "SUM with WHERE+JOIN failed: " << sum_result.error().message();
    EXPECT_EQ(sum_result.value(), 180);

    (*this->msg_qs).reset();

    auto count_result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 25)
                                .template join<&Message::sender>()
                                .count()
                                .execute();
    ASSERT_TRUE(count_result.has_value()) << "COUNT with WHERE+JOIN failed: " << count_result.error().message();
    EXPECT_EQ(count_result.value(), 4);

    (*this->msg_qs).reset();

    auto min_result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 25)
                              .template join<&Message::sender>()
                              .template min<^^Message::value>()
                              .execute();
    ASSERT_TRUE(min_result.has_value()) << "MIN with WHERE+JOIN failed: " << min_result.error().message();
    EXPECT_DOUBLE_EQ(min_result.value(), 30.0);
}

TYPED_TEST(AggregateTest, SingleRow_AllAggregates) {
    auto insert_result =
            this->qs->insert(Person{.id = 0, .name = "Alice", .age = 25, .salary = 50000.0, .years_experience = 3})
                    .execute();
    ASSERT_TRUE(insert_result.has_value());

    auto sum = this->qs->template sum<^^Person::age>().execute();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 25);

    auto count = this->qs->count().execute();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 1);

    auto avg = this->qs->template avg<^^Person::age>().execute();
    ASSERT_TRUE(avg.has_value());
    EXPECT_DOUBLE_EQ(avg.value(), 25.0);

    auto min_val = this->qs->template min<^^Person::age>().execute();
    ASSERT_TRUE(min_val.has_value());
    EXPECT_DOUBLE_EQ(min_val.value(), 25.0);

    auto max_val = this->qs->template max<^^Person::age>().execute();
    ASSERT_TRUE(max_val.has_value());
    EXPECT_DOUBLE_EQ(max_val.value(), 25.0);
}

// ============================================================================
// Large Dataset Tests
// ============================================================================

TYPED_TEST(AggregateTest, LargeDataset_Sum) {
    std::vector<Person> people;
    people.reserve(1000);
    for (int i = 1; i <= 1000; ++i) {
        people.push_back(
                Person{.id = 0, .name = std::format("Person{}", i), .age = i, .salary = 50000.0, .years_experience = 5}
        );
    }
    auto batch_result = this->qs->insert(std::span<const Person>(people)).execute();
    ASSERT_TRUE(batch_result.has_value()) << "Batch insert failed: " << batch_result.error().message();

    auto result = this->qs->template sum<^^Person::age>().execute();
    ASSERT_TRUE(result.has_value()) << "SUM large dataset failed: " << result.error().message();
    EXPECT_EQ(result.value(), 500500);
}

TYPED_TEST(AggregateTest, LargeDataset_Count) {
    std::vector<Person> people;
    people.reserve(10000);
    for (int i = 1; i <= 10000; ++i) {
        people.push_back(
                Person{.id = 0, .name = std::format("Person{}", i), .age = 25, .salary = 50000.0, .years_experience = 5}
        );
    }
    auto insert_result = this->qs->insert(std::span<const Person>(people)).execute();
    ASSERT_TRUE(insert_result.has_value()) << "Batch insert failed: " << insert_result.error().message();

    auto result = this->qs->count().execute();
    ASSERT_TRUE(result.has_value()) << "COUNT large dataset failed: " << result.error().message();
    EXPECT_EQ(result.value(), 10000);
}

// ============================================================================
// Type Safety Tests
// ============================================================================

TYPED_TEST(AggregateTest, TypeSafety_IntegerResult) {
    this->insert_test_data();

    auto result = this->qs->template sum<^^Person::age>().execute();
    ASSERT_TRUE(result.has_value());
    static_assert(
            std::is_same_v<std::remove_reference_t<decltype(result.value())>, int64_t>, "SUM should return int64_t"
    );
}

TYPED_TEST(AggregateTest, TypeSafety_DoubleResult) {
    this->insert_test_data();

    auto result = this->qs->template avg<^^Person::age>().execute();
    ASSERT_TRUE(result.has_value());
    static_assert(
            std::is_same_v<std::remove_reference_t<decltype(result.value())>, double>, "AVG should return double"
    );
}

// ============================================================================
// Statement Caching Tests
// ============================================================================

TYPED_TEST(AggregateTest, StatementCaching_RepeatedQueries) {
    this->insert_test_data();

    for (int i = 0; i < 100; ++i) {
        auto result = this->qs->count().execute();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
        EXPECT_EQ(result.value(), 25);
    }
}

TYPED_TEST(AggregateTest, StatementCaching_DifferentAggregates) {
    this->insert_test_data();

    for (int i = 0; i < 10; ++i) {
        auto sum = this->qs->template sum<^^Person::age>().execute();
        ASSERT_TRUE(sum.has_value());
        EXPECT_EQ(sum.value(), 829);

        auto count = this->qs->count().execute();
        ASSERT_TRUE(count.has_value());
        EXPECT_EQ(count.value(), 25);

        auto avg = this->qs->template avg<^^Person::salary>().execute();
        ASSERT_TRUE(avg.has_value());
        EXPECT_NEAR(avg.value(), 68080.0, 0.01);
    }
}

// ============================================================================
// Integration with Other ORM Features
// ============================================================================

TYPED_TEST(AggregateTest, Integration_AfterInsert) {
    for (int i = 1; i <= 5; ++i) {
        auto insert_result = this->qs->insert(
                                             Person{.id               = 0,
                                                    .name             = std::format("Person{}", i),
                                                    .age              = i * 10,
                                                    .salary           = 50000.0,
                                                    .years_experience = 5}
        )
                                     .execute();
        ASSERT_TRUE(insert_result.has_value());

        auto count = this->qs->count().execute();
        ASSERT_TRUE(count.has_value());
        EXPECT_EQ(count.value(), i);
    }

    auto sum = this->qs->template sum<^^Person::age>().execute();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 10 + 20 + 30 + 40 + 50);
}

TYPED_TEST(AggregateTest, Integration_AfterUpdate) {
    this->insert_test_data();

    auto people = this->qs->select().execute();
    ASSERT_TRUE(people.has_value());

    for (auto& person : people.value()) {
        person.age         = 30;
        auto update_result = this->qs->update(person).execute();
        ASSERT_TRUE(update_result.has_value());
    }

    auto sum = this->qs->template sum<^^Person::age>().execute();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 750);
}

TYPED_TEST(AggregateTest, Integration_AfterDelete) {
    this->insert_test_data();

    auto people = this->qs->select().execute();
    ASSERT_TRUE(people.has_value());

    auto it            = people.value().begin();
    auto delete_result = this->qs->erase(*it).execute();
    ASSERT_TRUE(delete_result.has_value());
    ++it;
    delete_result = this->qs->erase(*it).execute();
    ASSERT_TRUE(delete_result.has_value());

    auto count = this->qs->count().execute();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 23);
}

// ============================================================================
// WHERE + Aggregate Tests
// ============================================================================

TYPED_TEST(AggregateTest, WhereWithMultiFieldSum) {
    this->insert_test_data();

    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() >= 35)
                          .template sum<^^Person::age, ^^Person::years_experience>()
                          .execute();
    ASSERT_TRUE(result.has_value()) << "WHERE + SUM(multi-field) failed: " << result.error().message();
    EXPECT_EQ(result.value(), 582);
}

TYPED_TEST(AggregateTest, WhereRepeatedQueries) {
    this->insert_test_data();

    for (int i = 0; i < 100; ++i) {
        auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 30).count().execute();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed: " << result.error().message();
        EXPECT_EQ(result.value(), 13);
    }
}

TYPED_TEST(AggregateTest, WhereDifferentConditions) {
    this->insert_test_data();

    auto result1 = this->qs->where(storm::orm::where::field<^^Person::age>() > 30).count().execute();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value(), 13);

    (*this->qs).reset();

    auto result2 = this->qs->where(storm::orm::where::field<^^Person::age>() < 30).count().execute();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value(), 9);

    (*this->qs).reset();

    auto result3 = this->qs->where(storm::orm::where::field<^^Person::age>() == 30).count().execute();
    ASSERT_TRUE(result3.has_value());
    EXPECT_EQ(result3.value(), 3);
}

// ============================================================================
// JOIN + Aggregate Tests
// ============================================================================

TYPED_TEST(AggregateTest, WhereJoinRepeatedQueries) {
    this->insert_join_test_data();

    for (int i = 0; i < 50; ++i) {
        auto result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 20)
                              .template join<&Message::sender>()
                              .count()
                              .execute();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed: " << result.error().message();
        EXPECT_EQ(result.value(), 4);
    }
}
