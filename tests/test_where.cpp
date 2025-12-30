#include <gtest/gtest.h>

import storm;
import <string>;
import <vector>;
import <expected>;
import <optional>;

using namespace storm;
using namespace storm::orm::where;

// Test model for WHERE clause operations (matching test_select.cpp)
struct WherePerson {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               name;
    int                                       age{};
};

// Test fixture for WHERE operations
class WhereTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        // Set up in-memory SQLite database
        auto result = QuerySet<WherePerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<WherePerson>::get_default_connection();

        // Create table with AUTOINCREMENT (matching test_select.cpp)
        auto create_result = conn->execute(
                "CREATE TABLE WherePerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        // Insert test data (use ID 0 to let AUTOINCREMENT generate IDs)
        std::vector<WherePerson> const people =
                {{0, "Alice", 30}, {0, "Bob", 25}, {0, "Charlie", 35}, {0, "Diana", 28}, {0, "Eve", 40}};

        QuerySet<WherePerson> queryset;
        // Insert one by one to avoid batch insert issues
        for (const auto& person : people) {
            auto insert_result = queryset.insert(person);
            ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data: " << insert_result.error().message();
        }
    }

    auto TearDown() -> void override {
        QuerySet<WherePerson>::clear_default_connection();
    }
};

// Test: WHERE with single integer condition
TEST_F(WhereTest, WhereSingleIntegerCondition) {
    QuerySet<WherePerson> queryset;

    auto result = queryset.where(field<^^WherePerson::age>() == 30).select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1) << "Expected 1 person with age 30";
    auto it = people.begin();
    EXPECT_EQ(it->name, "Alice");
    EXPECT_EQ(it->age, 30);
}

// Test: WHERE with greater than condition
TEST_F(WhereTest, WhereGreaterThan) {
    QuerySet<WherePerson> queryset;

    auto result = queryset.where(field<^^WherePerson::age>() > 30).select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 2) << "Expected 2 people with age > 30";
    auto it = people.begin();
    EXPECT_EQ(it->name, "Charlie");
    EXPECT_EQ(it->age, 35);
    ++it;
    EXPECT_EQ(it->name, "Eve");
    EXPECT_EQ(it->age, 40);
}

// Test: WHERE with less than condition
TEST_F(WhereTest, WhereLessThan) {
    QuerySet<WherePerson> queryset;

    auto result = queryset.where(field<^^WherePerson::age>() < 30).select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 2) << "Expected 2 people with age < 30";
    auto it = people.begin();
    EXPECT_EQ(it->name, "Bob");
    EXPECT_EQ(it->age, 25);
    ++it;
    EXPECT_EQ(it->name, "Diana");
    EXPECT_EQ(it->age, 28);
}

// Test: WHERE with string condition
TEST_F(WhereTest, WhereStringCondition) {
    QuerySet<WherePerson> queryset;

    auto result = queryset.where(field<^^WherePerson::name>() == "Bob").select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1) << "Expected 1 person named Bob";
    auto it = people.begin();
    EXPECT_EQ(it->name, "Bob");
    EXPECT_EQ(it->age, 25);
}

// Test: WHERE with LIKE pattern
TEST_F(WhereTest, WhereLikePattern) {
    QuerySet<WherePerson> queryset;

    auto result = queryset.where(field<^^WherePerson::name>().like("A%")).select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1) << "Expected 1 person with name starting with 'A'";
    EXPECT_EQ(people.begin()->name, "Alice");
}

// Test: WHERE with multiple conditions (AND logic)
TEST_F(WhereTest, WhereMultipleConditions) {
    QuerySet<WherePerson> queryset;

    auto expr1  = field<^^WherePerson::age>() > 25;
    auto expr2  = field<^^WherePerson::age>() < 40;
    auto result = queryset.where(and_(expr1, expr2)).select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 3) << "Expected 3 people with age between 26 and 39";
    auto it = people.begin();
    EXPECT_EQ(it->name, "Alice");
    ++it;
    EXPECT_EQ(it->name, "Charlie");
    ++it;
    EXPECT_EQ(it->name, "Diana");
}

