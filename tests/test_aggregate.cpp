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

// Test fixture for aggregate functions
template <typename ConnType> class AggregateTest : public StormTestFixture<Person, ConnType> {
  protected:
    auto SetUp() -> void override {
        if (!this->setup_connection()) {
            GTEST_SKIP() << "Backend unavailable";
            return;
        }
        const auto& conn = QuerySet<Person, ConnType>::get_default_connection();

        auto create_result = storm::test::ensure_table<ConnType>(conn, person_create_sql);
        ASSERT_TRUE(create_result.has_value()) << "Failed to create Person table: " << create_result.error().message();

        auto create_msg = storm::test::ensure_table<ConnType>(conn, message_create_sql);
        ASSERT_TRUE(create_msg.has_value()) << "Failed to create Message table: " << create_msg.error().message();

        storm::test::begin_test_txn<ConnType>(conn, {"Person"});

        qs     = std::make_unique<QuerySet<Person, ConnType>>();
        msg_qs = std::make_unique<QuerySet<Message, ConnType>>();
    }

    // Override TearDown to null smart pointers before clearing the connection.
    auto TearDown() -> void override {
        qs     = nullptr;
        msg_qs = nullptr;
        StormTestFixture<Person, ConnType>::TearDown();
    }

    // Helper to insert test data
    auto insert_test_data() -> void {
        std::vector<Person> const people =
                {{.id = 0, .name = "Alice", .age = 25, .salary = 50000.0, .years_experience = 3},
                 {.id = 0, .name = "Bob", .age = 30, .salary = 60000.0, .years_experience = 5},
                 {.id = 0, .name = "Charlie", .age = 35, .salary = 70000.0, .years_experience = 7},
                 {.id = 0, .name = "Dave", .age = 40, .salary = 80000.0, .years_experience = 10},
                 {.id = 0, .name = "Eve", .age = 45, .salary = 90000.0, .years_experience = 15}};
        for (const auto& person : people) {
            auto result = qs->insert(person).execute();
            ASSERT_TRUE(result.has_value()) << "Failed to insert: " << result.error().message();
        }
    }

    // Helper to insert JOIN test data
    void insert_join_test_data() {
        const auto& conn = QuerySet<Person, ConnType>::get_default_connection();

        // Insert users
        std::ignore = conn->execute(
                "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Alice', 30, 0, 0, 0)"
        );
        std::ignore = conn->execute(
                "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Bob', 25, 0, 0, 0)"
        );
        std::ignore = conn->execute(
                "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Charlie', 35, 0, 0, 0)"
        );

        // Insert messages with different values
        // Alice: 2 messages, values 10 and 20
        // Bob: 3 messages, values 30, 40, 50
        // Charlie: 1 message, value 60
        std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('Hello', 10, 1)");
        std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('World', 20, 1)");
        std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('Hi', 30, 2)");
        std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('There', 40, 2)");
        std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('Foo', 50, 2)");
        std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('Bar', 60, 3)");
    }

    // Helper: insert 6 people in two groups (years_experience = 5 and 10, 3 each).
    // Group 5: Alice(25), Bob(30), Frank(28) — sum=83, avg≈27.67, min=25, max=30
    // Group 10: Charlie(35), Dave(40), Eve(45) — sum=120, avg=40, min=35, max=45
    void insert_two_group_test_data() {
        std::vector<Person> const people = {
                {.id = 0, .name = "Alice", .age = 25, .salary = 50000.0, .years_experience = 5},
                {.id = 0, .name = "Bob", .age = 30, .salary = 60000.0, .years_experience = 5},
                {.id = 0, .name = "Charlie", .age = 35, .salary = 70000.0, .years_experience = 10},
                {.id = 0, .name = "Dave", .age = 40, .salary = 80000.0, .years_experience = 10},
                {.id = 0, .name = "Eve", .age = 45, .salary = 90000.0, .years_experience = 10},
                {.id = 0, .name = "Frank", .age = 28, .salary = 55000.0, .years_experience = 5},
        };
        for (const auto& person : people) {
            std::ignore = qs->insert(person).execute();
        }
    }

    // Helper: insert 5 people with overlapping age and years_experience.
    // (age=25, ye=5) x2, (age=25, ye=10) x1, (age=30, ye=5) x1, (age=30, ye=10) x1
    void insert_multi_group_test_data() {
        std::vector<Person> const people = {
                {.id = 0, .name = "Alice", .age = 25, .salary = 50000.0, .years_experience = 5},
                {.id = 0, .name = "Bob", .age = 25, .salary = 60000.0, .years_experience = 5},
                {.id = 0, .name = "Charlie", .age = 25, .salary = 70000.0, .years_experience = 10},
                {.id = 0, .name = "Dave", .age = 30, .salary = 80000.0, .years_experience = 5},
                {.id = 0, .name = "Eve", .age = 30, .salary = 90000.0, .years_experience = 10},
        };
        for (const auto& person : people) {
            std::ignore = qs->insert(person).execute();
        }
    }

    // Helper: insert 3 people with years_experience 5/5/10 for HAVING sum tests.
    // Group 5: Alice(25) + Bob(30) = sum 55
    // Group 10: Charlie(35) = sum 35
    void insert_having_sum_test_data() {
        std::vector<Person> const people = {
                {.id = 0, .name = "Alice", .age = 25, .salary = 50000.0, .years_experience = 5},
                {.id = 0, .name = "Bob", .age = 30, .salary = 60000.0, .years_experience = 5},
                {.id = 0, .name = "Charlie", .age = 35, .salary = 70000.0, .years_experience = 10},
        };
        for (const auto& person : people) {
            std::ignore = qs->insert(person).execute();
        }
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

TYPED_TEST(AggregateTest, SumSingleField) {
    this->insert_test_data();

    // SUM(age) = 25 + 30 + 35 + 40 + 45 = 175
    auto result = this->qs->template sum<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "SUM failed: " << result.error().message();
    EXPECT_EQ(result.value(), 175);
}

TYPED_TEST(AggregateTest, SumMultipleFields) {
    this->insert_test_data();

    // SUM(age + years_experience) = (25+3) + (30+5) + (35+7) + (40+10) + (45+15) = 28+35+42+50+60 = 215
    auto result = this->qs->template sum<^^Person::age, ^^Person::years_experience>().get();
    ASSERT_TRUE(result.has_value()) << "SUM multi-field failed: " << result.error().message();
    EXPECT_EQ(result.value(), 215);
}

TYPED_TEST(AggregateTest, CountAll) {
    this->insert_test_data();

    // COUNT(*) = 5
    auto result = this->qs->count().get();
    ASSERT_TRUE(result.has_value()) << "COUNT failed: " << result.error().message();
    EXPECT_EQ(result.value(), 5);
}

TYPED_TEST(AggregateTest, CountField) {
    this->insert_test_data();

    // COUNT(id) = 5
    auto result = this->qs->template count<^^Person::id>().get();
    ASSERT_TRUE(result.has_value()) << "COUNT field failed: " << result.error().message();
    EXPECT_EQ(result.value(), 5);
}

TYPED_TEST(AggregateTest, AvgSingleField) {
    this->insert_test_data();

    // AVG(age) = (25 + 30 + 35 + 40 + 45) / 5 = 175 / 5 = 35.0
    auto result = this->qs->template avg<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "AVG failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 35.0);
}

TYPED_TEST(AggregateTest, AvgMultipleFields) {
    this->insert_test_data();

    // AVG(age + years_experience) = 215 / 5 = 43.0
    auto result = this->qs->template avg<^^Person::age, ^^Person::years_experience>().get();
    ASSERT_TRUE(result.has_value()) << "AVG multi-field failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 43.0);
}

TYPED_TEST(AggregateTest, MinSingleField) {
    this->insert_test_data();

    // MIN(age) = 25
    auto result = this->qs->template min<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "MIN failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 25.0);
}

TYPED_TEST(AggregateTest, MinMultipleFields) {
    this->insert_test_data();

    // MIN(age + years_experience) = MIN(28, 35, 42, 50, 60) = 28
    auto result = this->qs->template min<^^Person::age, ^^Person::years_experience>().get();
    ASSERT_TRUE(result.has_value()) << "MIN multi-field failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 28.0);
}

TYPED_TEST(AggregateTest, MaxSingleField) {
    this->insert_test_data();

    // MAX(salary) = 90000.0
    auto result = this->qs->template max<^^Person::salary>().get();
    ASSERT_TRUE(result.has_value()) << "MAX failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 90000.0);
}

TYPED_TEST(AggregateTest, MaxMultipleFields) {
    this->insert_test_data();

    // MAX(age + years_experience) = MAX(28, 35, 42, 50, 60) = 60
    auto result = this->qs->template max<^^Person::age, ^^Person::years_experience>().get();
    ASSERT_TRUE(result.has_value()) << "MAX multi-field failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 60.0);
}

// ============================================================================
// Multiple Aggregate Functions in One Query (direct chaining)
// ============================================================================

TYPED_TEST(AggregateTest, DirectChain_SumAndCount) {
    this->insert_test_data();

    // SELECT SUM(age), COUNT(*) FROM Person
    auto result = this->qs->template sum<^^Person::age>().count().get();
    ASSERT_TRUE(result.has_value()) << "Direct chain sum+count failed: " << result.error().message();

    auto [sum_age, count_all] = result.value();
    EXPECT_EQ(sum_age, 175);
    EXPECT_EQ(count_all, 5);
}

TYPED_TEST(AggregateTest, DirectChain_SumCountAvg) {
    this->insert_test_data();

    // SELECT SUM(age), COUNT(*), AVG(salary) FROM Person
    auto result = this->qs->template sum<^^Person::age>().count().template avg<^^Person::salary>().get();
    ASSERT_TRUE(result.has_value()) << "Direct chain sum+count+avg failed: " << result.error().message();

    auto [sum_age, count_all, avg_salary] = result.value();
    EXPECT_EQ(sum_age, 175);
    EXPECT_EQ(count_all, 5);
    EXPECT_DOUBLE_EQ(avg_salary, 70000.0);
}

TYPED_TEST(AggregateTest, DirectChain_AllFiveTypes) {
    this->insert_test_data();

    // SELECT SUM(age), COUNT(*), AVG(salary), MIN(years_experience), MAX(age)
    auto result = this->qs->template sum<^^Person::age>()
                          .count()
                          .template avg<^^Person::salary>()
                          .template min<^^Person::years_experience>()
                          .template max<^^Person::age>()
                          .get();
    ASSERT_TRUE(result.has_value()) << "Direct chain all five failed: " << result.error().message();

    auto [sum_age, count_all, avg_salary, min_exp, max_age] = result.value();
    EXPECT_EQ(sum_age, 175);
    EXPECT_EQ(count_all, 5);
    EXPECT_DOUBLE_EQ(avg_salary, 70000.0);
    EXPECT_DOUBLE_EQ(min_exp, 3.0);
    EXPECT_DOUBLE_EQ(max_age, 45.0);
}

TYPED_TEST(AggregateTest, DirectChain_WithWhere) {
    this->insert_test_data();

    // Filter: age > 30 → Charlie(35), Dave(40), Eve(45)
    auto result =
            this->qs->where(storm::orm::where::field<^^Person::age>() > 30).template sum<^^Person::age>().count().get();
    ASSERT_TRUE(result.has_value()) << "Direct chain with WHERE failed: " << result.error().message();

    auto [sum_age, count_all] = result.value();
    EXPECT_EQ(sum_age, 120); // 35 + 40 + 45
    EXPECT_EQ(count_all, 3);
}

TYPED_TEST(AggregateTest, DirectChain_WithJoin) {
    this->insert_join_test_data();

    auto result = this->msg_qs->template join<&Message::sender>()
                          .template sum<^^Message::value>()
                          .count()
                          .template avg<^^Message::value>()
                          .get();
    ASSERT_TRUE(result.has_value()) << "Direct chain with JOIN failed: " << result.error().message();

    auto [sum_val, count_all, avg_val] = result.value();
    EXPECT_EQ(sum_val, 210); // 10+20+30+40+50+60
    EXPECT_EQ(count_all, 6);
    EXPECT_DOUBLE_EQ(avg_val, 35.0); // 210/6
}

TYPED_TEST(AggregateTest, DirectChain_TypeSafety) {
    this->insert_test_data();

    auto result = this->qs->template sum<^^Person::age>().count().get();
    ASSERT_TRUE(result.has_value());
    static_assert(
            std::is_same_v<std::remove_reference_t<decltype(result.value())>, std::tuple<int64_t, int64_t>>,
            "Multiple aggregates should return tuple"
    );
}

// ============================================================================
// Edge Cases
// ============================================================================

TYPED_TEST(AggregateTest, EmptyTable_Count) {
    // COUNT(*) on empty table should return 0
    auto result = this->qs->count().get();
    ASSERT_TRUE(result.has_value()) << "COUNT on empty table failed: " << result.error().message();
    EXPECT_EQ(result.value(), 0);
}

TYPED_TEST(AggregateTest, EmptyTable_Sum) {
    // SUM on empty table should return 0 (SQLite NULL → 0)
    auto result = this->qs->template sum<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "SUM on empty table failed: " << result.error().message();
    EXPECT_EQ(result.value(), 0);
}

TYPED_TEST(AggregateTest, EmptyTable_Avg) {
    // AVG on empty table should return 0 (SQLite NULL → 0.0)
    auto result = this->qs->template avg<^^Person::salary>().get();
    ASSERT_TRUE(result.has_value()) << "AVG on empty table failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 0.0);
}

TYPED_TEST(AggregateTest, EmptyTable_Min) {
    // MIN on empty table should return 0 (SQLite NULL → 0.0)
    auto result = this->qs->template min<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "MIN on empty table failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 0.0);
}

TYPED_TEST(AggregateTest, EmptyTable_Max) {
    // MAX on empty table should return 0 (SQLite NULL → 0.0)
    auto result = this->qs->template max<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "MAX on empty table failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 0.0);
}

TYPED_TEST(AggregateTest, EmptyTable_MultipleAggregates) {
    // Multiple aggregates on empty table should return tuple of default values
    auto result = this->qs->template sum<^^Person::age>().count().template avg<^^Person::salary>().get();
    ASSERT_TRUE(result.has_value()) << "Multiple aggregates on empty table failed: " << result.error().message();

    // Verify tuple values: (SUM=0, COUNT=0, AVG=0.0)
    auto [sum_val, count_val, avg_val] = result.value();
    EXPECT_EQ(sum_val, 0) << "SUM on empty table should be 0";
    EXPECT_EQ(count_val, 0) << "COUNT on empty table should be 0";
    EXPECT_DOUBLE_EQ(avg_val, 0.0) << "AVG on empty table should be 0.0";
}

TYPED_TEST(AggregateTest, EmptyTable_AggregateWithWhere) {
    // Aggregate with WHERE on empty table
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 25).count().get();
    ASSERT_TRUE(result.has_value()) << "COUNT with WHERE on empty table failed: " << result.error().message();
    EXPECT_EQ(result.value(), 0) << "COUNT with WHERE on empty table should be 0";
}

TYPED_TEST(AggregateTest, SingleAggregates_WithWhere) {
    this->insert_test_data();
    // Filter: age > 30 → Charlie(35), Dave(40), Eve(45)
    auto sum_result =
            this->qs->where(storm::orm::where::field<^^Person::age>() > 30).template sum<^^Person::age>().get();
    ASSERT_TRUE(sum_result.has_value()) << "SUM with WHERE failed: " << sum_result.error().message();
    EXPECT_EQ(sum_result.value(), 120); // 35 + 40 + 45

    this->qs->reset();

    auto count_result = this->qs->where(storm::orm::where::field<^^Person::age>() > 30).count().get();
    ASSERT_TRUE(count_result.has_value()) << "COUNT with WHERE failed: " << count_result.error().message();
    EXPECT_EQ(count_result.value(), 3); // 3 people

    this->qs->reset();

    auto avg_result =
            this->qs->where(storm::orm::where::field<^^Person::age>() > 30).template avg<^^Person::salary>().get();
    ASSERT_TRUE(avg_result.has_value()) << "AVG with WHERE failed: " << avg_result.error().message();
    EXPECT_DOUBLE_EQ(avg_result.value(), 80000.0); // (70k + 80k + 90k) / 3
}

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

    // SUM(age) = 1+2+3+...+1000 = 1000*1001/2 = 500500
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

TYPED_TEST(AggregateTest, FloatingPoint_Salary) {
    this->insert_test_data();

    // SUM(salary) = 50000 + 60000 + 70000 + 80000 + 90000 = 350000.0
    auto sum = this->qs->template sum<^^Person::salary>().get();
    ASSERT_TRUE(sum.has_value());
    EXPECT_DOUBLE_EQ(sum.value(), 350000.0);

    // AVG(salary) = 350000 / 5 = 70000.0
    auto avg = this->qs->template avg<^^Person::salary>().get();
    ASSERT_TRUE(avg.has_value());
    EXPECT_DOUBLE_EQ(avg.value(), 70000.0);
}

// ============================================================================
// Statement Caching Tests
// ============================================================================

TYPED_TEST(AggregateTest, StatementCaching_RepeatedQueries) {
    this->insert_test_data();

    // Run same query 100 times - should use cached statement
    for (int i = 0; i < 100; ++i) {
        auto result = this->qs->count().get();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
        EXPECT_EQ(result.value(), 5);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(AggregateTest, StatementCaching_DifferentAggregates) {
    this->insert_test_data();

    // Run different aggregates multiple times
    for (int i = 0; i < 10; ++i) {
        auto sum = this->qs->template sum<^^Person::age>().get();
        ASSERT_TRUE(sum.has_value());
        EXPECT_EQ(sum.value(), 175);

        auto count = this->qs->count().get();
        ASSERT_TRUE(count.has_value());
        EXPECT_EQ(count.value(), 5);

        auto avg = this->qs->template avg<^^Person::salary>().get();
        ASSERT_TRUE(avg.has_value());
        EXPECT_DOUBLE_EQ(avg.value(), 70000.0);
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

    // SUM(age) should now be 30 * 5 = 150
    auto sum = this->qs->template sum<^^Person::age>().get();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 150);
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

    // COUNT should now be 3
    auto count = this->qs->count().get();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 3);
}

// ============================================================================
// WHERE + Aggregate Tests
// ============================================================================

TYPED_TEST(AggregateTest, WhereWithCount) {
    this->insert_test_data();

    // COUNT with WHERE: age > 30
    // Data: Alice(25), Bob(30), Charlie(35), Dave(40), Eve(45)
    // Expected: Charlie, Dave, Eve = 3
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 30).count().get();
    ASSERT_TRUE(result.has_value()) << "WHERE + COUNT failed: " << result.error().message();
    EXPECT_EQ(result.value(), 3);
}

TYPED_TEST(AggregateTest, WhereWithSum) {
    this->insert_test_data();

    // SUM with WHERE: age >= 35
    // Data: Charlie(35), Dave(40), Eve(45)
    // Expected: 35 + 40 + 45 = 120
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() >= 35).template sum<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "WHERE + SUM failed: " << result.error().message();
    EXPECT_EQ(result.value(), 120);
}

