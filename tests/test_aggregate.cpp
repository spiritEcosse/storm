#include <gtest/gtest.h>

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)

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
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               name;
    int                                       age{};
    double                                    salary{};
    int                                       years_experience{};
};

// Test models for JOIN + Aggregate tests
struct AggUser {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

struct AggMessage {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               content;
    int                                       value;
    [[= storm::meta::FieldAttr::fk]] AggUser  sender;
};

// Test fixture for aggregate functions
class AggregateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        // Open in-memory database
        auto result = QuerySet<AggregatePerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<AggregatePerson>::get_default_connection();

        // Create AggregatePerson table
        auto create_result = conn->execute(
                "CREATE TABLE AggregatePerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL, "
                "salary REAL NOT NULL, "
                "years_experience INTEGER NOT NULL)"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        // Create AggUser table (for JOIN tests)
        auto create_user = conn->execute(
                "CREATE TABLE AggUser ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL)"
        );
        ASSERT_TRUE(create_user.has_value()) << "Failed to create AggUser table";

        // Create AggMessage table with FK (for JOIN tests)
        auto create_msg = conn->execute(
                "CREATE TABLE AggMessage ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "content TEXT NOT NULL, "
                "value INTEGER NOT NULL, "
                "sender_id INTEGER NOT NULL, "
                "FOREIGN KEY (sender_id) REFERENCES AggUser(id))"
        );
        ASSERT_TRUE(create_msg.has_value()) << "Failed to create AggMessage table";

        qs     = std::make_unique<QuerySet<AggregatePerson>>();
        msg_qs = std::make_unique<QuerySet<AggMessage>>();
    }

    auto TearDown() -> void override {
        qs     = nullptr;
        msg_qs = nullptr;
        QuerySet<AggregatePerson>::clear_default_connection();
    }

