#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)

import storm;

import <expected>;
import <format>;
import <string>;
import <vector>;
import <span>;
import <optional>;
import <meta>;

using namespace storm;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"
#include "test_query_dispatch.h"

// Test fixture for aggregate functions
template <typename ConnType> class AggregateTest : public StormTestFixture<Person, ConnType, Message> {
  public:
    auto on_after_setup(const std::shared_ptr<ConnType>&) -> void override {
        qs     = std::make_unique<QuerySet<Person, ConnType>>();
        msg_qs = std::make_unique<QuerySet<Message, ConnType>>();
    }

    // Override TearDown to null smart pointers before clearing the connection.
    auto TearDown() -> void override {
        qs     = nullptr;
        msg_qs = nullptr;
        StormTestFixture<Person, ConnType, Message>::TearDown();
    }

    auto insert_test_data() -> void {
        ASSERT_TRUE((storm::test::batch_insert<Person, ConnType>(
                std::vector<Person>(storm::test::PEOPLE_25.begin(), storm::test::PEOPLE_25.end())
        )));
    }

    // Helper to insert PEOPLE_25 + MESSAGES_8 with FK safety (query back IDs for PG)
    void insert_join_test_data() {
        ASSERT_TRUE((storm::test::batch_insert<Person, ConnType>(
                std::vector<Person>(storm::test::PEOPLE_25.begin(), storm::test::PEOPLE_25.end())
        )));

        // Query back actual IDs for FK safety (PostgreSQL may assign different IDs)
        QuerySet<Person, ConnType> pqs;
        auto                       people_result = pqs.template order_by<^^Person::name>().select().execute();
        ASSERT_TRUE(people_result.has_value()) << people_result.error().message();
        ASSERT_EQ(people_result.value().size(), 25u);
        std::array<int, 3> sender_ids{}; // Alice, Bob, Charlie
        for (const auto& p : people_result.value()) {
            if (p.name == "Alice")
                sender_ids[0] = p.id;
            else if (p.name == "Bob")
                sender_ids[1] = p.id;
            else if (p.name == "Charlie")
                sender_ids[2] = p.id;
        }
        std::vector<Message> const messages = {
                {.content = "Hello", .value = 10, .sender = {.id = sender_ids[0]}},
                {.content = "World", .value = 20, .sender = {.id = sender_ids[0]}},
                {.content = "Hi", .value = 30, .sender = {.id = sender_ids[1]}},
                {.content = "There", .value = 40, .sender = {.id = sender_ids[1]}},
                {.content = "Foo", .value = 50, .sender = {.id = sender_ids[1]}},
                {.content = "Bar", .value = 60, .sender = {.id = sender_ids[2]}},
        };
        ASSERT_TRUE((storm::test::batch_insert<Message, ConnType>(messages)));
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    // GoogleTest fixtures conventionally use protected members for TEST_F access
    std::unique_ptr<QuerySet<Person, ConnType>>  qs;     // NOSONAR cpp:S3656
    std::unique_ptr<QuerySet<Message, ConnType>> msg_qs; // NOSONAR cpp:S3656
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
};

TYPED_TEST_SUITE(AggregateTest, DatabaseTypes);

// ============================================================================
// Single Aggregate Function Tests
// ============================================================================

TYPED_TEST(AggregateTest, SumMultipleFields) {
    this->insert_test_data();

    // SUM(age + years_experience) = 829 + 235 = 1064
    auto result = this->qs->template sum<^^Person::age, ^^Person::years_experience>().get();
    ASSERT_TRUE(result.has_value()) << "SUM multi-field failed: " << result.error().message();
    EXPECT_EQ(result.value(), 1064);
}

TYPED_TEST(AggregateTest, AvgMultipleFields) {
    this->insert_test_data();

    // AVG(age + years_experience) = 1064 / 25 = 42.56
    auto result = this->qs->template avg<^^Person::age, ^^Person::years_experience>().get();
    ASSERT_TRUE(result.has_value()) << "AVG multi-field failed: " << result.error().message();
    EXPECT_NEAR(result.value(), 42.56, 0.01);
}

TYPED_TEST(AggregateTest, MinMultipleFields) {
    this->insert_test_data();

    // MIN(age + years_experience) = MIN(30,40,50,33,...) = 27 (Paul: 22+5, Yara: 22+5)
    auto result = this->qs->template min<^^Person::age, ^^Person::years_experience>().get();
    ASSERT_TRUE(result.has_value()) << "MIN multi-field failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 27.0);
}

TYPED_TEST(AggregateTest, MaxMultipleFields) {
    this->insert_test_data();

    // MAX(age + years_experience) = MAX(..., 45+15, 40+15, ...) = 60 (Frank or Victor: 45+15)
    auto result = this->qs->template max<^^Person::age, ^^Person::years_experience>().get();
    ASSERT_TRUE(result.has_value()) << "MAX multi-field failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 60.0);
}

// ============================================================================
// Multiple Aggregate Functions in One Query (direct chaining)
// ============================================================================

// DirectChain_SumAndCount, DirectChain_SumCountAvg, DirectChain_AllFiveTypes,
// DirectChain_WithWhere, DirectChain_WithJoin, EmptyTable_MultipleAggregates:
// migrated to aggregate_cases.yaml (chain_* entries) → AggregateYamlTest.

TYPED_TEST(AggregateTest, DirectChain_TypeSafety) {
    this->insert_test_data();

    auto result = this->qs->template sum<^^Person::age>().count().get();
    ASSERT_TRUE(result.has_value());
    static_assert(
            std::is_same_v<std::remove_reference_t<decltype(result.value())>, std::tuple<int64_t, int64_t>>,
            "Multiple aggregates should return tuple"
    );
}

// EmptyTable_MultipleAggregates: migrated to aggregate_cases.yaml
// (chain_sum_count_avg_empty) → AggregateYamlTest.

// SingleAggregates_WithWhere: migrated to aggregate_cases.yaml
// (sum_age_where_gt_30, where_count_age_gt_30, avg_salary_where_age_gt_30)

TYPED_TEST(AggregateTest, SingleAggregates_WithWhereAndJoin) {
    this->insert_join_test_data();

    // Individual aggregates with WHERE and JOIN
    // Filter: value > 25 → 30, 40, 50, 60 (4 messages)
    auto sum_result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 25)
                              .template join<&Message::sender>()
                              .template sum<^^Message::value>()
                              .get();
    ASSERT_TRUE(sum_result.has_value()) << "SUM with WHERE+JOIN failed: " << sum_result.error().message();
    EXPECT_EQ(sum_result.value(), 180); // 30+40+50+60

    this->msg_qs->reset();

    auto count_result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 25)
                                .template join<&Message::sender>()
                                .count()
                                .get();
    ASSERT_TRUE(count_result.has_value()) << "COUNT with WHERE+JOIN failed: " << count_result.error().message();
    EXPECT_EQ(count_result.value(), 4); // 4 messages

    this->msg_qs->reset();

    auto min_result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 25)
                              .template join<&Message::sender>()
                              .template min<^^Message::value>()
                              .get();
    ASSERT_TRUE(min_result.has_value()) << "MIN with WHERE+JOIN failed: " << min_result.error().message();
    EXPECT_DOUBLE_EQ(min_result.value(), 30.0); // min is 30
}

TYPED_TEST(AggregateTest, SingleRow_AllAggregates) {
    // Insert single row
    auto insert_result =
            this->qs->insert(Person{.id = 0, .name = "Alice", .age = 25, .salary = 50000.0, .years_experience = 3})
                    .execute();
    ASSERT_TRUE(insert_result.has_value());

    // Test all aggregate types
    auto sum = this->qs->template sum<^^Person::age>().get();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 25);

    auto count = this->qs->count().get();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 1);

    auto avg = this->qs->template avg<^^Person::age>().get();
    ASSERT_TRUE(avg.has_value());
    EXPECT_DOUBLE_EQ(avg.value(), 25.0);

    auto min_val = this->qs->template min<^^Person::age>().get();
    ASSERT_TRUE(min_val.has_value());
    EXPECT_DOUBLE_EQ(min_val.value(), 25.0);

    auto max_val = this->qs->template max<^^Person::age>().get();
    ASSERT_TRUE(max_val.has_value());
    EXPECT_DOUBLE_EQ(max_val.value(), 25.0);
}