// Test: WHERE with three conditions
TEST_F(WhereTest, WhereThreeConditions) {
    QuerySet<WherePerson> queryset;

    auto expr1  = field<^^WherePerson::age>() >= 28;
    auto expr2  = field<^^WherePerson::age>() <= 35;
    auto expr3  = field<^^WherePerson::name>() != "Charlie";
    auto result = queryset.where(and_(and_(expr1, expr2), expr3)).select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 2) << "Expected 2 people matching all three conditions";
    auto it = people.begin();
    EXPECT_EQ(it->name, "Alice");
    ++it;
    EXPECT_EQ(it->name, "Diana");
}

// Test: WHERE with BETWEEN clause
TEST_F(WhereTest, WhereBetween) {
    QuerySet<WherePerson> queryset;

    auto result = queryset.where(field<^^WherePerson::age>().between(28, 35)).select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 3) << "Expected 3 people with age between 28 and 35";
    auto it = people.begin();
    EXPECT_EQ(it->name, "Alice");
    ++it;
    EXPECT_EQ(it->name, "Charlie");
    ++it;
    EXPECT_EQ(it->name, "Diana");
}

// Test: WHERE with IN clause (multiple parameters)
TEST_F(WhereTest, WhereIn) {
    QuerySet<WherePerson> queryset;

    // IN expressions use runtime builder pattern, requires .select()
    auto result = queryset.where(field<^^WherePerson::age>().in(25, 30, 40)).select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 3) << "Expected 3 people with age in (25, 30, 40)";
    auto it = people.begin();
    EXPECT_EQ(it->name, "Alice");
    ++it;
    EXPECT_EQ(it->name, "Bob");
    ++it;
    EXPECT_EQ(it->name, "Eve");
}

// Test: WHERE returning empty result
TEST_F(WhereTest, WhereNoMatch) {
    QuerySet<WherePerson> queryset;

    auto result = queryset.where(field<^^WherePerson::age>() > 100).select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    EXPECT_TRUE(people.empty()) << "Expected no results for age > 100";
}

// Test: WHERE returning all rows
TEST_F(WhereTest, WhereMatchesAll) {
    QuerySet<WherePerson> queryset;

    auto result = queryset.where(field<^^WherePerson::age>() > 0).select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 5) << "Expected all 5 people with age > 0";
}

// Test: WHERE with complex expression
TEST_F(WhereTest, WhereComplexExpression) {
    QuerySet<WherePerson> queryset;

    auto expr1  = field<^^WherePerson::age>() < 30;
    auto expr2  = field<^^WherePerson::age>() > 35;
    auto expr3  = field<^^WherePerson::name>() != "Charlie";
    auto result = queryset.where(and_(or_(expr1, expr2), expr3)).select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 3) << "Expected 3 people matching complex condition";
    auto it = people.begin();
    EXPECT_EQ(it->name, "Bob");
    ++it;
    EXPECT_EQ(it->name, "Diana");
    ++it;
    EXPECT_EQ(it->name, "Eve");
}

// Test: WHERE state persists after select() - enables query reusability
TEST_F(WhereTest, WherePreservesStateAfterSelect) {
    QuerySet<WherePerson> queryset;

    // Set base filter
    queryset.where(field<^^WherePerson::age>() >= 25);

    // First query uses the filter
    auto result1 = queryset.select();
    ASSERT_TRUE(result1.has_value());
    ASSERT_EQ(result1.value().size(), 5) << "Expected 5 people with age >= 25";

    // Second query still has the same filter (state preserved)
    auto result2 = queryset.select();
    ASSERT_TRUE(result2.has_value());
    ASSERT_EQ(result2.value().size(), 5) << "State should persist after first select()";

    // Add more conditions - they accumulate
    queryset.where(field<^^WherePerson::age>() < 40);
    auto result3 = queryset.select();
    ASSERT_TRUE(result3.has_value());
    ASSERT_EQ(result3.value().size(), 4) << "Expected 4 people with age >= 25 AND age < 40";

    // reset() clears all state
    queryset.reset();
    auto result4 = queryset.select();
    ASSERT_TRUE(result4.has_value());
    ASSERT_EQ(result4.value().size(), 5) << "After reset() should select all rows";
}