TYPED_TEST(AggregateTest, WhereWithAvg) {
    this->insert_test_data();

    // AVG with WHERE: age < 40
    // Data: Alice(25), Bob(30), Charlie(35)
    // Expected: (25 + 30 + 35) / 3 = 30.0
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() < 40).template avg<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "WHERE + AVG failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 30.0);
}

TYPED_TEST(AggregateTest, WhereWithMin) {
    this->insert_test_data();

    // MIN with WHERE: age > 25
    // Data: Bob(30), Charlie(35), Dave(40), Eve(45)
    // Expected: 30
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 25).template min<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "WHERE + MIN failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 30.0);
}

TYPED_TEST(AggregateTest, WhereWithMax) {
    this->insert_test_data();

    // MAX with WHERE: age <= 40
    // Data: Alice(25), Bob(30), Charlie(35), Dave(40)
    // Expected: 40
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() <= 40).template max<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "WHERE + MAX failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 40.0);
}

TYPED_TEST(AggregateTest, WhereWithSumSalary) {
    this->insert_test_data();

    // SUM(salary) with WHERE: salary >= 70000
    // Data: Charlie(70000), Dave(80000), Eve(90000)
    // Expected: 70000 + 80000 + 90000 = 240000
    auto result = this->qs->where(storm::orm::where::field<^^Person::salary>() >= 70000.0)
                          .template sum<^^Person::salary>()
                          .get();
    ASSERT_TRUE(result.has_value()) << "WHERE + SUM(salary) failed: " << result.error().message();
    EXPECT_EQ(result.value(), 240000);
}

