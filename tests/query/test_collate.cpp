// test_collate.cpp - Comprehensive tests for COLLATE support (ORDER BY + WHERE)
#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-const-correctness,performance-unnecessary-copy-initialization)

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954
using storm::QuerySet;
using storm::orm::utilities::Collate;
using storm::orm::where::f;

// SQLite-only: COLLATE NOCASE/BINARY/RTRIM are SQLite-specific collation sequences.
// PostgreSQL uses different syntax (COLLATE "C", COLLATE "en_US").
using SqliteTypes = ::testing::Types<storm::db::sqlite::Connection>;

// ============================================================================
// Test Fixture — inserts mixed-case names for COLLATE testing
// ============================================================================

template <typename ConnType> class CollateTest : public StormTestFixture<Person, ConnType> {
  public:
    auto on_after_setup(const std::shared_ptr<ConnType>&) -> void override {
        std::vector<Person> const test_data = {
                {.id = 1, .name = "alice", .age = 25, .department = "eng"},
                {.id = 2, .name = "Alice", .age = 30, .department = "Eng"},
                {.id = 3, .name = "BOB", .age = 35, .department = "HR"},
                {.id = 4, .name = "bob", .age = 40, .department = "hr"},
                {.id = 5, .name = "Charlie", .age = 22, .department = "Sales"},
                {.id = 6, .name = "charlie", .age = 28, .department = "sales"},
                {.id = 7, .name = "ALICE", .age = 33, .department = "ENG"},
                {.id = 8, .name = "  bob", .age = 45, .department = "HR  "},
        };

        QuerySet<Person, ConnType> qs;
        auto                       result = qs.insert(test_data).execute();
        ASSERT_TRUE(result.has_value()) << "Failed to insert COLLATE test data";
    }
};

TYPED_TEST_SUITE(CollateTest, SqliteTypes);

// ============================================================================
// ORDER BY with COLLATE Tests
// ============================================================================

TYPED_TEST(CollateTest, OrderByNoCaseAsc) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.template order_by<^^Person::name, Collate::NoCase>().select().execute();
    ASSERT_TRUE(result.has_value());

    auto items = result.value();
    ASSERT_EQ(items.size(), 8);

    // With COLLATE NOCASE, "  bob" sorts first (space < 'A'),
    // then alice/Alice/ALICE grouped together, then bob/BOB, then charlie/Charlie
    auto it = items.begin();
    EXPECT_EQ(it->name, "  bob"); // space sorts before letters
}

TYPED_TEST(CollateTest, OrderByNoCaseDesc) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.template order_by<^^Person::name, Collate::NoCase, false>().select().execute();
    ASSERT_TRUE(result.has_value());

    auto items = result.value();
    ASSERT_EQ(items.size(), 8);

    // DESC + NOCASE: charlie/Charlie first, then bob/BOB, then alice/Alice/ALICE, then "  bob"
    auto it = items.begin();
    // First item should be a charlie variant (case-insensitive DESC)
    EXPECT_TRUE(it->name == "charlie" || it->name == "Charlie");
}

TYPED_TEST(CollateTest, OrderByBinary) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.template order_by<^^Person::name, Collate::Binary>().select().execute();
    ASSERT_TRUE(result.has_value());

    auto items = result.value();
    ASSERT_EQ(items.size(), 8);

    // COLLATE BINARY: strict byte order. Uppercase letters (A=65) come before lowercase (a=97)
    // "  bob" < "ALICE" < "Alice" < "BOB" < "Charlie" < "alice" < "bob" < "charlie"
    auto it = items.begin();
    EXPECT_EQ(it->name, "  bob"); // Space (32) < uppercase
    ++it;
    EXPECT_EQ(it->name, "ALICE"); // 'A'=65
    ++it;
    EXPECT_EQ(it->name, "Alice"); // 'A'=65, 'l'=108 > 'L'=76
    ++it;
    EXPECT_EQ(it->name, "BOB");
    ++it;
    EXPECT_EQ(it->name, "Charlie");
    ++it;
    EXPECT_EQ(it->name, "alice");
    ++it;
    EXPECT_EQ(it->name, "bob");
    ++it;
    EXPECT_EQ(it->name, "charlie");
}

TYPED_TEST(CollateTest, OrderByRTrim) {
    QuerySet<Person, TypeParam> qs;

    // COLLATE RTRIM trims trailing spaces before comparison
    auto result =
            qs.template order_by<^^Person::department, Collate::RTrim, true, ^^Person::name, true>().select().execute();
    ASSERT_TRUE(result.has_value());

    auto items = result.value();
    ASSERT_EQ(items.size(), 8);
}

TYPED_TEST(CollateTest, OrderByCollateWithBoolDirection) {
    QuerySet<Person, TypeParam> qs;

    // Test: field, Collate, bool — both modifiers after field
    auto result = qs.template order_by<^^Person::name, Collate::NoCase, false>().select().execute();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 8);
}