// Test: WHERE with std::string_view parameter
TEST_F(WhereTest, WhereStringView) {
    QuerySet<WherePerson> queryset;

    std::string_view const name_view = "Charlie";
    auto                   result    = queryset.where(field<^^WherePerson::name>() == name_view).select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1);
    EXPECT_EQ(people.begin()->name, "Charlie");
}

// Test: WHERE with const char* parameter
TEST_F(WhereTest, WhereConstCharPtr) {
    QuerySet<WherePerson> queryset;

    const char* name_ptr = "Bob";
    auto        result   = queryset.where(field<^^WherePerson::name>() == name_ptr).select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1);
    auto it = people.begin();
    EXPECT_EQ(it->name, "Bob");
    EXPECT_EQ(it->age, 25);
}

// Test: WHERE with mixed parameter types
TEST_F(WhereTest, WhereMixedTypes) {
    QuerySet<WherePerson> queryset;

    auto expr1  = field<^^WherePerson::name>() == "Diana";
    auto expr2  = field<^^WherePerson::age>() == 28;
    auto result = queryset.where(and_(expr1, expr2)).select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1);
    EXPECT_EQ(people.begin()->name, "Diana");
}

// Test model with FK for JOIN + WHERE tests
// Note: Named WhereUser/WhereMessage to avoid ODR violation with test_fk_fields.cpp
struct WhereUser {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               username;
    int                                       level{};
};

struct WhereMessage {
    [[= storm::meta::FieldAttr::primary]] int  id{};
    std::string                                content;
    [[= storm::meta::FieldAttr::fk]] WhereUser sender;
};

// Test fixture for WHERE + JOIN operations
class WhereJoinTest : public ::testing::Test {
  protected:
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    auto SetUp() -> void override {
        auto result = QuerySet<WhereMessage>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());

        const auto& conn = QuerySet<WhereMessage>::get_default_connection();