    // Helper to insert test data
    auto insert_test_data() -> void {
        std::vector<AggregatePerson> const people =
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

    // Helper to insert JOIN test data
    static void insert_join_test_data() {
        const auto& conn = QuerySet<AggregatePerson>::get_default_connection();

        // Insert users
        std::ignore = conn->execute("INSERT INTO AggUser (name, age) VALUES ('Alice', 30)");
        std::ignore = conn->execute("INSERT INTO AggUser (name, age) VALUES ('Bob', 25)");
        std::ignore = conn->execute("INSERT INTO AggUser (name, age) VALUES ('Charlie', 35)");

        // Insert messages with different values
        // Alice: 2 messages, values 10 and 20
        // Bob: 3 messages, values 30, 40, 50
        // Charlie: 1 message, value 60
        std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('Hello', 10, 1)");
        std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('World', 20, 1)");
        std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('Hi', 30, 2)");
        std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('There', 40, 2)");
        std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('Foo', 50, 2)");
        std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('Bar', 60, 3)");
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    // GoogleTest fixtures conventionally use protected members for TEST_F access
    std::unique_ptr<QuerySet<AggregatePerson>> qs;
    std::unique_ptr<QuerySet<AggMessage>>      msg_qs;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
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

TEST_F(AggregateTest, EmptyTable_Min) {
    // MIN on empty table should return 0 (SQLite NULL → 0.0)
    auto result = qs->min<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "MIN on empty table failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 0.0);
}

TEST_F(AggregateTest, EmptyTable_Max) {
    // MAX on empty table should return 0 (SQLite NULL → 0.0)
    auto result = qs->max<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "MAX on empty table failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 0.0);
}

TEST_F(AggregateTest, SingleRow_AllAggregates) {
    // Insert single row
    auto insert_result =
            qs->insert(AggregatePerson{.id = 0, .name = "Alice", .age = 25, .salary = 50000.0, .years_experience = 3});
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
        auto result = qs->insert(
                AggregatePerson{
                        .id               = 0,
                        .name             = "AggregatePerson" + std::to_string(i),
                        .age              = i,
                        .salary           = 50000.0,
                        .years_experience = 5
                }
        );
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
        auto result = qs->insert(
                AggregatePerson{
                        .id               = 0,
                        .name             = "AggregatePerson" + std::to_string(i),
                        .age              = 25,
                        .salary           = 50000.0,
                        .years_experience = 5
                }
        );
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(AggregateTest, Integration_AfterInsert) {
    // Insert and immediately aggregate
    for (int i = 1; i <= 5; ++i) {
        auto insert_result = qs->insert(
                AggregatePerson{
                        .id               = 0,
                        .name             = "AggregatePerson" + std::to_string(i),
                        .age              = i * 10,
                        .salary           = 50000.0,
                        .years_experience = 5
                }
        );
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

// ============================================================================
// WHERE + Aggregate Tests
// ============================================================================

TEST_F(AggregateTest, WhereWithCount) {
    insert_test_data();

    // COUNT with WHERE: age > 30
    // Data: Alice(25), Bob(30), Charlie(35), Dave(40), Eve(45)
    // Expected: Charlie, Dave, Eve = 3
    auto result = qs->where(storm::orm::where::field<^^AggregatePerson::age>() > 30).count().select();
    ASSERT_TRUE(result.has_value()) << "WHERE + COUNT failed: " << result.error().message();
    EXPECT_EQ(result.value(), 3);
}

TEST_F(AggregateTest, WhereWithSum) {
    insert_test_data();

    // SUM with WHERE: age >= 35
    // Data: Charlie(35), Dave(40), Eve(45)
    // Expected: 35 + 40 + 45 = 120
    auto result =
            qs->where(storm::orm::where::field<^^AggregatePerson::age>() >= 35).sum<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "WHERE + SUM failed: " << result.error().message();
    EXPECT_EQ(result.value(), 120);
}

TEST_F(AggregateTest, WhereWithAvg) {
    insert_test_data();

    // AVG with WHERE: age < 40
    // Data: Alice(25), Bob(30), Charlie(35)
    // Expected: (25 + 30 + 35) / 3 = 30.0
    auto result =
            qs->where(storm::orm::where::field<^^AggregatePerson::age>() < 40).avg<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "WHERE + AVG failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 30.0);
}

TEST_F(AggregateTest, WhereWithMin) {
    insert_test_data();

    // MIN with WHERE: age > 25
    // Data: Bob(30), Charlie(35), Dave(40), Eve(45)
    // Expected: 30
    auto result =
            qs->where(storm::orm::where::field<^^AggregatePerson::age>() > 25).min<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "WHERE + MIN failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 30.0);
}

TEST_F(AggregateTest, WhereWithMax) {
    insert_test_data();

    // MAX with WHERE: age <= 40
    // Data: Alice(25), Bob(30), Charlie(35), Dave(40)
    // Expected: 40
    auto result =
            qs->where(storm::orm::where::field<^^AggregatePerson::age>() <= 40).max<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "WHERE + MAX failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 40.0);
}

TEST_F(AggregateTest, WhereWithSumSalary) {
    insert_test_data();

    // SUM(salary) with WHERE: salary >= 70000
    // Data: Charlie(70000), Dave(80000), Eve(90000)
    // Expected: 70000 + 80000 + 90000 = 240000
    auto result = qs->where(storm::orm::where::field<^^AggregatePerson::salary>() >= 70000.0)
                          .sum<^^AggregatePerson::salary>()
                          .select();
    ASSERT_TRUE(result.has_value()) << "WHERE + SUM(salary) failed: " << result.error().message();
    EXPECT_EQ(result.value(), 240000);
}

TEST_F(AggregateTest, WhereNoResults) {
    insert_test_data();

    // COUNT with WHERE: age > 100 (no results)
    auto result = qs->where(storm::orm::where::field<^^AggregatePerson::age>() > 100).count().select();
    ASSERT_TRUE(result.has_value()) << "WHERE (no results) + COUNT failed: " << result.error().message();
    EXPECT_EQ(result.value(), 0);
}

TEST_F(AggregateTest, WhereAllResults) {
    insert_test_data();

    // COUNT with WHERE: age > 0 (all results)
    auto result = qs->where(storm::orm::where::field<^^AggregatePerson::age>() > 0).count().select();
    ASSERT_TRUE(result.has_value()) << "WHERE (all results) + COUNT failed: " << result.error().message();
    EXPECT_EQ(result.value(), 5);
}

TEST_F(AggregateTest, WhereWithMultiFieldSum) {
    insert_test_data();

    // SUM(age + years_experience) with WHERE: age >= 35
    // Data: Charlie(35, 7), Dave(40, 10), Eve(45, 15)
    // Expected: (35+7) + (40+10) + (45+15) = 42 + 50 + 60 = 152
    auto result = qs->where(storm::orm::where::field<^^AggregatePerson::age>() >= 35)
                          .sum<^^AggregatePerson::age, ^^AggregatePerson::years_experience>()
                          .select();
    ASSERT_TRUE(result.has_value()) << "WHERE + SUM(multi-field) failed: " << result.error().message();
    EXPECT_EQ(result.value(), 152);
}

TEST_F(AggregateTest, WhereRepeatedQueries) {
    insert_test_data();

    // Run same WHERE + aggregate query multiple times (tests caching)
    for (int i = 0; i < 100; ++i) {
        auto result = qs->where(storm::orm::where::field<^^AggregatePerson::age>() > 30).count().select();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
        EXPECT_EQ(result.value(), 3);
    }
}

TEST_F(AggregateTest, WhereDifferentConditions) {
    insert_test_data();

    // Run queries with different WHERE conditions
    auto result1 = qs->where(storm::orm::where::field<^^AggregatePerson::age>() > 30).count().select();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value(), 3); // Charlie, Dave, Eve

    (*qs).reset(); // Clear WHERE clause

    auto result2 = qs->where(storm::orm::where::field<^^AggregatePerson::age>() < 30).count().select();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value(), 1); // Only Alice

    (*qs).reset();

    auto result3 = qs->where(storm::orm::where::field<^^AggregatePerson::age>() == 30).count().select();
    ASSERT_TRUE(result3.has_value());
    EXPECT_EQ(result3.value(), 1); // Only Bob
}

// ============================================================================
// JOIN + Aggregate Tests
// ============================================================================

TEST_F(AggregateTest, JoinWithCount) {
    insert_join_test_data();

    // COUNT with JOIN: count all messages with their senders
    // Total messages: 6
    auto result = msg_qs->join<&AggMessage::sender>().count().select();
    ASSERT_TRUE(result.has_value()) << "JOIN + COUNT failed: " << result.error().message();
    EXPECT_EQ(result.value(), 6);
}

TEST_F(AggregateTest, JoinWithSum) {
    insert_join_test_data();

    // SUM(value) with JOIN
    // Total: 10 + 20 + 30 + 40 + 50 + 60 = 210
    auto result = msg_qs->join<&AggMessage::sender>().sum<^^AggMessage::value>().select();
    ASSERT_TRUE(result.has_value()) << "JOIN + SUM failed: " << result.error().message();
    EXPECT_EQ(result.value(), 210);
}

TEST_F(AggregateTest, JoinWithAvg) {
    insert_join_test_data();

    // AVG(value) with JOIN
    // Average: 210 / 6 = 35.0
    auto result = msg_qs->join<&AggMessage::sender>().avg<^^AggMessage::value>().select();
    ASSERT_TRUE(result.has_value()) << "JOIN + AVG failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 35.0);
}

TEST_F(AggregateTest, JoinWithMin) {
    insert_join_test_data();

    // MIN(value) with JOIN
    // Min: 10
    auto result = msg_qs->join<&AggMessage::sender>().min<^^AggMessage::value>().select();
    ASSERT_TRUE(result.has_value()) << "JOIN + MIN failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 10.0);
}

TEST_F(AggregateTest, JoinWithMax) {
    insert_join_test_data();

    // MAX(value) with JOIN
    // Max: 60
    auto result = msg_qs->join<&AggMessage::sender>().max<^^AggMessage::value>().select();
    ASSERT_TRUE(result.has_value()) << "JOIN + MAX failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 60.0);
}

// ============================================================================
// WHERE + JOIN + Aggregate Tests
// ============================================================================

TEST_F(AggregateTest, WhereJoinWithCount) {
    insert_join_test_data();

    // COUNT with WHERE + JOIN: messages with value > 30
    // Data: value 40, 50, 60 = 3 messages
    auto result = msg_qs->where(storm::orm::where::field<^^AggMessage::value>() > 30)
                          .join<&AggMessage::sender>()
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN + COUNT failed: " << result.error().message();
    EXPECT_EQ(result.value(), 3);
}

TEST_F(AggregateTest, WhereJoinWithSum) {
    insert_join_test_data();

    // SUM(value) with WHERE + JOIN: values >= 30
    // Data: 30 + 40 + 50 + 60 = 180
    auto result = msg_qs->where(storm::orm::where::field<^^AggMessage::value>() >= 30)
                          .join<&AggMessage::sender>()
                          .sum<^^AggMessage::value>()
                          .select();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN + SUM failed: " << result.error().message();
    EXPECT_EQ(result.value(), 180);
}

TEST_F(AggregateTest, WhereJoinWithAvg) {
    insert_join_test_data();

    // AVG(value) with WHERE + JOIN: values < 50
    // Data: 10, 20, 30, 40 = average 25.0
    auto result = msg_qs->where(storm::orm::where::field<^^AggMessage::value>() < 50)
                          .join<&AggMessage::sender>()
                          .avg<^^AggMessage::value>()
                          .select();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN + AVG failed: " << result.error().message();
    EXPECT_DOUBLE_EQ(result.value(), 25.0);
}

TEST_F(AggregateTest, WhereJoinNoResults) {
    insert_join_test_data();

    // COUNT with WHERE + JOIN: no results
    auto result = msg_qs->where(storm::orm::where::field<^^AggMessage::value>() > 100)
                          .join<&AggMessage::sender>()
                          .count()
                          .select();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN (no results) + COUNT failed: " << result.error().message();
    EXPECT_EQ(result.value(), 0);
}

TEST_F(AggregateTest, WhereJoinRepeatedQueries) {
    insert_join_test_data();

    // Run same WHERE + JOIN + aggregate query multiple times (tests caching)
    for (int i = 0; i < 50; ++i) {
        auto result = msg_qs->where(storm::orm::where::field<^^AggMessage::value>() > 20)
                              .join<&AggMessage::sender>()
                              .count()
                              .select();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
        EXPECT_EQ(result.value(), 4); // 30, 40, 50, 60
    }
}

// =============================================================================
// GROUP BY Tests
// =============================================================================
// These tests verify GROUP BY functionality with various aggregates.
// Returns: plf::hive<std::tuple<GroupKeyTypes..., AggResultTypes...>>

TEST_F(AggregateTest, GroupByWithCount) {
    insert_test_data();

    // Group by years_experience and count
    // Each person has unique years_experience (3, 5, 7, 10, 15)
    auto result = qs->group_by<^^AggregatePerson::years_experience>().count().select();
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

TEST_F(AggregateTest, GroupByWithSum) {
    insert_test_data();

    // Group by years_experience and sum age
    auto result = qs->group_by<^^AggregatePerson::years_experience>().sum<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + SUM failed: " << result.error().message();

    EXPECT_EQ(result.value().size(), 5); // 5 unique years_experience values

    // Sum all ages across all groups - should equal total sum (25+30+35+40+45=175)
    int64_t total_sum = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [years, age_sum] : result.value()) {
        total_sum += age_sum;
    }
    EXPECT_EQ(total_sum, 175);
}

TEST_F(AggregateTest, GroupByWithAvg) {
    insert_test_data();

    // Group by years_experience and average salary
    auto result = qs->group_by<^^AggregatePerson::years_experience>().avg<^^AggregatePerson::salary>().select();
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

TEST_F(AggregateTest, GroupByWithMin) {
    insert_test_data();

    // Group by years_experience and min salary
    auto result = qs->group_by<^^AggregatePerson::years_experience>().min<^^AggregatePerson::salary>().select();
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

TEST_F(AggregateTest, GroupByWithMax) {
    insert_test_data();

    // Group by years_experience and max age
    auto result = qs->group_by<^^AggregatePerson::years_experience>().max<^^AggregatePerson::age>().select();
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

TEST_F(AggregateTest, GroupByWithWhere) {
    insert_test_data();

    // Filter by age > 30, then group by years_experience
    auto result = qs->where(storm::orm::where::field<^^AggregatePerson::age>() > 30)
                          .group_by<^^AggregatePerson::years_experience>()
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

TEST_F(AggregateTest, GroupByWithJoin) {
    insert_join_test_data();

    // Group messages by value (10, 20, 30, 40, 50, 60 - all unique)
    auto result = msg_qs->join<&AggMessage::sender>().group_by<^^AggMessage::value>().count().select();
    ASSERT_TRUE(result.has_value()) << "JOIN + GROUP BY failed: " << result.error().message();

    EXPECT_EQ(result.value().size(), 6); // 6 unique values

    // Verify each group has count 1 (all unique values)
    for (const auto& [value, count] : result.value()) {
        EXPECT_EQ(count, 1);
    }
}

TEST_F(AggregateTest, GroupByWithJoinAndSum) {
    insert_join_test_data();

    // Group messages by content, sum values
    // Each content is unique, so each group has one value
    auto result =
            msg_qs->join<&AggMessage::sender>().group_by<^^AggMessage::content>().sum<^^AggMessage::value>().select();
    ASSERT_TRUE(result.has_value()) << "JOIN + GROUP BY + SUM failed: " << result.error().message();

    EXPECT_EQ(result.value().size(), 6);

    // Sum of all values should equal 10+20+30+40+50+60=210
    int64_t total_sum = 0; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& [content, value_sum] : result.value()) {
        total_sum += value_sum;
    }
    EXPECT_EQ(total_sum, 210);
}

TEST_F(AggregateTest, GroupByWithWhereAndJoin) {
    insert_join_test_data();

    // Filter messages with value > 20, join with sender, group by value
    auto result = msg_qs->where(storm::orm::where::field<^^AggMessage::value>() > 20)
                          .join<&AggMessage::sender>()
                          .group_by<^^AggMessage::value>()
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

TEST_F(AggregateTest, GroupByRepeatedQueries) {
    insert_test_data();

    // Run same GROUP BY query multiple times (tests caching)
    for (int i = 0; i < 50; ++i) {
        auto result = qs->group_by<^^AggregatePerson::years_experience>().count().select();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
        EXPECT_EQ(result.value().size(), 5);
    }
}

TEST_F(AggregateTest, GroupByEmptyTable) {
    // Don't insert any data
    auto result = qs->group_by<^^AggregatePerson::years_experience>().count().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY on empty table failed: " << result.error().message();
    EXPECT_EQ(result.value().size(), 0);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(AggregateTest, GroupByFullChain_WhereJoinGroupByAggregate) {
    // This test verifies the full query chain: WHERE + JOIN + GROUP BY + aggregate
    // Setup: Create users and messages with multiple messages per user at varying values

    const auto& conn = QuerySet<AggregatePerson>::get_default_connection();

    // Insert users with different ages
    std::ignore = conn->execute("INSERT INTO AggUser (name, age) VALUES ('Alice', 25)");   // id=1
    std::ignore = conn->execute("INSERT INTO AggUser (name, age) VALUES ('Bob', 35)");     // id=2
    std::ignore = conn->execute("INSERT INTO AggUser (name, age) VALUES ('Charlie', 45)"); // id=3
    std::ignore = conn->execute("INSERT INTO AggUser (name, age) VALUES ('Dave', 30)");    // id=4

    // Insert messages with varying values
    // Alice (sender_id=1): 3 messages, values 10, 20, 30 (total=60, avg=20)
    // Bob (sender_id=2): 2 messages, values 50, 70 (total=120, avg=60)
    // Charlie (sender_id=3): 4 messages, values 5, 15, 25, 35 (total=80, avg=20)
    // Dave (sender_id=4): 1 message, value 100 (total=100, avg=100)
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('A1', 10, 1)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('A2', 20, 1)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('A3', 30, 1)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('B1', 50, 2)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('B2', 70, 2)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('C1', 5, 3)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('C2', 15, 3)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('C3', 25, 3)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('C4', 35, 3)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('D1', 100, 4)");

    // Test 1: WHERE + JOIN + GROUP BY + COUNT
    // Filter messages with value >= 20, group by value (since we can't group by FK directly), count per value
    // We'll group by value to test the full chain with filterable data
    auto count_result = msg_qs->where(storm::orm::where::field<^^AggMessage::value>() >= 20)
                                .join<&AggMessage::sender>()
                                .group_by<^^AggMessage::value>()
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
    msg_qs->reset();

    // Test 2: WHERE + JOIN + GROUP BY + SUM
    // Filter messages with value < 50, group by content (each unique)
    auto sum_result = msg_qs->where(storm::orm::where::field<^^AggMessage::value>() < 50)
                              .join<&AggMessage::sender>()
                              .group_by<^^AggMessage::content>()
                              .sum<^^AggMessage::value>()
                              .select();
    ASSERT_TRUE(sum_result.has_value()) << "Full chain SUM failed: " << sum_result.error().message();
    // Messages with value < 50: A1(10), A2(20), A3(30), C1(5), C2(15), C3(25), C4(35)
    // That's 7 messages, each with unique content
    EXPECT_EQ(sum_result.value().size(), 7) << "Expected 7 groups (7 messages with value < 50)";

    // Reset QuerySet state
    msg_qs->reset();

    // Test 3: WHERE + JOIN + GROUP BY + AVG
    // Filter messages with value between 10 and 70, group by value (each value unique, avg = value)
    auto avg_result = msg_qs->where(storm::orm::where::field<^^AggMessage::value>() >= 10 &&
                                    storm::orm::where::field<^^AggMessage::value>() <= 70)
                              .join<&AggMessage::sender>()
                              .group_by<^^AggMessage::value>()
                              .avg<^^AggMessage::value>()
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

TEST_F(AggregateTest, CountDistinctBasic) {
    insert_test_data();

    // Count distinct years_experience values (all unique: 3, 5, 7, 10, 15)
    auto result = qs->count_distinct<^^AggregatePerson::years_experience>().select();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT) failed: " << result.error().message();
    EXPECT_EQ(result.value(), 5); // 5 unique values
}

TEST_F(AggregateTest, CountDistinctAge) {
    insert_test_data();

    // Count distinct ages (all unique: 25, 30, 35, 40, 45)
    auto result = qs->count_distinct<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT age) failed: " << result.error().message();
    EXPECT_EQ(result.value(), 5);
}

TEST_F(AggregateTest, CountDistinctWithDuplicates) {
    // Insert data with duplicate ages
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Alice", .age = 30, .salary = 50000.0, .years_experience = 3});
    std::ignore = qs->insert(
            AggregatePerson{.id = 0, .name = "Bob", .age = 30, .salary = 60000.0, .years_experience = 5}
    ); // Same age as Alice
    std::ignore = qs->insert(
            AggregatePerson{.id = 0, .name = "Charlie", .age = 35, .salary = 70000.0, .years_experience = 7}
    );
    std::ignore = qs->insert(
            AggregatePerson{.id = 0, .name = "Dave", .age = 30, .salary = 80000.0, .years_experience = 10}
    ); // Same age as Alice and Bob
    std::ignore = qs->insert(
            AggregatePerson{.id = 0, .name = "Eve", .age = 35, .salary = 90000.0, .years_experience = 15}
    ); // Same age as Charlie

    auto result = qs->count_distinct<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT) with duplicates failed";
    EXPECT_EQ(result.value(), 2); // Only 2 unique ages (30, 35)
}

TEST_F(AggregateTest, CountDistinctWithWhere) {
    insert_test_data();

    // Count distinct ages where age > 30
    auto result = qs->where(storm::orm::where::field<^^AggregatePerson::age>() > 30)
                          .count_distinct<^^AggregatePerson::age>()
                          .select();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT) with WHERE failed";
    EXPECT_EQ(result.value(), 3); // 35, 40, 45
}

TEST_F(AggregateTest, CountDistinctWithJoin) {
    insert_join_test_data();

    // Count distinct message values
    auto result = msg_qs->join<&AggMessage::sender>().count_distinct<^^AggMessage::value>().select();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT) with JOIN failed";
    EXPECT_EQ(result.value(), 6); // 10, 20, 30, 40, 50, 60 - all unique
}

TEST_F(AggregateTest, CountDistinctEmptyTable) {
    // Don't insert any data
    auto result = qs->count_distinct<^^AggregatePerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT) on empty table failed";
    EXPECT_EQ(result.value(), 0);
}

TEST_F(AggregateTest, CountDistinctRepeatedQueries) {
    insert_test_data();

    // Run same COUNT(DISTINCT) query multiple times (tests caching)
    for (int i = 0; i < 50; ++i) {
        auto result = qs->count_distinct<^^AggregatePerson::years_experience>().select();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed";
        EXPECT_EQ(result.value(), 5);
    }
}

// =============================================================================
// NULL/Optional and Negative Number Tests
// =============================================================================

// Test model with optional field for NULL testing
struct OptionalPerson {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    std::optional<int>                        age; // Can be NULL
    double                                    salary;
};

class OptionalAggregateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        auto result = QuerySet<OptionalPerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to open database";

        const auto& conn = QuerySet<OptionalPerson>::get_default_connection();

        auto create_result = conn->execute(
                "CREATE TABLE OptionalPerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER, " // Nullable
                "salary REAL NOT NULL)"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table";

        qs = std::make_unique<QuerySet<OptionalPerson>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        QuerySet<OptionalPerson>::clear_default_connection();
    }

    // Protected member is standard GoogleTest fixture pattern for test case access
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    std::unique_ptr<QuerySet<OptionalPerson>> qs; // NOSONAR(cpp:S3656) - test fixture pattern
};

TEST_F(OptionalAggregateTest, CountWithNullValues) {
    // Insert with NULL ages
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Alice", .age = 25, .salary = 50000.0});
    std::ignore =
            qs->insert(OptionalPerson{.id = 0, .name = "Bob", .age = std::nullopt, .salary = 60000.0}); // NULL age
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Charlie", .age = 35, .salary = 70000.0});
    std::ignore =
            qs->insert(OptionalPerson{.id = 0, .name = "Dave", .age = std::nullopt, .salary = 80000.0}); // NULL age

    // COUNT(*) includes all rows
    auto count_all = qs->count().select();
    ASSERT_TRUE(count_all.has_value());
    EXPECT_EQ(count_all.value(), 4);

    // COUNT(age) excludes NULL values
    auto count_age = qs->count<^^OptionalPerson::age>().select();
    ASSERT_TRUE(count_age.has_value());
    EXPECT_EQ(count_age.value(), 2); // Only Alice (25) and Charlie (35)
}

TEST_F(OptionalAggregateTest, SumWithNullValues) {
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Alice", .age = 25, .salary = 50000.0});
    std::ignore =
            qs->insert(OptionalPerson{.id = 0, .name = "Bob", .age = std::nullopt, .salary = 60000.0}); // NULL age
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Charlie", .age = 35, .salary = 70000.0});

    auto sum = qs->sum<^^OptionalPerson::age>().select();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), 60); // 25 + 35, NULL ignored
}

TEST_F(OptionalAggregateTest, AvgWithNullValues) {
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Alice", .age = 20, .salary = 50000.0});
    std::ignore =
            qs->insert(OptionalPerson{.id = 0, .name = "Bob", .age = std::nullopt, .salary = 60000.0}); // NULL age
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Charlie", .age = 40, .salary = 70000.0});

    auto avg = qs->avg<^^OptionalPerson::age>().select();
    ASSERT_TRUE(avg.has_value());
    EXPECT_NEAR(avg.value(), 30.0, 0.01); // (20 + 40) / 2 = 30, NULL ignored
}

TEST_F(OptionalAggregateTest, MinMaxWithNullValues) {
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Alice", .age = 25, .salary = 50000.0});
    std::ignore =
            qs->insert(OptionalPerson{.id = 0, .name = "Bob", .age = std::nullopt, .salary = 60000.0}); // NULL age
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Charlie", .age = 45, .salary = 70000.0});
    std::ignore =
            qs->insert(OptionalPerson{.id = 0, .name = "Dave", .age = std::nullopt, .salary = 80000.0}); // NULL age

    auto min_val = qs->min<^^OptionalPerson::age>().select();
    ASSERT_TRUE(min_val.has_value());
    EXPECT_NEAR(min_val.value(), 25.0, 0.01);

    auto max_val = qs->max<^^OptionalPerson::age>().select();
    ASSERT_TRUE(max_val.has_value());
    EXPECT_NEAR(max_val.value(), 45.0, 0.01);
}

TEST_F(OptionalAggregateTest, CountDistinctWithNullValues) {
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Alice", .age = 30, .salary = 50000.0});
    std::ignore =
            qs->insert(OptionalPerson{.id = 0, .name = "Bob", .age = std::nullopt, .salary = 60000.0}); // NULL age
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Charlie", .age = 30, .salary = 70000.0}); // Same as Alice
    std::ignore =
            qs->insert(OptionalPerson{.id = 0, .name = "Dave", .age = std::nullopt, .salary = 80000.0}); // NULL age
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Eve", .age = 40, .salary = 90000.0});

    // COUNT(DISTINCT age) - NULL excluded, should count 2 (30, 40)
    auto result = qs->count_distinct<^^OptionalPerson::age>().select();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 2);
}

