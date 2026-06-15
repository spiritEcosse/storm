#include <gtest/gtest.h>
#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"

// Tests for the public transaction API (#415):
//   - storm::TransactionGuard re-exported from storm.cppm
//   - storm::begin(conn) thin factory returning the guard
//   - success path COMMITs, early-return / scope-exit ROLLBACKs
//   - batch ops nested inside an outer transaction skip their inner BEGIN (#9)
template <typename ConnType> class PublicTransactionApiTest : public StormTestFixture<Person, ConnType> {
  protected:
    static auto countPersons() -> int {
        storm::QuerySet<Person, ConnType> qs;
        auto                              result = qs.count().execute();
        return result.has_value() ? static_cast<int>(result.value()) : -1;
    }

    static auto conn() -> std::shared_ptr<ConnType> {
        return storm::QuerySet<Person, ConnType>::get_default_connection();
    }

    // Build `count` distinct Person rows for batch tests.
    static auto make_people(int count) -> std::vector<Person> {
        std::vector<Person> people;
        people.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            people.push_back(Person{.name = std::format("Person{}", i), .age = 20 + i});
        }
        return people;
    }

    // InsertOptions that force chunking (so the inner TransactionGuard runs).
    static auto chunked_opts() -> storm::orm::statements::InsertOptions {
        storm::orm::statements::InsertOptions opts;
        opts.batch_size = 2; // chunks of 2 → multiple inner BEGIN/COMMIT attempts.
        return opts;
    }
};

TYPED_TEST_SUITE(PublicTransactionApiTest, DatabaseTypes);

// The guard type is reachable through the public storm:: namespace (re-export).
TYPED_TEST(PublicTransactionApiTest, GuardTypeIsPublic) {
    auto txn = storm::begin(this->conn());
    ASSERT_TRUE(txn.has_value()) << "storm::begin should start a transaction";
    auto commit_result = txn->commit();
    EXPECT_TRUE(commit_result.has_value()) << "explicit commit should succeed";
}

// Success path: several QuerySet ops run atomically, commit persists them.
TYPED_TEST(PublicTransactionApiTest, CommitPersistsMultipleOps) {
    storm::QuerySet<Person, TypeParam> qs;
    EXPECT_EQ(this->countPersons(), 0);

    auto txn = storm::begin(this->conn());
    ASSERT_TRUE(txn.has_value());

    Person const alice{.name = "Alice", .age = 30};
    Person const bob{.name = "Bob", .age = 25};
    ASSERT_TRUE(qs.insert(alice).execute().has_value());
    ASSERT_TRUE(qs.insert(bob).execute().has_value());

    ASSERT_TRUE(txn->commit().has_value());
    EXPECT_EQ(this->countPersons(), 2) << "both inserts visible after commit";
}

// Failure path: dropping the guard without commit ROLLBACKs everything.
TYPED_TEST(PublicTransactionApiTest, ScopeExitWithoutCommitRollsBack) {
    storm::QuerySet<Person, TypeParam> qs;
    EXPECT_EQ(this->countPersons(), 0);

    {
        auto txn = storm::begin(this->conn());
        ASSERT_TRUE(txn.has_value());
        Person const alice{.name = "Alice", .age = 30};
        ASSERT_TRUE(qs.insert(alice).execute().has_value());
        // No commit() — guard destructor must ROLLBACK here.
    }

    EXPECT_EQ(this->countPersons(), 0) << "insert must be rolled back on scope exit";
}

