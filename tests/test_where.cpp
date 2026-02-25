#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)

import storm;
import <string>;
import <vector>;
import <expected>;
import <optional>;

#include "test_models.h"

using namespace storm;
using namespace storm::orm::where;

// Test fixture for WHERE operations — templated on database backend
template <typename ConnType> class WhereTest : public StormTestFixture<Person, ConnType> {
  protected:
    auto SetUp() -> void override {
        if (!this->setup_connection()) {
            GTEST_SKIP() << "Backend unavailable";
            return;
        }

        const auto& conn = QuerySet<Person, ConnType>::get_default_connection();

        auto create_result = storm::test::ensure_table<ConnType>(conn, person_create_sql);
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        storm::test::begin_test_txn<ConnType>(conn, {"Person"});

        // Insert test data (use ID 0 to let AUTOINCREMENT generate IDs)
        std::vector<Person> const people = {
                {.id = 0, .name = "Alice", .age = 30},
                {.id = 0, .name = "Bob", .age = 25},
                {.id = 0, .name = "Charlie", .age = 35},
                {.id = 0, .name = "Diana", .age = 28},
                {.id = 0, .name = "Eve", .age = 40},
        };

        QuerySet<Person, ConnType> queryset;
        // Insert one by one to avoid batch insert issues
        for (const auto& person : people) {
            auto insert_result = queryset.insert(person).execute();
            ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data: " << insert_result.error().message();
        }
    }
};

TYPED_TEST_SUITE(WhereTest, DatabaseTypes);

// Test: WHERE with single integer condition
TYPED_TEST(WhereTest, WhereSingleIntegerCondition) {
    QuerySet<Person, TypeParam> queryset;

    auto result = queryset.where(field<^^Person::age>() == 30).select().execute();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1) << "Expected 1 person with age 30";
    auto it = people.begin();
    EXPECT_EQ(it->name, "Alice");
    EXPECT_EQ(it->age, 30);
}

// Test: WHERE with greater than condition
TYPED_TEST(WhereTest, WhereGreaterThan) {
    QuerySet<Person, TypeParam> queryset;

    auto result = queryset.where(field<^^Person::age>() > 30).select().execute();
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
TYPED_TEST(WhereTest, WhereLessThan) {
    QuerySet<Person, TypeParam> queryset;

    auto result = queryset.where(field<^^Person::age>() < 30).select().execute();
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
TYPED_TEST(WhereTest, WhereStringCondition) {
    QuerySet<Person, TypeParam> queryset;

    auto result = queryset.where(field<^^Person::name>() == "Bob").select().execute();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1) << "Expected 1 person named Bob";
    auto it = people.begin();
    EXPECT_EQ(it->name, "Bob");
    EXPECT_EQ(it->age, 25);
}

// Test: WHERE with LIKE pattern
TYPED_TEST(WhereTest, WhereLikePattern) {
    QuerySet<Person, TypeParam> queryset;

    auto result = queryset.where(field<^^Person::name>().like("A%")).select().execute();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1) << "Expected 1 person with name starting with 'A'";
    EXPECT_EQ(people.begin()->name, "Alice");
}

// Test: WHERE with multiple conditions (AND logic)
TYPED_TEST(WhereTest, WhereMultipleConditions) {
    QuerySet<Person, TypeParam> queryset;

    auto expr1  = field<^^Person::age>() > 25;
    auto expr2  = field<^^Person::age>() < 40;
    auto result = queryset.where(expr1 && expr2).select().execute();
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
TYPED_TEST(WhereTest, WhereThreeConditions) {
    QuerySet<Person, TypeParam> queryset;

    auto expr1  = field<^^Person::age>() >= 28;
    auto expr2  = field<^^Person::age>() <= 35;
    auto expr3  = field<^^Person::name>() != "Charlie";
    auto result = queryset.where(expr1 && expr2 && expr3).select().execute();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 2) << "Expected 2 people matching all three conditions";
    auto it = people.begin();
    EXPECT_EQ(it->name, "Alice");
    ++it;
    EXPECT_EQ(it->name, "Diana");
}