// ============================================================================
// Large Dataset Tests
// ============================================================================

TYPED_TEST(AggregateTest, LargeDataset_Sum) {
    // Batch insert 1000 people with ages 1-1000
    std::vector<Person> people;
    people.reserve(1000);
    for (int i = 1; i <= 1000; ++i) {
        people.push_back(
                Person{.id = 0, .name = std::format("Person{}", i), .age = i, .salary = 50000.0, .years_experience = 5}
        );
    }
    auto batch_result = this->qs->insert(std::span<const Person>(people)).execute();
    ASSERT_TRUE(batch_result.has_value()) << "Batch insert failed: " << batch_result.error().message();

    auto result = this->qs->template sum<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "SUM large dataset failed: " << result.error().message();
    EXPECT_EQ(result.value(), 500500);
}

TYPED_TEST(AggregateTest, LargeDataset_Count) {
    // Batch insert 10000 people
    std::vector<Person> people;
    people.reserve(10000);
    for (int i = 1; i <= 10000; ++i) {
        people.push_back(
                Person{.id = 0, .name = std::format("Person{}", i), .age = 25, .salary = 50000.0, .years_experience = 5}
        );
    }
    auto insert_result = this->qs->insert(std::span<const Person>(people)).execute();
    ASSERT_TRUE(insert_result.has_value()) << "Batch insert failed: " << insert_result.error().message();

    auto result = this->qs->count().get();
    ASSERT_TRUE(result.has_value()) << "COUNT large dataset failed: " << result.error().message();
    EXPECT_EQ(result.value(), 10000);
}

// ============================================================================
// Type Safety Tests
// ============================================================================

TYPED_TEST(AggregateTest, TypeSafety_IntegerResult) {
    this->insert_test_data();

    // Verify SUM returns int64_t
    auto result = this->qs->template sum<^^Person::age>().get();
    ASSERT_TRUE(result.has_value());
    static_assert(
            std::is_same_v<std::remove_reference_t<decltype(result.value())>, int64_t>, "SUM should return int64_t"
    );
}

TYPED_TEST(AggregateTest, TypeSafety_DoubleResult) {
    this->insert_test_data();

    // Verify AVG returns double
    auto result = this->qs->template avg<^^Person::age>().get();
    ASSERT_TRUE(result.has_value());
    static_assert(
            std::is_same_v<std::remove_reference_t<decltype(result.value())>, double>, "AVG should return double"
    );
}

// ============================================================================
// Floating Point Precision Tests
// ============================================================================

// FloatingPoint_Salary: migrated to aggregate_cases.yaml (sum_salary_all, avg_salary_all)

// ============================================================================
// Statement Caching Tests
// ============================================================================

TYPED_TEST(AggregateTest, StatementCaching_RepeatedQueries) {
    this->insert_test_data();

    // Run same query 100 times - should use cached statement
    for (int i = 0; i < 100; ++i) {
        auto result = this->qs->count().get();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
        EXPECT_EQ(result.value(), 25);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(AggregateTest, StatementCaching_DifferentAggregates) {
    this->insert_test_data();

    // Run different aggregates multiple times
    for (int i = 0; i < 10; ++i) {
        auto sum = this->qs->template sum<^^Person::age>().get();
        ASSERT_TRUE(sum.has_value());
        EXPECT_EQ(sum.value(), 829);

        auto count = this->qs->count().get();
        ASSERT_TRUE(count.has_value());
        EXPECT_EQ(count.value(), 25);

        auto avg = this->qs->template avg<^^Person::salary>().get();
        ASSERT_TRUE(avg.has_value());
        EXPECT_NEAR(avg.value(), 68080.0, 0.01);
    }
}

// ============================================================================
// Integration with Other ORM Features
// ============================================================================

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(AggregateTest, Integration_AfterInsert) {
    // Insert and immediately aggregate
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

        auto count = this->qs->count().get();
        ASSERT_TRUE(count.has_value());
        EXPECT_EQ(count.value(), i);
    }

    // Final aggregate
    auto sum = this->qs->template sum<^^Person::age>().get();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 10 + 20 + 30 + 40 + 50); // 150
}

TYPED_TEST(AggregateTest, Integration_AfterUpdate) {
    this->insert_test_data();

    // Update all ages to 30
    auto people = this->qs->select().execute();
    ASSERT_TRUE(people.has_value());

    for (auto& person : people.value()) {
        person.age         = 30;
        auto update_result = this->qs->update(person).execute();
        ASSERT_TRUE(update_result.has_value());
    }

    // SUM(age) should now be 30 * 25 = 750
    auto sum = this->qs->template sum<^^Person::age>().get();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 750);
}

TYPED_TEST(AggregateTest, Integration_AfterDelete) {
    this->insert_test_data();

    // Delete 2 people
    auto people = this->qs->select().execute();
    ASSERT_TRUE(people.has_value());

    auto it            = people.value().begin();
    auto delete_result = this->qs->remove(*it).execute();
    ASSERT_TRUE(delete_result.has_value());
    ++it;
    delete_result = this->qs->remove(*it).execute();
    ASSERT_TRUE(delete_result.has_value());

    // COUNT should now be 23
    auto count = this->qs->count().get();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 23);
}

// ============================================================================
// WHERE + Aggregate Tests
// ============================================================================

TYPED_TEST(AggregateTest, WhereWithMultiFieldSum) {
    this->insert_test_data();

    // SUM(age + years_experience) with WHERE: age >= 35 (11 people)
    // sum_age=442, sum_ye=140 → total=582
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() >= 35)
                          .template sum<^^Person::age, ^^Person::years_experience>()
                          .get();
    ASSERT_TRUE(result.has_value()) << "WHERE + SUM(multi-field) failed: " << result.error().message();
    EXPECT_EQ(result.value(), 582);
}

TYPED_TEST(AggregateTest, WhereRepeatedQueries) {
    this->insert_test_data();

    // Run same WHERE + aggregate query multiple times (tests caching)
    // No reset() needed — where() returns a new QuerySet each time
    for (int i = 0; i < 100; ++i) {
        auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 30).count().get();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed: " << result.error().message();
        EXPECT_EQ(result.value(), 13);
    }
}

TYPED_TEST(AggregateTest, WhereDifferentConditions) {
    this->insert_test_data();

    // Run queries with different WHERE conditions
    auto result1 = this->qs->where(storm::orm::where::field<^^Person::age>() > 30).count().get();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value(), 13);

    (*this->qs).reset(); // Clear WHERE clause

    auto result2 = this->qs->where(storm::orm::where::field<^^Person::age>() < 30).count().get();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value(), 9);

    (*this->qs).reset();

    auto result3 = this->qs->where(storm::orm::where::field<^^Person::age>() == 30).count().get();
    ASSERT_TRUE(result3.has_value());
    EXPECT_EQ(result3.value(), 3); // Bob, Ivy, Quinn
}

// ============================================================================
// JOIN + Aggregate Tests
// ============================================================================

TYPED_TEST(AggregateTest, WhereJoinRepeatedQueries) {
    this->insert_join_test_data();

    // Run same WHERE + JOIN + aggregate query multiple times (tests caching)
    // No reset() needed — where() returns a new QuerySet each time
    for (int i = 0; i < 50; ++i) {
        auto result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 20)
                              .template join<&Message::sender>()
                              .count()
                              .get();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed: " << result.error().message();
        EXPECT_EQ(result.value(), 4); // 30, 40, 50, 60
    }
}

// =============================================================================
// GROUP BY Tests
// =============================================================================
// These tests verify GROUP BY functionality with various aggregates.
// Returns: plf::hive<std::tuple<GroupKeyTypes..., AggResultTypes...>>

TYPED_TEST(AggregateTest, GroupByWithCount) {
    this->insert_test_data();

    // Group by years_experience and count
    // years_experience groups: 5(10), 10(8), 15(7)
    auto result = this->qs->template group_by<^^Person::years_experience>().count().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + COUNT failed: " << result.error().message();

    auto& results = result.value();
    EXPECT_EQ(results.size(), 3); // 3 unique years_experience values

    // Count total to verify we got all rows
    int64_t total_count = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& row : results) {
        total_count += std::get<1>(row);
    }
    EXPECT_EQ(total_count, 25); // Total 25 people
}

