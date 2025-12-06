#include <gtest/gtest.h>

import storm;
import storm_db_sqlite;

import <expected>;
import <string>;
import <vector>;
import <optional>;
import <meta>;

using namespace storm;

// Test model: AggregatePerson with multiple numeric fields
struct AggregatePerson {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
    double                                    salary;
    int                                       years_experience;
};

// Test fixture for aggregate functions
class AggregateTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Open in-memory database
        auto result = QuerySet<AggregatePerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        auto& conn = QuerySet<AggregatePerson>::get_default_connection();

        // Create table
        auto create_result = conn->execute(
                "CREATE TABLE AggregatePerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL, "
                "salary REAL NOT NULL, "
                "years_experience INTEGER NOT NULL)"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        qs = std::make_unique<QuerySet<AggregatePerson>>();
    }

    void TearDown() override {
        qs.reset();
        QuerySet<AggregatePerson>::clear_default_connection();
    }

    // Helper to insert test data
    void insert_test_data() {
        std::vector<AggregatePerson> people =
                {{0, "Alice", 25, 50000.0, 3},
                 {0, "Bob", 30, 60000.0, 5},
                 {0, "Charlie", 35, 70000.0, 7},
                 {0, "Dave", 40, 80000.0, 10},
                 {0, "Eve", 45, 90000.0, 15}};

        for (const auto& person : people) {
            auto result = qs->insert(person);
            ASSERT_TRUE(result.has_value()) << "Failed to insert: " << result.error().message();
        }
    }

    std::unique_ptr<QuerySet<AggregatePerson>> qs;
};

// ============================================================================
// Single Aggregate Function Tests
// ============================================================================

TEST_F(AggregateTest, SumSingleField) {
    insert_test_data();

    // SUM(age) = 25 + 30 + 35 + 40 + 45 = 175
    auto result = qs->sum<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "SUM failed: " << result.error().message();
    EXPECT_EQ(result.value(), 175);
}

TEST_F(AggregateTest, SumMultipleFields) {
    insert_test_data();

    // SUM(age + years_experience) = (25+3) + (30+5) + (35+7) + (40+10) + (45+15) = 28+35+42+50+60 = 215
    auto result = qs->sum<^^AggregatePerson::age, ^^AggregatePerson::years_experience>().select();
    ASSERT_TRUE(result.has_value()) << "SUM multi-field failed: " << result.error().message();
    EXPECT_EQ(result.value(), 215);
}

TEST_F(AggregateTest, CountAll) {
    insert_test_data();

    // COUNT(*) = 5
    auto result = qs->count().select();
    ASSERT_TRUE(result.has_value()) << "COUNT failed: " << result.error().message();
    EXPECT_EQ(result.value(), 5);
}

TEST_F(AggregateTest, CountField) {
    insert_test_data();

    // COUNT(id) = 5
    auto result = qs->count<^^AggregatePerson::id>().select();
    ASSERT_TRUE(result.has_value()) << "COUNT field failed: " << result.error().message();
    EXPECT_EQ(result.value(), 5);
}

TEST_F(AggregateTest, AvgSingleField) {
    insert_test_data();

    // AVG(age) = (25 + 30 + 35 + 40 + 45) / 5 = 175 / 5 = 35.0
    auto result = qs->avg<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "AVG failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 35.0);
}

TEST_F(AggregateTest, AvgMultipleFields) {
    insert_test_data();

    // AVG(age + years_experience) = 215 / 5 = 43.0
    auto result = qs->avg<^^AggregatePerson::age, ^^AggregatePerson::years_experience>().select();
    ASSERT_TRUE(result.has_value()) << "AVG multi-field failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 43.0);
}

TEST_F(AggregateTest, MinSingleField) {
    insert_test_data();

    // MIN(age) = 25
    auto result = qs->min<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "MIN failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 25.0);
}

TEST_F(AggregateTest, MinMultipleFields) {
    insert_test_data();

    // MIN(age + years_experience) = MIN(28, 35, 42, 50, 60) = 28
    auto result = qs->min<^^AggregatePerson::age, ^^AggregatePerson::years_experience>().select();
    ASSERT_TRUE(result.has_value()) << "MIN multi-field failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 28.0);
}

TEST_F(AggregateTest, MaxSingleField) {
    insert_test_data();

    // MAX(salary) = 90000.0
    auto result = qs->max<^^AggregatePerson::salary>().select();
    ASSERT_TRUE(result.has_value()) << "MAX failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 90000.0);
}

TEST_F(AggregateTest, MaxMultipleFields) {
    insert_test_data();

    // MAX(age + years_experience) = MAX(28, 35, 42, 50, 60) = 60
    auto result = qs->max<^^AggregatePerson::age, ^^AggregatePerson::years_experience>().select();
    ASSERT_TRUE(result.has_value()) << "MAX multi-field failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 60.0);
}

// ============================================================================
// Multiple Aggregate Functions in One Query
// ============================================================================