// Test: WHERE with BETWEEN clause
TYPED_TEST(WhereTest, WhereBetween) {
    QuerySet<Person, TypeParam> queryset;

    auto result = queryset.where(field<^^Person::age>().between(28, 35)).select().execute();
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
TYPED_TEST(WhereTest, WhereIn) {
    QuerySet<Person, TypeParam> queryset;

    // IN expressions use runtime builder pattern, requires .select()
    auto result = queryset.where(field<^^Person::age>().in(25, 30, 40)).select().execute();
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
TYPED_TEST(WhereTest, WhereNoMatch) {
    QuerySet<Person, TypeParam> queryset;

    auto result = queryset.where(field<^^Person::age>() > 100).select().execute();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    EXPECT_TRUE(people.empty()) << "Expected no results for age > 100";
}

// Test: WHERE returning all rows
TYPED_TEST(WhereTest, WhereMatchesAll) {
    QuerySet<Person, TypeParam> queryset;

    auto result = queryset.where(field<^^Person::age>() > 0).select().execute();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 5) << "Expected all 5 people with age > 0";
}

// Test: WHERE with complex expression
TYPED_TEST(WhereTest, WhereComplexExpression) {
    QuerySet<Person, TypeParam> queryset;

    auto expr1  = field<^^Person::age>() < 30;
    auto expr2  = field<^^Person::age>() > 35;
    auto expr3  = field<^^Person::name>() != "Charlie";
    auto result = queryset.where((expr1 || expr2) && expr3).select().execute();
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
TYPED_TEST(WhereTest, WherePreservesStateAfterSelect) {
    QuerySet<Person, TypeParam> queryset;

    // Set base filter
    queryset.where(field<^^Person::age>() >= 25);

    // First query uses the filter
    auto result1 = queryset.select().execute();
    ASSERT_TRUE(result1.has_value());
    ASSERT_EQ(result1.value().size(), 5) << "Expected 5 people with age >= 25";

    // Second query still has the same filter (state preserved)
    auto result2 = queryset.select().execute();
    ASSERT_TRUE(result2.has_value());
    ASSERT_EQ(result2.value().size(), 5) << "State should persist after first select()";

    // Add more conditions - they accumulate
    queryset.where(field<^^Person::age>() < 40);
    auto result3 = queryset.select().execute();
    ASSERT_TRUE(result3.has_value());
    ASSERT_EQ(result3.value().size(), 4) << "Expected 4 people with age >= 25 AND age < 40";

    // reset() clears all state
    queryset.reset();
    auto result4 = queryset.select().execute();
    ASSERT_TRUE(result4.has_value());
    ASSERT_EQ(result4.value().size(), 5) << "After reset() should select all rows";
}

// Test: WHERE with std::string_view parameter
TYPED_TEST(WhereTest, WhereStringView) {
    QuerySet<Person, TypeParam> queryset;

    std::string_view const name_view = "Charlie";
    auto                   result    = queryset.where(field<^^Person::name>() == name_view).select().execute();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1);
    EXPECT_EQ(people.begin()->name, "Charlie");
}

// Test: WHERE with const char* parameter
TYPED_TEST(WhereTest, WhereConstCharPtr) {
    QuerySet<Person, TypeParam> queryset;

    const char* name_ptr = "Bob";
    auto        result   = queryset.where(field<^^Person::name>() == name_ptr).select().execute();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1);
    auto it = people.begin();
    EXPECT_EQ(it->name, "Bob");
    EXPECT_EQ(it->age, 25);
}

