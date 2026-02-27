#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)

import storm;
import <string>;
import <string_view>;
import <vector>;
import <expected>;
import <span>;
import <optional>;

using namespace storm;
using namespace storm::orm::where;

// ============================================================================
// Test model for SQL inspection
// ============================================================================

struct SqlPerson {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               name;
    int                                       age{};
};

// ============================================================================
// Test fixture
// ============================================================================

template <typename ConnType> class SqlInspectionTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        const auto& conn_str = storm::test::get_connection_string<ConnType>();
        auto        result   = QuerySet<SqlPerson, ConnType>::set_default_connection(conn_str);
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<SqlPerson, ConnType>::get_default_connection();

        storm::test::pg_schema_init<ConnType>(conn);
        auto create_result = storm::orm::schema::SchemaStatement<SqlPerson>::create_table_if_not_exists(conn);
        ASSERT_TRUE(create_result.has_value()) << "Failed to create table: " << create_result.error().message();

        storm::test::begin_test_txn<ConnType>(conn, {"SqlPerson"});
    }

    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<SqlPerson, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<SqlPerson, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<SqlPerson, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(SqlInspectionTest, DatabaseTypes);

// ============================================================================
// Helper: check that string contains a substring
// ============================================================================

static auto contains(const std::string& str, std::string_view substr) -> bool {
    return str.find(substr) != std::string::npos;
}

// ============================================================================
// SELECT .to_sql() tests
// ============================================================================

TYPED_TEST(SqlInspectionTest, SelectBareToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.select().to_sql();
    ASSERT_TRUE(result.has_value()) << "to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_FALSE(sql.empty()) << "SQL should not be empty";
    EXPECT_TRUE(contains(sql, "SELECT")) << "Should contain SELECT";
    EXPECT_TRUE(contains(sql, "SqlPerson")) << "Should contain table name";
    EXPECT_TRUE(contains(sql, "name")) << "Should contain field 'name'";
    EXPECT_TRUE(contains(sql, "age")) << "Should contain field 'age'";
    // No WHERE clause
    EXPECT_FALSE(contains(sql, "WHERE")) << "Bare select should have no WHERE";
}

TYPED_TEST(SqlInspectionTest, SelectWithWhereToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.where(field<^^SqlPerson::age>() > 30).select().to_sql();
    ASSERT_TRUE(result.has_value()) << "to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "SELECT")) << "Should contain SELECT";
    EXPECT_TRUE(contains(sql, "WHERE")) << "Should contain WHERE";
    EXPECT_TRUE(contains(sql, "age")) << "Should contain field 'age'";
    // Value 30 should appear in the SQL (either as 30 or '30')
    EXPECT_TRUE(contains(sql, "30")) << "Should contain value 30";
}

TYPED_TEST(SqlInspectionTest, SelectWithStringWhereToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.where(field<^^SqlPerson::name>() == "Alice").select().to_sql();
    ASSERT_TRUE(result.has_value()) << "to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "WHERE")) << "Should contain WHERE";
    EXPECT_TRUE(contains(sql, "Alice")) << "Should contain string value 'Alice'";
}

TYPED_TEST(SqlInspectionTest, SelectWithLimitToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.limit(10).select().to_sql();
    ASSERT_TRUE(result.has_value()) << "to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "LIMIT")) << "Should contain LIMIT";
    EXPECT_TRUE(contains(sql, "10")) << "Should contain limit value";
}

TYPED_TEST(SqlInspectionTest, SelectWithOffsetToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.limit(5).offset(10).select().to_sql();
    ASSERT_TRUE(result.has_value()) << "to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "LIMIT")) << "Should contain LIMIT";
    EXPECT_TRUE(contains(sql, "OFFSET")) << "Should contain OFFSET";
    EXPECT_TRUE(contains(sql, "10")) << "Should contain offset value";
}

