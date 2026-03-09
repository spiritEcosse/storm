/**
 * @file test_pq_mock.cpp
 * @brief Mock-based tests for the mock libpq infrastructure
 *
 * These tests verify that the mock libpq library works correctly, including:
 * - Default success behavior for all functions
 * - Configurable connection failures (null, bad status)
 * - Configurable prepare/exec failures
 * - Column data configuration for extraction tests
 * - Call count tracking
 * - RAII guard auto-reset
 */

#include <gtest/gtest.h>
#include "mock_libpq.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter) // NOSONAR(cpp:S125)
// NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes) // NOSONAR(cpp:S125)
// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,misc-const-correctness) // NOSONAR(cpp:S125)
// NOLINTBEGIN(readability-convert-member-functions-to-static,readability-static-accessed-through-instance) // NOSONAR(cpp:S125)

using namespace storm::test;

// ============================================================================
// Default Behavior Tests
// ============================================================================

class PqMockDefaultTest : public ::testing::Test {
  public:
    [[no_unique_address]] MockPqGuard guard_;

    auto SetUp() -> void override {
        MockPqConfig::reset();
    }
};

TEST_F(PqMockDefaultTest, ConnectdbReturnsValidConnection) {
    PGconn* const conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_NE(conn, nullptr);
    EXPECT_EQ(PQstatus(conn), CONNECTION_OK);
    PQfinish(conn);
}

TEST_F(PqMockDefaultTest, PrepareReturnsCommandOk) {
    PGconn* conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    PGresult* res = PQprepare( // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            conn, "stmt1", "SELECT $1", 1, nullptr
    );
    EXPECT_NE(res, nullptr);
    EXPECT_EQ(PQresultStatus(res), PGRES_COMMAND_OK);
    PQclear(res);
    PQfinish(conn);
}

TEST_F(PqMockDefaultTest, ExecReturnsCommandOk) {
    PGconn*   conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    PGresult* res  =                                // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            PQexec(conn, "CREATE TABLE test (id INTEGER)");
    EXPECT_NE(res, nullptr);
    EXPECT_EQ(PQresultStatus(res), PGRES_COMMAND_OK);
    PQclear(res);
    PQfinish(conn);
}

TEST_F(PqMockDefaultTest, ExecPreparedReturnsCommandOk) {
    PGconn* conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    PGresult* res = PQexecPrepared( // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            conn, "stmt1", 0, nullptr, nullptr, nullptr, 0
    );
    EXPECT_NE(res, nullptr);
    EXPECT_EQ(PQresultStatus(res), PGRES_COMMAND_OK);
    EXPECT_EQ(PQntuples(res), 0);
    PQclear(res);
    PQfinish(conn);
}

TEST_F(PqMockDefaultTest, ErrorMessageReturnsEmptyByDefault) {
    PGconn*     conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    const char* msg  = PQerrorMessage(conn);
    EXPECT_STREQ(msg, "");
    PQfinish(conn);
}

TEST_F(PqMockDefaultTest, ColumnDefaultsAreEmpty) {
    PGconn* conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    PGresult* res = PQexecPrepared( // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            conn, "stmt1", 0, nullptr, nullptr, nullptr, 0
    );
    EXPECT_STREQ(PQgetvalue(res, 0, 0), "");
    EXPECT_EQ(PQgetlength(res, 0, 0), 0);
    EXPECT_EQ(PQgetisnull(res, 0, 0), 0);
    PQclear(res);
    PQfinish(conn);
}

// ============================================================================
// Connection Configuration Tests
// ============================================================================

class PqMockConnectionTest : public ::testing::Test {
  public:
    [[no_unique_address]] MockPqGuard guard_;

    auto SetUp() -> void override {
        MockPqConfig::reset();
    }
};

TEST_F(PqMockConnectionTest, ConnectdbReturnsNullWhenConfigured) {
    MockPqConfig::connectdb_returns_null();
    PGconn* const conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_EQ(conn, nullptr);
}

TEST_F(PqMockConnectionTest, StatusReturnsBadWhenConfigured) {
    MockPqConfig::status_returns(CONNECTION_BAD);
    PGconn* const conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_NE(conn, nullptr);
    EXPECT_EQ(PQstatus(conn), CONNECTION_BAD);
    PQfinish(conn);
}

TEST_F(PqMockConnectionTest, ErrorMessageReturnsConfiguredMessage) {
    MockPqConfig::error_message("Connection refused");
    PGconn* const conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_STREQ(PQerrorMessage(conn), "Connection refused");
    PQfinish(conn);
}

TEST_F(PqMockConnectionTest, StatusOnNullReturnsConnectionBad) {
    EXPECT_EQ(PQstatus(nullptr), CONNECTION_BAD);
}

