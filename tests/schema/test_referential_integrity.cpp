#include <gtest/gtest.h>

#include "test_db_helpers.h"

// NOLINTBEGIN(misc-const-correctness)

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954

using storm::QuerySet;
using storm::orm::schema::Dialect;
using storm::orm::schema::SchemaStatement;

// ============================================================================
// Referential Integrity — FK REFERENCES (#412)
//
// Always-on: every FieldAttr::fk column emits REFERENCES <Related>(id). Base
// FKs use plain REFERENCES (= RESTRICT / NO ACTION, the SQL default). The
// related table name is the FK field's C++ type identifier; the PK is always
// "id". Both dialects.
// ============================================================================

// Test: a single FK column emits REFERENCES to its related table (SQLite).
TEST(SchemaFkRefTest, MessageFkEmitsReferencesSqlite) {
    const std::string& sql = storm::create_table_sql<Message>();
    EXPECT_NE(sql.find("sender_id INTEGER NOT NULL REFERENCES Person(id)"), std::string::npos)
            << "Expected FK column to carry REFERENCES Person(id) in: " << sql;
}

// Test: same column emits REFERENCES in the PostgreSQL dialect.
TEST(SchemaFkRefTest, MessageFkEmitsReferencesPostgres) {
    const std::string& sql = storm::create_table_sql<Message, Dialect::PostgreSQL>();
    EXPECT_NE(sql.find("sender_id BIGINT NOT NULL REFERENCES Person(id)"), std::string::npos)
            << "Expected FK column to carry REFERENCES Person(id) in: " << sql;
}

// Test: multiple FKs to the same table each get their own REFERENCES (SQLite).
TEST(SchemaFkRefTest, TaskBothFksEmitReferencesSqlite) {
    const std::string& sql = storm::create_table_sql<Task>();
    EXPECT_NE(sql.find("assignee_id INTEGER NOT NULL REFERENCES Person(id)"), std::string::npos)
            << "Expected assignee FK to reference Person(id) in: " << sql;
    EXPECT_NE(sql.find("reviewer_id INTEGER NOT NULL REFERENCES Person(id)"), std::string::npos)
            << "Expected reviewer FK to reference Person(id) in: " << sql;
}

// Test: a nullable FK (optional<Related>) still emits REFERENCES, without NOT NULL.
struct NullableFkRow {
    [[= storm::meta::FieldAttr::primary]] int              id{};
    [[= storm::meta::FieldAttr::fk]] std::optional<Person> owner;
    std::string                                            label;
};

TEST(SchemaFkRefTest, NullableFkEmitsReferencesWithoutNotNull) {
    const std::string& sql = storm::create_table_sql<NullableFkRow>();
    EXPECT_NE(sql.find("owner_id INTEGER REFERENCES Person(id)"), std::string::npos)
            << "Nullable FK should be 'owner_id INTEGER REFERENCES Person(id)' (no NOT NULL) in: " << sql;
    const std::size_t owner_pos = sql.find("owner_id INTEGER");
    ASSERT_NE(owner_pos, std::string::npos);
    EXPECT_EQ(sql.substr(owner_pos, 16).find("NOT NULL"), std::string::npos)
            << "Nullable FK must not carry NOT NULL, got: " << sql.substr(owner_pos, 40);
}

// Junction-table FOREIGN KEY tests (#412) live in tests/query/test_many_to_many.cpp,
// where the Student/Course m2m models already compile (they pull in plf_hive, which
// cannot be included in an `import std;` TU like this one).

// ============================================================================
// Referential Integrity — parent tables created first (#412)
//
// With always-on REFERENCES, a referencing table cannot be created before its
// FK target. create_table_if_not_exists<T> must create the referenced parent
// tables first. PG rejects a REFERENCES to a non-existent table; SQLite tolerated
// it. A fresh in-memory SQLite connection where neither table pre-exists is the
// minimal reproduction.
// ============================================================================

