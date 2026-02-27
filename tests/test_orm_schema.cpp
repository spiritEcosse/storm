#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)

import storm;
import <string>;
import <string_view>;
import <expected>;
import <optional>;
import <vector>;
import <cstdint>;

using namespace storm;

#include "test_models.h" // NOSONAR cpp:S954

// ============================================================================
// SQL Generation Unit Tests (no DB connection needed)
// ============================================================================

// Test: Person CREATE TABLE SQL matches expected hand-written string verbatim
TEST(SchemaUnitTest, PersonSqlMatchesHandWritten) {
    const std::string expected = "CREATE TABLE Person (\n"
                                 "    id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
                                 "    name TEXT NOT NULL,\n"
                                 "    age INTEGER NOT NULL,\n"
                                 "    salary REAL NOT NULL,\n"
                                 "    is_active INTEGER NOT NULL,\n"
                                 "    years_experience INTEGER NOT NULL,\n"
                                 "    score INTEGER,\n"
                                 "    nickname TEXT,\n"
                                 "    avatar BLOB\n"
                                 ")";

    const std::string& generated = storm::create_table_sql<Person>();
    EXPECT_EQ(generated, expected) << "Generated SQL:\n" << generated << "\n\nExpected SQL:\n" << expected;
}

// Test: Message CREATE TABLE SQL matches expected hand-written string verbatim
TEST(SchemaUnitTest, MessageSqlMatchesHandWritten) {
    const std::string expected = "CREATE TABLE Message (\n"
                                 "    id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
                                 "    content TEXT NOT NULL,\n"
                                 "    value INTEGER NOT NULL,\n"
                                 "    sender_id INTEGER NOT NULL\n"
                                 ")";

    const std::string& generated = storm::create_table_sql<Message>();
    EXPECT_EQ(generated, expected) << "Generated SQL:\n" << generated << "\n\nExpected SQL:\n" << expected;
}

// Test: Person id field generates PRIMARY KEY AUTOINCREMENT
TEST(SchemaUnitTest, PersonIdFieldIsPrimaryKeyAutoincrement) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_NE(sql.find("id INTEGER PRIMARY KEY AUTOINCREMENT"), std::string::npos)
            << "Expected 'id INTEGER PRIMARY KEY AUTOINCREMENT' in: " << sql;
}

// Test: Person name field generates TEXT NOT NULL
TEST(SchemaUnitTest, PersonNameFieldIsTextNotNull) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_NE(sql.find("name TEXT NOT NULL"), std::string::npos) << "Expected 'name TEXT NOT NULL' in: " << sql;
}

// Test: Person age field generates INTEGER NOT NULL
TEST(SchemaUnitTest, PersonAgeFieldIsIntegerNotNull) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_NE(sql.find("age INTEGER NOT NULL"), std::string::npos) << "Expected 'age INTEGER NOT NULL' in: " << sql;
}

// Test: Person salary field generates REAL NOT NULL
TEST(SchemaUnitTest, PersonSalaryFieldIsRealNotNull) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_NE(sql.find("salary REAL NOT NULL"), std::string::npos) << "Expected 'salary REAL NOT NULL' in: " << sql;
}

// Test: Person is_active field generates INTEGER NOT NULL (bool maps to INTEGER)
TEST(SchemaUnitTest, PersonIsActiveFieldIsIntegerNotNull) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_NE(sql.find("is_active INTEGER NOT NULL"), std::string::npos)
            << "Expected 'is_active INTEGER NOT NULL' in: " << sql;
}

// Test: Person years_experience field generates INTEGER NOT NULL
TEST(SchemaUnitTest, PersonYearsExperienceFieldIsIntegerNotNull) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_NE(sql.find("years_experience INTEGER NOT NULL"), std::string::npos)
            << "Expected 'years_experience INTEGER NOT NULL' in: " << sql;
}

// Test: Person score field generates INTEGER (nullable — optional<int>)
TEST(SchemaUnitTest, PersonScoreFieldIsNullableInteger) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_NE(sql.find("score INTEGER,"), std::string::npos) << "Expected 'score INTEGER,' (nullable) in: " << sql;
    const size_t score_pos = sql.find("score INTEGER");
    ASSERT_NE(score_pos, std::string::npos);
    const std::string after_score = sql.substr(score_pos, 20);
    EXPECT_EQ(after_score.find("NOT NULL"), std::string::npos)
            << "score should be nullable (no NOT NULL), got: " << after_score;
}

// Test: Person nickname field generates TEXT (nullable — optional<string>)
TEST(SchemaUnitTest, PersonNicknameFieldIsNullableText) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_NE(sql.find("nickname TEXT,"), std::string::npos) << "Expected 'nickname TEXT,' (nullable) in: " << sql;
    const size_t nick_pos = sql.find("nickname TEXT");
    ASSERT_NE(nick_pos, std::string::npos);
    const std::string after_nick = sql.substr(nick_pos, 20);
    EXPECT_EQ(after_nick.find("NOT NULL"), std::string::npos)
            << "nickname should be nullable (no NOT NULL), got: " << after_nick;
}

// Test: Person avatar field generates BLOB (nullable — vector<uint8_t>)
TEST(SchemaUnitTest, PersonAvatarFieldIsBlob) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_NE(sql.find("avatar BLOB"), std::string::npos) << "Expected 'avatar BLOB' in: " << sql;
    const size_t avatar_pos = sql.find("avatar BLOB");
    ASSERT_NE(avatar_pos, std::string::npos);
    const std::string after_avatar = sql.substr(avatar_pos, 20);
    EXPECT_EQ(after_avatar.find("NOT NULL"), std::string::npos)
            << "avatar should be nullable (no NOT NULL), got: " << after_avatar;
}