TYPED_TEST(AggregateTest, WhereNoResults) {
    this->insert_test_data();

    // COUNT with WHERE: age > 100 (no results)
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 100).count().get();
    ASSERT_TRUE(result.has_value()) << "WHERE (no results) + COUNT failed: " << result.error().message();
    EXPECT_EQ(result.value(), 0);
}

TYPED_TEST(AggregateTest, WhereAllResults) {
    this->insert_test_data();

    // COUNT with WHERE: age > 0 (all results)
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 0).count().get();
    ASSERT_TRUE(result.has_value()) << "WHERE (all results) + COUNT failed: " << result.error().message();
    EXPECT_EQ(result.value(), 5);
}

TYPED_TEST(AggregateTest, WhereWithMultiFieldSum) {
    this->insert_test_data();

    // SUM(age + years_experience) with WHERE: age >= 35
    // Data: Charlie(35, 7), Dave(40, 10), Eve(45, 15)
    // Expected: (35+7) + (40+10) + (45+15) = 42 + 50 + 60 = 152
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() >= 35)
                          .template sum<^^Person::age, ^^Person::years_experience>()
                          .get();
    ASSERT_TRUE(result.has_value()) << "WHERE + SUM(multi-field) failed: " << result.error().message();
    EXPECT_EQ(result.value(), 152);
}