TYPED_TEST(AggregateTest, GroupByWithSum) {
    this->insert_test_data();

    // Group by years_experience and sum age
    auto result = this->qs->template group_by<^^Person::years_experience>().template sum<^^Person::age>().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + SUM failed: " << result.error().message();

    EXPECT_EQ(result.value().size(), 3); // 3 unique years_experience values

    // Sum all ages across all groups - should equal total sum = 829
    int64_t total_sum = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, age_sum] : result.value()) {
        total_sum += age_sum;
    }
    EXPECT_EQ(total_sum, 829);
}

TYPED_TEST(AggregateTest, GroupByWithAvg) {
    this->insert_test_data();

    // Group by years_experience and average salary
    auto result = this->qs->template group_by<^^Person::years_experience>().template avg<^^Person::salary>().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + AVG failed: " << result.error().message();

    EXPECT_EQ(result.value().size(), 3);

    // Group 5 (10 people): avg_salary = 503000/10 = 50300.0
    double avg_5 = 0.0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, salary_avg] : result.value()) {
        if (years == 5) {
            avg_5 = salary_avg;
            break;
        }
    }
    EXPECT_NEAR(avg_5, 50300.0, 0.01);
}

TYPED_TEST(AggregateTest, GroupByWithMin) {
    this->insert_test_data();

    // Group by years_experience and min salary
    auto result = this->qs->template group_by<^^Person::years_experience>().template min<^^Person::salary>().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + MIN failed: " << result.error().message();

    EXPECT_EQ(result.value().size(), 3);

    // Group 5 (10 people): min_salary = 32000 (Paul)
    double min_5 = 0.0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, salary_min] : result.value()) {
        if (years == 5) {
            min_5 = salary_min;
            break;
        }
    }
    EXPECT_NEAR(min_5, 32000.0, 0.01);
}

TYPED_TEST(AggregateTest, GroupByWithMax) {
    this->insert_test_data();

    // Group by years_experience and max age
    auto result = this->qs->template group_by<^^Person::years_experience>().template max<^^Person::age>().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + MAX failed: " << result.error().message();

    EXPECT_EQ(result.value().size(), 3);

    // Group 5 (10 people): max_age = 36 (Rachel)
    double max_5 = 0.0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, age_max] : result.value()) {
        if (years == 5) {
            max_5 = age_max;
            break;
        }
    }
    EXPECT_NEAR(max_5, 36.0, 0.01);
}

// GroupByWithWhere, GroupByWithJoin, GroupByWithWhereAndJoin:
// migrated to unified_cases.yaml (group_by_where_age_gt30, group_by_join_count_value,
// group_by_where_join_value_gt20)

TYPED_TEST(AggregateTest, GroupByWithJoinAndSum) {
    this->insert_join_test_data();

    auto result = this->msg_qs->template join<&Message::sender>()
                          .template group_by<^^Message::content>()
                          .template sum<^^Message::value>()
                          .select();
    ASSERT_TRUE(result.has_value()) << "JOIN + GROUP BY + SUM failed: " << result.error().message();

    EXPECT_EQ(result.value().size(), 6);

    int64_t total_sum = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [content, value_sum] : result.value()) {
        total_sum += value_sum;
    }
    EXPECT_EQ(total_sum, 210);
}

TYPED_TEST(AggregateTest, GroupByRepeatedQueries) {
    this->insert_test_data();

    // Run same GROUP BY query multiple times (tests caching)
    for (int i = 0; i < 50; ++i) {
        auto result = this->qs->template group_by<^^Person::years_experience>().count().select();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
        EXPECT_EQ(result.value().size(), 3);
    }
}

TYPED_TEST(AggregateTest, GroupByEmptyTable) {
    // Don't insert any data
    auto result = this->qs->template group_by<^^Person::years_experience>().count().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY on empty table failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 0);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(AggregateTest, GroupByFullChain_WhereJoinGroupByAggregate) {
    // This test verifies the full query chain: WHERE + JOIN + GROUP BY + aggregate
    // Setup: Create users and messages with multiple messages per user at varying values

    // Insert users with different ages
    // Alice (sender_id=1): 3 messages, values 10, 20, 30 (total=60, avg=20)
    // Bob (sender_id=2): 2 messages, values 50, 70 (total=120, avg=60)
    // Charlie (sender_id=3): 4 messages, values 5, 15, 25, 35 (total=80, avg=20)
    // Dave (sender_id=4): 1 message, value 100 (total=100, avg=100)
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = 25},   // id=1
            {.name = "Bob", .age = 35},     // id=2
            {.name = "Charlie", .age = 45}, // id=3
            {.name = "Dave", .age = 30},    // id=4
    })));

    ASSERT_TRUE((storm::test::batch_insert<Message, TypeParam>(std::vector<Message>{
            {.content = "A1", .value = 10, .sender = {.id = 1}},
            {.content = "A2", .value = 20, .sender = {.id = 1}},
            {.content = "A3", .value = 30, .sender = {.id = 1}},
            {.content = "B1", .value = 50, .sender = {.id = 2}},
            {.content = "B2", .value = 70, .sender = {.id = 2}},
            {.content = "C1", .value = 5, .sender = {.id = 3}},
            {.content = "C2", .value = 15, .sender = {.id = 3}},
            {.content = "C3", .value = 25, .sender = {.id = 3}},
            {.content = "C4", .value = 35, .sender = {.id = 3}},
            {.content = "D1", .value = 100, .sender = {.id = 4}},
    })));

    // Test 1: WHERE + JOIN + GROUP BY + COUNT
    // Filter messages with value >= 20, group by value (since we can't group by FK directly), count per value
    // We'll group by value to test the full chain with filterable data
    auto count_result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() >= 20)
                                .template join<&Message::sender>()
                                .template group_by<^^Message::value>()
                                .count()
                                .select();
    ASSERT_TRUE(count_result.has_value()) << "Full chain COUNT failed: " << count_result.error().message();
    // Values >= 20: 20, 25, 30, 35, 50, 70, 100 (7 unique values, each appears once)
    EXPECT_EQ(count_result.value().size(), 7) << "Expected 7 groups (7 unique values >= 20)";

    // Verify each value appears exactly once
    for (const auto& [value, count_val] : count_result.value()) {
        EXPECT_EQ(count_val, 1) << "Each value should appear exactly once, but value " << value << " has count "
                                << count_val;
    }

    // Reset QuerySet state
    this->msg_qs->reset();

    // Test 2: WHERE + JOIN + GROUP BY + SUM
    // Filter messages with value < 50, group by content (each unique)
    auto sum_result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() < 50)
                              .template join<&Message::sender>()
                              .template group_by<^^Message::content>()
                              .template sum<^^Message::value>()
                              .select();
    ASSERT_TRUE(sum_result.has_value()) << "Full chain SUM failed: " << sum_result.error().message();
    EXPECT_EQ(sum_result.value().size(), 7) << "Expected 7 groups (7 messages with value < 50)";

    // Reset QuerySet state
    this->msg_qs->reset();

    // Test 3: WHERE + JOIN + GROUP BY + AVG
    // Filter messages with value between 10 and 70, group by value (each value unique, avg = value)
    auto avg_result = this->msg_qs
                              ->where(storm::orm::where::field<^^Message::value>() >= 10 &&
                                      storm::orm::where::field<^^Message::value>() <= 70)
                              .template join<&Message::sender>()
                              .template group_by<^^Message::value>()
                              .template avg<^^Message::value>()
                              .select();
    ASSERT_TRUE(avg_result.has_value()) << "Full chain AVG failed: " << avg_result.error().message();

    // Values between 10-70: 10, 15, 20, 25, 30, 35, 50, 70 (8 unique values)
    EXPECT_EQ(avg_result.value().size(), 8) << "Expected 8 groups (8 unique values between 10-70)";

    // Verify averages - since each group has one row, avg = value
    for (const auto& [value, avg_val] : avg_result.value()) {
        EXPECT_NEAR(avg_val, static_cast<double>(value), 0.01) << "For single-row groups, avg should equal the value";
    }
}