TEST_F(PqMockConnectionTest, ErrorMessageOnNullReturnsEmpty) {
    EXPECT_STREQ(PQerrorMessage(nullptr), "");
}

// ============================================================================
// Prepare Configuration Tests
// ============================================================================

class PqMockPrepareTest : public ::testing::Test {
  public:
    [[no_unique_address]] MockPqGuard guard_;
    PGconn*                           conn_ = nullptr;

    auto SetUp() -> void override {
        MockPqConfig::reset();
        conn_ = PQconnectdb("host=localhost");
    }

    auto TearDown() -> void override {
        PQfinish(conn_);
    }
};

TEST_F(PqMockPrepareTest, PrepareReturnsNullWhenConfigured) {
    MockPqConfig::prepare_returns_null();
    PGresult* const res = PQprepare( // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            conn_, "stmt1", "SELECT 1", 0, nullptr
    );
    EXPECT_EQ(res, nullptr);
}

TEST_F(PqMockPrepareTest, PrepareReturnsFatalErrorStatus) {
    MockPqConfig::prepare_status(PGRES_FATAL_ERROR);
    PGresult* const res = PQprepare( // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            conn_, "stmt1", "SELECT 1", 0, nullptr
    );
    EXPECT_NE(res, nullptr);
    EXPECT_EQ(PQresultStatus(res), PGRES_FATAL_ERROR);
    PQclear(res);
}

TEST_F(PqMockPrepareTest, PrepareFailsOnSpecificCall) {
    MockPqConfig::prepare_fails_on_call(2);

    // First call succeeds
    PGresult* const res1 = PQprepare( // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            conn_, "stmt1", "SELECT 1", 0, nullptr
    );
    EXPECT_NE(res1, nullptr);
    EXPECT_EQ(PQresultStatus(res1), PGRES_COMMAND_OK);
    PQclear(res1);

    // Second call fails (returns null)
    PGresult* const res2 = PQprepare( // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            conn_, "stmt2", "SELECT 2", 0, nullptr
    );
    EXPECT_EQ(res2, nullptr);

    // Third call succeeds again
    PGresult* res3 = PQprepare( // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            conn_, "stmt3", "SELECT 3", 0, nullptr
    );
    EXPECT_NE(res3, nullptr);
    PQclear(res3);
}

// ============================================================================
// ExecPrepared Configuration Tests
// ============================================================================

class PqMockExecPreparedTest : public ::testing::Test {
  public:
    [[no_unique_address]] MockPqGuard guard_;
    PGconn*                           conn_ = nullptr;

    auto SetUp() -> void override {
        MockPqConfig::reset();
        conn_ = PQconnectdb("host=localhost");
    }

    auto TearDown() -> void override {
        PQfinish(conn_);
    }
};

TEST_F(PqMockExecPreparedTest, ExecPreparedReturnsFatalError) {
    MockPqConfig::exec_prepared_status(PGRES_FATAL_ERROR);
    PGresult* const res = PQexecPrepared( // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            conn_, "stmt1", 0, nullptr, nullptr, nullptr, 0
    );
    EXPECT_NE(res, nullptr);
    EXPECT_EQ(PQresultStatus(res), PGRES_FATAL_ERROR);
    PQclear(res);
}

TEST_F(PqMockExecPreparedTest, ExecPreparedReturnsConfiguredNtuples) {
    MockPqConfig::exec_prepared_ntuples(5);
    PGresult* const res = PQexecPrepared( // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            conn_, "stmt1", 0, nullptr, nullptr, nullptr, 0
    );
    EXPECT_EQ(PQntuples(res), 5);
    PQclear(res);
}

TEST_F(PqMockExecPreparedTest, NtuplesOnNullReturnsZero) {
    EXPECT_EQ(PQntuples(nullptr), 0);
}

// ============================================================================
// Exec Configuration Tests
// ============================================================================

class PqMockExecTest : public ::testing::Test {
  public:
    [[no_unique_address]] MockPqGuard guard_;
    PGconn*                           conn_ = nullptr;

    auto SetUp() -> void override {
        MockPqConfig::reset();
        conn_ = PQconnectdb("host=localhost");
    }

    auto TearDown() -> void override {
        PQfinish(conn_);
    }
};