        // Create WhereUser table
        auto create_user = conn->execute(
                "CREATE TABLE WhereUser ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "username TEXT NOT NULL, "
                "level INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_user.has_value());

        // Create WhereMessage table with FK
        auto create_message = conn->execute(
                "CREATE TABLE WhereMessage ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "content TEXT NOT NULL, "
                "sender_id INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_message.has_value());

        // Insert users (use ID 0 to let AUTOINCREMENT generate IDs)
        QuerySet<WhereUser> user_qs;
        auto                alice_id = user_qs.insert(WhereUser{.id = 0, .username = "alice", .level = 10});
        ASSERT_TRUE(alice_id.has_value()) << "Failed to insert alice: " << alice_id.error().message();

        auto bob_id = user_qs.insert(WhereUser{.id = 0, .username = "bob", .level = 5});
        ASSERT_TRUE(bob_id.has_value()) << "Failed to insert bob: " << bob_id.error().message();

        auto charlie_id = user_qs.insert(WhereUser{.id = 0, .username = "charlie", .level = 15});
        ASSERT_TRUE(charlie_id.has_value()) << "Failed to insert charlie: " << charlie_id.error().message();

        // Insert messages (use generated user IDs)
        QuerySet<WhereMessage> msg_qs;
        auto                   msg1 = msg_qs.insert(
                WhereMessage{
                                          .id      = 0,
                                          .content = "Hello from Alice",
                                          .sender = WhereUser{.id = static_cast<int>(alice_id.value()), .username = "", .level = 0}
                }
        );
        ASSERT_TRUE(msg1.has_value()) << "Failed to insert message 1: " << msg1.error().message();

        auto msg2 = msg_qs.insert(
                WhereMessage{
                        .id      = 0,
                        .content = "Hi from Bob",
                        .sender  = WhereUser{.id = static_cast<int>(bob_id.value()), .username = "", .level = 0}
                }
        );
        ASSERT_TRUE(msg2.has_value()) << "Failed to insert message 2: " << msg2.error().message();

        auto msg3 = msg_qs.insert(
                WhereMessage{
                        .id      = 0,
                        .content = "Greetings from Alice",
                        .sender  = WhereUser{.id = static_cast<int>(alice_id.value()), .username = "", .level = 0}
                }
        );
        ASSERT_TRUE(msg3.has_value()) << "Failed to insert message 3: " << msg3.error().message();

        auto msg4 = msg_qs.insert(
                WhereMessage{
                        .id      = 0,
                        .content = "Greetings Charlie",
                        .sender  = WhereUser{.id = static_cast<int>(charlie_id.value()), .username = "", .level = 0}
                }
        );
        ASSERT_TRUE(msg4.has_value()) << "Failed to insert message 4: " << msg4.error().message();
    }

    auto TearDown() -> void override {
        QuerySet<WhereMessage>::clear_default_connection();
    }
};

// Test: WHERE with JOIN
TEST_F(WhereJoinTest, WhereWithJoin) {
    QuerySet<WhereMessage> queryset;

    // SELECT with JOIN and WHERE filtering by WhereUser.level
    auto result = queryset.join<&WhereMessage::sender>().where(field<^^WhereUser::level>() >= 10).select();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN failed: " << result.error().message();

    const auto& messages = result.value();
    ASSERT_EQ(messages.size(), 3) << "Expected 3 messages from users with level >= 10";

    // Verify sender information is populated
    auto it = messages.begin();
    EXPECT_EQ(it->sender.username, "alice");
    EXPECT_EQ(it->sender.level, 10);
    ++it;
    EXPECT_EQ(it->sender.username, "alice");
    ++it;
    EXPECT_EQ(it->sender.username, "charlie");
    EXPECT_EQ(it->sender.level, 15);
}

// Test: WHERE with JOIN and content filter
TEST_F(WhereJoinTest, WhereWithJoinContentFilter) {
    QuerySet<WhereMessage> queryset;

    auto result =
            queryset.join<&WhereMessage::sender>().where(field<^^WhereMessage::content>().like("%Alice%")).select();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN failed: " << result.error().message();

    const auto& messages = result.value();
    ASSERT_EQ(messages.size(), 2) << "Expected 2 messages with 'Alice' in content";
}

// Test: WHERE with JOIN and multiple conditions
TEST_F(WhereJoinTest, WhereWithJoinMultipleConditions) {
    QuerySet<WhereMessage> queryset;

    auto expr1  = field<^^WhereUser::level>() > 5;
    auto expr2  = field<^^WhereMessage::content>().like("%from%");
    auto result = queryset.join<&WhereMessage::sender>().where(and_(expr1, expr2)).select();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN failed: " << result.error().message();

    const auto& messages = result.value();
    ASSERT_EQ(messages.size(), 2) << "Expected 2 messages matching both conditions";
}

// Test: Natural && and || operators (and/or keywords)
TEST_F(WhereJoinTest, WhereWithNaturalOperators) {
    QuerySet<WhereMessage> queryset;

    // Using natural 'and' operator (also works with &&)
    auto result = queryset.join<&WhereMessage::sender>()
                          .where(field<^^WhereUser::level>() > 5 and field<^^WhereMessage::content>().like("%from%"))
                          .select();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN failed: " << result.error().message();

    const auto& messages = result.value();
    ASSERT_EQ(messages.size(), 2) << "Expected 2 messages matching both conditions";
    auto it = messages.begin();
    EXPECT_EQ(it->sender.username, "alice");
    ++it;
    EXPECT_EQ(it->sender.username, "alice");
}

// Test: Natural operators with complex expressions
TEST_F(WhereTest, WhereNaturalOperatorsComplex) {
    QuerySet<WherePerson> queryset;

    // Using natural 'and' and 'or' operators
    auto result = queryset.where((field<^^WherePerson::age>() < 30 or field<^^WherePerson::age>() > 35) and
                                 field<^^WherePerson::name>() != "Charlie")
                          .select();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 3) << "Expected 3 people matching complex condition";
    auto it = people.begin();
    EXPECT_EQ(it->name, "Bob");
    ++it;
    EXPECT_EQ(it->name, "Diana");
    ++it;
    EXPECT_EQ(it->name, "Eve");
}