TYPED_TEST(CollateTest, OrderByBoolThenCollate) {
    QuerySet<Person, TypeParam> qs;

    // Test: field, bool, Collate — reversed modifier order
    auto result = qs.template order_by<^^Person::name, false, Collate::NoCase>().select().execute();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 8);
}

TYPED_TEST(CollateTest, OrderByMultipleFieldsWithCollate) {
    QuerySet<Person, TypeParam> qs;

    // ORDER BY name COLLATE NOCASE ASC, age DESC
    auto result = qs.template order_by<^^Person::name, Collate::NoCase, ^^Person::age, false>().select().execute();
    ASSERT_TRUE(result.has_value());

    auto items = result.value();
    ASSERT_EQ(items.size(), 8);
}

// ============================================================================
// WHERE with COLLATE Tests — all 6 comparison operators
// ============================================================================

TYPED_TEST(CollateTest, WhereEqualNoCase) {
    QuerySet<Person, TypeParam> qs;

    // Case-insensitive equality: "alice" should match "alice", "Alice", "ALICE"
    auto result = qs.where(f<^^Person::name>().collate(Collate::NoCase) == "alice").select().execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3);
}

TYPED_TEST(CollateTest, WhereNotEqualNoCase) {
    QuerySet<Person, TypeParam> qs;

    // Case-insensitive not-equal: exclude all "alice" variants → 5 remaining
    auto result = qs.where(f<^^Person::name>().collate(Collate::NoCase) != "alice").select().execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 5);
}

TYPED_TEST(CollateTest, WhereGreaterNoCase) {
    QuerySet<Person, TypeParam> qs;

    // Case-insensitive greater: names > "bob" → charlie variants + leading-space bob
    // Actually "  bob" < "bob" even with NOCASE since space < 'b'
    auto result = qs.where(f<^^Person::name>().collate(Collate::NoCase) > "bob").select().execute();
    ASSERT_TRUE(result.has_value());
    // "charlie", "Charlie" are > "bob" (case-insensitive)
    EXPECT_EQ(result.value().size(), 2);
}

TYPED_TEST(CollateTest, WhereGreaterEqualNoCase) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.where(f<^^Person::name>().collate(Collate::NoCase) >= "bob").select().execute();
    ASSERT_TRUE(result.has_value());
    // "bob", "BOB", "charlie", "Charlie" = 4
    EXPECT_EQ(result.value().size(), 4);
}

TYPED_TEST(CollateTest, WhereLessNoCase) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.where(f<^^Person::name>().collate(Collate::NoCase) < "bob").select().execute();
    ASSERT_TRUE(result.has_value());
    // "alice", "Alice", "ALICE", "  bob" = 4
    EXPECT_EQ(result.value().size(), 4);
}

TYPED_TEST(CollateTest, WhereLessEqualNoCase) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.where(f<^^Person::name>().collate(Collate::NoCase) <= "bob").select().execute();
    ASSERT_TRUE(result.has_value());
    // "alice" x3, "bob" x2, "  bob" = 6
    EXPECT_EQ(result.value().size(), 6);
}

// ============================================================================
// WHERE COLLATE with LIKE
// ============================================================================

TYPED_TEST(CollateTest, WhereLikeNoCase) {
    QuerySet<Person, TypeParam> qs;

    // LIKE with NOCASE: "A%" should match alice, Alice, ALICE
    auto result = qs.where(f<^^Person::name>().collate(Collate::NoCase).like("a%")).select().execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3);
}

// ============================================================================
// WHERE COLLATE with BETWEEN
// ============================================================================

TYPED_TEST(CollateTest, WhereBetweenNoCase) {
    QuerySet<Person, TypeParam> qs;

    // BETWEEN with NOCASE: names between "alice" and "bob" (inclusive)
    auto result =
            qs.where(f<^^Person::name>().collate(Collate::NoCase).between(std::string("alice"), std::string("bob")))
                    .select()
                    .execute();
    ASSERT_TRUE(result.has_value());
    // "alice" x3, "bob" x2 = 5 (not "  bob" — space < 'a')
    EXPECT_EQ(result.value().size(), 5);
}

// ============================================================================
// WHERE COLLATE with IN
// ============================================================================

TYPED_TEST(CollateTest, WhereInNoCase) {
    QuerySet<Person, TypeParam> qs;

    // IN with NOCASE: match "alice" (case-insensitive) and "bob" (case-insensitive)
    // Note: IN comparisons in SQLite still use the collation of the left-hand operand
    auto result = qs.where(f<^^Person::name>().collate(Collate::NoCase).in("alice", "bob")).select().execute();
    ASSERT_TRUE(result.has_value());
    // "alice" x3, "bob" x2 = 5 (not "  bob" — it's "  bob" not "bob")
    EXPECT_EQ(result.value().size(), 5);
}

// ============================================================================
// WHERE COLLATE Binary (exact comparison)
// ============================================================================

