#include <gtest/gtest.h>
#include "test_db_helpers.h"
#include "plf_hive/plf_hive.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)

import storm;
import <string>;
import <vector>;
import <expected>;
import <algorithm>;
import <set>;
import <tuple>;
import <format>;
import <optional>;

using namespace storm;
using namespace storm::orm::where;

// Test models for VALUES operations
struct ValuesPerson {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

struct ValuesUser {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

struct ValuesMessage {
    [[= storm::meta::FieldAttr::primary]] int   id;
    std::string                                 content;
    [[= storm::meta::FieldAttr::fk]] ValuesUser sender;
};

// Model with optional fields for NULL testing
struct ValuesOptionalPerson {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    std::optional<int>                        age;      // Can be NULL
    std::optional<std::string>                nickname; // Can be NULL
};

// Test fixture for VALUES operations — templated on database backend
template <typename ConnType> class ValuesTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        auto result = QuerySet<ValuesPerson, ConnType>::set_default_connection(
                storm::test::get_connection_string<ConnType>()
        );
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<ValuesPerson, ConnType>::get_default_connection();

        auto create_person = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE ValuesPerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_person.has_value())
                << "Failed to create ValuesPerson table: " << create_person.error().message();

        auto create_user = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE ValuesUser ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_user.has_value());

        auto create_msg = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE ValuesMessage ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "content TEXT NOT NULL, "
                "sender_id INTEGER NOT NULL, "
                "FOREIGN KEY (sender_id) REFERENCES ValuesUser(id)"
                ")"
        );
        ASSERT_TRUE(create_msg.has_value());

        auto create_optional = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE ValuesOptionalPerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER, "
                "nickname TEXT"
                ")"
        );
        ASSERT_TRUE(create_optional.has_value());

        storm::test::begin_test_txn<ConnType>(conn, {"ValuesUser", "ValuesPerson", "ValuesOptionalPerson"});
    }

    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<ValuesPerson, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<ValuesPerson, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<ValuesPerson, ConnType>::clear_default_connection();
    }

    static void populate_join_test_data() {
        const auto& conn = QuerySet<ValuesPerson, ConnType>::get_default_connection();

        // Insert users
        std::ignore = conn->execute("INSERT INTO ValuesUser (name, age) VALUES ('Alice', 30)");
        std::ignore = conn->execute("INSERT INTO ValuesUser (name, age) VALUES ('Bob', 25)");
        std::ignore = conn->execute("INSERT INTO ValuesUser (name, age) VALUES ('Charlie', 35)");

        // Insert messages (multiple messages per user)
        std::ignore = conn->execute("INSERT INTO ValuesMessage (content, sender_id) VALUES ('Hello', 1)");    // Alice
        std::ignore = conn->execute("INSERT INTO ValuesMessage (content, sender_id) VALUES ('World', 1)");    // Alice
        std::ignore = conn->execute("INSERT INTO ValuesMessage (content, sender_id) VALUES ('Hi there', 2)"); // Bob
        std::ignore = conn->execute("INSERT INTO ValuesMessage (content, sender_id) VALUES ('Goodbye', 2)");  // Bob
        std::ignore = conn->execute("INSERT INTO ValuesMessage (content, sender_id) VALUES ('Test', 3)");     // Charlie
    }
};

TYPED_TEST_SUITE(ValuesTest, DatabaseTypes);

// ============================================================================
// Single-Field Projection Tests
// ============================================================================