TEST_F(OptionalAggregateTest, GroupByWithAllNullValuesInGroupColumn) {
    // Insert rows where all ages are NULL
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Alice", .age = std::nullopt, .salary = 50000.0});
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Bob", .age = std::nullopt, .salary = 60000.0});
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Charlie", .age = std::nullopt, .salary = 70000.0});

    // GROUP BY on age (all NULL) - should produce one group with NULL key
    // SQLite treats NULL as a distinct group key in GROUP BY
    auto result = qs->group_by<^^OptionalPerson::age>().count().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY with all NULL values failed";

    // Should have exactly one group (the NULL group) with count 3
    EXPECT_EQ(result.value().size(), 1);

    // Verify the single group has count 3
    auto& groups = result.value();
    auto  it     = groups.begin();
    ASSERT_NE(it, groups.end());
    auto [age_key, count_val] = *it;
    // age_key is std::optional<int> - should be nullopt for the NULL group
    EXPECT_FALSE(age_key.has_value()) << "Expected NULL group key";
    EXPECT_EQ(count_val, 3) << "Expected count of 3 in NULL group";
}

TEST_F(OptionalAggregateTest, GroupByWithMixedNullAndNonNullValues) {
    // Insert mix of NULL and non-NULL ages
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Alice", .age = 25, .salary = 50000.0});
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Bob", .age = std::nullopt, .salary = 60000.0});
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Charlie", .age = 25, .salary = 70000.0});
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Dave", .age = std::nullopt, .salary = 80000.0});
    std::ignore = qs->insert(OptionalPerson{.id = 0, .name = "Eve", .age = 30, .salary = 90000.0});

    // GROUP BY on age - should produce 3 groups: NULL (2 rows), 25 (2 rows), 30 (1 row)
    auto result = qs->group_by<^^OptionalPerson::age>().count().select();
    ASSERT_TRUE(result.has_value()) << "GROUP BY with mixed NULL values failed";

    EXPECT_EQ(result.value().size(), 3) << "Expected 3 groups (NULL, 25, 30)";

    // Verify counts for each group
    int64_t null_count   = 0; // NOLINT(misc-const-correctness) - modified in loop
    int64_t age_25_count = 0; // NOLINT(misc-const-correctness) - modified in loop
    int64_t age_30_count = 0; // NOLINT(misc-const-correctness) - modified in loop

    for (const auto& [age_key, count_val] : result.value()) {
        if (!age_key.has_value()) {
            null_count = count_val;
        } else if (age_key.value() == 25) {
            age_25_count = count_val;
        } else if (age_key.value() == 30) {
            age_30_count = count_val;
        }
    }

    EXPECT_EQ(null_count, 2) << "Expected 2 rows in NULL group";
    EXPECT_EQ(age_25_count, 2) << "Expected 2 rows in age=25 group";
    EXPECT_EQ(age_30_count, 1) << "Expected 1 row in age=30 group";
}

