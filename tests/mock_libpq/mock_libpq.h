/**
 * @file mock_libpq.h
 * @brief Mock libpq library for testing PostgreSQL error paths
 *
 * This mock library provides controllable libpq function implementations
 * that allow tests to trigger specific error conditions in the PostgreSQL
 * backend code paths (connection failures, prepare errors, exec errors, etc.).
 *
 * Usage:
 *   1. Include this header instead of <libpq-fe.h> in test code
 *   2. Use MockPqConfig to set up expected behaviors
 *   3. Run your code that calls libpq functions
 *   4. Verify expected error handling occurred
 *
 * Example:
 *   MockPqConfig::connectdb_returns_null();
 *   // Your code that calls PQconnectdb() will now return nullptr
 */

#ifndef STORM_MOCK_LIBPQ_H
#define STORM_MOCK_LIBPQ_H

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <memory>

// ============================================================================
// libpq Constants (from libpq-fe.h)
// ============================================================================
//
// These macros replicate the real libpq C API constants.
// Production code uses #include <libpq-fe.h> while tests include this mock
// header instead; the two must be value-compatible.

#define CONNECTION_OK 0  // NOSONAR(cpp:S5028)
#define CONNECTION_BAD 1 // NOSONAR(cpp:S5028)

#define PGRES_EMPTY_QUERY 0 // NOSONAR(cpp:S5028)
#define PGRES_COMMAND_OK 1  // NOSONAR(cpp:S5028)
#define PGRES_TUPLES_OK 2   // NOSONAR(cpp:S5028)
#define PGRES_FATAL_ERROR 7 // NOSONAR(cpp:S5028)

// ============================================================================
// libpq Types (opaque handles)
// ============================================================================

typedef struct pg_conn   PGconn;         // NOLINT(modernize-use-using) - matches libpq C API
typedef struct pg_result PGresult;       // NOLINT(modernize-use-using) - matches libpq C API
typedef int              ConnStatusType; // NOLINT(modernize-use-using) - matches libpq C API
typedef int              ExecStatusType; // NOLINT(modernize-use-using) - matches libpq C API
typedef unsigned int     Oid;            // NOLINT(modernize-use-using) - matches libpq C API

// ============================================================================
// Mock Configuration API
// ============================================================================

namespace storm::test {

    /**
     * @brief Configuration builder for mock libpq behavior
     */
    class MockPqConfig {
      public:
        // Reset all mock configurations to default (success) behavior
        static auto reset() -> void;

        // Connection behavior
        static auto connectdb_returns_null() -> MockPqConfig&;
        static auto status_returns(int status) -> MockPqConfig&;
        static auto error_message(std::string_view msg) -> MockPqConfig&;

        // Prepare behavior
        static auto prepare_returns_null() -> MockPqConfig&;
        static auto prepare_status(int status) -> MockPqConfig&;
        static auto prepare_fails_on_call(int call_number) -> MockPqConfig&;

        // ExecPrepared behavior
        static auto exec_prepared_status(int status) -> MockPqConfig&;
        static auto exec_prepared_ntuples(int ntuples) -> MockPqConfig&;

        // Exec behavior
        static auto exec_returns_null() -> MockPqConfig&;
        static auto exec_status(int status) -> MockPqConfig&;
        static auto exec_fails_on_call(int call_number) -> MockPqConfig&;

        // Column data configuration (for BLOB and value extraction tests)
        static auto set_column_value(int row, int col, std::string value) -> MockPqConfig&;
        static auto set_column_null(int row, int col) -> MockPqConfig&;
        static auto set_column_length(int row, int col, int length) -> MockPqConfig&;

        // Call count tracking
        static auto get_connectdb_call_count() -> int;
        static auto get_prepare_call_count() -> int;
        static auto get_exec_call_count() -> int;
        static auto get_exec_prepared_call_count() -> int;

      private:
        static MockPqConfig instance_;
    };

    /**
     * @brief RAII guard that resets mock configuration on destruction
     */
    class MockPqGuard {
      public:
        MockPqGuard() = default;
        ~MockPqGuard() noexcept {
            try {
                MockPqConfig::reset();
            } catch (...) {
                // Suppress exceptions in destructor
            }
        }

        MockPqGuard(const MockPqGuard&)                    = delete;
        auto operator=(const MockPqGuard&) -> MockPqGuard& = delete;
        MockPqGuard(MockPqGuard&&)                         = delete;
        auto operator=(MockPqGuard&&) -> MockPqGuard&      = delete;
    };

} // namespace storm::test

// ============================================================================
// libpq API Functions (mock implementations)
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// Connection management
auto PQconnectdb(const char* conninfo) -> PGconn*;
auto PQstatus(const PGconn* conn) -> ConnStatusType;
auto PQfinish(PGconn* conn) -> void;
auto PQerrorMessage(const PGconn* conn) -> char*;

// Statement preparation and execution
auto PQprepare(PGconn* conn, const char* stmtName, const char* query, int nParams, const Oid* paramTypes) -> PGresult*;
auto PQexecPrepared(
        PGconn*            conn,
        const char*        stmtName,
        int                nParams,
        const char* const* paramValues,
        const int*         paramLengths,
        const int*         paramFormats,
        int                resultFormat
) -> PGresult*;
auto PQexec(PGconn* conn, const char* query) -> PGresult*;

// Result inspection
auto PQresultStatus(const PGresult* res) -> ExecStatusType;
auto PQclear(PGresult* res) -> void;
auto PQntuples(const PGresult* res) -> int;

// Column value extraction
auto PQgetvalue(const PGresult* res, int tup_num, int field_num) -> char*;
auto PQgetlength(const PGresult* res, int tup_num, int field_num) -> int;
auto PQgetisnull(const PGresult* res, int tup_num, int field_num) -> int;

#ifdef __cplusplus
}
#endif

#endif // STORM_MOCK_LIBPQ_H