TYPED_TEST(CollateTest, WhereEqualBinary) {
    QuerySet<Person, TypeParam> qs;

    // COLLATE BINARY: exact case match
    auto result = qs.where(f<^^Person::name>().collate(Collate::Binary) == "alice").select().execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 1); // Only lowercase "alice"
}

// ============================================================================
// Combined COLLATE in WHERE + ORDER BY
// ============================================================================

TYPED_TEST(CollateTest, WhereCollateWithOrderByCollate) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.where(f<^^Person::name>().collate(Collate::NoCase) >= "bob")
                          .template order_by<^^Person::name, Collate::NoCase>()
                          .select()
                          .execute();
    ASSERT_TRUE(result.has_value());

    auto items = result.value();
    EXPECT_EQ(items.size(), 4); // bob, BOB, charlie, Charlie
}

// ============================================================================
// COLLATE with LIMIT/OFFSET
// ============================================================================

TYPED_TEST(CollateTest, OrderByCollateWithLimit) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.template order_by<^^Person::name, Collate::NoCase>().limit(3).select().execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3);
}

TYPED_TEST(CollateTest, OrderByCollateWithLimitOffset) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.template order_by<^^Person::name, Collate::NoCase>().limit(2).offset(3).select().execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 2);
}

// ============================================================================
// WHERE COLLATE combined with AND/OR logic
// ============================================================================

TYPED_TEST(CollateTest, WhereCollateWithAndLogic) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.where(f<^^Person::name>().collate(Collate::NoCase) == "alice" && f<^^Person::age>() > 30)
                          .select()
                          .execute();
    ASSERT_TRUE(result.has_value());
    // "ALICE" age=33 is the only alice variant with age > 30
    EXPECT_EQ(result.value().size(), 1);
}

TYPED_TEST(CollateTest, WhereCollateWithOrLogic) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.where(f<^^Person::name>().collate(Collate::NoCase) == "alice" ||
                           f<^^Person::name>().collate(Collate::NoCase) == "charlie")
                          .select()
                          .execute();
    ASSERT_TRUE(result.has_value());
    // alice x3, charlie x2 = 5
    EXPECT_EQ(result.value().size(), 5);
}

// ============================================================================
// Repeated queries (statement caching correctness)
// ============================================================================

TYPED_TEST(CollateTest, RepeatedCollateQueries) {
    QuerySet<Person, TypeParam> qs;

    // Same query twice — should get same result (caching correctness)
    auto r1 = qs.where(f<^^Person::name>().collate(Collate::NoCase) == "bob").select().execute();
    auto r2 = qs.where(f<^^Person::name>().collate(Collate::NoCase) == "bob").select().execute();

    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r1.value().size(), r2.value().size());
}

TYPED_TEST(CollateTest, DifferentCollateOnSameField) {
    QuerySet<Person, TypeParam> qs;

    // NOCASE vs BINARY on same field — different results (cache invalidation)
    auto r_nocase = qs.where(f<^^Person::name>().collate(Collate::NoCase) == "alice").select().execute();
    auto r_binary = qs.where(f<^^Person::name>().collate(Collate::Binary) == "alice").select().execute();

    ASSERT_TRUE(r_nocase.has_value());
    ASSERT_TRUE(r_binary.has_value());

    EXPECT_EQ(r_nocase.value().size(), 3); // Case-insensitive: 3 matches
    EXPECT_EQ(r_binary.value().size(), 1); // Exact: 1 match
}

// ============================================================================
// Empty result set
// ============================================================================

TYPED_TEST(CollateTest, WhereCollateNoMatch) {
    QuerySet<Person, TypeParam> qs;

    auto result = qs.where(f<^^Person::name>().collate(Collate::NoCase) == "nonexistent").select().execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 0);
}

// ============================================================================
// SQL generation verification
// ============================================================================

TYPED_TEST(CollateTest, OrderBySqlGeneration) {
    QuerySet<Person, TypeParam> qs;

    auto sql_result = qs.template order_by<^^Person::name, Collate::NoCase>().select().to_sql();
    ASSERT_TRUE(sql_result.has_value());
    const auto& sql = sql_result.value();
    EXPECT_NE(sql.find("COLLATE NOCASE"), std::string::npos) << "SQL should contain COLLATE NOCASE: " << sql;
    EXPECT_NE(sql.find("ORDER BY"), std::string::npos) << "SQL should contain ORDER BY: " << sql;
}

TYPED_TEST(CollateTest, WhereCollateSqlGeneration) {
    QuerySet<Person, TypeParam> qs;

    auto sql_result = qs.where(f<^^Person::name>().collate(Collate::NoCase) == "test").select().to_sql();
    ASSERT_TRUE(sql_result.has_value());
    const auto& sql = sql_result.value();
    EXPECT_NE(sql.find("name COLLATE NOCASE"), std::string::npos)
            << "SQL should contain 'name COLLATE NOCASE': " << sql;
}

// NOLINTEND(misc-const-correctness,performance-unnecessary-copy-initialization)