// Negative number tests using the main AggregatePerson model
TEST_F(AggregateTest, NegativeNumbersInSum) {
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3});
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Bob", .age = 5, .salary = 60000.0, .years_experience = 5});
    std::ignore = qs->insert(
            AggregatePerson{.id = 0, .name = "Charlie", .age = -3, .salary = 70000.0, .years_experience = 7}
    );

    auto sum = qs->sum<^^AggregatePerson::age>().select();
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(sum.value(), -8); // -10 + 5 + (-3) = -8
}

TEST_F(AggregateTest, NegativeNumbersInAvg) {
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Alice", .age = -12, .salary = 50000.0, .years_experience = 3});
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Bob", .age = 6, .salary = 60000.0, .years_experience = 5});
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Charlie", .age = 0, .salary = 70000.0, .years_experience = 7});

    auto avg = qs->avg<^^AggregatePerson::age>().select();
    ASSERT_TRUE(avg.has_value());
    EXPECT_NEAR(avg.value(), -2.0, 0.01); // (-12 + 6 + 0) / 3 = -2
}

TEST_F(AggregateTest, NegativeNumbersInMinMax) {
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3});
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Bob", .age = 5, .salary = 60000.0, .years_experience = 5});
    std::ignore = qs->insert(
            AggregatePerson{.id = 0, .name = "Charlie", .age = -20, .salary = 70000.0, .years_experience = 7}
    );
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Dave", .age = 15, .salary = 80000.0, .years_experience = 10});

    auto min_val = qs->min<^^AggregatePerson::age>().select();
    ASSERT_TRUE(min_val.has_value());
    EXPECT_NEAR(min_val.value(), -20.0, 0.01);

    auto max_val = qs->max<^^AggregatePerson::age>().select();
    ASSERT_TRUE(max_val.has_value());
    EXPECT_NEAR(max_val.value(), 15.0, 0.01);
}