// =============================================================================
// COUNT(DISTINCT) Tests
// =============================================================================

TYPED_TEST(AggregateTest, CountDistinctWithDuplicates) {
    // Insert data with duplicate ages
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.id = 0, .name = "Alice", .age = 30, .salary = 50000.0, .years_experience = 3},
            {.id = 0, .name = "Bob", .age = 30, .salary = 60000.0, .years_experience = 5}, // Same age as Alice
            {.id = 0, .name = "Charlie", .age = 35, .salary = 70000.0, .years_experience = 7},
            {.id = 0, .name = "Dave", .age = 30, .salary = 80000.0, .years_experience = 10}, // Same age as Alice and
                                                                                             // Bob
            {.id = 0, .name = "Eve", .age = 35, .salary = 90000.0, .years_experience = 15},  // Same age as Charlie
    })));

    auto result = this->qs->template count_distinct<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT) with duplicates failed";
    EXPECT_EQ(result.value(), 2); // Only 2 unique ages (30, 35)
}

TYPED_TEST(AggregateTest, CountDistinctWithJoin) {
    this->insert_join_test_data();

    // Count distinct message values
    auto result = this->msg_qs->template join<&Message::sender>().template count_distinct<^^Message::value>().get();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT) with JOIN failed";
    EXPECT_EQ(result.value(), 6); // 10, 20, 30, 40, 50, 60 - all unique
}

TYPED_TEST(AggregateTest, CountDistinctRepeatedQueries) {
    this->insert_test_data();

    // Run same COUNT(DISTINCT) query multiple times (tests caching)
    for (int i = 0; i < 50; ++i) {
        auto result = this->qs->template count_distinct<^^Person::years_experience>().get();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
        EXPECT_EQ(result.value(), 3);
    }
}

// =============================================================================
// NULL/Optional and Negative Number Tests
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

    // COUNT(*) includes all rows
    auto count_all = this->qs->count().get();
    ASSERT_TRUE(count_all.has_value());
    EXPECT_EQ(count_all.value(), 4);

    // COUNT(age) excludes NULL values
    auto count_age = this->qs->template count<^^Person::score>().get();
    ASSERT_TRUE(count_age.has_value());
    EXPECT_EQ(count_age.value(), 2); // Only Alice (25) and Charlie (35)
}

TYPED_TEST(OptionalAggregateTest, SumWithNullValues) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .salary = 50000.0, .score = 25},
            {.name = "Bob", .salary = 60000.0, .score = std::nullopt},
            {.name = "Charlie", .salary = 70000.0, .score = 35},
    })));

    auto sum = this->qs->template sum<^^Person::score>().get();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 60); // 25 + 35, NULL ignored
}

TYPED_TEST(OptionalAggregateTest, AvgWithNullValues) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .salary = 50000.0, .score = 20},
            {.name = "Bob", .salary = 60000.0, .score = std::nullopt},
            {.name = "Charlie", .salary = 70000.0, .score = 40},
    })));

    auto avg = this->qs->template avg<^^Person::score>().get();
    ASSERT_TRUE(avg.has_value());
    EXPECT_NEAR(avg.value(), 30.0, 0.01); // (20 + 40) / 2 = 30, NULL ignored
}

TYPED_TEST(OptionalAggregateTest, MinMaxWithNullValues) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .salary = 50000.0, .score = 25},
            {.name = "Bob", .salary = 60000.0, .score = std::nullopt},
            {.name = "Charlie", .salary = 70000.0, .score = 45},
            {.name = "Dave", .salary = 80000.0, .score = std::nullopt},
    })));

    auto min_val = this->qs->template min<^^Person::score>().get();
    ASSERT_TRUE(min_val.has_value());
    EXPECT_NEAR(min_val.value(), 25.0, 0.01);

    auto max_val = this->qs->template max<^^Person::score>().get();
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

    // COUNT(DISTINCT age) - NULL excluded, should count 2 (30, 40)
    auto result = this->qs->template count_distinct<^^Person::score>().get();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 2);
}

TYPED_TEST(OptionalAggregateTest, GroupByWithAllNullValuesInGroupColumn) {
    // Insert rows where all scores are NULL
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .salary = 50000.0, .score = std::nullopt},
            {.name = "Bob", .salary = 60000.0, .score = std::nullopt},
            {.name = "Charlie", .salary = 70000.0, .score = std::nullopt},
    })));

    // GROUP BY on age (all NULL) - should produce one group with NULL key
    // SQLite treats NULL as a distinct group key in GROUP BY
    auto result = this->qs->template group_by<^^Person::score>().count().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY with all NULL values failed";

    // Should have exactly one group (the NULL group) with count 3
    EXPECT_EQ(result.value().size(), 1);

    // Verify the single group has count 3
    auto& groups = result.value();
    auto  it     = groups.begin();
    ASSERT_NE(it, groups.end());
    auto [score_key, count_val] = *it;
    // score_key is std::optional<int> - should be nullopt for the NULL group
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

    // GROUP BY on age - should produce 3 groups: NULL (2 rows), 25 (2 rows), 30 (1 row)
    auto result = this->qs->template group_by<^^Person::score>().count().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY with mixed NULL values failed";

    EXPECT_EQ(result.value().size(), 3) << "Expected 3 groups (NULL, 25, 30)";

    // Verify counts for each group
    int64_t null_count   = 0; // NOLINT(misc-const-correctness) - modified in loop
    int64_t age_25_count = 0; // NOLINT(misc-const-correctness) - modified in loop
    int64_t age_30_count = 0; // NOLINT(misc-const-correctness) - modified in loop

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

// Negative number tests using the main Person model
TYPED_TEST(AggregateTest, NegativeNumbersInSum) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3},
            {.name = "Bob", .age = 5, .salary = 60000.0, .years_experience = 5},
            {.name = "Charlie", .age = -3, .salary = 70000.0, .years_experience = 7},
    })));

    auto sum = this->qs->template sum<^^Person::age>().get();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), -8); // -10 + 5 + (-3) = -8
}

TYPED_TEST(AggregateTest, NegativeNumbersInAvg) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = -12, .salary = 50000.0, .years_experience = 3},
            {.name = "Bob", .age = 6, .salary = 60000.0, .years_experience = 5},
            {.name = "Charlie", .age = 0, .salary = 70000.0, .years_experience = 7},
    })));

    auto avg = this->qs->template avg<^^Person::age>().get();
    ASSERT_TRUE(avg.has_value());
    EXPECT_NEAR(avg.value(), -2.0, 0.01); // (-12 + 6 + 0) / 3 = -2
}

TYPED_TEST(AggregateTest, NegativeNumbersInMinMax) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3},
            {.name = "Bob", .age = 5, .salary = 60000.0, .years_experience = 5},
            {.name = "Charlie", .age = -20, .salary = 70000.0, .years_experience = 7},
            {.name = "Dave", .age = 15, .salary = 80000.0, .years_experience = 10},
    })));

    auto min_val = this->qs->template min<^^Person::age>().get();
    ASSERT_TRUE(min_val.has_value());
    EXPECT_NEAR(min_val.value(), -20.0, 0.01);

    auto max_val = this->qs->template max<^^Person::age>().get();
    ASSERT_TRUE(max_val.has_value());
    EXPECT_NEAR(max_val.value(), 15.0, 0.01);
}

TYPED_TEST(AggregateTest, NegativeNumbersInCount) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3},
            {.name = "Bob", .age = -5, .salary = 60000.0, .years_experience = 5},
            {.name = "Charlie", .age = 0, .salary = 70000.0, .years_experience = 7},
    })));

    // Count should still count all rows regardless of negative values
    auto count = this->qs->count().get();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 3);

    // Count with field also counts negative values
    auto count_age = this->qs->template count<^^Person::age>().get();
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

    // Count where age < 0
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() < 0).count().get();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 2); // Alice (-10) and Bob (-5)

    // Sum of negative ages
    auto sum_neg = this->qs->where(storm::orm::where::field<^^Person::age>() < 0).template sum<^^Person::age>().get();
    ASSERT_TRUE(sum_neg.has_value());
    EXPECT_EQ(sum_neg.value(), -15); // -10 + (-5) = -15
}

