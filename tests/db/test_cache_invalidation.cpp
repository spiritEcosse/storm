#include <gtest/gtest.h>
#include <sqlite3.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

import storm;
import storm_db_sqlite;
import <expected>;
import <string>;
import <string_view>;
import <vector>;

#include "test_models.h" // NOSONAR cpp:S954

using storm::db::sqlite::Connection;
using storm::db::sqlite::Statement;

// ============================================================================
// Issue #215 — Phase 1: Cache invalidation
//
// These tests pin the contract for the new per-table clear API, the Level 2
// invalidate_cache() methods on Insert/Update/Erase statements, and Level 1
// propagation via QuerySet::reset() / QuerySet::invalidate_cache(). Test 1
// also locks down the dangling-pointer fix that moves Level 3 storage from
// `unordered_map<string, Statement>` to `unordered_map<string, unique_ptr<...>>`.
// ============================================================================

namespace {

    class CacheInvalidationLevel3Test : public ::testing::Test {
      public:
        Connection conn_{Connection::open(":memory:").value()};
    };

    // ------------------------------------------------------------------ Test 1
    // Pointers returned by prepare_cached must survive arbitrary growth of the
    // underlying cache. With the old `unordered_map<string, Statement>` value
    // storage, the first emplace past the load-factor rehashes and invalidates
    // every Statement* held by upstream callers (Level 2). The fix is
    // `unordered_map<string, unique_ptr<Statement>>` — the unique_ptr stays
    // pinned in place, only the map nodes move.
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    TEST_F(CacheInvalidationLevel3Test, PointersStableAcrossRehash) {
        // Cache an initial statement and stash the pointer.
        auto first = conn_.prepare_cached("SELECT 1");
        ASSERT_TRUE(first.has_value());
        Statement* pinned = *first;
        ASSERT_NE(pinned, nullptr);

        // Force well past the default rehash threshold (libstdc++ starts at 1
        // bucket, libc++ at 2; 64 unique inserts guarantees several rehashes).
        constexpr int churn_count = 64;
        for (int i = 2; i < 2 + churn_count; ++i) {
            auto r = conn_.prepare_cached(std::format("SELECT {}", i));
            ASSERT_TRUE(r.has_value()) << "iteration " << i;
        }

        // Re-fetch the original SQL: must match the stashed pointer, must not
        // crash on reset/execute. With the buggy value storage, `pinned` is
        // dangling at this point.
        auto again = conn_.prepare_cached("SELECT 1");
        ASSERT_TRUE(again.has_value());
        EXPECT_EQ(*again, pinned) << "cached Statement* must survive map rehash";

        // Exercise the pointer to surface ASAN failures even if equality holds
        // by coincidence on a release build.
        pinned->reset();
        auto exec = pinned->execute();
        EXPECT_TRUE(exec.has_value() || !exec.has_value()); // touch the object
    }

    // ------------------------------------------------------------------ Test 2
    // clear_statement_cache(table) must drop entries that reference the table
    // and leave entries for other tables alone. Pre-fix there is no overload;
    // the test will not compile until Level 3 is updated.
    TEST_F(CacheInvalidationLevel3Test, PerTableClearKeepsUnrelatedEntries) {
        ASSERT_TRUE(conn_.execute("CREATE TABLE persons (id INTEGER PRIMARY KEY, name TEXT)").has_value());
        ASSERT_TRUE(conn_.execute("CREATE TABLE messages (id INTEGER PRIMARY KEY, body TEXT)").has_value());

        ASSERT_TRUE(conn_.prepare_cached("SELECT id FROM persons").has_value());
        ASSERT_TRUE(conn_.prepare_cached("SELECT id FROM messages").has_value());
        ASSERT_EQ(conn_.cached_statement_count(), 2U);

        conn_.clear_statement_cache(std::string_view{"persons"});

        EXPECT_EQ(conn_.cached_statement_count(), 1U) << "per-table clear must keep the unrelated `messages` entry";

        // Re-prepare both: persons should be a miss (count back to 2), messages
        // should be a hit (count unchanged after the miss bumped it to 2).
        auto persons_again = conn_.prepare_cached("SELECT id FROM persons");
        ASSERT_TRUE(persons_again.has_value());
        EXPECT_EQ(conn_.cached_statement_count(), 2U);

        auto messages_again = conn_.prepare_cached("SELECT id FROM messages");
        ASSERT_TRUE(messages_again.has_value());
        EXPECT_EQ(conn_.cached_statement_count(), 2U) << "messages cache hit must not grow the cache";
    }