// Test: values() on name field — returns all rows including duplicates
TYPED_TEST(ValuesTest, SingleFieldNameWithDuplicates) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::vector<ValuesPerson> people =
            {{1, "Alice", 30}, {2, "Bob", 25}, {3, "Alice", 35}, {4, "Charlie", 40}, {5, "Bob", 28}};

    auto insert_result = queryset.insert(std::span<const ValuesPerson>(people));
    ASSERT_TRUE(insert_result.has_value()) << "INSERT failed: " << insert_result.error().message();

    // SELECT name (no DISTINCT — should return ALL rows including duplicates)
    auto result = queryset.template values<^^ValuesPerson::name>().select();
    ASSERT_TRUE(result.has_value()) << "SELECT values failed: " << result.error().message();

    const auto& names = result.value();
    // Key difference from DISTINCT: should return 5 rows, not 3
    EXPECT_EQ(names.size(), 5) << "Expected all 5 rows (duplicates preserved)";
}

// Test: values() on age field
TYPED_TEST(ValuesTest, SingleFieldAgeWithDuplicates) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::vector<ValuesPerson> people = {{1, "Alice", 30}, {2, "Bob", 25}, {3, "Charlie", 30}, {4, "Dave", 25}};

    auto insert_result = queryset.insert(std::span<const ValuesPerson>(people));
    ASSERT_TRUE(insert_result.has_value());

    // SELECT age — should return 4 rows (duplicates preserved)
    auto result = queryset.template values<^^ValuesPerson::age>().select();
    ASSERT_TRUE(result.has_value());

    const auto& ages = result.value();
    EXPECT_EQ(ages.size(), 4) << "Expected all 4 rows (duplicates preserved)";

    // Count occurrences of each age
    int count_25 = 0; // NOLINT(misc-const-correctness)
    int count_30 = 0; // NOLINT(misc-const-correctness)
    for (const auto& age : ages) {
        if (age == 25) {
            count_25++;
        }
        if (age == 30) {
            count_30++;
        }
    }
    EXPECT_EQ(count_25, 2);
    EXPECT_EQ(count_30, 2);
}

// Test: values() on empty table
TYPED_TEST(ValuesTest, SingleFieldFromEmptyTable) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    auto result = queryset.template values<^^ValuesPerson::name>().select();
    ASSERT_TRUE(result.has_value());

    const auto& names = result.value();
    EXPECT_TRUE(names.empty()) << "Expected empty result from empty table";
}

// Test: values() with single row
TYPED_TEST(ValuesTest, SingleFieldWithSingleRow) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    ValuesPerson const alice{.id = 0, .name = "Alice", .age = 30};
    auto               insert_result = queryset.insert(alice);
    ASSERT_TRUE(insert_result.has_value());

    auto result = queryset.template values<^^ValuesPerson::name>().select();
    ASSERT_TRUE(result.has_value());

    const auto& names = result.value();
    ASSERT_EQ(names.size(), 1);
    EXPECT_EQ(*names.begin(), "Alice");
}

// ============================================================================
// Multi-Field Projection Tests
// ============================================================================

// Test: values() on two fields (name, age)
TYPED_TEST(ValuesTest, TwoFieldsNameAndAge) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::vector<ValuesPerson> people = {
            {1, "Alice", 30},
            {2, "Bob", 25},
            {3, "Alice", 30}, // Duplicate pair — should still appear
            {4, "Charlie", 30},
    };

    auto insert_result = queryset.insert(std::span<const ValuesPerson>(people));
    ASSERT_TRUE(insert_result.has_value());

    auto result = queryset.template values<^^ValuesPerson::name, ^^ValuesPerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "SELECT values failed: " << result.error().message();

    const auto& pairs = result.value();
    // Should return ALL 4 rows (duplicates preserved, unlike DISTINCT)
    EXPECT_EQ(pairs.size(), 4) << "Expected all 4 rows including duplicate pair";
}