TEST_F(AggregateTest, NegativeNumbersInCount) {
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3});
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Bob", .age = -5, .salary = 60000.0, .years_experience = 5});
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Charlie", .age = 0, .salary = 70000.0, .years_experience = 7});

    // Count should still count all rows regardless of negative values
    auto count = qs->count().select();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 3);

    // Count with field also counts negative values
    auto count_age = qs->count<^^AggregatePerson::age>().select();
    ASSERT_TRUE(count_age.has_value());
    EXPECT_EQ(count_age.value(), 3);
}

TEST_F(AggregateTest, NegativeNumbersInWhere) {
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Alice", .age = -10, .salary = 50000.0, .years_experience = 3});
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Bob", .age = -5, .salary = 60000.0, .years_experience = 5});
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Charlie", .age = 0, .salary = 70000.0, .years_experience = 7});
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Dave", .age = 5, .salary = 80000.0, .years_experience = 10});
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Eve", .age = 10, .salary = 90000.0, .years_experience = 15});

    // Count where age < 0
    auto result = qs->where(storm::orm::where::field<^^AggregatePerson::age>() < 0).count().select();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 2); // Alice (-10) and Bob (-5)

    // Sum of negative ages
    auto sum_neg =
            qs->where(storm::orm::where::field<^^AggregatePerson::age>() < 0).sum<^^AggregatePerson::age>().select();
    ASSERT_TRUE(sum_neg.has_value());
    EXPECT_EQ(sum_neg.value(), -15); // -10 + (-5) = -15
}

