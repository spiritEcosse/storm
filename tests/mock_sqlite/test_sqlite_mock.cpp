/**
 * @file test_sqlite_mock.cpp
 * @brief Mock-based tests for SQLite error paths that are hard to trigger with real SQLite
 *
 * These tests use a mock SQLite library to verify error handling for scenarios like:
 * - SQLITE_NOMEM (out of memory during bind/prepare/step)
 * - SQLITE_CORRUPT (database corruption during step)
 * - SQLITE_IOERR (I/O errors during operations)
 * - Internal failures that can't be triggered normally
 */

#include <gtest/gtest.h>
#include <numbers>
#include "mock_sqlite3.h"

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)

using namespace storm::test;

// ============================================================================
// Mock Configuration Tests - Verify the mock itself works correctly
// ============================================================================

class MockConfigTest : public ::testing::Test {
  public:
    [[no_unique_address]] MockSqlite3Guard guard_; // Reset mock config after each test

  protected:
    auto SetUp() -> void override {
        MockSqlite3Config::reset();
    }
};

TEST_F(MockConfigTest, DefaultBehaviorReturnsOk) {
    sqlite3* db = nullptr;
    int      rc = sqlite3_open_v2(":memory:", &db, 0, nullptr);
    EXPECT_EQ(rc, SQLITE_OK);

    sqlite3_stmt* stmt = nullptr;
    rc                 = sqlite3_prepare_v2(db, "SELECT 1", -1, &stmt, nullptr);
    EXPECT_EQ(rc, SQLITE_OK);

    rc = sqlite3_bind_int(stmt, 1, 42);
    EXPECT_EQ(rc, SQLITE_OK);

    rc = sqlite3_step(stmt);
    EXPECT_EQ(rc, SQLITE_DONE);

    sqlite3_finalize(stmt);
    sqlite3_close_v2(db);
}

TEST_F(MockConfigTest, CanConfigureBindIntFailure) {
    MockSqlite3Config::bind_int_returns(SQLITE_NOMEM);

    sqlite3* db = nullptr;
    sqlite3_open_v2(":memory:", &db, 0, nullptr);

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT ?", -1, &stmt, nullptr);

    int rc = sqlite3_bind_int(stmt, 1, 42);
    EXPECT_EQ(rc, SQLITE_NOMEM);

    sqlite3_finalize(stmt);
    sqlite3_close_v2(db);
}

TEST_F(MockConfigTest, CanConfigureStepFailure) {
    MockSqlite3Config::step_returns(SQLITE_CORRUPT);

    sqlite3* db = nullptr;
    sqlite3_open_v2(":memory:", &db, 0, nullptr);

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT 1", -1, &stmt, nullptr);

    int rc = sqlite3_step(stmt);
    EXPECT_EQ(rc, SQLITE_CORRUPT);

    sqlite3_finalize(stmt);
    sqlite3_close_v2(db);
}

TEST_F(MockConfigTest, CanConfigurePrepareFailure) {
    MockSqlite3Config::prepare_returns(SQLITE_ERROR);
    MockSqlite3Config::prepare_error_message("Custom error message");

    sqlite3* db = nullptr;
    sqlite3_open_v2(":memory:", &db, 0, nullptr);

    sqlite3_stmt* stmt = nullptr;
    int           rc   = sqlite3_prepare_v2(db, "SELECT 1", -1, &stmt, nullptr);
    EXPECT_EQ(rc, SQLITE_ERROR);
    EXPECT_EQ(stmt, nullptr);

    const char* msg = sqlite3_errmsg(db);
    EXPECT_STREQ(msg, "Custom error message");

    sqlite3_close_v2(db);
}

TEST_F(MockConfigTest, CanConfigureOpenFailure) {
    MockSqlite3Config::open_returns(SQLITE_CANTOPEN);
    MockSqlite3Config::open_error_message("Cannot open database");

    sqlite3* db = nullptr;
    int      rc = sqlite3_open_v2("/nonexistent/path.db", &db, 0, nullptr);
    EXPECT_EQ(rc, SQLITE_CANTOPEN);

    // Even on failure, db should be valid for error retrieval
    const char* msg = sqlite3_errmsg(db);
    EXPECT_STREQ(msg, "Cannot open database");

    sqlite3_close_v2(db);
}

TEST_F(MockConfigTest, CanConfigureExecFailure) {
    MockSqlite3Config::exec_returns(SQLITE_ERROR);
    MockSqlite3Config::exec_error_message("Exec failed");

    sqlite3* db = nullptr;
    sqlite3_open_v2(":memory:", &db, 0, nullptr);

    char* errmsg = nullptr;
    int   rc     = sqlite3_exec(db, "INVALID SQL", nullptr, nullptr, &errmsg);
    EXPECT_EQ(rc, SQLITE_ERROR);
    EXPECT_NE(errmsg, nullptr);
    EXPECT_STREQ(errmsg, "Exec failed");

    sqlite3_free(errmsg);
    sqlite3_close_v2(db);
}

