/**
 * @file test_orm_pg_mock_errors.cpp
 * @brief Tests for Storm ORM PostgreSQL error paths using mock libpq
 *
 * These tests use the mock libpq library to test hard-to-trigger error paths
 * in the Storm ORM PostgreSQL backend that would be difficult or impossible
 * to test with a real PostgreSQL server.
 *
 * Error paths tested:
 * - Connection failures (null PGconn, CONNECTION_BAD)
 * - Prepare failures (null result, PGRES_FATAL_ERROR)
 * - Execute failures (null result, SQL errors)
 * - Statement move assignment
 * - BLOB extraction (null, non-hex, uppercase hex)
 * - Error accessor methods
 * - Cache utilities
 * - Connection accessors
 * - Quote handling in placeholder translation
 */

#include <gtest/gtest.h>

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter)
// NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
// NOLINTBEGIN(readability-convert-member-functions-to-static,misc-const-correctness)

#include "mock_libpq.h"

import storm;
import <expected>;
import <string>;
import <vector>;

using PgConnection = storm::db::postgresql::Connection;
using PgStatement  = storm::db::postgresql::Statement;
using PgError      = storm::db::postgresql::Error;
using storm::test::MockPqConfig;
using storm::test::MockPqGuard;

namespace {

    // ============================================================================
    // Connection Error Tests (L520-530)
    // ============================================================================

    class PgConnectionErrorTest : public ::testing::Test {
      protected:
        MockPqGuard guard_;

        auto SetUp() -> void override {
            MockPqConfig::reset();
        }
    };