// =============================================================================
// Combined Clause Tests
// =============================================================================

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(AggregateTest, FullChain_WhereJoinOrderByLimitOffset) {
    // This test verifies the complete SELECT query chain:
    // WHERE + JOIN + ORDER BY + LIMIT + OFFSET
    // Note: GROUP BY doesn't currently support ORDER BY/LIMIT/OFFSET in the builder pattern
    // Setup: Create users and messages with multiple messages per user

    const auto& conn = QuerySet<AggregatePerson>::get_default_connection();

    // Insert users with different ages
    std::ignore = conn->execute("INSERT INTO AggUser (name, age) VALUES ('Alice', 25)");   // id=1
    std::ignore = conn->execute("INSERT INTO AggUser (name, age) VALUES ('Bob', 35)");     // id=2
    std::ignore = conn->execute("INSERT INTO AggUser (name, age) VALUES ('Charlie', 45)"); // id=3
    std::ignore = conn->execute("INSERT INTO AggUser (name, age) VALUES ('Dave', 30)");    // id=4
    std::ignore = conn->execute("INSERT INTO AggUser (name, age) VALUES ('Eve', 28)");     // id=5

    // Insert messages with varying values
    // Alice (sender_id=1): 3 messages, values 10, 15, 20
    // Bob (sender_id=2): 2 messages, values 25, 30
    // Charlie (sender_id=3): 4 messages, values 35, 40, 45, 50
    // Dave (sender_id=4): 2 messages, values 55, 60
    // Eve (sender_id=5): 3 messages, values 65, 70, 75
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('A1', 10, 1)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('A2', 15, 1)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('A3', 20, 1)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('B1', 25, 2)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('B2', 30, 2)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('C1', 35, 3)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('C2', 40, 3)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('C3', 45, 3)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('C4', 50, 3)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('D1', 55, 4)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('D2', 60, 4)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('E1', 65, 5)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('E2', 70, 5)");
    std::ignore = conn->execute("INSERT INTO AggMessage (content, value, sender_id) VALUES ('E3', 75, 5)");

    // Test: WHERE + JOIN + ORDER BY + LIMIT + OFFSET (SELECT, not GROUP BY)
    // Filter messages with value >= 20, order by value DESC, limit 5, offset 2
    // Values >= 20: 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75 (12 values)
    // Ordered DESC: 75, 70, 65, 60, 55, 50, 45, 40, 35, 30, 25, 20
    // After OFFSET 2: 65, 60, 55, 50, 45, 40, 35, 30, 25, 20 (skip first 2)
    // After LIMIT 5: 65, 60, 55, 50, 45 (take first 5)
    auto result = msg_qs->where(storm::orm::where::field<^^AggMessage::value>() >= 20)
                          .join<&AggMessage::sender>()
                          .order_by<^^AggMessage::value, false>()
                          .limit(5)
                          .offset(2)
                          .select();

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
    msg_qs->reset();

    // Take first 3 messages with value >= 20, ordered ASC
    auto result2 = msg_qs->where(storm::orm::where::field<^^AggMessage::value>() >= 20)
                           .join<&AggMessage::sender>()
                           .order_by<^^AggMessage::value, true>() // ASC this time
                           .limit(3)
                           .offset(0)
                           .select();

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
    msg_qs->reset();
    auto result3 =
            msg_qs->where(storm::orm::where::field<^^AggMessage::value>() == 75).join<&AggMessage::sender>().select();

    ASSERT_TRUE(result3.has_value()) << "JOIN verification query failed";
    EXPECT_EQ(result3.value().size(), 1) << "Expected 1 message with value 75";
    EXPECT_EQ(result3.value().begin()->sender.name, "Eve") << "Sender of value=75 should be Eve";
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(AggregateTest, GroupByWithAllAggregateTypes) {
    // Test all aggregate types (SUM, COUNT, AVG, MIN, MAX) with GROUP BY
    // Note: Current implementation supports one aggregate per query with GROUP BY
    // This test verifies each aggregate type works correctly with GROUP BY

    // Insert test data with multiple people having the same years_experience
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Alice", .age = 25, .salary = 50000.0, .years_experience = 5});
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Bob", .age = 30, .salary = 60000.0, .years_experience = 5});
    std::ignore = qs->insert(
            AggregatePerson{.id = 0, .name = "Charlie", .age = 35, .salary = 70000.0, .years_experience = 10}
    );
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Dave", .age = 40, .salary = 80000.0, .years_experience = 10});
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Eve", .age = 45, .salary = 90000.0, .years_experience = 10});
    std::ignore =
            qs->insert(AggregatePerson{.id = 0, .name = "Frank", .age = 28, .salary = 55000.0, .years_experience = 5});

    // Test data summary by years_experience:
    //   years_experience=5: Alice(25), Bob(30), Frank(28) -> count=3, sum=83, avg=27.67, min=25, max=30
    //   years_experience=10: Charlie(35), Dave(40), Eve(45) -> count=3, sum=120, avg=40, min=35, max=45

    // Test 1: GROUP BY with COUNT
    auto count_result = qs->group_by<^^AggregatePerson::years_experience>().count().select();
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
    qs->reset();
    auto sum_result = qs->group_by<^^AggregatePerson::years_experience>().sum<^^AggregatePerson::age>().select();
    ASSERT_TRUE(sum_result.has_value()) << "GROUP BY + SUM failed: " << sum_result.error().message();
    for (const auto& [years_exp, sum_age] : sum_result.value()) {
        if (years_exp == 5) {
            EXPECT_EQ(sum_age, 83) << "Sum of ages for group 5: 25+30+28=83";
        } else if (years_exp == 10) {
            EXPECT_EQ(sum_age, 120) << "Sum of ages for group 10: 35+40+45=120";
        }
    }

    // Test 3: GROUP BY with AVG
    qs->reset();
    auto avg_result = qs->group_by<^^AggregatePerson::years_experience>().avg<^^AggregatePerson::age>().select();
    ASSERT_TRUE(avg_result.has_value()) << "GROUP BY + AVG failed: " << avg_result.error().message();
    for (const auto& [years_exp, avg_age] : avg_result.value()) {
        if (years_exp == 5) {
            EXPECT_NEAR(avg_age, 27.67, 0.01) << "Avg of ages for group 5: 83/3≈27.67";
        } else if (years_exp == 10) {
            EXPECT_NEAR(avg_age, 40.0, 0.01) << "Avg of ages for group 10: 120/3=40";
        }
    }

    // Test 4: GROUP BY with MIN
    qs->reset();
    auto min_result = qs->group_by<^^AggregatePerson::years_experience>().min<^^AggregatePerson::age>().select();
    ASSERT_TRUE(min_result.has_value()) << "GROUP BY + MIN failed: " << min_result.error().message();
    for (const auto& [years_exp, min_age] : min_result.value()) {
        if (years_exp == 5) {
            EXPECT_NEAR(min_age, 25.0, 0.01) << "Min age for group 5 is 25";
        } else if (years_exp == 10) {
            EXPECT_NEAR(min_age, 35.0, 0.01) << "Min age for group 10 is 35";
        }
    }

    // Test 5: GROUP BY with MAX
    qs->reset();
    auto max_result = qs->group_by<^^AggregatePerson::years_experience>().max<^^AggregatePerson::age>().select();
    ASSERT_TRUE(max_result.has_value()) << "GROUP BY + MAX failed: " << max_result.error().message();
    for (const auto& [years_exp, max_age] : max_result.value()) {
        if (years_exp == 5) {
            EXPECT_NEAR(max_age, 30.0, 0.01) << "Max age for group 5 is 30";
        } else if (years_exp == 10) {
            EXPECT_NEAR(max_age, 45.0, 0.01) << "Max age for group 10 is 45";
        }
    }

    // Test 6: GROUP BY with WHERE + SUM (combined clauses)
    qs->reset();
    auto filtered_sum = qs->where(storm::orm::where::field<^^AggregatePerson::years_experience>() == 5)
                                .group_by<^^AggregatePerson::years_experience>()
                                .sum<^^AggregatePerson::age>()
                                .select();
    ASSERT_TRUE(filtered_sum.has_value()) << "WHERE + GROUP BY + SUM failed";
    EXPECT_EQ(filtered_sum.value().size(), 1) << "Expected 1 group after WHERE filter";
    const auto& [years_exp, sum_age] = *filtered_sum.value().begin();
    EXPECT_EQ(years_exp, 5);
    EXPECT_EQ(sum_age, 83);
}

// Note: main() is provided by main.cpp (shared across all test files)

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