TEST_F(MockConfigTest, CanTrackCallCounts) {
    sqlite3* db = nullptr;
    sqlite3_open_v2(":memory:", &db, 0, nullptr);

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT ?, ?, ?", -1, &stmt, nullptr);

    EXPECT_EQ(MockSqlite3Config::get_bind_int_call_count(), 0);

    sqlite3_bind_int(stmt, 1, 1);
    EXPECT_EQ(MockSqlite3Config::get_bind_int_call_count(), 1);

    sqlite3_bind_int(stmt, 2, 2);
    EXPECT_EQ(MockSqlite3Config::get_bind_int_call_count(), 2);

    sqlite3_bind_int(stmt, 3, 3);
    EXPECT_EQ(MockSqlite3Config::get_bind_int_call_count(), 3);

    sqlite3_finalize(stmt);
    sqlite3_close_v2(db);
}

TEST_F(MockConfigTest, CanFailOnSpecificCall) {
    // Configure to fail on 3rd bind_int call
    MockSqlite3Config::bind_int_fails_on_call(3, SQLITE_NOMEM);

    sqlite3* db = nullptr;
    sqlite3_open_v2(":memory:", &db, 0, nullptr);

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT ?, ?, ?", -1, &stmt, nullptr);

    int rc1 = sqlite3_bind_int(stmt, 1, 1);
    EXPECT_EQ(rc1, SQLITE_OK);

    int rc2 = sqlite3_bind_int(stmt, 2, 2);
    EXPECT_EQ(rc2, SQLITE_OK);

    int rc3 = sqlite3_bind_int(stmt, 3, 3);
    EXPECT_EQ(rc3, SQLITE_NOMEM); // This one fails!

    sqlite3_finalize(stmt);
    sqlite3_close_v2(db);
}

TEST_F(MockConfigTest, CanConfigureStepSequence) {
    // Configure step to return ROW, ROW, DONE
    MockSqlite3Config::step_returns_sequence({SQLITE_ROW, SQLITE_ROW, SQLITE_DONE});

    sqlite3* db = nullptr;
    sqlite3_open_v2(":memory:", &db, 0, nullptr);

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT * FROM test", -1, &stmt, nullptr);

    EXPECT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_step(stmt), SQLITE_DONE);
    EXPECT_EQ(sqlite3_step(stmt), SQLITE_DONE); // After sequence, returns DONE

    sqlite3_finalize(stmt);
    sqlite3_close_v2(db);
}

// ============================================================================
// Error Code Coverage Tests - Test all bind types
// ============================================================================

class BindErrorCoverageTest : public ::testing::Test {
  public:
    [[no_unique_address]] MockSqlite3Guard guard_;
    sqlite3*                               db_   = nullptr;
    sqlite3_stmt*                          stmt_ = nullptr;

  protected:
    auto SetUp() -> void override {
        MockSqlite3Config::reset();
        sqlite3_open_v2(":memory:", &db_, 0, nullptr);
        sqlite3_prepare_v2(db_, "SELECT ?", -1, &stmt_, nullptr);
    }

    auto TearDown() -> void override {
        sqlite3_finalize(stmt_);
        sqlite3_close_v2(db_);
    }
};

TEST_F(BindErrorCoverageTest, BindTextNomem) {
    MockSqlite3Config::bind_text_returns(SQLITE_NOMEM);
    int rc = sqlite3_bind_text(stmt_, 1, "test", 4, SQLITE_TRANSIENT);
    EXPECT_EQ(rc, SQLITE_NOMEM);
}

TEST_F(BindErrorCoverageTest, BindInt64Nomem) {
    MockSqlite3Config::bind_int64_returns(SQLITE_NOMEM);
    int rc = sqlite3_bind_int64(stmt_, 1, 123456789LL);
    EXPECT_EQ(rc, SQLITE_NOMEM);
}

TEST_F(BindErrorCoverageTest, BindDoubleNomem) {
    MockSqlite3Config::bind_double_returns(SQLITE_NOMEM);
    int rc = sqlite3_bind_double(stmt_, 1, std::numbers::pi);
    EXPECT_EQ(rc, SQLITE_NOMEM);
}

TEST_F(BindErrorCoverageTest, BindNullNomem) {
    MockSqlite3Config::bind_null_returns(SQLITE_NOMEM);
    int rc = sqlite3_bind_null(stmt_, 1);
    EXPECT_EQ(rc, SQLITE_NOMEM);
}