TYPED_TEST(SqlInspectionTest, SelectWithOrderByToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.template order_by<^^SqlPerson::age>().select().to_sql();
    ASSERT_TRUE(result.has_value()) << "to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "ORDER BY")) << "Should contain ORDER BY";
    EXPECT_TRUE(contains(sql, "age")) << "Should contain sorted field";
}

TYPED_TEST(SqlInspectionTest, SelectWithWhereAndLimitToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.where(field<^^SqlPerson::age>() > 25).limit(5).select().to_sql();
    ASSERT_TRUE(result.has_value()) << "to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "WHERE")) << "Should contain WHERE";
    EXPECT_TRUE(contains(sql, "LIMIT")) << "Should contain LIMIT";
    EXPECT_TRUE(contains(sql, "25")) << "Should contain WHERE value";
    EXPECT_TRUE(contains(sql, "5")) << "Should contain LIMIT value";
}

TYPED_TEST(SqlInspectionTest, SelectOperatorsToSql) {
    QuerySet<SqlPerson, TypeParam> qs;

    // Test all comparison operators generate proper SQL
    auto r_eq = qs.where(field<^^SqlPerson::age>() == 30).select().to_sql();
    auto r_ne = qs.where(field<^^SqlPerson::age>() != 30).select().to_sql();
    auto r_gt = qs.where(field<^^SqlPerson::age>() > 30).select().to_sql();
    auto r_ge = qs.where(field<^^SqlPerson::age>() >= 30).select().to_sql();
    auto r_lt = qs.where(field<^^SqlPerson::age>() < 30).select().to_sql();
    auto r_le = qs.where(field<^^SqlPerson::age>() <= 30).select().to_sql();

    ASSERT_TRUE(r_eq.has_value());
    ASSERT_TRUE(r_ne.has_value());
    ASSERT_TRUE(r_gt.has_value());
    ASSERT_TRUE(r_ge.has_value());
    ASSERT_TRUE(r_lt.has_value());
    ASSERT_TRUE(r_le.has_value());

    EXPECT_TRUE(contains(r_eq.value(), "=")) << "EQ should use =";
    EXPECT_TRUE(contains(r_ne.value(), "!=") || contains(r_ne.value(), "<>")) << "NE should use != or <>";
    EXPECT_TRUE(contains(r_gt.value(), ">")) << "GT should use >";
    EXPECT_TRUE(contains(r_ge.value(), ">=")) << "GE should use >=";
    EXPECT_TRUE(contains(r_lt.value(), "<")) << "LT should use <";
    EXPECT_TRUE(contains(r_le.value(), "<=")) << "LE should use <=";
}

TYPED_TEST(SqlInspectionTest, SelectWithInToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.where(field<^^SqlPerson::age>().in(20, 25, 30)).select().to_sql();
    ASSERT_TRUE(result.has_value()) << "to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "IN")) << "Should contain IN";
    EXPECT_TRUE(contains(sql, "20")) << "Should contain first value";
    EXPECT_TRUE(contains(sql, "25")) << "Should contain second value";
    EXPECT_TRUE(contains(sql, "30")) << "Should contain third value";
}

TYPED_TEST(SqlInspectionTest, SelectWithBetweenToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.where(field<^^SqlPerson::age>().between(20, 40)).select().to_sql();
    ASSERT_TRUE(result.has_value()) << "to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "BETWEEN")) << "Should contain BETWEEN";
    EXPECT_TRUE(contains(sql, "20")) << "Should contain lower bound";
    EXPECT_TRUE(contains(sql, "40")) << "Should contain upper bound";
}

TYPED_TEST(SqlInspectionTest, SelectWithLikeToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.where(field<^^SqlPerson::name>().like("A%")).select().to_sql();
    ASSERT_TRUE(result.has_value()) << "to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "LIKE")) << "Should contain LIKE";
    EXPECT_TRUE(contains(sql, "A%")) << "Should contain LIKE pattern";
}