// =============================================================================
// Combined Clause Tests
// =============================================================================

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(AggregateTest, FullChain_WhereJoinOrderByLimitOffset) {
    // This test verifies the complete SELECT query chain:
    // WHERE + JOIN + ORDER BY + LIMIT + OFFSET
    // Note: GROUP BY doesn't currently support ORDER BY/LIMIT/OFFSET in the builder pattern
    // Setup: Create users and messages with multiple messages per user

    // Insert users with different ages
    // Alice (sender_id=1): 3 messages, values 10, 15, 20
    // Bob (sender_id=2): 2 messages, values 25, 30
    // Charlie (sender_id=3): 4 messages, values 35, 40, 45, 50
    // Dave (sender_id=4): 2 messages, values 55, 60
    // Eve (sender_id=5): 3 messages, values 65, 70, 75
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = 25},   // id=1
            {.name = "Bob", .age = 35},     // id=2
            {.name = "Charlie", .age = 45}, // id=3
            {.name = "Dave", .age = 30},    // id=4
            {.name = "Eve", .age = 28},     // id=5
    })));

    ASSERT_TRUE((storm::test::batch_insert<Message, TypeParam>(std::vector<Message>{
            {.content = "A1", .value = 10, .sender = {.id = 1}},
            {.content = "A2", .value = 15, .sender = {.id = 1}},
            {.content = "A3", .value = 20, .sender = {.id = 1}},
            {.content = "B1", .value = 25, .sender = {.id = 2}},
            {.content = "B2", .value = 30, .sender = {.id = 2}},
            {.content = "C1", .value = 35, .sender = {.id = 3}},
            {.content = "C2", .value = 40, .sender = {.id = 3}},
            {.content = "C3", .value = 45, .sender = {.id = 3}},
            {.content = "C4", .value = 50, .sender = {.id = 3}},
            {.content = "D1", .value = 55, .sender = {.id = 4}},
            {.content = "D2", .value = 60, .sender = {.id = 4}},
            {.content = "E1", .value = 65, .sender = {.id = 5}},
            {.content = "E2", .value = 70, .sender = {.id = 5}},
            {.content = "E3", .value = 75, .sender = {.id = 5}},
    })));

    // Test: WHERE + JOIN + ORDER BY + LIMIT + OFFSET (SELECT, not GROUP BY)
    // Filter messages with value >= 20, order by value DESC, limit 5, offset 2
    // Values >= 20: 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75 (12 values)
    // Ordered DESC: 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20
    // After OFFSET 2: 65, 60, 55, 50, 45, 40, 35, 30, 25, 20 (skip first 2)
    // After LIMIT 5: 65, 60, 55, 50, 45 (take first 5)
    auto result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() >= 20)
                          .template join<&Message::sender>()
                          .template order_by<^^Message::value, false>()
                          .limit(5)
                          .offset(2)
                          .select()
                          .execute();

    ASSERT_TRUE(result.has_value()) << "Full chain query failed: " << result.error().message();

    auto& results = result.value();
    EXPECT_EQ(results.size(), 5) << "Expected 5 messages after LIMIT 5 OFFSET 2";

    // Verify the values are ordered correctly (DESC): 65, 60, 55, 50, 45
    std::vector<int> expected_values = {65, 60, 55, 50, 45};
    size_t           idx             = 0;
    for (const auto& msg : results) {
        ASSERT_LT(idx, expected_values.size());
        EXPECT_EQ(msg.value, expected_values[idx]) << "Value at index " << idx << " should be " << expected_values[idx];
        ++idx;
    }

    // Reset and test another variation: different ORDER BY direction and LIMIT/OFFSET
    this->msg_qs->reset();

    // Take first 3 messages with value >= 20, ordered ASC
    auto result2 = this->msg_qs->where(storm::orm::where::field<^^Message::value>() >= 20)
                           .template join<&Message::sender>()
                           .template order_by<^^Message::value, true>() // ASC this time
                           .limit(3)
                           .offset(0)
                           .select()
                           .execute();

    ASSERT_TRUE(result2.has_value()) << "Second full chain query failed: " << result2.error().message();

    auto& results2 = result2.value();
    EXPECT_EQ(results2.size(), 3) << "Expected 3 messages";

    // Verify ASC ordering: 20, 25, 30
    std::vector<int> expected_values2 = {20, 25, 30};
    idx                               = 0;
    for (const auto& msg : results2) {
        ASSERT_LT(idx, expected_values2.size());
        EXPECT_EQ(msg.value, expected_values2[idx])
                << "Value at index " << idx << " should be " << expected_values2[idx];
        ++idx;
    }

    // Test 3: Verify that JOIN actually works by checking we can access joined data
    this->msg_qs->reset();
    auto result3 = this->msg_qs->where(storm::orm::where::field<^^Message::value>() == 75)
                           .template join<&Message::sender>()
                           .select()
                           .execute();

    ASSERT_TRUE(result3.has_value()) << "JOIN verification query failed";
    EXPECT_EQ(result3.value().size(), 1) << "Expected 1 message with value 75";
    EXPECT_EQ(result3.value().begin()->sender.name, "Eve") << "Sender of value=75 should be Eve";
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(AggregateTest, GroupByWithAllAggregateTypes) {
    // Test all aggregate types (SUM, COUNT, AVG, MIN, MAX) with GROUP BY
    // Note: Current implementation supports one aggregate per query with GROUP BY
    // This test verifies each aggregate type works correctly with GROUP BY

    this->insert_test_data();

    // Test data summary by years_experience (PEOPLE_25):
    //   ye=5 (10 people): sum_age=268, avg=26.8, min=22, max=36
    //   ye=10 (8 people): sum_age=285, avg=35.625, min=29, max=48
    //   ye=15 (7 people): sum_age=276, avg≈39.43, min=35, max=45

    // Test 1: GROUP BY with COUNT
    auto count_result = this->qs->template group_by<^^Person::years_experience>().count().select();
    ASSERT_TRUE(count_result.has_value()) << "GROUP BY + COUNT failed: " << count_result.error().message();
    EXPECT_EQ(count_result.value().size(), 3) << "Expected 3 groups";
    for (const auto& [years_exp, count_val] : count_result.value()) {
        if (years_exp == 5) {
            EXPECT_EQ(count_val, 10);
        } else if (years_exp == 10) {
            EXPECT_EQ(count_val, 8);
        } else if (years_exp == 15) {
            EXPECT_EQ(count_val, 7);
        } else {
            FAIL() << "Unexpected years_experience: " << years_exp;
        }
    }

    // Test 2: GROUP BY with SUM
    this->qs->reset();
    auto sum_result = this->qs->template group_by<^^Person::years_experience>().template sum<^^Person::age>().select();
    ASSERT_TRUE(sum_result.has_value()) << "GROUP BY + SUM failed: " << sum_result.error().message();
    for (const auto& [years_exp, sum_age] : sum_result.value()) {
        if (years_exp == 5) {
            EXPECT_EQ(sum_age, 268);
        } else if (years_exp == 10) {
            EXPECT_EQ(sum_age, 285);
        } else if (years_exp == 15) {
            EXPECT_EQ(sum_age, 276);
        }
    }

    // Test 3: GROUP BY with AVG
    this->qs->reset();
    auto avg_result = this->qs->template group_by<^^Person::years_experience>().template avg<^^Person::age>().select();
    ASSERT_TRUE(avg_result.has_value()) << "GROUP BY + AVG failed: " << avg_result.error().message();
    for (const auto& [years_exp, avg_age] : avg_result.value()) {
        if (years_exp == 5) {
            EXPECT_NEAR(avg_age, 26.8, 0.01);
        } else if (years_exp == 10) {
            EXPECT_NEAR(avg_age, 35.625, 0.01);
        } else if (years_exp == 15) {
            EXPECT_NEAR(avg_age, 39.43, 0.01);
        }
    }

    // Test 4: GROUP BY with MIN
    this->qs->reset();
    auto min_result = this->qs->template group_by<^^Person::years_experience>().template min<^^Person::age>().select();
    ASSERT_TRUE(min_result.has_value()) << "GROUP BY + MIN failed: " << min_result.error().message();
    for (const auto& [years_exp, min_age] : min_result.value()) {
        if (years_exp == 5) {
            EXPECT_NEAR(min_age, 22.0, 0.01);
        } else if (years_exp == 10) {
            EXPECT_NEAR(min_age, 29.0, 0.01);
        } else if (years_exp == 15) {
            EXPECT_NEAR(min_age, 35.0, 0.01);
        }
    }

    // Test 5: GROUP BY with MAX
    this->qs->reset();
    auto max_result = this->qs->template group_by<^^Person::years_experience>().template max<^^Person::age>().select();
    ASSERT_TRUE(max_result.has_value()) << "GROUP BY + MAX failed: " << max_result.error().message();
    for (const auto& [years_exp, max_age] : max_result.value()) {
        if (years_exp == 5) {
            EXPECT_NEAR(max_age, 36.0, 0.01);
        } else if (years_exp == 10) {
            EXPECT_NEAR(max_age, 48.0, 0.01);
        } else if (years_exp == 15) {
            EXPECT_NEAR(max_age, 45.0, 0.01);
        }
    }

    // Test 6: GROUP BY with WHERE + SUM (combined clauses)
    this->qs->reset();
    auto filtered_sum = this->qs->where(storm::orm::where::field<^^Person::years_experience>() == 5)
                                .template group_by<^^Person::years_experience>()
                                .template sum<^^Person::age>()
                                .select();
    ASSERT_TRUE(filtered_sum.has_value()) << "WHERE + GROUP BY + SUM failed";
    EXPECT_EQ(filtered_sum.value().size(), 1) << "Expected 1 group after WHERE filter";
    const auto& [years_exp, sum_age] = *filtered_sum.value().begin();
    EXPECT_EQ(years_exp, 5);
    EXPECT_EQ(sum_age, 268);
}