TEST_F(BindErrorCoverageTest, BindBlobNomem) {
    MockSqlite3Config::bind_blob_returns(SQLITE_NOMEM);
    int rc = sqlite3_bind_blob(stmt_, 1, "data", 4, SQLITE_TRANSIENT);
    EXPECT_EQ(rc, SQLITE_NOMEM);
}

// ============================================================================
// Step Error Codes Test
// ============================================================================

class StepErrorCoverageTest : public ::testing::Test {
  public:
    [[no_unique_address]] MockSqlite3Guard guard_;
    sqlite3*                               db_   = nullptr;
    sqlite3_stmt*                          stmt_ = nullptr;

  protected:
    auto SetUp() -> void override {
        MockSqlite3Config::reset();
        sqlite3_open_v2(":memory:", &db_, 0, nullptr);
        sqlite3_prepare_v2(db_, "SELECT 1", -1, &stmt_, nullptr);
    }

    auto TearDown() -> void override {
        sqlite3_finalize(stmt_);
        sqlite3_close_v2(db_);
    }
};

TEST_F(StepErrorCoverageTest, StepCorrupt) {
    MockSqlite3Config::step_returns(SQLITE_CORRUPT);
    int rc = sqlite3_step(stmt_);
    EXPECT_EQ(rc, SQLITE_CORRUPT);
}

TEST_F(StepErrorCoverageTest, StepIoError) {
    MockSqlite3Config::step_returns(SQLITE_IOERR);
    int rc = sqlite3_step(stmt_);
    EXPECT_EQ(rc, SQLITE_IOERR);
}

TEST_F(StepErrorCoverageTest, StepBusy) {
    MockSqlite3Config::step_returns(SQLITE_BUSY);
    int rc = sqlite3_step(stmt_);
    EXPECT_EQ(rc, SQLITE_BUSY);
}

TEST_F(StepErrorCoverageTest, StepLocked) {
    MockSqlite3Config::step_returns(SQLITE_LOCKED);
    int rc = sqlite3_step(stmt_);
    EXPECT_EQ(rc, SQLITE_LOCKED);
}

TEST_F(StepErrorCoverageTest, StepNomem) {
    MockSqlite3Config::step_returns(SQLITE_NOMEM);
    int rc = sqlite3_step(stmt_);
    EXPECT_EQ(rc, SQLITE_NOMEM);
}

TEST_F(StepErrorCoverageTest, StepMisuse) {
    MockSqlite3Config::step_returns(SQLITE_MISUSE);
    int rc = sqlite3_step(stmt_);
    EXPECT_EQ(rc, SQLITE_MISUSE);
}

// ============================================================================
// Reset Behavior Tests
// ============================================================================

class MockResetTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        MockSqlite3Config::reset();
    }
};

TEST_F(MockResetTest, ResetClearsAllConfiguration) {
    // Configure various failures
    MockSqlite3Config::bind_int_returns(SQLITE_NOMEM);
    MockSqlite3Config::step_returns(SQLITE_CORRUPT);
    MockSqlite3Config::prepare_returns(SQLITE_ERROR);

    // Reset
    MockSqlite3Config::reset();

    // Verify everything is back to OK
    sqlite3* db = nullptr;
    sqlite3_open_v2(":memory:", &db, 0, nullptr);

    sqlite3_stmt* stmt = nullptr;
    int           rc   = sqlite3_prepare_v2(db, "SELECT ?", -1, &stmt, nullptr);
    EXPECT_EQ(rc, SQLITE_OK);

    rc = sqlite3_bind_int(stmt, 1, 42);
    EXPECT_EQ(rc, SQLITE_OK);

    rc = sqlite3_step(stmt);
    EXPECT_EQ(rc, SQLITE_DONE);

    sqlite3_finalize(stmt);
    sqlite3_close_v2(db);
}

TEST_F(MockResetTest, GuardResetsOnDestruction) {
    {
        MockSqlite3Guard guard;
        MockSqlite3Config::bind_int_returns(SQLITE_NOMEM);

        sqlite3* db = nullptr;
        sqlite3_open_v2(":memory:", &db, 0, nullptr);
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db, "SELECT ?", -1, &stmt, nullptr);

        int rc = sqlite3_bind_int(stmt, 1, 42);
        EXPECT_EQ(rc, SQLITE_NOMEM);

        sqlite3_finalize(stmt);
        sqlite3_close_v2(db);
    }
    // Guard destructor called, config should be reset

    sqlite3* db = nullptr;
    sqlite3_open_v2(":memory:", &db, 0, nullptr);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT ?", -1, &stmt, nullptr);

    int rc = sqlite3_bind_int(stmt, 1, 42);
    EXPECT_EQ(rc, SQLITE_OK); // Back to normal

    sqlite3_finalize(stmt);
    sqlite3_close_v2(db);
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