TEST_F(PqMockExecTest, ExecReturnsNullWhenConfigured) {
    MockPqConfig::exec_returns_null();
    PGresult* const res =
            PQexec(conn_, "DROP TABLE test"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_EQ(res, nullptr);
}

TEST_F(PqMockExecTest, ExecReturnsFatalErrorStatus) {
    MockPqConfig::exec_status(PGRES_FATAL_ERROR);
    PGresult* const res =
            PQexec(conn_, "DROP TABLE test"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_NE(res, nullptr);
    EXPECT_EQ(PQresultStatus(res), PGRES_FATAL_ERROR);
    PQclear(res);
}

TEST_F(PqMockExecTest, ExecFailsOnSpecificCall) {
    MockPqConfig::exec_fails_on_call(2);

    // First call succeeds
    PGresult* const res1 = PQexec(conn_, "SELECT 1"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_NE(res1, nullptr);
    EXPECT_EQ(PQresultStatus(res1), PGRES_COMMAND_OK);
    PQclear(res1);

    // Second call fails
    PGresult* const res2 = PQexec(conn_, "SELECT 2"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_NE(res2, nullptr);
    EXPECT_EQ(PQresultStatus(res2), PGRES_FATAL_ERROR);
    PQclear(res2);

    // Third call succeeds again
    PGresult* res3 = PQexec(conn_, "SELECT 3"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_NE(res3, nullptr);
    EXPECT_EQ(PQresultStatus(res3), PGRES_COMMAND_OK);
    PQclear(res3);
}

TEST_F(PqMockExecTest, ExecFailsOnCallWithNullConfigured) {
    MockPqConfig::exec_returns_null();
    MockPqConfig::exec_fails_on_call(1);
    PGresult* const res = PQexec(conn_, "SELECT 1"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_EQ(res, nullptr);
}

TEST_F(PqMockExecTest, ResultStatusOnNullReturnsFatalError) {
    EXPECT_EQ(PQresultStatus(nullptr), PGRES_FATAL_ERROR);
}

// ============================================================================
// Column Data Configuration Tests
// ============================================================================

class PqMockColumnDataTest : public ::testing::Test {
  public:
    [[no_unique_address]] MockPqGuard guard_;
    PGconn*                           conn_ = nullptr;
    PGresult*                         res_  = nullptr;

    auto SetUp() -> void override {
        MockPqConfig::reset();
        conn_ = PQconnectdb("host=localhost");
        MockPqConfig::exec_prepared_ntuples(2);
        res_ = PQexecPrepared(conn_, "stmt1", 0, nullptr, nullptr, nullptr, 0);
    }

    auto TearDown() -> void override {
        PQclear(res_);
        PQfinish(conn_);
    }
};

TEST_F(PqMockColumnDataTest, GetvalueReturnsConfiguredValue) {
    MockPqConfig::set_column_value(0, 0, "hello");
    EXPECT_STREQ(PQgetvalue(res_, 0, 0), "hello");
}

TEST_F(PqMockColumnDataTest, GetlengthReturnsValueLength) {
    MockPqConfig::set_column_value(0, 1, "world");
    EXPECT_EQ(PQgetlength(res_, 0, 1), 5);
}

TEST_F(PqMockColumnDataTest, GetlengthReturnsExplicitOverride) {
    MockPqConfig::set_column_value(0, 0, "abc");
    MockPqConfig::set_column_length(0, 0, 100);
    EXPECT_EQ(PQgetlength(res_, 0, 0), 100);
}

TEST_F(PqMockColumnDataTest, GetisnullReturnsTrueWhenConfigured) {
    MockPqConfig::set_column_null(0, 2);
    EXPECT_EQ(PQgetisnull(res_, 0, 2), 1);
}

TEST_F(PqMockColumnDataTest, GetisnullReturnsFalseByDefault) {
    EXPECT_EQ(PQgetisnull(res_, 1, 0), 0);
}

TEST_F(PqMockColumnDataTest, MultipleRowsAndColumns) {
    MockPqConfig::set_column_value(0, 0, "r0c0");
    MockPqConfig::set_column_value(0, 1, "r0c1");
    MockPqConfig::set_column_value(1, 0, "r1c0");
    MockPqConfig::set_column_value(1, 1, "r1c1");

    EXPECT_STREQ(PQgetvalue(res_, 0, 0), "r0c0");
    EXPECT_STREQ(PQgetvalue(res_, 0, 1), "r0c1");
    EXPECT_STREQ(PQgetvalue(res_, 1, 0), "r1c0");
    EXPECT_STREQ(PQgetvalue(res_, 1, 1), "r1c1");
}

// ============================================================================
// Call Count Tracking Tests
// ============================================================================

class PqMockCallCountTest : public ::testing::Test {
  public:
    [[no_unique_address]] MockPqGuard guard_;

    auto SetUp() -> void override {
        MockPqConfig::reset();
    }
};

TEST_F(PqMockCallCountTest, ConnectdbCallsTracked) {
    EXPECT_EQ(MockPqConfig::get_connectdb_call_count(), 0);
    PGconn* const conn1 = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_EQ(MockPqConfig::get_connectdb_call_count(), 1);
    PGconn* const conn2 = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_EQ(MockPqConfig::get_connectdb_call_count(), 2);
    PQfinish(conn1);
    PQfinish(conn2);
}

TEST_F(PqMockCallCountTest, PrepareCallsTracked) {
    PGconn* const conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_EQ(MockPqConfig::get_prepare_call_count(), 0);

    PGresult* const res1 = // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            PQprepare(conn, "s1", "SELECT 1", 0, nullptr);
    EXPECT_EQ(MockPqConfig::get_prepare_call_count(), 1);

    PGresult* const res2 = // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            PQprepare(conn, "s2", "SELECT 2", 0, nullptr);
    EXPECT_EQ(MockPqConfig::get_prepare_call_count(), 2);

    PQclear(res1);
    PQclear(res2);
    PQfinish(conn);
}

TEST_F(PqMockCallCountTest, ExecCallsTracked) {
    PGconn* const conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_EQ(MockPqConfig::get_exec_call_count(), 0);

    PGresult* const res = PQexec(conn, "SELECT 1"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_EQ(MockPqConfig::get_exec_call_count(), 1);

    PQclear(res);
    PQfinish(conn);
}

TEST_F(PqMockCallCountTest, ExecPreparedCallsTracked) {
    PGconn* const conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_EQ(MockPqConfig::get_exec_prepared_call_count(), 0);

    PGresult* const res = PQexecPrepared( // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            conn, "stmt1", 0, nullptr, nullptr, nullptr, 0
    );
    EXPECT_EQ(MockPqConfig::get_exec_prepared_call_count(), 1);

    PQclear(res);
    PQfinish(conn);
}

// ============================================================================
// Guard Auto-Reset Tests
// ============================================================================

class PqMockGuardTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        MockPqConfig::reset();
    }
};

TEST_F(PqMockGuardTest, GuardResetsOnDestruction) {
    {
        MockPqGuard guard;
        MockPqConfig::connectdb_returns_null();

        PGconn* const conn =
                PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
        EXPECT_EQ(conn, nullptr);
    }
    // Guard destructor called, config should be reset

    PGconn* const conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_NE(conn, nullptr);
    EXPECT_EQ(PQstatus(conn), CONNECTION_OK);
    PQfinish(conn);
}

TEST_F(PqMockGuardTest, ResetClearsAllConfiguration) {
    MockPqConfig::connectdb_returns_null();
    MockPqConfig::status_returns(CONNECTION_BAD);
    MockPqConfig::error_message("test error");
    MockPqConfig::prepare_returns_null();
    MockPqConfig::exec_returns_null();

    MockPqConfig::reset();

    PGconn* const conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_NE(conn, nullptr);
    EXPECT_EQ(PQstatus(conn), CONNECTION_OK);

    PGresult* prep_res = PQprepare( // NOSONAR(cpp:S5350) - PQclear requires non-const ptr
            conn, "s1", "SELECT 1", 0, nullptr
    );
    EXPECT_NE(prep_res, nullptr);
    EXPECT_EQ(PQresultStatus(prep_res), PGRES_COMMAND_OK);
    PQclear(prep_res);

    PGresult* exec_res = PQexec(conn, "SELECT 1"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_NE(exec_res, nullptr);
    EXPECT_EQ(PQresultStatus(exec_res), PGRES_COMMAND_OK);
    PQclear(exec_res);

    PQfinish(conn);
}

TEST_F(PqMockGuardTest, ResetClearsCallCounts) {
    PGconn* const conn = PQconnectdb("host=localhost"); // NOSONAR(cpp:S5350) - PQfinish/PQclear require non-const ptr
    EXPECT_EQ(MockPqConfig::get_connectdb_call_count(), 1);
    PQfinish(conn);

    MockPqConfig::reset();
    EXPECT_EQ(MockPqConfig::get_connectdb_call_count(), 0);
    EXPECT_EQ(MockPqConfig::get_prepare_call_count(), 0);
    EXPECT_EQ(MockPqConfig::get_exec_call_count(), 0);
    EXPECT_EQ(MockPqConfig::get_exec_prepared_call_count(), 0);
}

// ============================================================================
// PQclear and PQfinish Safety Tests
// ============================================================================

TEST_F(PqMockGuardTest, ClearOnNullIsSafe) {
    PQclear(nullptr); // Should not crash
}

TEST_F(PqMockGuardTest, FinishOnNullIsSafe) {
    PQfinish(nullptr); // Should not crash
}

// NOLINTEND(readability-convert-member-functions-to-static,readability-static-accessed-through-instance) // NOSONAR(cpp:S125)
// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,misc-const-correctness) // NOSONAR(cpp:S125)
// NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes) // NOSONAR(cpp:S125)
// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter) // NOSONAR(cpp:S125)