TYPED_TEST(AggregateTest, WhereRepeatedQueries) {
    this->insert_test_data();

    // Run same WHERE + aggregate query multiple times (tests caching)
    for (int i = 0; i < 100; ++i) {
        auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 30).count().get();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
        EXPECT_EQ(result.value(), 3);
    }
}

TYPED_TEST(AggregateTest, WhereDifferentConditions) {
    this->insert_test_data();

    // Run queries with different WHERE conditions
    auto result1 = this->qs->where(storm::orm::where::field<^^Person::age>() > 30).count().get();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value(), 3); // Charlie, Dave, Eve

    (*this->qs).reset(); // Clear WHERE clause

    auto result2 = this->qs->where(storm::orm::where::field<^^Person::age>() < 30).count().get();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value(), 1); // Only Alice

    (*this->qs).reset();

    auto result3 = this->qs->where(storm::orm::where::field<^^Person::age>() == 30).count().get();
    ASSERT_TRUE(result3.has_value());
    EXPECT_EQ(result3.value(), 1); // Only Bob
}

// ============================================================================
// JOIN + Aggregate Tests
// ============================================================================

TYPED_TEST(AggregateTest, JoinWithCount) {
    this->insert_join_test_data();

    // COUNT with JOIN: count all messages with their senders
    // Total messages: 6
    auto result = this->msg_qs->template join<&Message::sender>().count().get();
    ASSERT_TRUE(result.has_value()) << "JOIN + COUNT failed: " << result.error().message();
    EXPECT_EQ(result.value(), 6);
}

TYPED_TEST(AggregateTest, JoinWithSum) {
    this->insert_join_test_data();

    // SUM(value) with JOIN
    // Total: 10 + 20 + 30 + 40 + 50 + 60 = 210
    auto result = this->msg_qs->template join<&Message::sender>().template sum<^^Message::value>().get();
    ASSERT_TRUE(result.has_value()) << "JOIN + SUM failed: " << result.error().message();
    EXPECT_EQ(result.value(), 210);
}

TYPED_TEST(AggregateTest, JoinWithAvg) {
    this->insert_join_test_data();

    // AVG(value) with JOIN
    // Average: 210 / 6 = 35.0
    auto result = this->msg_qs->template join<&Message::sender>().template avg<^^Message::value>().get();
    ASSERT_TRUE(result.has_value()) << "JOIN + AVG failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 35.0);
}

TYPED_TEST(AggregateTest, JoinWithMin) {
    this->insert_join_test_data();

    // MIN(value) with JOIN
    // Min: 10
    auto result = this->msg_qs->template join<&Message::sender>().template min<^^Message::value>().get();
    ASSERT_TRUE(result.has_value()) << "JOIN + MIN failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 10.0);
}

TYPED_TEST(AggregateTest, JoinWithMax) {
    this->insert_join_test_data();

    // MAX(value) with JOIN
    // Max: 60
    auto result = this->msg_qs->template join<&Message::sender>().template max<^^Message::value>().get();
    ASSERT_TRUE(result.has_value()) << "JOIN + MAX failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 60.0);
}

// ============================================================================
// WHERE + JOIN + Aggregate Tests
// ============================================================================

TYPED_TEST(AggregateTest, WhereJoinWithCount) {
    this->insert_join_test_data();

    // COUNT with WHERE + JOIN: messages with value > 30
    // Data: value 40, 50, 60 = 3 messages
    auto result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 30)
                          .template join<&Message::sender>()
                          .count()
                          .get();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN + COUNT failed: " << result.error().message();
    EXPECT_EQ(result.value(), 3);
}

TYPED_TEST(AggregateTest, WhereJoinWithSum) {
    this->insert_join_test_data();

    // SUM(value) with WHERE + JOIN: values >= 30
    // Data: 30 + 40 + 50 + 60 = 180
    auto result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() >= 30)
                          .template join<&Message::sender>()
                          .template sum<^^Message::value>()
                          .get();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN + SUM failed: " << result.error().message();
    EXPECT_EQ(result.value(), 180);
}

TYPED_TEST(AggregateTest, WhereJoinWithAvg) {
    this->insert_join_test_data();

    // AVG(value) with WHERE + JOIN: values < 50
    // Data: 10, 20, 30, 40 = average 25.0
    auto result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() < 50)
                          .template join<&Message::sender>()
                          .template avg<^^Message::value>()
                          .get();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN + AVG failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 25.0);
}

TYPED_TEST(AggregateTest, WhereJoinNoResults) {
    this->insert_join_test_data();

    // COUNT with WHERE + JOIN: no results
    auto result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 100)
                          .template join<&Message::sender>()
                          .count()
                          .get();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN (no results) + COUNT failed: " << result.error().message();
    EXPECT_EQ(result.value(), 0);
}

TYPED_TEST(AggregateTest, WhereJoinRepeatedQueries) {
    this->insert_join_test_data();

    // Run same WHERE + JOIN + aggregate query multiple times (tests caching)
    for (int i = 0; i < 50; ++i) {
        auto result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 20)
                              .template join<&Message::sender>()
                              .count()
                              .get();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
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
    // Each person has unique years_experience (3, 5, 7, 10, 15)
    auto result = this->qs->template group_by<^^Person::years_experience>().count().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + COUNT failed: " << result.error().message();

    auto& results = result.value();
    EXPECT_EQ(results.size(), 5); // 5 unique years_experience values

    // Count total to verify we got all rows
    int64_t total_count = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& row : results) {
        total_count += std::get<1>(row);
    }
    EXPECT_EQ(total_count, 5); // Total 5 people
}

TYPED_TEST(AggregateTest, GroupByWithSum) {
    this->insert_test_data();

    // Group by years_experience and sum age
    auto result = this->qs->template group_by<^^Person::years_experience>().template sum<^^Person::age>().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + SUM failed: " << result.error().message();

    EXPECT_EQ(result.value().size(), 5); // 5 unique years_experience values

    // Sum all ages across all groups - should equal total sum (25+30+35+40+45=175)
    int64_t total_sum = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, age_sum] : result.value()) {
        total_sum += age_sum;
    }
    EXPECT_EQ(total_sum, 175);
}

TYPED_TEST(AggregateTest, GroupByWithAvg) {
    this->insert_test_data();

    // Group by years_experience and average salary
    auto result = this->qs->template group_by<^^Person::years_experience>().template avg<^^Person::salary>().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + AVG failed: " << result.error().message();

    EXPECT_EQ(result.value().size(), 5);

    // Since each person has unique years_experience, each group has one person
    // so the average for each group is just that person's salary
    // Find the group with years_experience=3 (Alice 50000)
    double avg_3 = 0.0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, salary_avg] : result.value()) {
        if (years == 3) {
            avg_3 = salary_avg;
            break;
        }
    }
    EXPECT_NEAR(avg_3, 50000.0, 0.01);
}