// Test: Reusing WHERE conditions across multiple queries
TEST_F(WhereTest, WhereReuseCondition) {
    // Store condition in variable
    auto age_condition = field<^^WherePerson::age>() > 25;

    // First use - should return 4 people (Alice=30, Charlie=35, Diana=28, Eve=40)
    QuerySet<WherePerson> queryset1;
    auto                  result1 = queryset1.where(age_condition).select();
    ASSERT_TRUE(result1.has_value()) << "First WHERE failed: " << result1.error().message();
    EXPECT_EQ(result1.value().size(), 4) << "Expected 4 people with age > 25";

    // Reuse same condition with fresh QuerySet - should return same results
    QuerySet<WherePerson> queryset2;
    auto                  result2 = queryset2.where(age_condition).select();
    ASSERT_TRUE(result2.has_value()) << "Second WHERE failed: " << result2.error().message();
    EXPECT_EQ(result2.value().size(), 4) << "Expected 4 people with age > 25 (reused condition)";

    // Combine reused condition with new one
    QuerySet<WherePerson> queryset3;
    auto                  result3 = queryset3.where(age_condition and field<^^WherePerson::name>() == "Alice").select();
    ASSERT_TRUE(result3.has_value()) << "Combined WHERE failed: " << result3.error().message();
    ASSERT_EQ(result3.value().size(), 1) << "Expected 1 person matching both conditions";
    auto it = result3.value().begin();
    EXPECT_EQ(it->name, "Alice");
    EXPECT_EQ(it->age, 30);

    // Reuse the original condition again with fresh QuerySet
    QuerySet<WherePerson> queryset4;
    auto                  result4 = queryset4.where(age_condition).select();
    ASSERT_TRUE(result4.has_value()) << "Third WHERE failed: " << result4.error().message();
    EXPECT_EQ(result4.value().size(), 4) << "Expected 4 people with age > 25 (reused after combining)";
}

// Test: Building composable filter library
TEST_F(WhereTest, WhereComposableFilters) {
    // Create reusable filter components
    auto young_filter       = field<^^WherePerson::age>() < 30;
    auto old_filter         = field<^^WherePerson::age>() >= 35;
    auto name_starts_with_a = field<^^WherePerson::name>().like("A%");

    // Use filters independently with fresh QuerySets
    QuerySet<WherePerson> qs1;
    auto                  young_people = qs1.where(young_filter).select();
    ASSERT_TRUE(young_people.has_value());
    EXPECT_EQ(young_people.value().size(), 2) << "Expected 2 young people (Bob=25, Diana=28)";

    QuerySet<WherePerson> qs2;
    auto                  old_people = qs2.where(old_filter).select();
    ASSERT_TRUE(old_people.has_value());
    EXPECT_EQ(old_people.value().size(), 2) << "Expected 2 old people (Charlie=35, Eve=40)";

    // Combine filters
    QuerySet<WherePerson> qs3;
    auto                  young_or_old = qs3.where(young_filter or old_filter).select();
    ASSERT_TRUE(young_or_old.has_value());
    EXPECT_EQ(young_or_old.value().size(), 4) << "Expected 4 people (young or old)";

    // Complex composition
    QuerySet<WherePerson> qs4;
    auto                  young_a_names = qs4.where(young_filter and name_starts_with_a).select();
    ASSERT_TRUE(young_a_names.has_value());
    EXPECT_EQ(young_a_names.value().size(), 0) << "Expected 0 people (no young people with A names)";

    // Reuse filters in different combinations
    QuerySet<WherePerson> qs5;
    auto                  old_a_names = qs5.where(old_filter and name_starts_with_a).select();
    ASSERT_TRUE(old_a_names.has_value());
    EXPECT_EQ(old_a_names.value().size(), 0) << "Expected 0 people (no old people with A names)";
}