TEST_F(AggregateTest, MultipleAggregates_SumAndCount) {
    insert_test_data();

    // SELECT SUM(age), COUNT(*) FROM AggregatePerson
    auto result = qs->aggregate().sum<^^AggregatePerson::age>().count().select();
    ASSERT_TRUE(result.has_value()) << "Multiple aggregates failed: " << result.error().message();

    auto [sum_age, count_all] = result.value();
    EXPECT_EQ(sum_age, 175);
    EXPECT_EQ(count_all, 5);
}

TEST_F(AggregateTest, MultipleAggregates_SumCountAvg) {
    insert_test_data();

    // SELECT SUM(age), COUNT(*), AVG(salary) FROM AggregatePerson
    auto result = qs->aggregate().sum<^^AggregatePerson::age>().count().avg<^^AggregatePerson::salary>().select();
    ASSERT_TRUE(result.has_value()) << "Triple aggregate failed: " << result.error().message();

    auto [sum_age, count_all, avg_salary] = result.value();
    EXPECT_EQ(sum_age, 175);
    EXPECT_EQ(count_all, 5);
    EXPECT_DOUBLE_EQ(avg_salary, 70000.0); // (50k+60k+70k+80k+90k)/5
}

TEST_F(AggregateTest, MultipleAggregates_AllTypes) {
    insert_test_data();

    // SELECT SUM(age), COUNT(*), AVG(salary), MIN(years_experience), MAX(age) FROM AggregatePerson
    auto result = qs->aggregate()
                          .sum<^^AggregatePerson::age>()
                          .count()
                          .avg<^^AggregatePerson::salary>()
                          .min<^^AggregatePerson::years_experience>()
                          .max<^^AggregatePerson::age>()
                          .select();
    ASSERT_TRUE(result.has_value()) << "All aggregates failed: " << result.error().message();

    auto [sum_age, count_all, avg_salary, min_exp, max_age] = result.value();
    EXPECT_EQ(sum_age, 175);
    EXPECT_EQ(count_all, 5);
    EXPECT_DOUBLE_EQ(avg_salary, 70000.0);
    EXPECT_DOUBLE_EQ(min_exp, 3.0);
    EXPECT_DOUBLE_EQ(max_age, 45.0);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(AggregateTest, EmptyTable_Count) {
    // COUNT(*) on empty table should return 0
    auto result = qs->count().select();
    ASSERT_TRUE(result.has_value()) << "COUNT on empty table failed: " << result.error().message();
    EXPECT_EQ(result.value(), 0);
}

TEST_F(AggregateTest, EmptyTable_Sum) {
    // SUM on empty table should return 0 (SQLite NULL → 0)
    auto result = qs->sum<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "SUM on empty table failed: " << result.error().message();
    EXPECT_EQ(result.value(), 0);
}

TEST_F(AggregateTest, EmptyTable_Avg) {
    // AVG on empty table should return 0 (SQLite NULL → 0.0)
    auto result = qs->avg<^^AggregatePerson::salary>().select();
    ASSERT_TRUE(result.has_value()) << "AVG on empty table failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 0.0);
}

TEST_F(AggregateTest, SingleRow_AllAggregates) {
    // Insert single row
    auto insert_result = qs->insert(AggregatePerson{0, "Alice", 25, 50000.0, 3});
    ASSERT_TRUE(insert_result.has_value());

    // Test all aggregate types
    auto sum = qs->sum<^^AggregatePerson::age>().select();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 25);

    auto count = qs->count().select();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 1);

    auto avg = qs->avg<^^AggregatePerson::age>().select();
    ASSERT_TRUE(avg.has_value());
    EXPECT_DOUBLE_EQ(avg.value(), 25.0);

    auto min_val = qs->min<^^AggregatePerson::age>().select();
    ASSERT_TRUE(min_val.has_value());
    EXPECT_DOUBLE_EQ(min_val.value(), 25.0);

    auto max_val = qs->max<^^AggregatePerson::age>().select();
    ASSERT_TRUE(max_val.has_value());
    EXPECT_DOUBLE_EQ(max_val.value(), 25.0);
}

// ============================================================================
// Large Dataset Tests
// ============================================================================

TEST_F(AggregateTest, LargeDataset_Sum) {
    // Insert 1000 people with ages 1-1000
    for (int i = 1; i <= 1000; ++i) {
        auto result = qs->insert(AggregatePerson{0, "AggregatePerson" + std::to_string(i), i, 50000.0, 5});
        ASSERT_TRUE(result.has_value());
    }

    // SUM(age) = 1+2+3+...+1000 = 1000*1001/2 = 500500
    auto result = qs->sum<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "SUM large dataset failed: " << result.error().message();
    EXPECT_EQ(result.value(), 500500);
}

TEST_F(AggregateTest, LargeDataset_Count) {
    // Insert 10000 people
    for (int i = 1; i <= 10000; ++i) {
        auto result = qs->insert(AggregatePerson{0, "AggregatePerson" + std::to_string(i), 25, 50000.0, 5});
        ASSERT_TRUE(result.has_value());
    }

    auto result = qs->count().select();
    ASSERT_TRUE(result.has_value()) << "COUNT large dataset failed: " << result.error().message();
    EXPECT_EQ(result.value(), 10000);
}