TYPED_TEST(AggregateTest, GroupByWithMin) {
    this->insert_test_data();

    // Group by years_experience and min salary
    auto result = this->qs->template group_by<^^Person::years_experience>().template min<^^Person::salary>().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + MIN failed: " << result.error().message();

    EXPECT_EQ(result.value().size(), 5);

    // Find the group with years_experience=3 (Alice 50000 - only person)
    double min_3 = 0.0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, salary_min] : result.value()) {
        if (years == 3) {
            min_3 = salary_min;
            break;
        }
    }
    EXPECT_NEAR(min_3, 50000.0, 0.01);
}

TYPED_TEST(AggregateTest, GroupByWithMax) {
    this->insert_test_data();

    // Group by years_experience and max age
    auto result = this->qs->template group_by<^^Person::years_experience>().template max<^^Person::age>().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + MAX failed: " << result.error().message();

    EXPECT_EQ(result.value().size(), 5);

    // Find the group with years_experience=3 (Alice 25 - only person)
    double max_3 = 0.0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, age_max] : result.value()) {
        if (years == 3) {
            max_3 = age_max;
            break;
        }
    }
    EXPECT_NEAR(max_3, 25.0, 0.01);
}

TYPED_TEST(AggregateTest, GroupByWithWhere) {
    this->insert_test_data();

    // Filter by age > 30, then group by years_experience
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 30)
                          .template group_by<^^Person::years_experience>()
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "WHERE + GROUP BY failed: " << result.error().message();

    // Only Charlie (35), Dave (40), Eve (45) should be included
    // Charlie: years=7, Dave: years=10, Eve: years=15
    EXPECT_EQ(result.value().size(), 3);

    // Total count should be 3
    int64_t total = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, count] : result.value()) {
        total += count;
    }
    EXPECT_EQ(total, 3);
}

TYPED_TEST(AggregateTest, GroupByWithJoin) {
    this->insert_join_test_data();

    // Group messages by value (10, 20, 30, 40, 50, 60 - all unique)
    auto result =
            this->msg_qs->template join<&Message::sender>().template group_by<^^Message::value>().count().select();
    ASSERT_TRUE(result.has_value()) << "JOIN + GROUP BY failed: " << result.error().message();

    EXPECT_EQ(result.value().size(), 6); // 6 unique values

    // Verify each group has count 1 (all unique values)
    for (const auto& [value, count] : result.value()) {
        EXPECT_EQ(count, 1);
    }
}

TYPED_TEST(AggregateTest, GroupByWithJoinAndSum) {
    this->insert_join_test_data();

    // Group messages by content, sum values
    // Each content is unique, so each group has one value
    auto result = this->msg_qs->template join<&Message::sender>()
                          .template group_by<^^Message::content>()
                          .template sum<^^Message::value>()
                          .select();
    ASSERT_TRUE(result.has_value()) << "JOIN + GROUP BY + SUM failed: " << result.error().message();

    EXPECT_EQ(result.value().size(), 6);

    // Sum of all values should equal 10+20+30+40+50+60=210
    int64_t total_sum = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [content, value_sum] : result.value()) {
        total_sum += value_sum;
    }
    EXPECT_EQ(total_sum, 210);
}

TYPED_TEST(AggregateTest, GroupByWithWhereAndJoin) {
    this->insert_join_test_data();

    // Filter messages with value > 20, join with sender, group by value
    auto result = this->msg_qs->where(storm::orm::where::field<^^Message::value>() > 20)
                          .template join<&Message::sender>()
                          .template group_by<^^Message::value>()
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN + GROUP BY failed: " << result.error().message();

    // Values > 20: 30, 40, 50, 60 (4 groups)
    EXPECT_EQ(result.value().size(), 4);

    // Each value appears once
    for (const auto& [value, count] : result.value()) {
        EXPECT_EQ(count, 1);
    }
}

TYPED_TEST(AggregateTest, GroupByRepeatedQueries) {
    this->insert_test_data();

    // Run same GROUP BY query multiple times (tests caching)
    for (int i = 0; i < 50; ++i) {
        auto result = this->qs->template group_by<^^Person::years_experience>().count().select();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
        EXPECT_EQ(result.value().size(), 5);
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

    const auto& conn = QuerySet<Person, TypeParam>::get_default_connection();

    // Insert users with different ages
    std::ignore = conn->execute(
            "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Alice', 25, 0, 0, 0)"
    ); // id=1
    std::ignore = conn->execute(
            "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Bob', 35, 0, 0, 0)"
    ); // id=2
    std::ignore = conn->execute(
            "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Charlie', 45, 0, 0, 0)"
    ); // id=3
    std::ignore = conn->execute(
            "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Dave', 30, 0, 0, 0)"
    ); // id=4

    // Insert messages with varying values
    // Alice (sender_id=1): 3 messages, values 10, 20, 30 (total=60, avg=20)
    // Bob (sender_id=2): 2 messages, values 50, 70 (total=120, avg=60)
    // Charlie (sender_id=3): 4 messages, values 5, 15, 25, 35 (total=80, avg=20)
    // Dave (sender_id=4): 1 message, value 100 (total=100, avg=100)
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('A1', 10, 1)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('A2', 20, 1)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('A3', 30, 1)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('B1', 50, 2)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('B2', 70, 2)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('C1', 5, 3)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('C2', 15, 3)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('C3', 25, 3)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('C4', 35, 3)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('D1', 100, 4)");

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
    // Messages with value < 50: A1(10), A2(20), A3(30), C1(5), C2(15), C3(25), C4(35)
    // That's 7 messages, each with unique content
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

TYPED_TEST(AggregateTest, CountDistinctBasic) {
    this->insert_test_data();

    // Count distinct years_experience values (all unique: 3, 5, 7, 10, 15)
    auto result = this->qs->template count_distinct<^^Person::years_experience>().get();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT) failed: " << result.error().message();
    EXPECT_EQ(result.value(), 5); // 5 unique values
}

TYPED_TEST(AggregateTest, CountDistinctAge) {
    this->insert_test_data();

    // Count distinct ages (all unique: 25, 30, 35, 40, 45)
    auto result = this->qs->template count_distinct<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT age) failed: " << result.error().message();
    EXPECT_EQ(result.value(), 5);
}

TYPED_TEST(AggregateTest, CountDistinctWithDuplicates) {
    // Insert data with duplicate ages
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Alice", .age = 30, .salary = 50000.0, .years_experience = 3})
                    .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .age = 30, .salary = 60000.0, .years_experience = 5})
                          .execute(); // Same age as Alice
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Charlie", .age = 35, .salary = 70000.0, .years_experience = 7})
                    .execute();
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Dave", .age = 30, .salary = 80000.0, .years_experience = 10})
                    .execute(); // Same age as Alice and Bob
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Eve", .age = 35, .salary = 90000.0, .years_experience = 15})
                          .execute(); // Same age as Charlie

    auto result = this->qs->template count_distinct<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT) with duplicates failed";
    EXPECT_EQ(result.value(), 2); // Only 2 unique ages (30, 35)
}

TYPED_TEST(AggregateTest, CountDistinctWithWhere) {
    this->insert_test_data();

    // Count distinct ages where age > 30
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 30)
                          .template count_distinct<^^Person::age>()
                          .get();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT) with WHERE failed";
    EXPECT_EQ(result.value(), 3); // 35, 40, 45
}