// Test: WHERE with mixed parameter types
TYPED_TEST(WhereTest, WhereMixedTypes) {
    QuerySet<Person, TypeParam> queryset;

    auto expr1  = field<^^Person::name>() == "Diana";
    auto expr2  = field<^^Person::age>() == 28;
    auto result = queryset.where(expr1 && expr2).select().execute();
    ASSERT_TRUE(result.has_value()) << "WHERE failed: " << result.error().message();

    const auto& people = result.value();
    ASSERT_EQ(people.size(), 1);
    EXPECT_EQ(people.begin()->name, "Diana");
}

// Test fixture for WHERE + JOIN operations — templated on database backend
template <typename ConnType> class WhereJoinTest : public StormTestFixture<Message, ConnType> {
  protected:
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    auto SetUp() -> void override {
        if (!this->setup_connection()) {
            GTEST_SKIP() << "Backend unavailable";
            return;
        }

        const auto& conn = QuerySet<Message, ConnType>::get_default_connection();

        auto create_person = storm::test::ensure_table<ConnType>(conn, person_create_sql);
        ASSERT_TRUE(create_person.has_value()) << "Failed to create Person table: " << create_person.error().message();

        auto create_message = storm::test::ensure_table<ConnType>(conn, message_create_sql);
        ASSERT_TRUE(create_message.has_value())
                << "Failed to create Message table: " << create_message.error().message();

        storm::test::begin_test_txn<ConnType>(conn, {"Message", "Person"});

        // Insert users (use ID 0 to let AUTOINCREMENT generate IDs)
        QuerySet<Person, ConnType> user_qs;
        auto                       alice_id = user_qs.insert(Person{.id = 0, .name = "alice", .age = 10}).execute();
        ASSERT_TRUE(alice_id.has_value()) << "Failed to insert alice: " << alice_id.error().message();

        auto bob_id = user_qs.insert(Person{.id = 0, .name = "bob", .age = 5}).execute();
        ASSERT_TRUE(bob_id.has_value()) << "Failed to insert bob: " << bob_id.error().message();

        auto charlie_id = user_qs.insert(Person{.id = 0, .name = "charlie", .age = 15}).execute();
        ASSERT_TRUE(charlie_id.has_value()) << "Failed to insert charlie: " << charlie_id.error().message();

        // Insert messages (use generated user IDs)
        QuerySet<Message, ConnType> msg_qs;
        auto                        msg1 = msg_qs.insert(
                                  Message{.id      = 0,
                                                                 .content = "Hello from Alice",
                                                                 .sender = Person{.id = static_cast<int>(alice_id.value())}}
        ).execute();
        ASSERT_TRUE(msg1.has_value()) << "Failed to insert message 1: " << msg1.error().message();

        auto msg2 = msg_qs.insert(
                                  Message{.id      = 0,
                                          .content = "Hi from Bob",
                                          .sender  = Person{.id = static_cast<int>(bob_id.value())}}
        ).execute();
        ASSERT_TRUE(msg2.has_value()) << "Failed to insert message 2: " << msg2.error().message();

        auto msg3 = msg_qs.insert(
                                  Message{.id      = 0,
                                          .content = "Greetings from Alice",
                                          .sender  = Person{.id = static_cast<int>(alice_id.value())}}
        ).execute();
        ASSERT_TRUE(msg3.has_value()) << "Failed to insert message 3: " << msg3.error().message();

        auto msg4 = msg_qs.insert(
                                  Message{.id      = 0,
                                          .content = "Greetings Charlie",
                                          .sender  = Person{.id = static_cast<int>(charlie_id.value())}}
        ).execute();
        ASSERT_TRUE(msg4.has_value()) << "Failed to insert message 4: " << msg4.error().message();
    }
};

TYPED_TEST_SUITE(WhereJoinTest, DatabaseTypes);