TEST(SchemaParentOrderTest, CreatesReferencedParentTableFirst) {
    using Connection = storm::db::sqlite::Connection;
    auto conn        = std::make_shared<Connection>(Connection::open(":memory:").value());

    // Message REFERENCES Person — Person must be created first. Creating only Message
    // must still succeed and leave BOTH tables present.
    auto created = SchemaStatement<Message>::create_table_if_not_exists(conn);
    ASSERT_TRUE(created.has_value()) << "create_table_if_not_exists<Message> failed: "
                                     << (created.has_value() ? std::string{} : created.error().message());

    for (std::string_view table : {"Person", "Message"}) {
        auto stmt = conn->prepare(std::format("SELECT 1 FROM {} LIMIT 1", table));
        EXPECT_TRUE(stmt.has_value()) << "Expected table '" << table << "' to exist after create_table_if_not_exists";
    }
}

// ============================================================================
// Referential Integrity — runtime enforcement (#412)
//
// TYPED_TEST over SQLite + PostgreSQL. With REFERENCES emitted AND (SQLite)
// PRAGMA foreign_keys = ON, the database must reject a dangling-FK insert and
// a delete of a referenced parent.
// ============================================================================

template <typename ConnType> class ReferentialIntegrityTest : public StormTestFixture<Person, ConnType, Task> {
  public:
    // Insert one Person and return its generated id (the shared setup for the
    // valid-FK and referenced-parent-delete cases).
    static auto seed_person() -> int {
        QuerySet<Person, ConnType> user_qs;
        auto                       result = user_qs.insert(Person{.id = 0, .name = "Alice", .age = 30}).execute();
        EXPECT_TRUE(result.has_value()) << (result.has_value() ? std::string{} : result.error().message());
        return result.has_value() ? static_cast<int>(result.value()) : 0;
    }
};

TYPED_TEST_SUITE(ReferentialIntegrityTest, DatabaseTypes);

// Test: inserting a child whose FK points at a non-existent parent is rejected.
TYPED_TEST(ReferentialIntegrityTest, RejectsInsertWithDanglingFk) {
    QuerySet<Task, TypeParam> task_qs;

    // No Person rows exist (fresh table) — assignee/reviewer id 999 are dangling.
    Task const orphan{.id = 0, .assignee = Person{.id = 999}, .reviewer = Person{.id = 999}, .description = "dangling"};

    auto result = task_qs.insert(orphan).execute();
    EXPECT_FALSE(result.has_value()) << "Insert with a dangling FK must be rejected by the database";
}

// Test: a valid FK insert still succeeds (the constraint does not over-reject).
TYPED_TEST(ReferentialIntegrityTest, AcceptsInsertWithValidFk) {
    QuerySet<Task, TypeParam> task_qs;
    int const                 alice_id = TestFixture::seed_person();

    Task const valid{
            .id = 0, .assignee = Person{.id = alice_id}, .reviewer = Person{.id = alice_id}, .description = "v"
    };
    auto result = task_qs.insert(valid).execute();
    EXPECT_TRUE(result.has_value()) << "Valid FK insert must succeed: "
                                    << (result.has_value() ? std::string{} : result.error().message());
}

// Test: deleting a parent that is still referenced is rejected (RESTRICT default).
TYPED_TEST(ReferentialIntegrityTest, RejectsDeleteOfReferencedParent) {
    QuerySet<Person, TypeParam> user_qs;
    QuerySet<Task, TypeParam>   task_qs;
    int const                   alice_id = TestFixture::seed_person();

    Task const ref{.id = 0, .assignee = Person{.id = alice_id}, .reviewer = Person{.id = alice_id}, .description = "r"};
    ASSERT_TRUE(task_qs.insert(ref).execute().has_value());

    // Alice is still referenced by the task — RESTRICT must block the delete.
    using storm::orm::where::f;
    auto del = user_qs.where(f<^^Person::id>() == alice_id).erase().execute();
    EXPECT_FALSE(del.has_value()) << "Deleting a referenced parent must be rejected (RESTRICT)";
}

// NOLINTEND(misc-const-correctness)