TYPED_TEST(SqlInspectionTest, SelectWithAndOrToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto result = qs.where((field<^^SqlPerson::age>() > 25) && (field<^^SqlPerson::age>() < 50)).select().to_sql();
    ASSERT_TRUE(result.has_value()) << "to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "AND")) << "Should contain AND";
    EXPECT_TRUE(contains(sql, "25")) << "Should contain lower bound value";
    EXPECT_TRUE(contains(sql, "50")) << "Should contain upper bound value";
}

// ============================================================================
// FIRST .to_sql() tests
// ============================================================================

TYPED_TEST(SqlInspectionTest, FirstBareToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.first().to_sql();
    ASSERT_TRUE(result.has_value()) << "first().to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "SELECT")) << "Should contain SELECT";
    EXPECT_TRUE(contains(sql, "LIMIT 1")) << "first() should use LIMIT 1";
}

TYPED_TEST(SqlInspectionTest, FirstWithWhereToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.where(field<^^SqlPerson::age>() > 25).first().to_sql();
    ASSERT_TRUE(result.has_value()) << "first().to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "WHERE")) << "Should contain WHERE";
    EXPECT_TRUE(contains(sql, "LIMIT 1")) << "first() should use LIMIT 1";
    EXPECT_TRUE(contains(sql, "25")) << "Should contain value";
}

TYPED_TEST(SqlInspectionTest, FirstWithOrderByToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.template order_by<^^SqlPerson::age>().first().to_sql();
    ASSERT_TRUE(result.has_value()) << "first().to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "ORDER BY")) << "Should contain ORDER BY";
    EXPECT_TRUE(contains(sql, "LIMIT 1")) << "first() should use LIMIT 1";
}

// ============================================================================
// GET .to_sql() tests
// ============================================================================

TYPED_TEST(SqlInspectionTest, GetBareToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.get().to_sql();
    ASSERT_TRUE(result.has_value()) << "get().to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "SELECT")) << "Should contain SELECT";
    EXPECT_TRUE(contains(sql, "LIMIT 2")) << "get() should use LIMIT 2";
}

TYPED_TEST(SqlInspectionTest, GetWithWhereToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    auto                           result = qs.where(field<^^SqlPerson::id>() == 42).get().to_sql();
    ASSERT_TRUE(result.has_value()) << "get().to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "WHERE")) << "Should contain WHERE";
    EXPECT_TRUE(contains(sql, "LIMIT 2")) << "get() should use LIMIT 2";
    EXPECT_TRUE(contains(sql, "42")) << "Should contain value";
}

// ============================================================================
// INSERT .to_sql() tests
// ============================================================================

TYPED_TEST(SqlInspectionTest, InsertSingleToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    SqlPerson                      alice{.id = 0, .name = "Alice", .age = 30};

    auto result = qs.insert(alice).to_sql();
    ASSERT_TRUE(result.has_value()) << "insert().to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "INSERT INTO")) << "Should contain INSERT INTO";
    EXPECT_TRUE(contains(sql, "SqlPerson")) << "Should contain table name";
    EXPECT_TRUE(contains(sql, "VALUES")) << "Should contain VALUES";
    EXPECT_TRUE(contains(sql, "Alice")) << "Should contain name value";
    EXPECT_TRUE(contains(sql, "30")) << "Should contain age value";
}

TYPED_TEST(SqlInspectionTest, InsertSingleToSqlDoesNotExecute) {
    QuerySet<SqlPerson, TypeParam> qs;
    SqlPerson                      alice{.id = 0, .name = "Alice", .age = 30};

    // Calling to_sql() should NOT insert into the database
    auto sql_result = qs.insert(alice).to_sql();
    ASSERT_TRUE(sql_result.has_value());

    // Table should still be empty
    auto select_result = qs.select().execute();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_TRUE(select_result.value().empty()) << "to_sql() should not insert data";
}