// Test: WHERE with JOIN
TYPED_TEST(WhereJoinTest, WhereWithJoin) {
    QuerySet<Message, TypeParam> queryset;

    // SELECT with JOIN and WHERE filtering by Person.age
    auto result = queryset.template join<&Message::sender>().where(field<^^Person::age>() >= 10).select().execute();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN failed: " << result.error().message();

    const auto& messages = result.value();
    ASSERT_EQ(messages.size(), 3) << "Expected 3 messages from users with age >= 10";

    // Verify sender information is populated
    auto it = messages.begin();
    EXPECT_EQ(it->sender.name, "alice");
    EXPECT_EQ(it->sender.age, 10);
    ++it;
    EXPECT_EQ(it->sender.name, "alice");
    ++it;
    EXPECT_EQ(it->sender.name, "charlie");
    EXPECT_EQ(it->sender.age, 15);
}

// Test: WHERE with JOIN and content filter
TYPED_TEST(WhereJoinTest, WhereWithJoinContentFilter) {
    QuerySet<Message, TypeParam> queryset;

    auto result = queryset.template join<&Message::sender>()
                          .where(field<^^Message::content>().like("%Alice%"))
                          .select()
                          .execute();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN failed: " << result.error().message();

    const auto& messages = result.value();
    ASSERT_EQ(messages.size(), 2) << "Expected 2 messages with 'Alice' in content";
}

// Test: WHERE with JOIN and multiple conditions
TYPED_TEST(WhereJoinTest, WhereWithJoinMultipleConditions) {
    QuerySet<Message, TypeParam> queryset;

    auto expr1  = field<^^Person::age>() > 5;
    auto expr2  = field<^^Message::content>().like("%from%");
    auto result = queryset.template join<&Message::sender>().where(expr1 && expr2).select().execute();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN failed: " << result.error().message();

    const auto& messages = result.value();
    ASSERT_EQ(messages.size(), 2) << "Expected 2 messages matching both conditions";
}

// Test: Natural && and || operators (and/or keywords)
TYPED_TEST(WhereJoinTest, WhereWithNaturalOperators) {
    QuerySet<Message, TypeParam> queryset;

    // Using natural 'and' operator (also works with &&)
    auto result = queryset.template join<&Message::sender>()
                          .where(field<^^Person::age>() > 5 and field<^^Message::content>().like("%from%"))
                          .select()
                          .execute();
    ASSERT_TRUE(result.has_value()) << "WHERE + JOIN failed: " << result.error().message();

    const auto& messages = result.value();
    ASSERT_EQ(messages.size(), 2) << "Expected 2 messages matching both conditions";
    auto it = messages.begin();
    EXPECT_EQ(it->sender.name, "alice");
    ++it;
    EXPECT_EQ(it->sender.name, "alice");
}