// =============================================================================
// GROUP BY with ORDER BY, LIMIT, OFFSET Tests
// =============================================================================

TYPED_TEST(AggregateTest, GroupByWithOrderBy) {
    this->insert_test_data();

    // Group by years_experience and count, ordered ascending
    // years_experience: 5(10), 10(8), 15(7)
    auto result = this->qs->template order_by<^^Person::years_experience>()
                          .template group_by<^^Person::years_experience>()
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + ORDER BY failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 3);

    // Verify order: 5, 10, 15
    std::vector<int>     expected_order  = {5, 10, 15};
    std::vector<int64_t> expected_counts = {10, 8, 7};
    size_t               idx             = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, count_val] : result.value()) {
        EXPECT_EQ(years, expected_order[idx]) << "Unexpected order at index " << idx;
        EXPECT_EQ(count_val, expected_counts[idx]) << "Unexpected count at index " << idx;
        idx++;
    }
}

TYPED_TEST(AggregateTest, GroupByWithOrderByDesc) {
    this->insert_test_data();

    // Group by years_experience, ordered descending
    auto result = this->qs->template order_by<^^Person::years_experience, false>()
                          .template group_by<^^Person::years_experience>()
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + ORDER BY DESC failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 3);

    // Verify order: 15, 10, 5
    std::vector<int> expected_order = {15, 10, 5};
    size_t           idx            = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, count_val] : result.value()) {
        EXPECT_EQ(years, expected_order[idx]) << "Unexpected order at index " << idx;
        idx++;
    }
}

TYPED_TEST(AggregateTest, GroupByWithLimitOffset) {
    this->insert_test_data();

    // Group by years_experience, ordered, with LIMIT 2 OFFSET 1
    // Ordered: 5, 10, 15 -> skip 1 (5), take 2 (10, 15)
    auto result = this->qs->template order_by<^^Person::years_experience>()
                          .limit(2)
                          .offset(1)
                          .template group_by<^^Person::years_experience>()
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + LIMIT + OFFSET failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 2);

    // Verify we got years 10 and 15 (skipped 5)
    std::vector<int> expected = {10, 15};
    size_t           idx      = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, count_val] : result.value()) {
        EXPECT_EQ(years, expected[idx]) << "Expected years_experience=" << expected[idx] << " at index " << idx;
        idx++;
    }
}

TYPED_TEST(AggregateTest, GroupByMultipleFields) {
    this->insert_test_data();

    // Group by TWO fields: age AND years_experience using PEOPLE_25
    // Many (age, ye) combos — verify total counts match
    auto result = this->qs->template group_by<^^Person::age, ^^Person::years_experience>().count().select();
    ASSERT_TRUE(result.has_value()) << "Multi-field GROUP BY failed: " << result.error().message();

    // Verify total count across all groups = 25
    int64_t total = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [age, years_exp, count_val] : result.value()) {
        total += count_val;
    }
    EXPECT_EQ(total, 25);

    // Verify the (25, 5) group has count=3 (Alice, Grace, Karen)
    for (const auto& [age, years_exp, count_val] : result.value()) {
        if (age == 25 && years_exp == 5) {
            EXPECT_EQ(count_val, 3) << "Group (25, 5) should have 3 people";
        }
    }
}

TYPED_TEST(AggregateTest, GroupByMultipleFieldsWithOrderByLimit) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = 25, .salary = 50000.0, .years_experience = 5},
            {.name = "Bob", .age = 25, .salary = 60000.0, .years_experience = 5},
            {.name = "Charlie", .age = 25, .salary = 70000.0, .years_experience = 10},
            {.name = "Dave", .age = 30, .salary = 80000.0, .years_experience = 5},
            {.name = "Eve", .age = 30, .salary = 90000.0, .years_experience = 10},
    })));

    // Multi-field GROUP BY with ORDER BY and LIMIT
    auto result = this->qs->template order_by<^^Person::age>()
                          .limit(2)
                          .template group_by<^^Person::age, ^^Person::years_experience>()
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "Multi-field GROUP BY + ORDER BY + LIMIT failed";
    EXPECT_EQ(result.value().size(), 2) << "Should return only 2 groups due to LIMIT";
}

// =============================================================================
// HAVING Clause Tests
// =============================================================================

TYPED_TEST(AggregateTest, HavingOnGroupByBuilder) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = 25, .salary = 50000.0, .years_experience = 5},
            {.name = "Bob", .age = 30, .salary = 60000.0, .years_experience = 5},
            {.name = "Charlie", .age = 35, .salary = 70000.0, .years_experience = 10},
            {.name = "Dave", .age = 40, .salary = 80000.0, .years_experience = 10},
            {.name = "Eve", .age = 45, .salary = 90000.0, .years_experience = 10},
    })));

    // HAVING on GroupByBuilder (before aggregate): years_experience > 5
    // Groups: years_experience=5 (filtered out), years_experience=10 (kept)
    auto result = this->qs->template group_by<^^Person::years_experience>()
                          .having(storm::orm::where::field<^^Person::years_experience>() > 5)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING on GroupByBuilder failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 1) << "Expected 1 group (years_experience=10)";

    auto [years_exp, count_val] = *result.value().begin();
    EXPECT_EQ(years_exp, 10);
    EXPECT_EQ(count_val, 3);
}

TYPED_TEST(AggregateTest, HavingOnAggregateStatement) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = 25, .salary = 50000.0, .years_experience = 5},
            {.name = "Bob", .age = 30, .salary = 60000.0, .years_experience = 5},
            {.name = "Charlie", .age = 35, .salary = 70000.0, .years_experience = 10},
            {.name = "Dave", .age = 40, .salary = 80000.0, .years_experience = 10},
            {.name = "Eve", .age = 45, .salary = 90000.0, .years_experience = 10},
    })));

    // HAVING on AggregateStatement (after aggregate): years_experience > 5
    auto result = this->qs->template group_by<^^Person::years_experience>()
                          .count()
                          .having(storm::orm::where::field<^^Person::years_experience>() > 5)
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING on AggregateStatement failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 1) << "Expected 1 group (years_experience=10)";

    auto [years_exp, count_val] = *result.value().begin();
    EXPECT_EQ(years_exp, 10);
    EXPECT_EQ(count_val, 3);
}