// #9 fix: a CHUNKED batch insert issues its own inner BEGIN/COMMIT. Forcing a
// small batch_size (3 chunks of 2 rows) drives that transaction-wrapped path.
// Inside an outer transaction it must NOT raise a nested-BEGIN error.
TYPED_TEST(PublicTransactionApiTest, ChunkedBatchNestedInTransactionDoesNotConflict) {
    storm::QuerySet<Person, TypeParam> qs;

    auto txn = storm::begin(this->conn());
    ASSERT_TRUE(txn.has_value());

    auto const batch  = this->make_people(5); // 3 chunks (2+2+1) via chunked_opts().
    auto       result = qs.insert(std::span<const Person>(batch), this->chunked_opts()).execute();
    ASSERT_TRUE(result.has_value()) << "nested chunked batch must not raise a nested-BEGIN error";

    ASSERT_TRUE(txn->commit().has_value());
    EXPECT_EQ(this->countPersons(), 5) << "all chunked rows committed atomically with the outer txn";
}

// #9 fix continued: rolling back the outer transaction must discard the rows the
// nested chunked batch wrote (proving the inner op did NOT self-commit per chunk).
TYPED_TEST(PublicTransactionApiTest, OuterRollbackDiscardsNestedChunkedBatch) {
    storm::QuerySet<Person, TypeParam> qs;

    {
        auto txn = storm::begin(this->conn());
        ASSERT_TRUE(txn.has_value());
        auto const batch = this->make_people(4); // 2 chunks via chunked_opts().
        ASSERT_TRUE(qs.insert(std::span<const Person>(batch), this->chunked_opts()).execute().has_value());
        // No commit — outer rollback on scope exit.
    }

    EXPECT_EQ(this->countPersons(), 0) << "nested chunked batch must not have self-committed";
}

// ── storm::transaction(conn, body) scope helper ────────────────────────────────
// Convenience layer over the guard: runs body inside a transaction, COMMITs when
// body returns a value, ROLLBACKs (and propagates) when body returns unexpected.

// Success: body's ops commit together and the returned value is forwarded.
TYPED_TEST(PublicTransactionApiTest, TransactionScopeCommitsOnSuccess) {
    using Error = typename TypeParam::Error;
    storm::QuerySet<Person, TypeParam> qs;

    auto result = storm::transaction(this->conn(), [&](auto& /*txn*/) -> std::expected<int, Error> {
        if (auto r = qs.insert(Person{.name = "Alice", .age = 30}).execute(); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = qs.insert(Person{.name = "Bob", .age = 25}).execute(); !r) {
            return std::unexpected(r.error());
        }
        return 42; // body's value is forwarded out on commit.
    });

    ASSERT_TRUE(result.has_value()) << "scope should commit and forward the body value";
    EXPECT_EQ(result.value(), 42);
    EXPECT_EQ(this->countPersons(), 2) << "both inserts committed by the scope";
}

// Failure: body returns unexpected → scope ROLLBACKs and propagates the error.
TYPED_TEST(PublicTransactionApiTest, TransactionScopeRollsBackOnUnexpected) {
    using Error = typename TypeParam::Error;
    storm::QuerySet<Person, TypeParam> qs;

    auto result = storm::transaction(this->conn(), [&](auto& /*txn*/) -> std::expected<void, Error> {
        if (auto r = qs.insert(Person{.name = "Alice", .age = 30}).execute(); !r) {
            return std::unexpected(r.error());
        }
        return std::unexpected(Error{-1, "deliberate failure after a write"});
    });

    ASSERT_FALSE(result.has_value()) << "scope must propagate the body's error";
    EXPECT_EQ(this->countPersons(), 0) << "the write before the failure must be rolled back";
}

// Nested chunked batch inside the scope cooperates (no nested-BEGIN conflict, #9).
TYPED_TEST(PublicTransactionApiTest, TransactionScopeNestsChunkedBatch) {
    using Error = typename TypeParam::Error;
    storm::QuerySet<Person, TypeParam> qs;

    auto result = storm::transaction(this->conn(), [&](auto& /*txn*/) -> std::expected<void, Error> {
        auto const batch = this->make_people(5); // 3 chunks via chunked_opts().
        return qs.insert(std::span<const Person>(batch), this->chunked_opts()).execute();
    });

    ASSERT_TRUE(result.has_value()) << "nested chunked batch must not conflict inside the scope";
    EXPECT_EQ(this->countPersons(), 5);
}