    // ------------------------------------------------------------------ Test 3
    // Per-table clear must respect word boundaries: clearing "persons" must
    // NOT drop "person_addresses". A naive substring match would.
    TEST_F(CacheInvalidationLevel3Test, PerTableClearRespectsWordBoundaries) {
        ASSERT_TRUE(conn_.execute("CREATE TABLE persons (id INTEGER PRIMARY KEY)").has_value());
        ASSERT_TRUE(conn_.execute("CREATE TABLE person_addresses (id INTEGER PRIMARY KEY, street TEXT)").has_value());

        ASSERT_TRUE(conn_.prepare_cached("SELECT id FROM persons").has_value());
        ASSERT_TRUE(conn_.prepare_cached("SELECT id FROM person_addresses").has_value());
        ASSERT_EQ(conn_.cached_statement_count(), 2U);

        conn_.clear_statement_cache(std::string_view{"persons"});

        EXPECT_EQ(conn_.cached_statement_count(), 1U) << "person_addresses must survive a clear targeting `persons`";
    }

    // ============================================================================
    // Level 1 + 2 — invalidation propagation through QuerySet
    // ============================================================================

    template <typename ConnType> class CacheInvalidationLevel1Test : public StormTestFixture<Person, ConnType> {};

    TYPED_TEST_SUITE(CacheInvalidationLevel1Test, DatabaseTypes);

    // ------------------------------------------------------------------ Test 4
    // QuerySet::invalidate_cache() must reach the InsertStatement instance and
    // null its cached Statement* member. The observable consequence: clearing
    // Level 3 immediately after Level 1 invalidation must not leave the insert
    // path holding a dangling pointer. The next .insert().execute() must
    // re-prepare cleanly (cache miss → new entry → count == 1).
    TYPED_TEST(CacheInvalidationLevel1Test, InvalidateCachePropagatesToLevel2) {
        storm::QuerySet<Person, TypeParam> qs;

        // First insert: warms both Level 2 (InsertStatement::cached_*) and
        // Level 3 (Connection::statement_cache_).
        ASSERT_TRUE(qs.insert(Person{.name = "Alice", .age = 30}).execute().has_value());

        const auto& conn = storm::QuerySet<Person, TypeParam>::get_default_connection();
        ASSERT_GE(conn->cached_statement_count(), 1U);

        // Invalidate Level 1+2, then clear Level 3 underneath us. If Level 2
        // still holds the old pointer it now dangles; the next insert would
        // crash under ASAN.
        qs.invalidate_cache();
        conn->clear_statement_cache();
        EXPECT_EQ(conn->cached_statement_count(), 0U);

        ASSERT_TRUE(qs.insert(Person{.name = "Bob", .age = 25}).execute().has_value())
                << "insert after invalidate+clear must re-prepare without UB";
        EXPECT_GE(conn->cached_statement_count(), 1U) << "re-prepared statement must be cached again";
    }

    // ------------------------------------------------------------------ Test 5
    // QuerySet::reset() must invalidate Level 2 caches for every statement type
    // the QuerySet has touched (insert, update, erase, select). Same shape as
    // test 4 but covers all four statement kinds in one shot.
    TYPED_TEST(CacheInvalidationLevel1Test, ResetClearsAllLevel2Caches) {
        storm::QuerySet<Person, TypeParam> qs;

        // Warm every statement type.
        ASSERT_TRUE(qs.insert(Person{.name = "Alice", .age = 30}).execute().has_value());
        ASSERT_TRUE(qs.update(Person{.id = 1, .name = "Alice2", .age = 31}).execute().has_value());
        auto sel_before = qs.select().execute();
        ASSERT_TRUE(sel_before.has_value());

        const auto& conn = storm::QuerySet<Person, TypeParam>::get_default_connection();
        ASSERT_GE(conn->cached_statement_count(), 2U) << "insert + update + select should each cache a statement";

        // reset() invalidates Level 2 across all four kinds; clearing Level 3
        // afterwards would leave a dangling pointer on the buggy code.
        qs.reset();
        conn->clear_statement_cache();
        EXPECT_EQ(conn->cached_statement_count(), 0U);

        // Each operation must re-prepare without UB.
        ASSERT_TRUE(qs.insert(Person{.name = "Bob", .age = 25}).execute().has_value());
        ASSERT_TRUE(qs.update(Person{.id = 1, .name = "Alice3", .age = 32}).execute().has_value());
        auto sel_after = qs.select().execute();
        ASSERT_TRUE(sel_after.has_value());
        // Erase last so the row count is irrelevant to the assertion above.
        ASSERT_TRUE(qs.erase(Person{.id = 1}).execute().has_value());

        EXPECT_GE(conn->cached_statement_count(), 1U);
    }

} // namespace

// NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