TYPED_TEST(AggregateTest, HavingWithJoin) {
    this->insert_join_test_data();

    // JOIN + GROUP BY value HAVING value > 30
    // Values: 10, 20, 30, 40, 50, 60 -> After HAVING > 30: 40, 50, 60
    auto result = this->msg_qs->template join<&Message::sender>()
                          .template group_by<^^Message::value>()
                          .having(storm::orm::where::field<^^Message::value>() > 30)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING + JOIN failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 3) << "Expected 3 groups (values 40, 50, 60)";
}

TYPED_TEST(AggregateTest, HavingRepeatedQueries) {
    this->insert_test_data();

    // Run same HAVING query multiple times (tests caching)
    for (int i = 0; i < 50; ++i) {
        auto result = this->qs->template group_by<^^Person::age>()
                              .having(storm::orm::where::field<^^Person::age>() > 30)
                              .count()
                              .select();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
        EXPECT_EQ(result.value().size(), 8); // 8 unique ages > 30
    }
}

TYPED_TEST(AggregateTest, HavingWithSum) {
    this->insert_test_data();

    // GROUP BY years_experience, SUM(age), HAVING years_experience == 5
    auto result = this->qs->template group_by<^^Person::years_experience>()
                          .having(storm::orm::where::field<^^Person::years_experience>() == 5)
                          .template sum<^^Person::age>()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING + SUM failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 1);

    auto [years_exp, sum_age] = *result.value().begin();
    EXPECT_EQ(years_exp, 5);
    EXPECT_EQ(sum_age, 268); // sum of ages for ye=5 group
}

TYPED_TEST(AggregateTest, HavingWithWhereAndJoin) {
    ASSERT_TRUE((storm::test::batch_insert<Person, TypeParam>(std::vector<Person>{
            {.name = "Alice", .age = 25},
            {.name = "Bob", .age = 35},
    })));
    ASSERT_TRUE((storm::test::batch_insert<Message, TypeParam>(std::vector<Message>{
            {.content = "A1", .value = 10, .sender = {.id = 1}},
            {.content = "A2", .value = 20, .sender = {.id = 1}},
            {.content = "A3", .value = 30, .sender = {.id = 1}},
            {.content = "B1", .value = 50, .sender = {.id = 2}},
            {.content = "B2", .value = 70, .sender = {.id = 2}},
    })));

    // WHERE value >= 20 AND JOIN + GROUP BY value HAVING value > 25
    // After WHERE: A2(20), A3(30), B1(50), B2(70)
    // After HAVING value > 25: A3(30), B1(50), B2(70) = 3 groups
    auto result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() >= 20)
                          .template join<&Message::sender>()
                          .template group_by<^^Message::value>()
                          .having(storm::orm::where::field<^^Message::value>() > 25)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING + WHERE + JOIN failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 3) << "Expected 3 groups after WHERE + HAVING + JOIN";
}

// ----- HAVING with all ExpressionVariant types -----

TYPED_TEST(AggregateTest, HavingWithNotEqual) {
    // GROUP BY age HAVING age != 30 → should exclude age=30 groups
    auto result = this->qs->template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>() != 30)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING != failed: " << result.error().message();
    // All groups except age=30
    for (const auto& [age, count] : result.value()) {
        EXPECT_NE(age, 30) << "HAVING != should exclude age=30";
    }
}

TYPED_TEST(AggregateTest, HavingWithLessThan) {
    // GROUP BY age HAVING age < 30 → only groups with age < 30
    auto result = this->qs->template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>() < 30)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING < failed: " << result.error().message();
    for (const auto& [age, count] : result.value()) {
        EXPECT_LT(age, 30) << "HAVING < should only include age < 30";
    }
}

TYPED_TEST(AggregateTest, HavingWithLessEqual) {
    // GROUP BY age HAVING age <= 30
    auto result = this->qs->template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>() <= 30)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING <= failed: " << result.error().message();
    for (const auto& [age, count] : result.value()) {
        EXPECT_LE(age, 30) << "HAVING <= should only include age <= 30";
    }
}

TYPED_TEST(AggregateTest, HavingWithGreaterEqual) {
    // GROUP BY age HAVING age >= 30
    auto result = this->qs->template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>() >= 30)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING >= failed: " << result.error().message();
    for (const auto& [age, count] : result.value()) {
        EXPECT_GE(age, 30) << "HAVING >= should only include age >= 30";
    }
}

TYPED_TEST(AggregateTest, HavingWithIn) {
    // GROUP BY age HAVING age IN (25, 30, 35)
    auto result = this->qs->template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>().in(25, 30, 35))
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING IN failed: " << result.error().message();
    for (const auto& [age, count] : result.value()) {
        EXPECT_TRUE(age == 25 || age == 30 || age == 35)
                << "HAVING IN should only include age 25, 30, or 35, got: " << age;
    }
}

TYPED_TEST(AggregateTest, HavingWithBetween) {
    // GROUP BY age HAVING age BETWEEN 25 AND 35
    auto result = this->qs->template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>().between(25, 35))
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING BETWEEN failed: " << result.error().message();
    for (const auto& [age, count] : result.value()) {
        EXPECT_GE(age, 25) << "HAVING BETWEEN lower bound failed";
        EXPECT_LE(age, 35) << "HAVING BETWEEN upper bound failed";
    }
}

TYPED_TEST(AggregateTest, HavingWithLike) {
    // GROUP BY name HAVING name LIKE 'A%'
    auto result = this->qs->template group_by<^^Person::name>()
                          .having(storm::orm::where::field<^^Person::name>().like("A%"))
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING LIKE failed: " << result.error().message();
    for (const auto& [name, count] : result.value()) {
        EXPECT_TRUE(name.find("A") == 0) << "HAVING LIKE 'A%' should only match names starting with A, got: " << name;
    }
}

TYPED_TEST(AggregateTest, HavingWithLogicalAnd) {
    // GROUP BY age HAVING age > 20 AND age < 40
    auto result = this->qs->template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>() > 20 &&
                                  storm::orm::where::field<^^Person::age>() < 40)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING AND failed: " << result.error().message();
    for (const auto& [age, count] : result.value()) {
        EXPECT_GT(age, 20) << "HAVING AND: age should be > 20";
        EXPECT_LT(age, 40) << "HAVING AND: age should be < 40";
    }
}

TYPED_TEST(AggregateTest, HavingWithLogicalOr) {
    // GROUP BY age HAVING age < 25 OR age > 35
    auto result = this->qs->template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>() < 25 ||
                                  storm::orm::where::field<^^Person::age>() > 35)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING OR failed: " << result.error().message();
    for (const auto& [age, count] : result.value()) {
        EXPECT_TRUE(age < 25 || age > 35) << "HAVING OR: age should be < 25 or > 35, got: " << age;
    }
}

TYPED_TEST(AggregateTest, HavingWithComplexLogical) {
    // GROUP BY age HAVING (age >= 25 AND age <= 35) OR age == 50
    auto result = this->qs->template group_by<^^Person::age>()
                          .having((storm::orm::where::field<^^Person::age>() >= 25 &&
                                   storm::orm::where::field<^^Person::age>() <= 35) ||
                                  storm::orm::where::field<^^Person::age>() == 50)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING complex logical failed: " << result.error().message();
    for (const auto& [age, count] : result.value()) {
        EXPECT_TRUE((age >= 25 && age <= 35) || age == 50) << "HAVING complex: age should be 25-35 or 50, got: " << age;
    }
}

TYPED_TEST(AggregateTest, HavingWithWhereAndIn) {
    // WHERE salary > 50000 + GROUP BY age HAVING age IN (25, 30, 35)
    auto result = this->qs->where(storm::orm::where::field<^^Person::salary>() > 50000.0)
                          .template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>().in(25, 30, 35))
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "WHERE + HAVING IN failed: " << result.error().message();
    for (const auto& [age, count] : result.value()) {
        EXPECT_TRUE(age == 25 || age == 30 || age == 35) << "WHERE + HAVING IN: unexpected age " << age;
    }
}