// Test: Natural operators with complex expressions
TYPED_TEST(WhereTest, WhereNaturalOperatorsComplex) {
    QuerySet<Person, TypeParam> queryset;

    // Using natural 'and' and 'or' operators
    auto result = queryset.where((field<^^Person::age>() < 30 or field<^^Person::age>() > 35) and
                                 field<^^Person::name>() != "Charlie")
                          .select()
                          .execute();
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
TYPED_TEST(WhereTest, WhereReuseCondition) {
    // Store condition in variable
    auto age_condition = field<^^Person::age>() > 25;

    // First use - should return 4 people (Alice=30, Charlie=35, Diana=28, Eve=40)
    QuerySet<Person, TypeParam> queryset1;
    auto                        result1 = queryset1.where(age_condition).select().execute();
    ASSERT_TRUE(result1.has_value()) << "First WHERE failed: " << result1.error().message();
    EXPECT_EQ(result1.value().size(), 4) << "Expected 4 people with age > 25";

    // Reuse same condition with fresh QuerySet - should return same results
    QuerySet<Person, TypeParam> queryset2;
    auto                        result2 = queryset2.where(age_condition).select().execute();
    ASSERT_TRUE(result2.has_value()) << "Second WHERE failed: " << result2.error().message();
    EXPECT_EQ(result2.value().size(), 4) << "Expected 4 people with age > 25 (reused condition)";

    // Combine reused condition with new one
    QuerySet<Person, TypeParam> queryset3;
    auto result3 = queryset3.where(age_condition and field<^^Person::name>() == "Alice").select().execute();
    ASSERT_TRUE(result3.has_value()) << "Combined WHERE failed: " << result3.error().message();
    ASSERT_EQ(result3.value().size(), 1) << "Expected 1 person matching both conditions";
    auto it = result3.value().begin();
    EXPECT_EQ(it->name, "Alice");
    EXPECT_EQ(it->age, 30);

    // Reuse the original condition again with fresh QuerySet
    QuerySet<Person, TypeParam> queryset4;
    auto                        result4 = queryset4.where(age_condition).select().execute();
    ASSERT_TRUE(result4.has_value()) << "Third WHERE failed: " << result4.error().message();
    EXPECT_EQ(result4.value().size(), 4) << "Expected 4 people with age > 25 (reused after combining)";
}

// Test: Building composable filter library
TYPED_TEST(WhereTest, WhereComposableFilters) {
    // Create reusable filter components
    auto young_filter       = field<^^Person::age>() < 30;
    auto old_filter         = field<^^Person::age>() >= 35;
    auto name_starts_with_a = field<^^Person::name>().like("A%");

    // Use filters independently with fresh QuerySets
    QuerySet<Person, TypeParam> qs1;
    auto                        young_people = qs1.where(young_filter).select().execute();
    ASSERT_TRUE(young_people.has_value());
    EXPECT_EQ(young_people.value().size(), 2) << "Expected 2 young people (Bob=25, Diana=28)";

    QuerySet<Person, TypeParam> qs2;
    auto                        old_people = qs2.where(old_filter).select().execute();
    ASSERT_TRUE(old_people.has_value());
    EXPECT_EQ(old_people.value().size(), 2) << "Expected 2 old people (Charlie=35, Eve=40)";

    // Combine filters
    QuerySet<Person, TypeParam> qs3;
    auto                        young_or_old = qs3.where(young_filter or old_filter).select().execute();
    ASSERT_TRUE(young_or_old.has_value());
    EXPECT_EQ(young_or_old.value().size(), 4) << "Expected 4 people (young or old)";

    // Complex composition
    QuerySet<Person, TypeParam> qs4;
    auto                        young_a_names = qs4.where(young_filter and name_starts_with_a).select().execute();
    ASSERT_TRUE(young_a_names.has_value());
    EXPECT_EQ(young_a_names.value().size(), 0) << "Expected 0 people (no young people with A names)";

    // Reuse filters in different combinations
    QuerySet<Person, TypeParam> qs5;
    auto                        old_a_names = qs5.where(old_filter and name_starts_with_a).select().execute();
    ASSERT_TRUE(old_a_names.has_value());
    EXPECT_EQ(old_a_names.value().size(), 0) << "Expected 0 people (no old people with A names)";
}

// Test: Reusable base QuerySet pattern
TYPED_TEST(WhereTest, ReusableBaseQuerySet) {
    QuerySet<Person, TypeParam> queryset;

    // Create a base filtered QuerySet (age >= 25)
    queryset.where(field<^^Person::age>() >= 25);

    // Reuse base filter multiple times
    auto result1 = queryset.select().execute();
    ASSERT_TRUE(result1.has_value());
    ASSERT_EQ(result1.value().size(), 5) << "Expected 5 people with age >= 25";

    // Still has base filter on second call
    auto result2 = queryset.select().execute();
    ASSERT_TRUE(result2.has_value());
    ASSERT_EQ(result2.value().size(), 5) << "Base filter should persist";

    // Refine the base filter
    queryset.where(field<^^Person::age>() >= 30);
    auto adults = queryset.select().execute();
    ASSERT_TRUE(adults.has_value());
    ASSERT_EQ(adults.value().size(), 3) << "Expected 3 people (age >= 25 AND age >= 30)";
}

// Test: Building query progressively
TYPED_TEST(WhereTest, ProgressiveQueryBuilding) {
    QuerySet<Person, TypeParam> queryset;

    // Start with broad filter
    queryset.where(field<^^Person::age>() >= 25);
    auto result1 = queryset.select().execute();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value().size(), 5) << "All people are >= 25";

    // Narrow it down
    queryset.where(field<^^Person::age>() < 35);
    auto result2 = queryset.select().execute();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value().size(), 3) << "Expected 3 people (25 <= age < 35)";

    // Add another condition (names starting with certain letter)
    queryset.where(field<^^Person::name>().like("D%"));
    auto result3 = queryset.select().execute();
    ASSERT_TRUE(result3.has_value());
    EXPECT_EQ(result3.value().size(), 1) << "Expected 1 person matching all conditions (Diana)";
    EXPECT_EQ(result3.value().begin()->name, "Diana");
}