    TEST_F(PgConnectionErrorTest, ConnectdbReturnsNull) {
        MockPqConfig::connectdb_returns_null();
        auto result = PgConnection::open("host=localhost");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), -1);
        EXPECT_EQ(result.error().message(), "Failed to allocate PGconn");
    }

    TEST_F(PgConnectionErrorTest, StatusReturnsBad) {
        MockPqConfig::status_returns(CONNECTION_BAD).error_message("Connection refused");
        auto result = PgConnection::open("host=localhost");

        ASSERT_FALSE(result.has_value());
        // Error code is static_cast<int>(PQstatus(raw_conn)) after PQfinish
        EXPECT_NE(result.error().code(), 0);
    }

    // ============================================================================
    // Error Accessor Tests (L24-31)
    // ============================================================================

    TEST_F(PgConnectionErrorTest, ErrorCodeAndMessage) {
        MockPqConfig::connectdb_returns_null();
        auto result = PgConnection::open("host=localhost");

        ASSERT_FALSE(result.has_value());
        const PgError& err = result.error();
        EXPECT_EQ(err.code(), -1);
        EXPECT_EQ(err.message(), "Failed to allocate PGconn");
    }

    // ============================================================================
    // Prepare Error Tests (L553-572)
    // ============================================================================

    class PgPrepareErrorTest : public ::testing::Test {
      protected:
        MockPqGuard guard_;

        auto SetUp() -> void override {
            MockPqConfig::reset();
        }
    };

    TEST_F(PgPrepareErrorTest, PrepareOnClosedConnection) {
        // Create connection, then move-from it to simulate closed state
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto moved = std::move(*conn_result);
        // conn_result is now in moved-from state (is_open() == false)

        auto result = conn_result->prepare("SELECT 1");
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), -1);
        EXPECT_EQ(result.error().message(), "Connection not open");
    }

    TEST_F(PgPrepareErrorTest, PrepareReturnsNull) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        MockPqConfig::prepare_returns_null();
        auto result = conn_result->prepare("SELECT 1");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), -1);
    }

    TEST_F(PgPrepareErrorTest, PrepareReturnsFatalError) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        MockPqConfig::prepare_status(PGRES_FATAL_ERROR).error_message("syntax error");
        auto result = conn_result->prepare("INVALID SQL");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), -1);
    }

    // ============================================================================
    // Prepare Cached Error Tests (L580-607)
    // ============================================================================

    TEST_F(PgPrepareErrorTest, PrepareCachedOnClosedConnection) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto moved = std::move(*conn_result);

        auto result = conn_result->prepare_cached("SELECT 1");
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), -1);
        EXPECT_EQ(result.error().message(), "Connection not open");
    }

    TEST_F(PgPrepareErrorTest, PrepareCachedReturnsNull) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        MockPqConfig::prepare_returns_null();
        auto result = conn_result->prepare_cached("SELECT 1");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), -1);
    }

    TEST_F(PgPrepareErrorTest, PrepareCachedReturnsFatalError) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        MockPqConfig::prepare_status(PGRES_FATAL_ERROR).error_message("relation not found");
        auto result = conn_result->prepare_cached("SELECT * FROM nonexistent");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), -1);
    }

    // ============================================================================
    // Execute (Direct SQL) Error Tests (L628-650)
    // ============================================================================

    class PgExecuteErrorTest : public ::testing::Test {
      protected:
        MockPqGuard guard_;

        auto SetUp() -> void override {
            MockPqConfig::reset();
        }
    };

    TEST_F(PgExecuteErrorTest, ExecuteOnClosedConnection) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto moved = std::move(*conn_result);

        auto result = conn_result->execute("CREATE TABLE test (id INTEGER)");
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), -1);
        EXPECT_EQ(result.error().message(), "Connection not open");
    }

    TEST_F(PgExecuteErrorTest, ExecuteReturnsNull) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        MockPqConfig::exec_returns_null();
        auto result = conn_result->execute("CREATE TABLE test (id INTEGER)");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), -1);
    }

    TEST_F(PgExecuteErrorTest, ExecuteReturnsSqlError) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        MockPqConfig::exec_status(PGRES_FATAL_ERROR).error_message("table already exists");
        auto result = conn_result->execute("CREATE TABLE test (id INTEGER)");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code(), PGRES_FATAL_ERROR);
    }

    // ============================================================================
    // Statement Execute Error Tests (L199)
    // ============================================================================

    class PgStatementErrorTest : public ::testing::Test {
      protected:
        MockPqGuard guard_;

        auto SetUp() -> void override {
            MockPqConfig::reset();
        }
    };

    TEST_F(PgStatementErrorTest, ExecPreparedReturnsFatalError) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        // Prepare succeeds, but exec_prepared fails
        auto stmt_result = conn_result->prepare("INSERT INTO test VALUES ($1)");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_status(PGRES_FATAL_ERROR).error_message("constraint violation");

        auto exec_result = stmt_result->execute();
        ASSERT_FALSE(exec_result.has_value());
    }

    // ============================================================================
    // Statement Step Error Tests (L218-240)
    // ============================================================================

    TEST_F(PgStatementErrorTest, StepAutoExecuteError) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT * FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        // step() auto-executes if result_ is null
        MockPqConfig::exec_prepared_status(PGRES_FATAL_ERROR);
        auto step_result = stmt_result->step();

        ASSERT_FALSE(step_result.has_value());
    }

    TEST_F(PgStatementErrorTest, StepReturnsFalseAtEnd) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT * FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        // Execute returns 1 row
        MockPqConfig::exec_prepared_ntuples(1);
        auto step1 = stmt_result->step();
        ASSERT_TRUE(step1.has_value());
        EXPECT_TRUE(step1.value()); // First row available

        auto step2 = stmt_result->step();
        ASSERT_TRUE(step2.has_value());
        EXPECT_FALSE(step2.value()); // No more rows
    }

    TEST_F(PgStatementErrorTest, StepRawAutoExecuteError) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT * FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_status(PGRES_FATAL_ERROR);
        int raw_result = stmt_result->step_raw();

        EXPECT_EQ(raw_result, -1); // Error
    }

    // ============================================================================
    // Statement Move Assignment Tests (L77-104)
    // ============================================================================

    TEST_F(PgStatementErrorTest, StatementMoveAssignment) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt1_result = conn_result->prepare("SELECT 1");
        ASSERT_TRUE(stmt1_result.has_value());

        auto stmt2_result = conn_result->prepare("SELECT 2");
        ASSERT_TRUE(stmt2_result.has_value());

        // Exercise move assignment
        *stmt1_result = std::move(*stmt2_result);

        // stmt1 should now hold stmt2's state
        // Verify it still works
        MockPqConfig::exec_prepared_ntuples(0);
        auto step = stmt1_result->step();
        ASSERT_TRUE(step.has_value());
        EXPECT_FALSE(step.value()); // 0 rows
    }

    // ============================================================================
    // BLOB Extraction Tests (L339-393)
    // ============================================================================

    class PgBlobExtractionTest : public ::testing::Test {
      protected:
        MockPqGuard guard_;

        auto SetUp() -> void override {
            MockPqConfig::reset();
        }
    };

    TEST_F(PgBlobExtractionTest, ExtractBlobPtrNull) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT data FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        // Configure: 1 row, column 0 is null
        MockPqConfig::exec_prepared_ntuples(1).set_column_null(0, 0);
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());
        ASSERT_TRUE(step.value());

        const void* blob = stmt_result->extract_blob_ptr(0);
        EXPECT_EQ(blob, nullptr);
    }

    TEST_F(PgBlobExtractionTest, ExtractBlobNonHexFallback) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT data FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        // Configure: 1 row, column 0 has raw data (no \x prefix)
        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "rawdata");
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());
        ASSERT_TRUE(step.value());

        const void* blob = stmt_result->extract_blob_ptr(0);
        EXPECT_NE(blob, nullptr);

        // extract_bytes should return the length of the raw data
        int bytes = stmt_result->extract_bytes(0);
        EXPECT_EQ(bytes, 7); // "rawdata" length
    }

    TEST_F(PgBlobExtractionTest, ExtractBlobUppercaseHex) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT data FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        // Configure: 1 row, column 0 has uppercase hex BYTEA
        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "\\xDEADBEEF");
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());
        ASSERT_TRUE(step.value());

        const void* blob = stmt_result->extract_blob_ptr(0);
        EXPECT_NE(blob, nullptr);

        // \xDEADBEEF is 4 bytes decoded
        int bytes = stmt_result->extract_bytes(0);
        EXPECT_EQ(bytes, 4);

        // Verify decoded bytes
        const auto* data = static_cast<const unsigned char*>(blob);
        EXPECT_EQ(data[0], 0xDE);
        EXPECT_EQ(data[1], 0xAD);
        EXPECT_EQ(data[2], 0xBE);
        EXPECT_EQ(data[3], 0xEF);
    }

    TEST_F(PgBlobExtractionTest, ExtractBlobWithNumericHexDigits) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT data FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        // Hex string with numeric digits (0-9) to cover hex_digit() numeric branch
        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "\\x0123456789");
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());
        ASSERT_TRUE(step.value());

        const void* blob = stmt_result->extract_blob_ptr(0);
        EXPECT_NE(blob, nullptr);

        int bytes = stmt_result->extract_bytes(0);
        EXPECT_EQ(bytes, 5); // 10 hex chars = 5 bytes

        const auto* data = static_cast<const unsigned char*>(blob);
        EXPECT_EQ(data[0], 0x01);
        EXPECT_EQ(data[1], 0x23);
        EXPECT_EQ(data[2], 0x45);
        EXPECT_EQ(data[3], 0x67);
        EXPECT_EQ(data[4], 0x89);
    }

    TEST_F(PgBlobExtractionTest, ExtractBlobInvalidHexDigitFallback) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT data FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        // Hex string with invalid hex characters to cover hex_digit() return 0 fallback
        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "\\xGG");
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());
        ASSERT_TRUE(step.value());

        const void* blob = stmt_result->extract_blob_ptr(0);
        EXPECT_NE(blob, nullptr);

        int bytes = stmt_result->extract_bytes(0);
        EXPECT_EQ(bytes, 1); // 2 hex chars = 1 byte (both decode to 0)

        const auto* data = static_cast<const unsigned char*>(blob);
        EXPECT_EQ(data[0], 0x00); // Invalid chars fall back to 0
    }

    TEST_F(PgBlobExtractionTest, ExtractBlobLowercaseHex) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT data FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "\\xdeadbeef");
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());
        ASSERT_TRUE(step.value());

        const void* blob = stmt_result->extract_blob_ptr(0);
        EXPECT_NE(blob, nullptr);

        int bytes = stmt_result->extract_bytes(0);
        EXPECT_EQ(bytes, 4);

        const auto* data = static_cast<const unsigned char*>(blob);
        EXPECT_EQ(data[0], 0xDE);
        EXPECT_EQ(data[1], 0xAD);
        EXPECT_EQ(data[2], 0xBE);
        EXPECT_EQ(data[3], 0xEF);
    }

    // ============================================================================
    // Statement get_error_message Test (L375-380)
    // ============================================================================

    TEST_F(PgStatementErrorTest, GetErrorMessage) {
        MockPqConfig::error_message("test error from PQ");
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT 1");
        ASSERT_TRUE(stmt_result.has_value());

        const char* msg = stmt_result->get_error_message();
        EXPECT_STREQ(msg, "test error from PQ");
    }

    // ============================================================================
    // Cache Utility Tests (L616-624)
    // ============================================================================

    class PgCacheUtilityTest : public ::testing::Test {
      protected:
        MockPqGuard guard_;

        auto SetUp() -> void override {
            MockPqConfig::reset();
        }
    };

    TEST_F(PgCacheUtilityTest, CachedStatementCount) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        EXPECT_EQ(conn_result->cached_statement_count(), 0u);

        auto stmt1 = conn_result->prepare_cached("SELECT 1");
        ASSERT_TRUE(stmt1.has_value());
        EXPECT_EQ(conn_result->cached_statement_count(), 1u);

        auto stmt2 = conn_result->prepare_cached("SELECT 2");
        ASSERT_TRUE(stmt2.has_value());
        EXPECT_EQ(conn_result->cached_statement_count(), 2u);

        // Same SQL reuses cached statement
        auto stmt3 = conn_result->prepare_cached("SELECT 1");
        ASSERT_TRUE(stmt3.has_value());
        EXPECT_EQ(conn_result->cached_statement_count(), 2u);
    }

    TEST_F(PgCacheUtilityTest, ClearStatementCache) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        (void)conn_result->prepare_cached("SELECT 1");
        (void)conn_result->prepare_cached("SELECT 2");
        EXPECT_EQ(conn_result->cached_statement_count(), 2u);

        conn_result->clear_statement_cache();
        EXPECT_EQ(conn_result->cached_statement_count(), 0u);
    }

    // ============================================================================
    // Connection Accessor Tests (L656-668)
    // ============================================================================

    class PgConnectionAccessorTest : public ::testing::Test {
      protected:
        MockPqGuard guard_;

        auto SetUp() -> void override {
            MockPqConfig::reset();
        }
    };

    TEST_F(PgConnectionAccessorTest, GetReturnsRawPointer) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        PGconn* raw = conn_result->get();
        EXPECT_NE(raw, nullptr);
    }

    TEST_F(PgConnectionAccessorTest, LastInsertRowid) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        EXPECT_EQ(conn_result->last_insert_rowid(), 0);

        conn_result->set_last_insert_rowid(42);
        EXPECT_EQ(conn_result->last_insert_rowid(), 42);

        conn_result->set_last_insert_rowid(100);
        EXPECT_EQ(conn_result->last_insert_rowid(), 100);
    }

    // ============================================================================
    // Quote Handling in Placeholder Translation Tests (L688-693)
    // ============================================================================

    class PgQuoteHandlingTest : public ::testing::Test {
      protected:
        MockPqGuard guard_;

        auto SetUp() -> void override {
            MockPqConfig::reset();
        }
    };

    TEST_F(PgQuoteHandlingTest, SingleQuotedQuestionMarkNotTranslated) {
        // SQL with '?' inside single quotes should NOT translate the ?
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        // This calls translate_placeholders internally
        // The ? inside 'literal?' should not be translated
        auto result = conn_result->prepare("SELECT * FROM t WHERE col = 'literal?'");
        ASSERT_TRUE(result.has_value());
    }

    TEST_F(PgQuoteHandlingTest, DoubleQuotedQuestionMarkNotTranslated) {
        // SQL with ? inside double quotes should NOT translate the ?
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto result = conn_result->prepare("SELECT \"col?\" FROM t WHERE id = ?");
        ASSERT_TRUE(result.has_value());
    }

    TEST_F(PgQuoteHandlingTest, MixedQuotesAndPlaceholders) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        // Mix of quoted ? and real ? placeholders
        auto result = conn_result->prepare("SELECT * FROM t WHERE name = 'test?' AND age = ? AND \"col?\" = ?");
        ASSERT_TRUE(result.has_value());
    }

    // ============================================================================
    // Connection open success test (covers normal path)
    // ============================================================================

    TEST_F(PgConnectionAccessorTest, PrepareCommonStatements) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        // Call prepare_common_statements() directly - it's a no-op for PostgreSQL
        conn_result->prepare_common_statements();

        // Verify connection still works after calling it
        EXPECT_TRUE(conn_result->is_open());
    }

    TEST_F(PgConnectionAccessorTest, OpenSucceeds) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());
        EXPECT_TRUE(conn_result->is_open());
    }

    // ============================================================================
    // Statement prepare and execute success (covers normal paths)
    // ============================================================================

    TEST_F(PgStatementErrorTest, PrepareAndExecuteSuccess) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("INSERT INTO test VALUES ($1)");
        ASSERT_TRUE(stmt_result.has_value());

        auto exec_result = stmt_result->execute();
        ASSERT_TRUE(exec_result.has_value());
    }

    TEST_F(PgStatementErrorTest, PrepareAndStepRawSuccess) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT * FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_ntuples(1);
        int raw = stmt_result->step_raw();
        EXPECT_EQ(raw, PgStatement::ROW_AVAILABLE);

        raw = stmt_result->step_raw();
        EXPECT_EQ(raw, PgStatement::NO_MORE_ROWS);
    }

    // ============================================================================
    // Statement bind methods tests (verify they compile and work with mock)
    // ============================================================================

    TEST_F(PgStatementErrorTest, BindMethodsSucceed) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("INSERT INTO test VALUES ($1, $2, $3, $4, $5, $6)");
        ASSERT_TRUE(stmt_result.has_value());

        auto r1 = stmt_result->bind_int(1, 42);
        EXPECT_TRUE(r1.has_value());

        auto r2 = stmt_result->bind_text(2, "hello");
        EXPECT_TRUE(r2.has_value());

        auto r3 = stmt_result->bind_int64(3, 123456789LL);
        EXPECT_TRUE(r3.has_value());

        auto r4 = stmt_result->bind_double(4, 3.14);
        EXPECT_TRUE(r4.has_value());

        auto r5 = stmt_result->bind_null(5);
        EXPECT_TRUE(r5.has_value());

        std::vector<uint8_t> blob_data = {0xDE, 0xAD};
        auto                 r6        = stmt_result->bind_blob(6, blob_data.data(), blob_data.size());
        EXPECT_TRUE(r6.has_value());
    }

    // ============================================================================
    // Statement reset and handle tests
    // ============================================================================

    TEST_F(PgStatementErrorTest, ResetStatement) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT 1");
        ASSERT_TRUE(stmt_result.has_value());

        // Execute and step
        MockPqConfig::exec_prepared_ntuples(0);
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());

        // Reset should clear state
        stmt_result->reset();

        // After reset, handle should be null
        EXPECT_EQ(stmt_result->handle(), nullptr);
    }

    TEST_F(PgStatementErrorTest, HandleReturnsNullBeforeExecute) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT 1");
        ASSERT_TRUE(stmt_result.has_value());

        EXPECT_EQ(stmt_result->handle(), nullptr);
    }

    // ============================================================================
    // Column extraction method tests
    // ============================================================================

    class PgColumnExtractionTest : public ::testing::Test {
      protected:
        MockPqGuard guard_;

        auto SetUp() -> void override {
            MockPqConfig::reset();
        }
    };

    TEST_F(PgColumnExtractionTest, ExtractIntValue) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT age FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "42");
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());
        ASSERT_TRUE(step.value());

        EXPECT_EQ(stmt_result->extract_int(0), 42);
    }

    TEST_F(PgColumnExtractionTest, ExtractInt64Value) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT bigid FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "9876543210");
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());

        EXPECT_EQ(stmt_result->extract_int64(0), 9876543210LL);
    }

    TEST_F(PgColumnExtractionTest, ExtractDoubleValue) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT price FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "3.14159");
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());

        EXPECT_NEAR(stmt_result->extract_double(0), 3.14159, 0.0001);
    }

    TEST_F(PgColumnExtractionTest, ExtractTextPtr) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT name FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "hello");
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());

        const unsigned char* text = stmt_result->extract_text_ptr(0);
        EXPECT_NE(text, nullptr);
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(text)), "hello");
    }

    TEST_F(PgColumnExtractionTest, ExtractTextView) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT name FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "world");
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());

        auto view = stmt_result->extract_text_view(0);
        EXPECT_EQ(view, "world");
    }

    TEST_F(PgColumnExtractionTest, ExtractTextViewNull) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT name FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_ntuples(1).set_column_null(0, 0);
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());

        auto view = stmt_result->extract_text_view(0);
        EXPECT_TRUE(view.empty());
    }

    TEST_F(PgColumnExtractionTest, ExtractBoolTrue) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT active FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "t");
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());

        EXPECT_TRUE(stmt_result->extract_bool(0));
    }

    TEST_F(PgColumnExtractionTest, ExtractBoolFalse) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT active FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "f");
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());

        EXPECT_FALSE(stmt_result->extract_bool(0));
    }

    TEST_F(PgColumnExtractionTest, ExtractFloatValue) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT score FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "2.5");
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());

        EXPECT_NEAR(stmt_result->extract_float(0), 2.5f, 0.01f);
    }

    TEST_F(PgColumnExtractionTest, ExtractBytes) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT name FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "hello");
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());

        EXPECT_EQ(stmt_result->extract_bytes(0), 5);
    }

    TEST_F(PgColumnExtractionTest, IsNull) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT name FROM test");
        ASSERT_TRUE(stmt_result.has_value());

        MockPqConfig::exec_prepared_ntuples(1).set_column_null(0, 0);
        auto step = stmt_result->step();
        ASSERT_TRUE(step.has_value());

        EXPECT_TRUE(stmt_result->is_null(0));
    }

    // ============================================================================
    // Statement finalize test
    // ============================================================================

    TEST_F(PgStatementErrorTest, FinalizeStatement) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("SELECT 1");
        ASSERT_TRUE(stmt_result.has_value());

        // finalize() calls PQexec("DEALLOCATE ...") and clears the statement
        stmt_result->finalize();

        // After finalize, handle should be null
        EXPECT_EQ(stmt_result->handle(), nullptr);
    }

    // ============================================================================
    // Execute success test
    // ============================================================================

    TEST_F(PgExecuteErrorTest, ExecuteSuccess) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto result = conn_result->execute("CREATE TABLE test (id INTEGER)");
        ASSERT_TRUE(result.has_value());
    }

    // ============================================================================
    // Bind blob with null data
    // ============================================================================

    // Test bind + execute to cover rebuild_param_ptrs() loop body
    TEST_F(PgStatementErrorTest, BindAndExecuteCoversRebuildParamPtrs) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("INSERT INTO test VALUES ($1, $2)");
        ASSERT_TRUE(stmt_result.has_value());

        auto r1 = stmt_result->bind_int(1, 42);
        EXPECT_TRUE(r1.has_value());

        auto r2 = stmt_result->bind_text(2, "hello");
        EXPECT_TRUE(r2.has_value());

        // Execute with bound params - this calls rebuild_param_ptrs()
        auto exec_result = stmt_result->execute();
        ASSERT_TRUE(exec_result.has_value());
    }

    // Test bind with NULL param + execute to cover rebuild_param_ptrs() nullptr branch
    TEST_F(PgStatementErrorTest, BindNullAndExecuteCoversRebuildPtrsBranch) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("INSERT INTO test VALUES ($1, $2)");
        ASSERT_TRUE(stmt_result.has_value());

        auto r1 = stmt_result->bind_int(1, 42);
        EXPECT_TRUE(r1.has_value());

        auto r2 = stmt_result->bind_null(2);
        EXPECT_TRUE(r2.has_value());

        // Execute - rebuild_param_ptrs will see non-null for param 1 and null for param 2
        auto exec_result = stmt_result->execute();
        ASSERT_TRUE(exec_result.has_value());
    }

    TEST_F(PgStatementErrorTest, BindBlobNull) {
        auto conn_result = PgConnection::open("host=localhost");
        ASSERT_TRUE(conn_result.has_value());

        auto stmt_result = conn_result->prepare("INSERT INTO test VALUES ($1)");
        ASSERT_TRUE(stmt_result.has_value());

        auto r = stmt_result->bind_blob(1, nullptr, 0);
        EXPECT_TRUE(r.has_value());
    }

    // ============================================================================
    // ORM-level PostgreSQL tests (QuerySet with PgConnection)
    // These tests exercise the INSERT RETURNING path in insert.cppm
    // and the LIMIT ALL path in base.cppm
    // ============================================================================

    struct MockPgPerson {
        [[= storm::meta::FieldAttr::primary]] int64_t id{};
        std::string                                   name;
        int                                           age{};
    };

    using PgQuerySet = storm::QuerySet<MockPgPerson, PgConnection>;

    class PgOrmInsertReturningTest : public ::testing::Test {
      protected:
        MockPqGuard guard_;

        auto SetUp() -> void override {
            MockPqConfig::reset();
            // Set up PG default connection via mock
            (void)PgQuerySet::set_default_connection("host=localhost");
        }
    };

    // Covers insert.cppm lines 251-285: INSERT RETURNING happy path
    TEST_F(PgOrmInsertReturningTest, InsertReturningSuccess) {
        // Configure mock: exec_prepared returns 1 row with the generated ID
        MockPqConfig::exec_prepared_ntuples(1).set_column_value(0, 0, "42");

        PgQuerySet         qs;
        MockPgPerson const person{.id = 0, .name = "Alice", .age = 30};

        auto result = qs.insert(person);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), 42);
    }

    // Covers insert.cppm LCOV_EXCL lines: INSERT RETURNING prepare error
    TEST_F(PgOrmInsertReturningTest, InsertReturningPrepareError) {
        MockPqConfig::prepare_returns_null();

        PgQuerySet         qs;
        MockPgPerson const person{.id = 0, .name = "Bob", .age = 25};

        auto result = qs.insert(person);
        ASSERT_FALSE(result.has_value());
    }

    // Covers insert.cppm LCOV_EXCL lines: INSERT RETURNING step error
    TEST_F(PgOrmInsertReturningTest, InsertReturningStepError) {
        MockPqConfig::exec_prepared_status(PGRES_FATAL_ERROR).error_message("constraint violation");

        PgQuerySet         qs;
        MockPgPerson const person{.id = 0, .name = "Charlie", .age = 35};

        auto result = qs.insert(person);
        ASSERT_FALSE(result.has_value());
    }

    // Covers base.cppm line 583: LIMIT ALL path (offset without limit)
    TEST_F(PgOrmInsertReturningTest, SelectWithOffsetOnlyUsesLimitAll) {
        // Configure mock: exec_prepared returns 0 rows for the SELECT
        MockPqConfig::exec_prepared_ntuples(0);

        PgQuerySet qs;
        auto       results = qs.offset(5).select();
        ASSERT_TRUE(results.has_value());
        EXPECT_TRUE(results.value().empty());
    }

} // namespace

// NOLINTEND(readability-convert-member-functions-to-static,misc-const-correctness)
// NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter)