TYPED_TEST(AggregateTest, CountDistinctWithJoin) {
    this->insert_join_test_data();

    // Count distinct message values
    auto result = this->msg_qs->template join<&Message::sender>().template count_distinct<^^Message::value>().get();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT) with JOIN failed";
    EXPECT_EQ(result.value(), 6); // 10, 20, 30, 40, 50, 60 - all unique
}

TYPED_TEST(AggregateTest, CountDistinctEmptyTable) {
    // Don't insert any data
    auto result = this->qs->template count_distinct<^^Person::age>().get();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT) on empty table failed";
    EXPECT_EQ(result.value(), 0);
}

TYPED_TEST(AggregateTest, CountDistinctRepeatedQueries) {
    this->insert_test_data();

    // Run same COUNT(DISTINCT) query multiple times (tests caching)
    for (int i = 0; i < 50; ++i) {
        auto result = this->qs->template count_distinct<^^Person::years_experience>().get();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
        EXPECT_EQ(result.value(), 5);
    }
}

// =============================================================================
// NULL/Optional and Negative Number Tests
// =============================================================================

template <typename ConnType> class OptionalAggregateTest : public StormTestFixture<Person, ConnType> {
  protected:
    auto SetUp() -> void override {
        if (!this->setup_connection()) {
            GTEST_SKIP() << "Backend unavailable";
            return;
        }
        const auto& conn = QuerySet<Person, ConnType>::get_default_connection();

        auto create_result = storm::test::ensure_table<ConnType>(conn, person_create_sql);
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table";

        storm::test::begin_test_txn<ConnType>(conn, {"Person"});

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
    // Insert with NULL ages
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Alice", .salary = 50000.0, .score = 25}).execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .salary = 60000.0, .score = std::nullopt})
                          .execute(); // NULL age
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Charlie", .salary = 70000.0, .score = 35}).execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Dave", .salary = 80000.0, .score = std::nullopt})
                          .execute(); // NULL age

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
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Alice", .salary = 50000.0, .score = 25}).execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .salary = 60000.0, .score = std::nullopt})
                          .execute(); // NULL age
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Charlie", .salary = 70000.0, .score = 35}).execute();

    auto sum = this->qs->template sum<^^Person::score>().get();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 60); // 25 + 35, NULL ignored
}

TYPED_TEST(OptionalAggregateTest, AvgWithNullValues) {
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Alice", .salary = 50000.0, .score = 20}).execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .salary = 60000.0, .score = std::nullopt})
                          .execute(); // NULL age
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Charlie", .salary = 70000.0, .score = 40}).execute();

    auto avg = this->qs->template avg<^^Person::score>().get();
    ASSERT_TRUE(avg.has_value());
    EXPECT_NEAR(avg.value(), 30.0, 0.01); // (20 + 40) / 2 = 30, NULL ignored
}

TYPED_TEST(OptionalAggregateTest, MinMaxWithNullValues) {
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Alice", .salary = 50000.0, .score = 25}).execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .salary = 60000.0, .score = std::nullopt})
                          .execute(); // NULL age
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Charlie", .salary = 70000.0, .score = 45}).execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Dave", .salary = 80000.0, .score = std::nullopt})
                          .execute(); // NULL age

    auto min_val = this->qs->template min<^^Person::score>().get();
    ASSERT_TRUE(min_val.has_value());
    EXPECT_NEAR(min_val.value(), 25.0, 0.01);

    auto max_val = this->qs->template max<^^Person::score>().get();
    ASSERT_TRUE(max_val.has_value());
    EXPECT_NEAR(max_val.value(), 45.0, 0.01);
}

TYPED_TEST(OptionalAggregateTest, CountDistinctWithNullValues) {
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Alice", .salary = 50000.0, .score = 30}).execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .salary = 60000.0, .score = std::nullopt})
                          .execute(); // NULL age
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Charlie", .salary = 70000.0, .score = 30})
                          .execute(); // Same as Alice
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Dave", .salary = 80000.0, .score = std::nullopt})
                          .execute(); // NULL age
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Eve", .salary = 90000.0, .score = 40}).execute();

    // COUNT(DISTINCT age) - NULL excluded, should count 2 (30, 40)
    auto result = this->qs->template count_distinct<^^Person::score>().get();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 2);
}

TYPED_TEST(OptionalAggregateTest, GroupByWithAllNullValuesInGroupColumn) {
    // Insert rows where all ages are NULL
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Alice", .salary = 50000.0, .score = std::nullopt}).execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .salary = 60000.0, .score = std::nullopt}).execute();
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Charlie", .salary = 70000.0, .score = std::nullopt}).execute();

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
    // Insert mix of NULL and non-NULL ages
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Alice", .salary = 50000.0, .score = 25}).execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .salary = 60000.0, .score = std::nullopt}).execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Charlie", .salary = 70000.0, .score = 25}).execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Dave", .salary = 80000.0, .score = std::nullopt}).execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Eve", .salary = 90000.0, .score = 30}).execute();

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
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3})
                    .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .age = 5, .salary = 60000.0, .years_experience = 5})
                          .execute();
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Charlie", .age = -3, .salary = 70000.0, .years_experience = 7})
                    .execute();

    auto sum = this->qs->template sum<^^Person::age>().get();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), -8); // -10 + 5 + (-3) = -8
}

TYPED_TEST(AggregateTest, NegativeNumbersInAvg) {
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Alice", .age = -12, .salary = 50000.0, .years_experience = 3})
                    .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .age = 6, .salary = 60000.0, .years_experience = 5})
                          .execute();
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Charlie", .age = 0, .salary = 70000.0, .years_experience = 7})
                    .execute();

    auto avg = this->qs->template avg<^^Person::age>().get();
    ASSERT_TRUE(avg.has_value());
    EXPECT_NEAR(avg.value(), -2.0, 0.01); // (-12 + 6 + 0) / 3 = -2
}

TYPED_TEST(AggregateTest, NegativeNumbersInMinMax) {
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3})
                    .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .age = 5, .salary = 60000.0, .years_experience = 5})
                          .execute();
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Charlie", .age = -20, .salary = 70000.0, .years_experience = 7})
                    .execute();
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Dave", .age = 15, .salary = 80000.0, .years_experience = 10})
                    .execute();

    auto min_val = this->qs->template min<^^Person::age>().get();
    ASSERT_TRUE(min_val.has_value());
    EXPECT_NEAR(min_val.value(), -20.0, 0.01);

    auto max_val = this->qs->template max<^^Person::age>().get();
    ASSERT_TRUE(max_val.has_value());
    EXPECT_NEAR(max_val.value(), 15.0, 0.01);
}