TYPED_TEST(SqlInspectionTest, InsertBulkToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    std::vector<SqlPerson>         people = {
            SqlPerson{.id = 0, .name = "Alice", .age = 30},
            SqlPerson{.id = 0, .name = "Bob", .age = 25},
    };

    auto result = qs.insert(std::span<const SqlPerson>(people)).to_sql();
    ASSERT_TRUE(result.has_value()) << "bulk insert().to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "INSERT INTO")) << "Should contain INSERT INTO";
    EXPECT_TRUE(contains(sql, "VALUES")) << "Should contain VALUES";
    EXPECT_TRUE(contains(sql, "Alice")) << "Should contain first name";
    EXPECT_TRUE(contains(sql, "Bob")) << "Should contain second name";
}

TYPED_TEST(SqlInspectionTest, InsertWithSpecialCharsToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    SqlPerson                      tricky{.id = 0, .name = "O'Brien", .age = 35};

    auto result = qs.insert(tricky).to_sql();
    ASSERT_TRUE(result.has_value()) << "insert().to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_FALSE(sql.empty());
    EXPECT_TRUE(contains(sql, "O")) << "Should contain part of name";
    EXPECT_TRUE(contains(sql, "Brien")) << "Should contain part of name";
}

// ============================================================================
// UPDATE .to_sql() tests
// ============================================================================

TYPED_TEST(SqlInspectionTest, UpdateSingleToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    // First insert to get a valid ID
    SqlPerson alice{.id = 0, .name = "Alice", .age = 30};
    auto      insert_result = qs.insert(alice).execute();
    ASSERT_TRUE(insert_result.has_value());
    alice.id = static_cast<int>(insert_result.value());

    alice.age   = 31;
    auto result = qs.update(alice).to_sql();
    ASSERT_TRUE(result.has_value()) << "update().to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "UPDATE")) << "Should contain UPDATE";
    EXPECT_TRUE(contains(sql, "SqlPerson")) << "Should contain table name";
    EXPECT_TRUE(contains(sql, "SET")) << "Should contain SET";
    EXPECT_TRUE(contains(sql, "WHERE")) << "Should contain WHERE";
    EXPECT_TRUE(contains(sql, "31")) << "Should contain updated age value";
}

TYPED_TEST(SqlInspectionTest, UpdateToSqlDoesNotExecute) {
    QuerySet<SqlPerson, TypeParam> qs;
    SqlPerson                      alice{.id = 0, .name = "Alice", .age = 30};
    auto                           insert_result = qs.insert(alice).execute();
    ASSERT_TRUE(insert_result.has_value());
    alice.id = static_cast<int>(insert_result.value());

    // Get SQL without executing
    SqlPerson modified = alice;
    modified.age       = 99;
    auto sql_result    = qs.update(modified).to_sql();
    ASSERT_TRUE(sql_result.has_value());

    // Check original value unchanged
    auto select_result = qs.select().execute();
    ASSERT_TRUE(select_result.has_value());
    ASSERT_EQ(select_result.value().size(), 1U);
    EXPECT_EQ(select_result.value().begin()->age, 30) << "to_sql() should not update data";
}

// ============================================================================
// REMOVE .to_sql() tests
// ============================================================================

TYPED_TEST(SqlInspectionTest, RemoveSingleToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    SqlPerson                      alice{.id = 0, .name = "Alice", .age = 30};
    auto                           insert_result = qs.insert(alice).execute();
    ASSERT_TRUE(insert_result.has_value());
    alice.id = static_cast<int>(insert_result.value());

    auto result = qs.remove(alice).to_sql();
    ASSERT_TRUE(result.has_value()) << "remove().to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "DELETE FROM")) << "Should contain DELETE FROM";
    EXPECT_TRUE(contains(sql, "SqlPerson")) << "Should contain table name";
    EXPECT_TRUE(contains(sql, "WHERE")) << "Should contain WHERE (id condition)";
}