TYPED_TEST(AggregateTest, HavingWithWhereAndBetween) {
    // WHERE salary > 30000 + GROUP BY years_experience HAVING years_experience BETWEEN 3 AND 8
    auto result = this->qs->where(storm::orm::where::field<^^Person::salary>() > 30000.0)
                          .template group_by<^^Person::years_experience>()
                          .having(storm::orm::where::field<^^Person::years_experience>().between(3, 8))
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "WHERE + HAVING BETWEEN failed: " << result.error().message();
    for (const auto& [years, count] : result.value()) {
        EXPECT_GE(years, 3) << "WHERE + HAVING BETWEEN lower bound failed";
        EXPECT_LE(years, 8) << "WHERE + HAVING BETWEEN upper bound failed";
    }
}

TYPED_TEST(AggregateTest, HavingWithWhereAndLogicalAnd) {
    // WHERE salary > 30000 + GROUP BY age HAVING age > 20 AND age < 40
    auto result = this->qs->where(storm::orm::where::field<^^Person::salary>() > 30000.0)
                          .template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>() > 20 &&
                                  storm::orm::where::field<^^Person::age>() < 40)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "WHERE + HAVING AND failed: " << result.error().message();
    for (const auto& [age, count] : result.value()) {
        EXPECT_GT(age, 20) << "WHERE + HAVING AND: age should be > 20";
        EXPECT_LT(age, 40) << "WHERE + HAVING AND: age should be < 40";
    }
}

TYPED_TEST(AggregateTest, HavingInOnAggregateStatement) {
    // having() chained after aggregate: group_by().count().having(IN)
    auto result = this->qs->template group_by<^^Person::age>()
                          .count()
                          .having(storm::orm::where::field<^^Person::age>().in(25, 30))
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING IN on AggregateStatement failed: " << result.error().message();
    for (const auto& [age, count] : result.value()) {
        EXPECT_TRUE(age == 25 || age == 30) << "HAVING IN on AggregateStatement: unexpected age " << age;
    }
}

TYPED_TEST(AggregateTest, HavingBetweenOnAggregateStatement) {
    // having() chained after aggregate: group_by().sum().having(BETWEEN)
    auto result = this->qs->template group_by<^^Person::age>()
                          .template sum<^^Person::salary>()
                          .having(storm::orm::where::field<^^Person::age>().between(25, 35))
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING BETWEEN on AggregateStatement failed: " << result.error().message();
    for (const auto& [age, sum_salary] : result.value()) {
        EXPECT_GE(age, 25) << "HAVING BETWEEN on AggregateStatement: lower bound";
        EXPECT_LE(age, 35) << "HAVING BETWEEN on AggregateStatement: upper bound";
    }
}

TYPED_TEST(AggregateTest, HavingLogicalOnAggregateStatement) {
    // having() chained after aggregate: group_by().avg().having(AND)
    auto result = this->qs->template group_by<^^Person::age>()
                          .template avg<^^Person::salary>()
                          .having(storm::orm::where::field<^^Person::age>() >= 25 &&
                                  storm::orm::where::field<^^Person::age>() <= 40)
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING AND on AggregateStatement failed: " << result.error().message();
    for (const auto& [age, avg_salary] : result.value()) {
        EXPECT_GE(age, 25) << "HAVING AND on AggregateStatement: lower bound";
        EXPECT_LE(age, 40) << "HAVING AND on AggregateStatement: upper bound";
    }
}

// =============================================================================
// GROUP BY + ORDER BY Tests (from test_coverage_gaps.cpp)
// =============================================================================

template <typename ConnType> class GroupByOrderByTest : public PersonSeedFixture<ConnType> {};

TYPED_TEST_SUITE(GroupByOrderByTest, DatabaseTypes);

TYPED_TEST(GroupByOrderByTest, GroupByWithOrderByAscending) {
    auto result = this->qs->template order_by<^^Person::department>()
                          .template group_by<^^Person::department>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY + ORDER BY should succeed";
    ASSERT_FALSE(result.value().empty());

    auto        it   = result.value().begin();
    std::string prev = "";
    while (it != result.value().end()) {
        const auto& [dept, count] = *it;
        EXPECT_GE(dept, prev) << "Results should be ordered ascending by department";
        prev = dept;
        ++it;
    }
}

TYPED_TEST(GroupByOrderByTest, GroupByWithOrderByDescending) {
    auto result = this->qs->template order_by<^^Person::department, false>()
                          .template group_by<^^Person::department>()
                          .count()
                          .select();

    ASSERT_TRUE(result.has_value()) << "GROUP BY + ORDER BY DESC should succeed";
    ASSERT_FALSE(result.value().empty());

    auto        it   = result.value().begin();
    std::string prev = "ZZZZZZ";
    while (it != result.value().end()) {
        const auto& [dept, count] = *it;
        EXPECT_LE(dept, prev) << "Results should be ordered descending by department";
        prev = dept;
        ++it;
    }
}

TYPED_TEST(GroupByOrderByTest, GroupByRepeatedExecution) {
    for (int i = 0; i < 5; ++i) {
        auto result = this->qs->template order_by<^^Person::department>()
                              .template group_by<^^Person::department>()
                              .count()
                              .select();

        ASSERT_TRUE(result.has_value()) << "Repeated GROUP BY should succeed on iteration " << i;
        EXPECT_EQ(result.value().size(), 5) << "Should have 5 departments";
    }
}

TYPED_TEST(GroupByOrderByTest, GroupByWithDifferentAggregatesSequentially) {
    auto count_result = this->qs->template group_by<^^Person::department>().count().select();
    ASSERT_TRUE(count_result.has_value());

    auto sum_result = this->qs->template group_by<^^Person::department>().template sum<^^Person::salary>().select();
    ASSERT_TRUE(sum_result.has_value());

    auto avg_result = this->qs->template group_by<^^Person::department>().template avg<^^Person::age>().select();
    ASSERT_TRUE(avg_result.has_value());

    EXPECT_EQ(count_result.value().size(), sum_result.value().size());
    EXPECT_EQ(count_result.value().size(), avg_result.value().size());
}

// ============================================================================
// HAVING + ORDER BY/LIMIT combined tests (#177)
// Covers aggregate.cppm:540-544 — HAVING with modifiers path
// ============================================================================

TYPED_TEST(AggregateTest, HavingWithOrderByAndLimit) {
    // ORDER BY + LIMIT set on QuerySet, then GROUP BY + HAVING + COUNT
    // Exercises execute_simple() path: HasGroupBy=true, having_expr_!=null, has_modifiers=true
    auto result = this->qs->template order_by<^^Person::age>()
                          .limit(3)
                          .template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>() > 25)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING + ORDER BY + LIMIT failed: " << result.error().message();

    const auto& groups = result.value();
    EXPECT_LE(groups.size(), 3) << "LIMIT 3 should return at most 3 groups";

    // Verify ordering — ages should be ascending
    int prev_age = 0;
    for (const auto& [age, count] : groups) {
        EXPECT_GT(age, 25) << "HAVING age > 25 should filter out ages <= 25";
        EXPECT_GE(age, prev_age) << "ORDER BY age ASC violated";
        prev_age = age;
    }
}

TYPED_TEST(AggregateTest, HavingWithOrderByOnly) {
    // ORDER BY DESC set before GROUP BY + HAVING
    auto result = this->qs->template order_by<^^Person::age, false>()
                          .template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>() > 30)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING + ORDER BY failed: " << result.error().message();

    const auto& groups   = result.value();
    int         prev_age = 100;
    for (const auto& [age, count] : groups) {
        EXPECT_GT(age, 30) << "HAVING age > 30 should filter";
        EXPECT_LE(age, prev_age) << "ORDER BY DESC violated";
        prev_age = age;
    }
}

TYPED_TEST(AggregateTest, HavingWithLimitOnly) {
    // LIMIT set before GROUP BY + HAVING
    auto result = this->qs->limit(2)
                          .template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>() > 25)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING + LIMIT failed: " << result.error().message();

    EXPECT_LE(result.value().size(), 2) << "LIMIT 2 should return at most 2 groups";
}

TYPED_TEST(AggregateTest, HavingWithOffsetOnly) {
    // OFFSET set before GROUP BY + HAVING
    auto result = this->qs->offset(1)
                          .template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>() > 25)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING + OFFSET failed: " << result.error().message();
}

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