// Test: Message FK field generates sender_id (not sender)
TEST(SchemaUnitTest, MessageFkFieldGeneratesSenderId) {
    const std::string& sql = storm::create_table_sql<Message>();
    EXPECT_NE(sql.find("sender_id INTEGER NOT NULL"), std::string::npos)
            << "Expected 'sender_id INTEGER NOT NULL' in: " << sql;
    EXPECT_EQ(sql.find(" sender "), std::string::npos) << "Should not contain bare 'sender' column, got: " << sql;
}

// Test: create_table_sql() returns consistent value across multiple calls (caching)
TEST(SchemaUnitTest, CreateTableSqlIsCached) {
    const std::string& sql1 = storm::create_table_sql<Person>();
    const std::string& sql2 = storm::create_table_sql<Person>();
    EXPECT_EQ(&sql1, &sql2) << "create_table_sql() should return the same cached reference";
}

// Test: SQL starts with CREATE TABLE <name> (
TEST(SchemaUnitTest, PersonSqlStartsWithCreateTable) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_EQ(sql.substr(0, 21), "CREATE TABLE Person (") << "SQL should start with 'CREATE TABLE Person ('";
}

// Test: SQL ends with closing paren
TEST(SchemaUnitTest, PersonSqlEndsWithClosingParen) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_EQ(sql.back(), ')') << "SQL should end with ')'";
}

// ============================================================================
// Integration Tests (TYPED_TEST — both SQLite + PostgreSQL)
//
// SetUp uses ensure_table to create the Person table and handle PostgreSQL
// schema isolation (same pattern as every other test fixture). Tests then
// call SchemaStatement<T>::create_table_if_not_exists(conn) which verifies
// the IF NOT EXISTS path and dialect transforms on both backends.
// ============================================================================

template <typename ConnType> class SchemaTest : public StormTestFixture<Person, ConnType> {
  protected:
    auto SetUp() -> void override {
        if (!this->setup_connection()) {
            GTEST_SKIP() << "Backend unavailable";
            return;
        }
        const auto& conn          = QuerySet<Person, ConnType>::get_default_connection();
        auto        create_result = storm::test::ensure_table<Person, ConnType>(conn);
        ASSERT_TRUE(create_result.has_value()) << "Failed to create Person table: " << create_result.error().message();
        storm::test::begin_test_txn<ConnType>(conn, {"Person"});
    }
};

TYPED_TEST_SUITE(SchemaTest, DatabaseTypes);

// Test: create_table_if_not_exists() for Person succeeds
// Person already exists via ensure_table in SetUp — exercises IF NOT EXISTS semantics
TYPED_TEST(SchemaTest, CreatePersonTableSucceeds) {
    const auto& conn   = QuerySet<Person, TypeParam>::get_default_connection();
    auto        result = orm::schema::SchemaStatement<Person>::create_table_if_not_exists(conn);
    ASSERT_TRUE(result.has_value()) << "create_table_if_not_exists() failed: " << result.error().message();
}

// Test: create_table_if_not_exists() for Message succeeds (fresh table — not created in SetUp)
TYPED_TEST(SchemaTest, CreateMessageTableSucceeds) {
    const auto& conn   = QuerySet<Person, TypeParam>::get_default_connection();
    auto        result = orm::schema::SchemaStatement<Message>::create_table_if_not_exists(conn);
    ASSERT_TRUE(result.has_value()) << "create_table_if_not_exists() for Message failed: " << result.error().message();
}

// Test: create_table_if_not_exists() is idempotent (IF NOT EXISTS — can call twice)
TYPED_TEST(SchemaTest, CreateTableIsIdempotent) {
    const auto& conn    = QuerySet<Person, TypeParam>::get_default_connection();
    auto        result1 = orm::schema::SchemaStatement<Person>::create_table_if_not_exists(conn);
    ASSERT_TRUE(result1.has_value()) << "First call failed: " << result1.error().message();

    auto result2 = orm::schema::SchemaStatement<Person>::create_table_if_not_exists(conn);
    ASSERT_TRUE(result2.has_value()) << "Second call failed (not idempotent): " << result2.error().message();
}

// Test: After create_table_if_not_exists(), Person table is queryable (correct schema)
TYPED_TEST(SchemaTest, PersonTableIsUsableAfterCreate) {
    const auto& conn          = QuerySet<Person, TypeParam>::get_default_connection();
    auto        create_result = orm::schema::SchemaStatement<Person>::create_table_if_not_exists(conn);
    ASSERT_TRUE(create_result.has_value()) << "Create table failed: " << create_result.error().message();

    QuerySet<Person, TypeParam> qs;
    auto                        select_result = qs.select().execute();
    ASSERT_TRUE(select_result.has_value())
            << "SELECT on created Person table failed: " << select_result.error().message();
}

// Test: After create_table_if_not_exists(), Message table is queryable (correct schema)
TYPED_TEST(SchemaTest, MessageTableIsUsableAfterCreate) {
    const auto& conn       = QuerySet<Person, TypeParam>::get_default_connection();
    auto        create_msg = orm::schema::SchemaStatement<Message>::create_table_if_not_exists(conn);
    ASSERT_TRUE(create_msg.has_value()) << "Create Message table failed: " << create_msg.error().message();

    QuerySet<Message, TypeParam> msg_qs;
    auto                         msg_select = msg_qs.select().execute();
    ASSERT_TRUE(msg_select.has_value()) << "SELECT on created Message table failed: " << msg_select.error().message();
}

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