TYPED_TEST(AggregateTest, NegativeNumbersInCount) {
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3})
                    .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .age = -5, .salary = 60000.0, .years_experience = 5})
                          .execute();
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Charlie", .age = 0, .salary = 70000.0, .years_experience = 7})
                    .execute();

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
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3})
                    .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .age = -5, .salary = 60000.0, .years_experience = 5})
                          .execute();
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Charlie", .age = 0, .salary = 70000.0, .years_experience = 7})
                    .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Dave", .age = 5, .salary = 80000.0, .years_experience = 10})
                          .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Eve", .age = 10, .salary = 90000.0, .years_experience = 15})
                          .execute();

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

    const auto& conn = QuerySet<Person, TypeParam>::get_default_connection();

    // Insert users with different ages
    std::ignore = conn->execute(
            "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Alice', 25, 0, 0, 0)"
    ); // id=1
    std::ignore = conn->execute(
            "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Bob', 35, 0, 0, 0)"
    ); // id=2
    std::ignore = conn->execute(
            "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Charlie', 45, 0, 0, 0)"
    ); // id=3
    std::ignore = conn->execute(
            "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Dave', 30, 0, 0, 0)"
    ); // id=4
    std::ignore = conn->execute(
            "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Eve', 28, 0, 0, 0)"
    ); // id=5

    // Insert messages with varying values
    // Alice (sender_id=1): 3 messages, values 10, 15, 20
    // Bob (sender_id=2): 2 messages, values 25, 30
    // Charlie (sender_id=3): 4 messages, values 35, 40, 45, 50
    // Dave (sender_id=4): 2 messages, values 55, 60
    // Eve (sender_id=5): 3 messages, values 65, 70, 75
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('A1', 10, 1)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('A2', 15, 1)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('A3', 20, 1)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('B1', 25, 2)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('B2', 30, 2)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('C1', 35, 3)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('C2', 40, 3)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('C3', 45, 3)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('C4', 50, 3)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('D1', 55, 4)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('D2', 60, 4)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('E1', 65, 5)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('E2', 70, 5)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('E3', 75, 5)");

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

    this->insert_two_group_test_data();

    // Test data summary by years_experience:
    //   years_experience=5: Alice(25), Bob(30), Frank(28) -> count=3, sum=83, avg=27.67, min=25, max=30
    //   years_experience=10: Charlie(35), Dave(40), Eve(45) -> count=3, sum=120, avg=40, min=35, max=45

    // Test 1: GROUP BY with COUNT
    auto count_result = this->qs->template group_by<^^Person::years_experience>().count().select();
    ASSERT_TRUE(count_result.has_value()) << "GROUP BY + COUNT failed: " << count_result.error().message();
    EXPECT_EQ(count_result.value().size(), 2) << "Expected 2 groups";
    for (const auto& [years_exp, count_val] : count_result.value()) {
        if (years_exp == 5 || years_exp == 10) {
            EXPECT_EQ(count_val, 3) << "Each group should have 3 people";
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
            EXPECT_EQ(sum_age, 83) << "Sum of ages for group 5: 25+30+28=83";
        } else if (years_exp == 10) {
            EXPECT_EQ(sum_age, 120) << "Sum of ages for group 10: 35+40+45=120";
        }
    }

    // Test 3: GROUP BY with AVG
    this->qs->reset();
    auto avg_result = this->qs->template group_by<^^Person::years_experience>().template avg<^^Person::age>().select();
    ASSERT_TRUE(avg_result.has_value()) << "GROUP BY + AVG failed: " << avg_result.error().message();
    for (const auto& [years_exp, avg_age] : avg_result.value()) {
        if (years_exp == 5) {
            EXPECT_NEAR(avg_age, 27.67, 0.01) << "Avg of ages for group 5: 83/3≈27.67";
        } else if (years_exp == 10) {
            EXPECT_NEAR(avg_age, 40.0, 0.01) << "Avg of ages for group 10: 120/3=40";
        }
    }

    // Test 4: GROUP BY with MIN
    this->qs->reset();
    auto min_result = this->qs->template group_by<^^Person::years_experience>().template min<^^Person::age>().select();
    ASSERT_TRUE(min_result.has_value()) << "GROUP BY + MIN failed: " << min_result.error().message();
    for (const auto& [years_exp, min_age] : min_result.value()) {
        if (years_exp == 5) {
            EXPECT_NEAR(min_age, 25.0, 0.01) << "Min age for group 5 is 25";
        } else if (years_exp == 10) {
            EXPECT_NEAR(min_age, 35.0, 0.01) << "Min age for group 10 is 35";
        }
    }

    // Test 5: GROUP BY with MAX
    this->qs->reset();
    auto max_result = this->qs->template group_by<^^Person::years_experience>().template max<^^Person::age>().select();
    ASSERT_TRUE(max_result.has_value()) << "GROUP BY + MAX failed: " << max_result.error().message();
    for (const auto& [years_exp, max_age] : max_result.value()) {
        if (years_exp == 5) {
            EXPECT_NEAR(max_age, 30.0, 0.01) << "Max age for group 5 is 30";
        } else if (years_exp == 10) {
            EXPECT_NEAR(max_age, 45.0, 0.01) << "Max age for group 10 is 45";
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
    EXPECT_EQ(sum_age, 83);
}

// =============================================================================
// GROUP BY with ORDER BY, LIMIT, OFFSET Tests
// =============================================================================

TYPED_TEST(AggregateTest, GroupByWithOrderBy) {
    this->insert_test_data();

    // Group by years_experience and count, ordered by years_experience ascending
    // Test data has years_experience: 3, 5, 7, 10, 15
    auto result = this->qs->template order_by<^^Person::years_experience>()
                          .template group_by<^^Person::years_experience>()
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + ORDER BY failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 5);

    // Verify order: 3, 5, 7, 10, 15
    std::vector<int> expected_order = {3, 5, 7, 10, 15};
    size_t           idx            = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, count_val] : result.value()) {
        EXPECT_EQ(years, expected_order[idx]) << "Unexpected order at index " << idx;
        EXPECT_EQ(count_val, 1) << "Each group should have 1 person";
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
    EXPECT_EQ(result.value().size(), 5);

    // Verify order: 15, 10, 7, 5, 3
    std::vector<int> expected_order = {15, 10, 7, 5, 3};
    size_t           idx            = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, count_val] : result.value()) {
        EXPECT_EQ(years, expected_order[idx]) << "Unexpected order at index " << idx;
        idx++;
    }
}

TYPED_TEST(AggregateTest, GroupByWithLimit) {
    this->insert_test_data();

    // Group by years_experience with LIMIT 3
    auto result = this->qs->limit(3).template group_by<^^Person::years_experience>().count().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + LIMIT failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 3) << "Should return only 3 groups";
}

TYPED_TEST(AggregateTest, GroupByWithLimitOffset) {
    this->insert_test_data();

    // Group by years_experience, ordered, with LIMIT 2 OFFSET 2
    // Ordered: 3, 5, 7, 10, 15 -> skip 2 (3, 5), take 2 (7, 10)
    auto result = this->qs->template order_by<^^Person::years_experience>()
                          .limit(2)
                          .offset(2)
                          .template group_by<^^Person::years_experience>()
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + LIMIT + OFFSET failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 2);

    // Verify we got years 7 and 10 (skipped 3, 5)
    std::vector<int> expected = {7, 10};
    size_t           idx      = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, count_val] : result.value()) {
        EXPECT_EQ(years, expected[idx]) << "Expected years_experience=" << expected[idx] << " at index " << idx;
        idx++;
    }
}

TYPED_TEST(AggregateTest, GroupByWithWhereOrderByLimit) {
    this->insert_test_data();

    // WHERE + ORDER BY + LIMIT + GROUP BY
    auto result = this->qs->where(storm::orm::where::field<^^Person::age>() > 25)
                          .template order_by<^^Person::years_experience, false>()
                          .limit(2)
                          .template group_by<^^Person::years_experience>()
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "WHERE + ORDER BY + LIMIT + GROUP BY failed: " << result.error().message();
    EXPECT_LE(result.value().size(), 2) << "Should return at most 2 groups";
}