// Test: Reusable base QuerySet pattern
TEST_F(WhereTest, ReusableBaseQuerySet) {
    QuerySet<WherePerson> queryset;

    // Create a base filtered QuerySet (age >= 25)
    queryset.where(field<^^WherePerson::age>() >= 25);

    // Reuse base filter multiple times
    auto result1 = queryset.select();
    ASSERT_TRUE(result1.has_value());
    ASSERT_EQ(result1.value().size(), 5) << "Expected 5 people with age >= 25";

    // Still has base filter on second call
    auto result2 = queryset.select();
    ASSERT_TRUE(result2.has_value());
    ASSERT_EQ(result2.value().size(), 5) << "Base filter should persist";

    // Refine the base filter
    queryset.where(field<^^WherePerson::age>() >= 30);
    auto adults = queryset.select();
    ASSERT_TRUE(adults.has_value());
    ASSERT_EQ(adults.value().size(), 3) << "Expected 3 people (age >= 25 AND age >= 30)";
}

// Test: Building query progressively
TEST_F(WhereTest, ProgressiveQueryBuilding) {
    QuerySet<WherePerson> queryset;

    // Start with broad filter
    queryset.where(field<^^WherePerson::age>() >= 25);
    auto result1 = queryset.select();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value().size(), 5) << "All people are >= 25";

    // Narrow it down
    queryset.where(field<^^WherePerson::age>() < 35);
    auto result2 = queryset.select();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value().size(), 3) << "Expected 3 people (25 <= age < 35)";

    // Add another condition (names starting with certain letter)
    queryset.where(field<^^WherePerson::name>().like("D%"));
    auto result3 = queryset.select();
    ASSERT_TRUE(result3.has_value());
    EXPECT_EQ(result3.value().size(), 1) << "Expected 1 person matching all conditions (Diana)";
    EXPECT_EQ(result3.value().begin()->name, "Diana");
}

// Test: reset() clears all accumulated conditions
TEST_F(WhereTest, ResetClearsAllConditions) {
    QuerySet<WherePerson> queryset;

    // Build up complex filter
    queryset.where(field<^^WherePerson::age>() >= 25);
    queryset.where(field<^^WherePerson::age>() < 40);
    queryset.where(field<^^WherePerson::name>().like("D%"));

    auto filtered = queryset.select();
    ASSERT_TRUE(filtered.has_value());
    EXPECT_EQ(filtered.value().size(), 1) << "Complex filter should match Diana only";
    EXPECT_EQ(filtered.value().begin()->name, "Diana");

    // Reset and verify clean slate
    queryset.reset();
    auto all_people = queryset.select();
    ASSERT_TRUE(all_people.has_value());
    EXPECT_EQ(all_people.value().size(), 5) << "After reset() should get all rows";

    // Can build new filter after reset
    queryset.where(field<^^WherePerson::age>() == 25);
    auto bob_only = queryset.select();
    ASSERT_TRUE(bob_only.has_value());
    EXPECT_EQ(bob_only.value().size(), 1) << "New filter after reset works";
    EXPECT_EQ(bob_only.value().begin()->name, "Bob");
}