// Test: Return type verification for multi-field values
TYPED_TEST(ValuesTest, VerifyReturnTypes) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::ignore = queryset.insert(ValuesPerson{.id = 0, .name = "Alice", .age = 30});

    // Verify return type for values on name field is hive of strings
    auto names_result = queryset.template values<^^ValuesPerson::name>().select();
    static_assert(std::is_same_v<decltype(names_result.value()), plf::hive<std::string>&>);

    // Verify return type for values on age field is hive of integers
    auto ages_result = queryset.template values<^^ValuesPerson::age>().select();
    static_assert(std::is_same_v<decltype(ages_result.value()), plf::hive<int>&>);

    // Verify return type for multi-field values is hive of tuples
    auto pairs_result = queryset.template values<^^ValuesPerson::name, ^^ValuesPerson::age>().select();
    static_assert(std::is_same_v<decltype(pairs_result.value()), plf::hive<std::tuple<std::string, int>>&>);

    // Reversed order
    auto reversed_result = queryset.template values<^^ValuesPerson::age, ^^ValuesPerson::name>().select();
    static_assert(std::is_same_v<decltype(reversed_result.value()), plf::hive<std::tuple<int, std::string>>&>);
}

// ============================================================================
// Duplicates Preserved (Key difference from DISTINCT)
// ============================================================================

// Test: values() preserves duplicates — core differentiator from distinct()
TYPED_TEST(ValuesTest, DuplicatesPreserved) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    // Insert 10 people with the same name
    std::vector<ValuesPerson> people;
    for (int i = 1; i <= 10; ++i) {
        people.emplace_back(i, "SameName", 42);
    }

    auto insert_result = queryset.insert(std::span<const ValuesPerson>(people));
    ASSERT_TRUE(insert_result.has_value());

    // values() should return ALL 10 rows
    auto values_result = queryset.template values<^^ValuesPerson::name>().select();
    ASSERT_TRUE(values_result.has_value());
    EXPECT_EQ(values_result.value().size(), 10) << "values() should preserve all duplicates";

    // Verify all values are "SameName"
    for (const auto& name : values_result.value()) {
        EXPECT_EQ(name, "SameName");
    }
}

// ============================================================================
// WHERE Integration Tests
// ============================================================================

// Test: values() with WHERE clause — single field
TYPED_TEST(ValuesTest, WithWhereSingleField) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::vector<ValuesPerson> people = {
            {0, "Alice", 25},
            {0, "Alice", 25}, // Duplicate
            {0, "Bob", 30},
            {0, "Charlie", 20},
            {0, "David", 35},
    };

    auto insert_result = queryset.insert(people);
    ASSERT_TRUE(insert_result.has_value());

    // SELECT name WHERE age > 22
    auto result = queryset.where(field<^^ValuesPerson::age>() > 22).template values<^^ValuesPerson::name>().select();
    ASSERT_TRUE(result.has_value()) << "values with WHERE failed: " << result.error().message();

    const auto& names = result.value();
    // Alice (25), Alice (25), Bob (30), David (35) — 4 rows (Charlie filtered out)
    EXPECT_EQ(names.size(), 4);
}

// Test: values() with multiple WHERE clauses
TYPED_TEST(ValuesTest, WithMultipleWhereClauses) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::vector<ValuesPerson> people = {
            {0, "Alice", 25},
            {0, "Bob", 30},
            {0, "Charlie", 20},
            {0, "David", 35},
    };

    auto insert_result = queryset.insert(people);
    ASSERT_TRUE(insert_result.has_value());

    // Chain multiple WHERE clauses (AND)
    auto result = queryset.where(field<^^ValuesPerson::age>() > 22)
                          .where(field<^^ValuesPerson::age>() < 32)
                          .template values<^^ValuesPerson::name>()
                          .select();
    ASSERT_TRUE(result.has_value());

    const auto& names = result.value();
    // Alice (25) and Bob (30)
    EXPECT_EQ(names.size(), 2);
}