TYPED_TEST(AggregateTest, GroupByWithSumOrderByLimit) {
    this->insert_test_data();

    // GROUP BY + SUM with ORDER BY and LIMIT
    auto result = this->qs->template order_by<^^Person::years_experience>()
                          .limit(3)
                          .template group_by<^^Person::years_experience>()
                          .template sum<^^Person::salary>()
                          .select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + SUM + ORDER BY + LIMIT failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 3) << "Should return 3 groups";
}

TYPED_TEST(AggregateTest, GroupByWithOffsetOnly) {
    this->insert_test_data();

    // OFFSET without explicit LIMIT (should use LIMIT -1 internally for SQLite)
    auto result = this->qs->template order_by<^^Person::years_experience>()
                          .offset(2)
                          .template group_by<^^Person::years_experience>()
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + OFFSET only failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 3) << "Should return 3 groups (5 - 2 offset)";
}

TYPED_TEST(AggregateTest, GroupByMultipleFields) {
    this->insert_multi_group_test_data();

    // Group by TWO fields: age AND years_experience
    // Expected groups: (25,5)=2, (25,10)=1, (30,5)=1, (30,10)=1 = 4 groups
    auto result = this->qs->template group_by<^^Person::age, ^^Person::years_experience>().count().select();
    ASSERT_TRUE(result.has_value()) << "Multi-field GROUP BY failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 4) << "Expected 4 unique (age, years_experience) combinations";

    // Verify the (25, 5) group has count=2
    for (const auto& [age, years_exp, count_val] : result.value()) {
        if (age == 25 && years_exp == 5) {
            EXPECT_EQ(count_val, 2) << "Group (25, 5) should have 2 people";
        } else {
            EXPECT_EQ(count_val, 1) << "Other groups should have 1 person each";
        }
    }
}

TYPED_TEST(AggregateTest, GroupByMultipleFieldsWithOrderByLimit) {
    // Same data setup as above
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Alice", .age = 25, .salary = 50000.0, .years_experience = 5})
                    .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .age = 25, .salary = 60000.0, .years_experience = 5})
                          .execute();
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Charlie", .age = 25, .salary = 70000.0, .years_experience = 10})
                    .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Dave", .age = 30, .salary = 80000.0, .years_experience = 5})
                          .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Eve", .age = 30, .salary = 90000.0, .years_experience = 10})
                          .execute();

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
    // Insert data with duplicate years_experience values
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Alice", .age = 25, .salary = 50000.0, .years_experience = 5})
                    .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .age = 30, .salary = 60000.0, .years_experience = 5})
                          .execute();
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Charlie", .age = 35, .salary = 70000.0, .years_experience = 10})
                    .execute();
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Dave", .age = 40, .salary = 80000.0, .years_experience = 10})
                    .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Eve", .age = 45, .salary = 90000.0, .years_experience = 10})
                          .execute();

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
    // Insert data with duplicate years_experience values
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Alice", .age = 25, .salary = 50000.0, .years_experience = 5})
                    .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Bob", .age = 30, .salary = 60000.0, .years_experience = 5})
                          .execute();
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Charlie", .age = 35, .salary = 70000.0, .years_experience = 10})
                    .execute();
    std::ignore =
            this->qs->insert(Person{.id = 0, .name = "Dave", .age = 40, .salary = 80000.0, .years_experience = 10})
                    .execute();
    std::ignore = this->qs->insert(Person{.id = 0, .name = "Eve", .age = 45, .salary = 90000.0, .years_experience = 10})
                          .execute();

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

TYPED_TEST(AggregateTest, HavingWithFieldComparison) {
    this->insert_test_data();

    // GROUP BY age HAVING age > 30
    // Ages: 25 (filtered), 30 (filtered), 35 (kept), 40 (kept), 45 (kept)
    auto result = this->qs->template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>() > 30)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING with field comparison failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 3) << "Expected 3 groups (ages 35, 40, 45)";
}

TYPED_TEST(AggregateTest, HavingWithWhereClause) {
    this->insert_test_data();

    // WHERE salary > 50000 AND GROUP BY years_experience HAVING years_experience > 5
    // After WHERE: Bob(5), Charlie(7), Dave(10), Eve(15)
    // After GROUP BY + HAVING years_experience > 5: Charlie(7), Dave(10), Eve(15) = 3 groups
    auto result = this->qs->where(storm::orm::where::field<^^Person::salary>() > 50000.0)
                          .template group_by<^^Person::years_experience>()
                          .having(storm::orm::where::field<^^Person::years_experience>() > 5)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING + WHERE failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 3) << "Expected 3 groups after WHERE + HAVING";
}

TYPED_TEST(AggregateTest, HavingWithGroupByOrderByLimit) {
    this->insert_test_data();

    // GROUP BY age HAVING age > 25, ORDER BY age ASC, LIMIT 2
    // After HAVING: 30, 35, 40, 45
    // After ORDER BY ASC + LIMIT 2: 30, 35
    auto result = this->qs->template order_by<^^Person::age>()
                          .limit(2)
                          .template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>() > 25)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING + ORDER BY + LIMIT failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 2) << "Expected 2 groups after LIMIT";

    std::vector<int> expected_ages = {30, 35};
    size_t           idx           = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [age, count_val] : result.value()) {
        EXPECT_EQ(age, expected_ages[idx]) << "Unexpected age at index " << idx;
        idx++;
    }
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

TYPED_TEST(AggregateTest, HavingEmptyResult) {
    this->insert_test_data();

    // GROUP BY age HAVING age > 100 -> no groups match
    auto result = this->qs->template group_by<^^Person::age>()
                          .having(storm::orm::where::field<^^Person::age>() > 100)
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING empty result failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 0) << "Expected 0 groups";
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
        EXPECT_EQ(result.value().size(), 3);
    }
}

TYPED_TEST(AggregateTest, HavingWithSum) {
    this->insert_having_sum_test_data();

    // GROUP BY years_experience, SUM(age), HAVING years_experience == 5
    auto result = this->qs->template group_by<^^Person::years_experience>()
                          .having(storm::orm::where::field<^^Person::years_experience>() == 5)
                          .template sum<^^Person::age>()
                          .select();
    ASSERT_TRUE(result.has_value()) << "HAVING + SUM failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 1);

    auto [years_exp, sum_age] = *result.value().begin();
    EXPECT_EQ(years_exp, 5);
    EXPECT_EQ(sum_age, 55); // 25 + 30
}

TYPED_TEST(AggregateTest, HavingWithWhereAndJoin) {
    // Setup: messages with varying values per sender
    const auto& conn = QuerySet<Person, TypeParam>::get_default_connection();

    std::ignore = conn->execute(
            "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Alice', 25, 0, 0, 0)"
    ); // id=1
    std::ignore = conn->execute(
            "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Bob', 35, 0, 0, 0)"
    ); // id=2

    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('A1', 10, 1)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('A2', 20, 1)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('A3', 30, 1)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('B1', 50, 2)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('B2', 70, 2)");

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

// Note: main() is provided by main.cpp (shared across all test files)

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
