#include <gtest/gtest.h>
#include <meta>
#include <print>

#include "test_db_helpers.h"

// NOLINTBEGIN(readability-uppercase-literal-suffix)

import storm;
import std;

using namespace storm;

#include "test_models.h" // NOSONAR cpp:S954

// ============================================================================
// SQL Generation Unit Tests (no DB connection needed)
// ============================================================================

// Test: Person CREATE TABLE SQL matches expected hand-written string verbatim
TEST(SchemaUnitTest, PersonSqlMatchesHandWritten) {
    const std::string expected = "CREATE TABLE Person (\n"
                                 "    id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
                                 "    name TEXT NOT NULL UNIQUE,\n"
                                 "    age INTEGER NOT NULL,\n"
                                 "    salary REAL NOT NULL,\n"
                                 "    is_active INTEGER NOT NULL DEFAULT 0,\n"
                                 "    years_experience INTEGER NOT NULL,\n"
                                 "    department TEXT NOT NULL,\n"
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

// Test: Person name field generates TEXT NOT NULL UNIQUE
TEST(SchemaUnitTest, PersonNameFieldIsTextNotNullUnique) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_NE(sql.find("name TEXT NOT NULL UNIQUE"), std::string::npos)
            << "Expected 'name TEXT NOT NULL UNIQUE' in: " << sql;
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

// Test: Person is_active field generates INTEGER NOT NULL DEFAULT 0
// (bool maps to INTEGER; bool{} default member init → DEFAULT 0, issue #344)
TEST(SchemaUnitTest, PersonIsActiveFieldIsIntegerNotNull) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_NE(sql.find("is_active INTEGER NOT NULL DEFAULT 0"), std::string::npos)
            << "Expected 'is_active INTEGER NOT NULL DEFAULT 0' in: " << sql;
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
    const std::size_t score_pos = sql.find("score INTEGER");
    ASSERT_NE(score_pos, std::string::npos);
    const std::string after_score = sql.substr(score_pos, 20);
    EXPECT_EQ(after_score.find("NOT NULL"), std::string::npos)
            << "score should be nullable (no NOT NULL), got: " << after_score;
}

// Test: Person nickname field generates TEXT (nullable — optional<string>)
TEST(SchemaUnitTest, PersonNicknameFieldIsNullableText) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_NE(sql.find("nickname TEXT,"), std::string::npos) << "Expected 'nickname TEXT,' (nullable) in: " << sql;
    const std::size_t nick_pos = sql.find("nickname TEXT");
    ASSERT_NE(nick_pos, std::string::npos);
    const std::string after_nick = sql.substr(nick_pos, 20);
    EXPECT_EQ(after_nick.find("NOT NULL"), std::string::npos)
            << "nickname should be nullable (no NOT NULL), got: " << after_nick;
}