// Test: values() with complex WHERE clause
TYPED_TEST(ValuesTest, WithComplexWhere) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::vector<ValuesPerson> people = {
            {0, "Alice", 25},
            {0, "Bob", 30},
            {0, "Charlie", 20},
            {0, "David", 40},
    };

    auto insert_result = queryset.insert(people);
    ASSERT_TRUE(insert_result.has_value());

    // SELECT name WHERE age BETWEEN 25 AND 35
    auto result = queryset.where(field<^^ValuesPerson::age>().between(25, 35))
                          .template values<^^ValuesPerson::name>()
                          .select();
    ASSERT_TRUE(result.has_value());

    const auto& names = result.value();
    EXPECT_EQ(names.size(), 2); // Alice (25), Bob (30)
}

// Test: values() with WHERE — no results
TYPED_TEST(ValuesTest, WithWhereNoResults) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::vector<ValuesPerson> people        = {{0, "Alice", 25}, {0, "Bob", 30}};
    auto                      insert_result = queryset.insert(people);
    ASSERT_TRUE(insert_result.has_value());

    auto result = queryset.where(field<^^ValuesPerson::age>() > 100).template values<^^ValuesPerson::name>().select();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 0);
}

// ============================================================================
// JOIN Integration Tests
// ============================================================================

// Test: values() with JOIN — single field
TYPED_TEST(ValuesTest, WithJoinSingleField) {
    this->populate_join_test_data();

    QuerySet<ValuesMessage, TypeParam> msg_qs;

    // SELECT content FROM ValuesMessage JOIN ValuesUser (values replaces field list)
    auto result = msg_qs.template join<&ValuesMessage::sender>().template values<^^ValuesMessage::content>().select();
    ASSERT_TRUE(result.has_value()) << "values with JOIN failed: " << result.error().message();

    const auto& contents = result.value();
    // Should return all 5 messages
    EXPECT_EQ(contents.size(), 5);
}

// Test: values() with JOIN and WHERE
TYPED_TEST(ValuesTest, WithJoinAndWhere) {
    this->populate_join_test_data();

    QuerySet<ValuesMessage, TypeParam> msg_qs;

    auto result = msg_qs.template join<&ValuesMessage::sender>()
                          .where(field<^^ValuesMessage::content>().like("%o%")) // Hello, World, Goodbye
                          .template values<^^ValuesMessage::content>()
                          .select();
    ASSERT_TRUE(result.has_value()) << "values with JOIN and WHERE failed: " << result.error().message();

    const auto& contents = result.value();
    EXPECT_GE(contents.size(), 3); // At least Hello, World, Goodbye
}

// Test: FK field projection not supported (same limitation as DISTINCT)
// FK fields are complex objects — extract_column_value doesn't support them
// Workaround: use raw SQL or JOIN to access FK columns
TYPED_TEST(ValuesTest, ForeignKeyFieldNotSupported) {
    SUCCEED() << "FK object field projection not supported (by design)";
}

// ============================================================================
// ORDER BY Tests
// ============================================================================

// Test: values() with ORDER BY ASC
TYPED_TEST(ValuesTest, WithOrderByAsc) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::vector<ValuesPerson> people = {
            {0, "Charlie", 30},
            {0, "Alice", 25},
            {0, "Bob", 35},
    };

    auto insert_result = queryset.insert(people);
    ASSERT_TRUE(insert_result.has_value());

    auto result = queryset.template order_by<^^ValuesPerson::name>().template values<^^ValuesPerson::name>().select();
    ASSERT_TRUE(result.has_value());

    const auto& names = result.value();
    ASSERT_EQ(names.size(), 3);

    auto it = names.begin();
    EXPECT_EQ(*it++, "Alice");
    EXPECT_EQ(*it++, "Bob");
    EXPECT_EQ(*it++, "Charlie");
}