// ============================================================================
// Type Safety Tests
// ============================================================================

TEST_F(AggregateTest, TypeSafety_IntegerResult) {
    insert_test_data();

    // Verify SUM returns int64_t
    auto result = qs->sum<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value());
    static_assert(
            std::is_same_v<std::remove_reference_t<decltype(result.value())>, int64_t>, "SUM should return int64_t"
    );
}

TEST_F(AggregateTest, TypeSafety_DoubleResult) {
    insert_test_data();

    // Verify AVG returns double
    auto result = qs->avg<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value());
    static_assert(
            std::is_same_v<std::remove_reference_t<decltype(result.value())>, double>, "AVG should return double"
    );
}

TEST_F(AggregateTest, TypeSafety_TupleResult) {
    insert_test_data();

    // Verify multiple aggregates return tuple
    auto result = qs->aggregate().sum<^^AggregatePerson::age>().count().select();
    ASSERT_TRUE(result.has_value());
    static_assert(
            std::is_same_v<std::remove_reference_t<decltype(result.value())>, std::tuple<int64_t, int64_t>>,
            "Multiple aggregates should return tuple"
    );
}

// ============================================================================
// Floating Point Precision Tests
// ============================================================================

TEST_F(AggregateTest, FloatingPoint_Salary) {
    insert_test_data();

    // SUM(salary) = 50000 + 60000 + 70000 + 80000 + 90000 = 350000.0
    auto sum = qs->sum<^^AggregatePerson::salary>().select();
    ASSERT_TRUE(sum.has_value());
    EXPECT_DOUBLE_EQ(sum.value(), 350000.0);

    // AVG(salary) = 350000 / 5 = 70000.0
    auto avg = qs->avg<^^AggregatePerson::salary>().select();
    ASSERT_TRUE(avg.has_value());
    EXPECT_DOUBLE_EQ(avg.value(), 70000.0);
}

// ============================================================================
// Statement Caching Tests
// ============================================================================

TEST_F(AggregateTest, StatementCaching_RepeatedQueries) {
    insert_test_data();

    // Run same query 100 times - should use cached statement
    for (int i = 0; i < 100; ++i) {
        auto result = qs->count().select();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
        EXPECT_EQ(result.value(), 5);
    }
}

TEST_F(AggregateTest, StatementCaching_DifferentAggregates) {
    insert_test_data();

    // Run different aggregates multiple times
    for (int i = 0; i < 10; ++i) {
        auto sum = qs->sum<^^AggregatePerson::age>().select();
        ASSERT_TRUE(sum.has_value());
        EXPECT_EQ(sum.value(), 175);

        auto count = qs->count().select();
        ASSERT_TRUE(count.has_value());
        EXPECT_EQ(count.value(), 5);

        auto avg = qs->avg<^^AggregatePerson::salary>().select();
        ASSERT_TRUE(avg.has_value());
        EXPECT_DOUBLE_EQ(avg.value(), 70000.0);
    }
}

// ============================================================================
// Integration with Other ORM Features
// ============================================================================

TEST_F(AggregateTest, Integration_AfterInsert) {
    // Insert and immediately aggregate
    for (int i = 1; i <= 5; ++i) {
        auto insert_result = qs->insert(AggregatePerson{0, "AggregatePerson" + std::to_string(i), i * 10, 50000.0, 5});
        ASSERT_TRUE(insert_result.has_value());

        auto count = qs->count().select();
        ASSERT_TRUE(count.has_value());
        EXPECT_EQ(count.value(), i);
    }

    // Final aggregate
    auto sum = qs->sum<^^AggregatePerson::age>().select();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 10 + 20 + 30 + 40 + 50); // 150
}

TEST_F(AggregateTest, Integration_AfterUpdate) {
    insert_test_data();

    // Update all ages to 30
    auto people = qs->select();
    ASSERT_TRUE(people.has_value());

    for (auto& person : people.value()) {
        person.age         = 30;
        auto update_result = qs->update(person);
        ASSERT_TRUE(update_result.has_value());
    }

    // SUM(age) should now be 30 * 5 = 150
    auto sum = qs->sum<^^AggregatePerson::age>().select();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 150);
}

TEST_F(AggregateTest, Integration_AfterDelete) {
    insert_test_data();

    // Delete 2 people
    auto people = qs->select();
    ASSERT_TRUE(people.has_value());

    auto it            = people.value().begin();
    auto delete_result = qs->remove(*it);
    ASSERT_TRUE(delete_result.has_value());
    ++it;
    delete_result = qs->remove(*it);
    ASSERT_TRUE(delete_result.has_value());

    // COUNT should now be 3
    auto count = qs->count().select();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 3);
}

// Note: main() is provided by main.cpp (shared across all test files)