TYPED_TEST(SqlInspectionTest, RemoveToSqlDoesNotExecute) {
    QuerySet<SqlPerson, TypeParam> qs;
    SqlPerson                      alice{.id = 0, .name = "Alice", .age = 30};
    auto                           insert_result = qs.insert(alice).execute();
    ASSERT_TRUE(insert_result.has_value());
    alice.id = static_cast<int>(insert_result.value());

    // Get SQL without executing
    auto sql_result = qs.remove(alice).to_sql();
    ASSERT_TRUE(sql_result.has_value());

    // Row should still be there
    auto select_result = qs.select().execute();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 1U) << "to_sql() should not delete data";
}

TYPED_TEST(SqlInspectionTest, RemoveBulkToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    std::vector<SqlPerson>         people = {
            SqlPerson{.id = 0, .name = "Alice", .age = 30},
            SqlPerson{.id = 0, .name = "Bob", .age = 25},
    };

    // Insert and get IDs
    for (auto& p : people) {
        auto r = qs.insert(p).execute();
        ASSERT_TRUE(r.has_value());
        p.id = static_cast<int>(r.value());
    }

    auto result = qs.remove(std::span<const SqlPerson>(people)).to_sql();
    ASSERT_TRUE(result.has_value()) << "bulk remove().to_sql() failed: " << result.error().message();

    const std::string& sql = result.value();
    EXPECT_TRUE(contains(sql, "DELETE FROM")) << "Should contain DELETE FROM";
    EXPECT_FALSE(sql.empty()) << "Bulk delete SQL should not be empty";
}

// ============================================================================
// No side effects: execute() still works after to_sql()
// ============================================================================

TYPED_TEST(SqlInspectionTest, ToSqlAndExecuteAreIndependent) {
    QuerySet<SqlPerson, TypeParam> qs;
    SqlPerson                      alice{.id = 0, .name = "Alice", .age = 30};

    // Call to_sql() first
    auto sql_result = qs.insert(alice).to_sql();
    ASSERT_TRUE(sql_result.has_value());
    EXPECT_FALSE(sql_result.value().empty());

    // Then call execute() — should succeed
    auto exec_result = qs.insert(alice).execute();
    ASSERT_TRUE(exec_result.has_value());

    // Data should now be in the database
    auto select_result = qs.select().execute();
    ASSERT_TRUE(select_result.has_value());
    EXPECT_EQ(select_result.value().size(), 1U);
}

TYPED_TEST(SqlInspectionTest, SelectToSqlAndExecuteAreIndependent) {
    QuerySet<SqlPerson, TypeParam> qs;
    SqlPerson                      alice{.id = 0, .name = "Alice", .age = 30};
    ASSERT_TRUE(qs.insert(alice).execute().has_value());

    // Call to_sql()
    auto sql_result = qs.select().to_sql();
    ASSERT_TRUE(sql_result.has_value());
    EXPECT_FALSE(sql_result.value().empty());

    // Call execute() — should return data
    auto exec_result = qs.select().execute();
    ASSERT_TRUE(exec_result.has_value());
    EXPECT_EQ(exec_result.value().size(), 1U);
}

// ============================================================================
// Empty span edge case
// ============================================================================

TYPED_TEST(SqlInspectionTest, InsertEmptySpanToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    std::vector<SqlPerson>         empty;

    auto result = qs.insert(std::span<const SqlPerson>(empty)).to_sql();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().empty()) << "Empty span should return empty SQL";
}

TYPED_TEST(SqlInspectionTest, RemoveEmptySpanToSql) {
    QuerySet<SqlPerson, TypeParam> qs;
    std::vector<SqlPerson>         empty;

    auto result = qs.remove(std::span<const SqlPerson>(empty)).to_sql();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().empty()) << "Empty span should return empty SQL";
}

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