// Test: values() with ORDER BY DESC
TYPED_TEST(ValuesTest, WithOrderByDesc) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::vector<ValuesPerson> people = {
            {0, "Alice", 25},
            {0, "Charlie", 30},
            {0, "Bob", 35},
    };

    auto insert_result = queryset.insert(people);
    ASSERT_TRUE(insert_result.has_value());

    auto result =
            queryset.template order_by<^^ValuesPerson::name, false>().template values<^^ValuesPerson::name>().select();
    ASSERT_TRUE(result.has_value());

    const auto& names = result.value();
    ASSERT_EQ(names.size(), 3);

    auto it = names.begin();
    EXPECT_EQ(*it++, "Charlie");
    EXPECT_EQ(*it++, "Bob");
    EXPECT_EQ(*it++, "Alice");
}

// ============================================================================
// LIMIT / OFFSET Tests
// ============================================================================

// Test: values() with LIMIT
TYPED_TEST(ValuesTest, WithLimit) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::vector<ValuesPerson> people = {
            {0, "Alice", 25},
            {0, "Bob", 28},
            {0, "Charlie", 30},
            {0, "Dave", 35},
            {0, "Eve", 40},
    };

    auto insert_result = queryset.insert(people);
    ASSERT_TRUE(insert_result.has_value());

    auto result = queryset.template order_by<^^ValuesPerson::name>()
                          .limit(3)
                          .template values<^^ValuesPerson::name>()
                          .select();
    ASSERT_TRUE(result.has_value());

    const auto& names = result.value();
    ASSERT_EQ(names.size(), 3);

    auto it = names.begin();
    EXPECT_EQ(*it++, "Alice");
    EXPECT_EQ(*it++, "Bob");
    EXPECT_EQ(*it++, "Charlie");
}

// Test: values() with LIMIT and OFFSET
TYPED_TEST(ValuesTest, WithLimitAndOffset) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::vector<ValuesPerson> people = {
            {0, "Alice", 25},
            {0, "Bob", 28},
            {0, "Charlie", 30},
            {0, "Dave", 35},
            {0, "Eve", 40},
    };

    auto insert_result = queryset.insert(people);
    ASSERT_TRUE(insert_result.has_value());

    // Skip Alice, Bob and return Charlie, Dave
    auto result = queryset.template order_by<^^ValuesPerson::name>()
                          .limit(2)
                          .offset(2)
                          .template values<^^ValuesPerson::name>()
                          .select();
    ASSERT_TRUE(result.has_value());

    const auto& names = result.value();
    ASSERT_EQ(names.size(), 2);

    auto it = names.begin();
    EXPECT_EQ(*it++, "Charlie");
    EXPECT_EQ(*it++, "Dave");
}

// Test: values() with WHERE, ORDER BY, LIMIT combined
TYPED_TEST(ValuesTest, WithWhereOrderByLimit) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::vector<ValuesPerson> people = {
            {0, "Alice", 25},
            {0, "Bob", 35},
            {0, "Charlie", 30},
            {0, "Dave", 40},
            {0, "Eve", 20}, // Filtered out by WHERE
    };

    auto insert_result = queryset.insert(people);
    ASSERT_TRUE(insert_result.has_value());

    auto result = queryset.where(field<^^ValuesPerson::age>() > 25)
                          .template order_by<^^ValuesPerson::name>()
                          .limit(2)
                          .template values<^^ValuesPerson::name>()
                          .select();
    ASSERT_TRUE(result.has_value());

    const auto& names = result.value();
    ASSERT_EQ(names.size(), 2);

    // Bob, Charlie, Dave pass WHERE; first 2 alphabetically: Bob, Charlie
    auto it = names.begin();
    EXPECT_EQ(*it++, "Bob");
    EXPECT_EQ(*it++, "Charlie");
}

// ============================================================================
// Optional Fields (NULL handling) Tests
// ============================================================================