// Test: Person avatar field generates BLOB (nullable — vector<uint8_t>)
TEST(SchemaUnitTest, PersonAvatarFieldIsBlob) {
    const std::string& sql = storm::create_table_sql<Person>();
    EXPECT_NE(sql.find("avatar BLOB"), std::string::npos) << "Expected 'avatar BLOB' in: " << sql;
    const std::size_t avatar_pos = sql.find("avatar BLOB");
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
// INDEX SQL Generation Unit Tests (no DB connection needed)
// ============================================================================

// Test: Person index SQL contains CREATE INDEX for the indexed department field
TEST(SchemaUnitTest, PersonIndexSqlContainsIndexedField) {
    const auto& indexes = storm::create_index_sql<Person>();
    bool        found   = false;
    for (const auto& sql : indexes) {
        if (sql.contains("CREATE INDEX IF NOT EXISTS idx_Person_department ON Person(department)")) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected CREATE INDEX for department field";
}

// Test: Person index SQL contains CREATE UNIQUE INDEX for the unique name field
TEST(SchemaUnitTest, PersonIndexSqlContainsUniqueField) {
    const auto& indexes = storm::create_index_sql<Person>();
    bool        found   = false;
    for (const auto& sql : indexes) {
        if (sql.contains("CREATE UNIQUE INDEX IF NOT EXISTS idx_Person_name ON Person(name)")) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected CREATE UNIQUE INDEX for name field";
}

// Test: Person has exactly 4 index SQL statements (2 single-column + 2 composite)
TEST(SchemaUnitTest, PersonIndexSqlCount) {
    const auto& indexes = storm::create_index_sql<Person>();
    EXPECT_EQ(indexes.size(), 4u) << "Expected 4 indexes (name + department + 2 composite), got " << indexes.size();
}

// Test: Message index SQL contains CREATE INDEX for FK sender_id field
TEST(SchemaUnitTest, MessageIndexSqlContainsFkField) {
    const auto& indexes = storm::create_index_sql<Message>();
    bool        found   = false;
    for (const auto& sql : indexes) {
        if (sql.contains("CREATE INDEX IF NOT EXISTS idx_Message_sender_id ON Message(sender_id)")) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected CREATE INDEX for sender_id FK field";
}

// Test: SimpleRecord has no indexed/unique/FK fields — empty index list
TEST(SchemaUnitTest, SimpleRecordIndexSqlIsEmpty) {
    const auto& indexes = storm::create_index_sql<SimpleRecord>();
    EXPECT_TRUE(indexes.empty()) << "Expected no indexes for SimpleRecord, got " << indexes.size();
}

// Test: Task has 2 FK indexes (assignee_id + reviewer_id)
TEST(SchemaUnitTest, TaskIndexSqlContainsTwoFkFields) {
    const auto& indexes = storm::create_index_sql<Task>();
    EXPECT_EQ(indexes.size(), 2u) << "Expected 2 indexes for Task (2 FKs), got " << indexes.size();
}

// ============================================================================
// COMPOSITE INDEX SQL Generation Unit Tests
// ============================================================================

// Test: Person composite index SQL contains CREATE INDEX for (department, age)
TEST(SchemaUnitTest, CompositeIndexSqlContainsDeptAge) {
    const auto& indexes = storm::create_index_sql<Person>();
    bool        found   = false;
    for (const auto& sql : indexes) {
        if (sql.contains("CREATE INDEX IF NOT EXISTS idx_Person_department_age ON Person(department, age)")) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected composite CREATE INDEX for (department, age)";
}

// Test: Person composite unique index SQL contains CREATE UNIQUE INDEX for (name, department)
TEST(SchemaUnitTest, CompositeUniqueIndexSqlContainsNameDept) {
    const auto& indexes = storm::create_index_sql<Person>();
    bool        found   = false;
    for (const auto& sql : indexes) {
        if (sql.contains("CREATE UNIQUE INDEX IF NOT EXISTS idx_Person_name_department ON Person(name, department)")) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected composite CREATE UNIQUE INDEX for (name, department)";
}

// Test: SimpleRecord has no composite indexes (no Indexes<> specialization)
TEST(SchemaUnitTest, SimpleRecordNoCompositeIndexes) {
    const auto& indexes = storm::create_index_sql<SimpleRecord>();
    EXPECT_TRUE(indexes.empty()) << "Expected no indexes for SimpleRecord, got " << indexes.size();
}

// ============================================================================
// Integration Tests (TYPED_TEST — both SQLite + PostgreSQL)
//
// SetUp uses ensure_table to create the Person table and handle PostgreSQL
// schema isolation (same pattern as every other test fixture). Tests then
// call SchemaStatement<T>::create_table_if_not_exists(conn) which verifies
// the IF NOT EXISTS path and dialect transforms on both backends.
// ============================================================================

template <typename ConnType> class SchemaTest : public StormTestFixture<Person, ConnType> {};

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

// ============================================================================
// INDEX Integration Tests (TYPED_TEST — both SQLite + PostgreSQL)
// ============================================================================

// Test: create_indexes_if_not_exist() for Person executes without error
TYPED_TEST(SchemaTest, CreatePersonIndexesSucceeds) {
    const auto& conn   = QuerySet<Person, TypeParam>::get_default_connection();
    auto        result = orm::schema::SchemaStatement<Person>::create_indexes_if_not_exist(conn);
    ASSERT_TRUE(result.has_value()) << "create_indexes_if_not_exist() failed: " << result.error().message();
}

// Test: create_indexes_if_not_exist() is idempotent (IF NOT EXISTS — can call twice)
TYPED_TEST(SchemaTest, CreateIndexesIsIdempotent) {
    const auto& conn    = QuerySet<Person, TypeParam>::get_default_connection();
    auto        result1 = orm::schema::SchemaStatement<Person>::create_indexes_if_not_exist(conn);
    ASSERT_TRUE(result1.has_value()) << "First call failed: " << result1.error().message();

    auto result2 = orm::schema::SchemaStatement<Person>::create_indexes_if_not_exist(conn);
    ASSERT_TRUE(result2.has_value()) << "Second call failed (not idempotent): " << result2.error().message();
}

// Test: FK auto-index works for Message.sender_id
TYPED_TEST(SchemaTest, CreateMessageFkIndexSucceeds) {
    const auto& conn = QuerySet<Person, TypeParam>::get_default_connection();
    ASSERT_TRUE((storm::test::ensure_table<Message, TypeParam>(conn).has_value()));

    auto result = orm::schema::SchemaStatement<Message>::create_indexes_if_not_exist(conn);
    ASSERT_TRUE(result.has_value()) << "create_indexes_if_not_exist() for Message failed: " << result.error().message();
}

// ============================================================================
// PostgreSQL Dialect SQL Generation Unit Tests (no DB connection needed)
// ============================================================================

using storm::orm::schema::Dialect;
using storm::orm::schema::SchemaStatement;

TEST(PgDialectSchemaTest, PersonPgSqlMatchesHandWritten) {
    const std::string expected = "CREATE TABLE Person (\n"
                                 "    id BIGINT PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY,\n"
                                 "    name TEXT NOT NULL UNIQUE,\n"
                                 "    age BIGINT NOT NULL,\n"
                                 "    salary DOUBLE PRECISION NOT NULL,\n"
                                 "    is_active BOOLEAN NOT NULL DEFAULT FALSE,\n"
                                 "    years_experience BIGINT NOT NULL,\n"
                                 "    department TEXT NOT NULL,\n"
                                 "    score BIGINT,\n"
                                 "    nickname TEXT,\n"
                                 "    avatar BYTEA\n"
                                 ")";

    const std::string& generated = SchemaStatement<Person>::create_table_sql<Dialect::PostgreSQL>();
    EXPECT_EQ(generated, expected) << "Generated PG SQL:\n" << generated << "\n\nExpected PG SQL:\n" << expected;
}

TEST(PgDialectSchemaTest, PersonPgSqlHasBigintPrimaryKey) {
    const std::string& sql = SchemaStatement<Person>::create_table_sql<Dialect::PostgreSQL>();
    EXPECT_NE(sql.find("id BIGINT PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY"), std::string::npos)
            << "Expected PG-style PK in: " << sql;
    EXPECT_EQ(sql.find("AUTOINCREMENT"), std::string::npos) << "Should not contain AUTOINCREMENT in PG dialect";
}

TEST(PgDialectSchemaTest, PersonPgSqlHasBigintAge) {
    const std::string& sql = SchemaStatement<Person>::create_table_sql<Dialect::PostgreSQL>();
    EXPECT_NE(sql.find("age BIGINT NOT NULL"), std::string::npos) << "Expected 'age BIGINT NOT NULL' in: " << sql;
}

TEST(PgDialectSchemaTest, PersonPgSqlHasDoublePrecisionSalary) {
    const std::string& sql = SchemaStatement<Person>::create_table_sql<Dialect::PostgreSQL>();
    EXPECT_NE(sql.find("salary DOUBLE PRECISION NOT NULL"), std::string::npos)
            << "Expected 'salary DOUBLE PRECISION NOT NULL' in: " << sql;
}

TEST(PgDialectSchemaTest, PersonPgSqlHasBytea) {
    const std::string& sql = SchemaStatement<Person>::create_table_sql<Dialect::PostgreSQL>();
    EXPECT_NE(sql.find("avatar BYTEA"), std::string::npos) << "Expected 'avatar BYTEA' in: " << sql;
    EXPECT_EQ(sql.find("BLOB"), std::string::npos) << "Should not contain BLOB in PG dialect";
}

TEST(PgDialectSchemaTest, MessagePgSqlHasBigintFk) {
    const std::string& sql = SchemaStatement<Message>::create_table_sql<Dialect::PostgreSQL>();
    EXPECT_NE(sql.find("sender_id BIGINT NOT NULL"), std::string::npos)
            << "Expected 'sender_id BIGINT NOT NULL' in: " << sql;
}

TEST(PgDialectSchemaTest, MessagePgSqlMatchesHandWritten) {
    const std::string expected = "CREATE TABLE Message (\n"
                                 "    id BIGINT PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY,\n"
                                 "    content TEXT NOT NULL,\n"
                                 "    value BIGINT NOT NULL,\n"
                                 "    sender_id BIGINT NOT NULL\n"
                                 ")";

    const std::string& generated = SchemaStatement<Message>::create_table_sql<Dialect::PostgreSQL>();
    EXPECT_EQ(generated, expected) << "Generated PG SQL:\n" << generated << "\n\nExpected PG SQL:\n" << expected;
}

TEST(PgDialectSchemaTest, SqliteDefaultUnchanged) {
    const std::string& sql = SchemaStatement<Person>::create_table_sql();
    EXPECT_NE(sql.find("id INTEGER PRIMARY KEY AUTOINCREMENT"), std::string::npos)
            << "Default (SQLite) dialect should be unchanged";
}

TEST(PgDialectSchemaTest, PgSqlIsCached) {
    const std::string& sql1 = SchemaStatement<Person>::create_table_sql<Dialect::PostgreSQL>();
    const std::string& sql2 = SchemaStatement<Person>::create_table_sql<Dialect::PostgreSQL>();
    EXPECT_EQ(&sql1, &sql2) << "PG create_table_sql() should return the same cached reference";
}

// ============================================================================
// Reflection Unit Tests
// ============================================================================

TEST(PersonReflection, PrimaryKeyTest) {
    constexpr auto primary_key_member = storm::meta::find_primary_key<Person>();
    constexpr auto primary_key_name   = std::meta::identifier_of(primary_key_member);

    EXPECT_EQ(primary_key_name, "id") << "Expected to find 'id' field as primary key";
    EXPECT_FALSE(primary_key_name.empty()) << "Primary key field should be found";

    std::println(
            "Person primary key found: {:.{}}", primary_key_name.data(), static_cast<int>(primary_key_name.size())
    );
}

// NOLINTEND(readability-uppercase-literal-suffix)