// Test: reset() clears all accumulated conditions
TYPED_TEST(WhereTest, ResetClearsAllConditions) {
    QuerySet<Person, TypeParam> queryset;

    // Build up complex filter
    queryset.where(field<^^Person::age>() >= 25);
    queryset.where(field<^^Person::age>() < 40);
    queryset.where(field<^^Person::name>().like("D%"));

    auto filtered = queryset.select().execute();
    ASSERT_TRUE(filtered.has_value());
    EXPECT_EQ(filtered.value().size(), 1) << "Complex filter should match Diana only";
    EXPECT_EQ(filtered.value().begin()->name, "Diana");

    // Reset and verify clean slate
    queryset.reset();
    auto all_people = queryset.select().execute();
    ASSERT_TRUE(all_people.has_value());
    EXPECT_EQ(all_people.value().size(), 5) << "After reset() should get all rows";

    // Can build new filter after reset
    queryset.where(field<^^Person::age>() == 25);
    auto bob_only = queryset.select().execute();
    ASSERT_TRUE(bob_only.has_value());
    EXPECT_EQ(bob_only.value().size(), 1) << "New filter after reset works";
    EXPECT_EQ(bob_only.value().begin()->name, "Bob");
}

// ============================================================================
// WHERE Expression Coverage Tests
// Tests for specific code paths in where.cppm
// ============================================================================

// Test: InExpression with empty values generates "1 = 0" (always false)
TYPED_TEST(WhereTest, WhereInEmptyValuesReturnsFalse) {
    QuerySet<Person, TypeParam> queryset;

    // .in() with no arguments creates InExpression with empty vector → "1 = 0"
    auto result = queryset.where(field<^^Person::age>().in()).select().execute();
    ASSERT_TRUE(result.has_value()) << "Empty IN should succeed";
    EXPECT_TRUE(result.value().empty()) << "Empty IN (1=0) should match nothing";
}

// Test: Expr::get() accessor
TYPED_TEST(WhereTest, ExprGetReturnsValidPointer) {
    auto expr = field<^^Person::age>() > 25;
    auto ptr  = expr.get();
    EXPECT_NE(ptr, nullptr) << "Expr::get() should return a valid pointer";
}

// Test: Explicit Expr(ExpressionVariant&&) constructor
TYPED_TEST(WhereTest, ExprDirectConstructionFromVariant) {
    // Construct ExpressionVariant directly and wrap in Expr using the explicit rvalue constructor
    ExpressionVariant variant(ComparisonExpr<int>{std::string("age"), CompOp::Greater, 25});
    Expr              direct_expr(std::move(variant));

    // Verify it works by converting to SQL via get()
    auto sql = to_sql(*direct_expr.get());
    EXPECT_EQ(sql, "age > ?");

    // Also verify it works with QuerySet::where()
    QuerySet<Person, TypeParam> queryset;
    auto                        result = queryset.where(direct_expr).select().execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 4) << "Should find 4 people with age > 25";
}

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