// Test: values() on optional integer field with NULLs
TYPED_TEST(ValuesTest, OptionalIntFieldWithNulls) {
    QuerySet<ValuesOptionalPerson, TypeParam> queryset;

    std::vector<ValuesOptionalPerson> people = {
            {0, "Alice", 25, "Ali"},
            {0, "Bob", std::nullopt, "Bobby"},
            {0, "Charlie", 30, std::nullopt},
            {0, "Dave", std::nullopt, std::nullopt},
            {0, "Eve", 25, "Evie"}, // Duplicate age
    };

    auto insert_result = queryset.insert(people);
    ASSERT_TRUE(insert_result.has_value()) << "Insert failed: " << insert_result.error().message();

    auto result = queryset.template values<^^ValuesOptionalPerson::age>().select();
    ASSERT_TRUE(result.has_value()) << "values on optional field failed: " << result.error().message();

    const auto& ages = result.value();
    // Should return ALL 5 rows (not deduplicated)
    EXPECT_EQ(ages.size(), 5);

    // Count NULLs
    int null_count = 0; // NOLINT(misc-const-correctness)
    for (const auto& age : ages) {
        if (!age.has_value()) {
            null_count++;
        }
    }
    EXPECT_EQ(null_count, 2) << "Expected 2 NULL values (Bob and Dave)";
}

// Test: values() on optional string field with NULLs
TYPED_TEST(ValuesTest, OptionalStringFieldWithNulls) {
    QuerySet<ValuesOptionalPerson, TypeParam> queryset;

    std::vector<ValuesOptionalPerson> people = {
            {0, "Alice", 25, "Ali"},
            {0, "Bob", 30, std::nullopt},
            {0, "Charlie", 35, "Ali"},     // Duplicate nickname
            {0, "Dave", 40, std::nullopt}, // Duplicate NULL
    };

    auto insert_result = queryset.insert(people);
    ASSERT_TRUE(insert_result.has_value());

    auto result = queryset.template values<^^ValuesOptionalPerson::nickname>().select();
    ASSERT_TRUE(result.has_value());

    const auto& nicknames = result.value();
    // Should return ALL 4 rows
    EXPECT_EQ(nicknames.size(), 4);

    int null_count = 0; // NOLINT(misc-const-correctness)
    int ali_count  = 0; // NOLINT(misc-const-correctness)
    for (const auto& nickname : nicknames) {
        if (!nickname.has_value()) {
            null_count++;
        } else if (nickname.value() == "Ali") {
            ali_count++;
        }
    }
    EXPECT_EQ(null_count, 2);
    EXPECT_EQ(ali_count, 2); // Duplicates preserved
}

// ============================================================================
// Statement Reuse and Stability Tests
// ============================================================================

// Test: values() executed multiple times returns consistent results
TYPED_TEST(ValuesTest, StatementReuseStability) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::vector<ValuesPerson> people = {
            {0, "Alice", 25},
            {0, "Bob", 30},
            {0, "Charlie", 35},
    };
    std::ignore = queryset.insert(people);

    for (int i = 0; i < 5; ++i) {
        auto result = queryset.template values<^^ValuesPerson::name>().select();
        ASSERT_TRUE(result.has_value()) << "Iteration " << i << " failed: " << result.error().message();
        EXPECT_EQ(result.value().size(), 3) << "Iteration " << i << " returned wrong count";
    }
}

// Test: Different WHERE expressions with values()
TYPED_TEST(ValuesTest, DifferentWhereExpressionsWithCaching) {
    QuerySet<ValuesPerson, TypeParam> queryset;

    std::vector<ValuesPerson> people = {
            {0, "Alice", 25},
            {0, "Bob", 30},
            {0, "Charlie", 35},
            {0, "Dave", 40},
    };
    std::ignore = queryset.insert(people);

    // Query 1: age > 25
    auto result1 = queryset.where(field<^^ValuesPerson::age>() > 25).template values<^^ValuesPerson::name>().select();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value().size(), 3); // Bob, Charlie, Dave

    queryset.reset();

    // Query 2: age > 35
    auto result2 = queryset.where(field<^^ValuesPerson::age>() > 35).template values<^^ValuesPerson::name>().select();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value().size(), 1); // Dave only
}

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
