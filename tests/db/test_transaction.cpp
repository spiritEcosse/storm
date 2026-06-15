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

    std::vector<Person> const batch = {
            {.name = "Alice", .age = 30},
            {.name = "Bob", .age = 25},
            {.name = "Charlie", .age = 35},
            {.name = "Dave", .age = 40},
            {.name = "Eve", .age = 45},
    };
    storm::orm::statements::InsertOptions opts;
    opts.batch_size = 2; // 3 chunks (2+2+1) → exercises the inner TransactionGuard.
    auto result     = qs.insert(std::span<const Person>(batch), opts).execute();
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
        std::vector<Person> const batch = {
                {.name = "Alice", .age = 30},
                {.name = "Bob", .age = 25},
                {.name = "Charlie", .age = 35},
                {.name = "Dave", .age = 40},
        };
        storm::orm::statements::InsertOptions opts;
        opts.batch_size = 2; // 2 chunks → inner TransactionGuard would self-commit if not passive.
        ASSERT_TRUE(qs.insert(std::span<const Person>(batch), opts).execute().has_value());
        // No commit — outer rollback on scope exit.
    }

    EXPECT_EQ(this->countPersons(), 0) << "nested chunked batch must not have self-committed";
}
