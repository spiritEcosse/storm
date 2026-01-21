/**
 * @file mock_sqlite3.h
 * @brief Mock SQLite3 library for testing error paths
 *
 * This mock library provides controllable SQLite3 function implementations
 * that allow tests to trigger specific error conditions that are difficult
 * to achieve with real SQLite (e.g., SQLITE_NOMEM, SQLITE_CORRUPT, internal failures).
 *
 * Usage:
 *   1. Include this header instead of <sqlite3.h> in test code
 *   2. Use MockSqlite3::configure() to set up expected behaviors
 *   3. Run your code that calls SQLite functions
 *   4. Verify expected error handling occurred
 *
 * Example:
 *   MockSqlite3::configure()
 *       .bind_int_returns(SQLITE_NOMEM)
 *       .next_prepare_fails(SQLITE_ERROR);
 *
 *   // Your code that uses sqlite3_bind_int() will now return SQLITE_NOMEM
 */

#ifndef STORM_MOCK_SQLITE3_H
#define STORM_MOCK_SQLITE3_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

// ============================================================================
// SQLite3 Constants (from sqlite3.h)
// ============================================================================

#define SQLITE_OK 0          /* Successful result */
#define SQLITE_ERROR 1       /* Generic error */
#define SQLITE_INTERNAL 2    /* Internal logic error in SQLite */
#define SQLITE_PERM 3        /* Access permission denied */
#define SQLITE_ABORT 4       /* Callback routine requested an abort */
#define SQLITE_BUSY 5        /* The database file is locked */
#define SQLITE_LOCKED 6      /* A table in the database is locked */
#define SQLITE_NOMEM 7       /* A malloc() failed */
#define SQLITE_READONLY 8    /* Attempt to write a readonly database */
#define SQLITE_INTERRUPT 9   /* Operation terminated by sqlite3_interrupt()*/
#define SQLITE_IOERR 10      /* Some kind of disk I/O error occurred */
#define SQLITE_CORRUPT 11    /* The database disk image is malformed */
#define SQLITE_NOTFOUND 12   /* Unknown opcode in sqlite3_file_control() */
#define SQLITE_FULL 13       /* Insertion failed because database is full */
#define SQLITE_CANTOPEN 14   /* Unable to open the database file */
#define SQLITE_PROTOCOL 15   /* Database lock protocol error */
#define SQLITE_EMPTY 16      /* Internal use only */
#define SQLITE_SCHEMA 17     /* The database schema changed */
#define SQLITE_TOOBIG 18     /* String or BLOB exceeds size limit */
#define SQLITE_CONSTRAINT 19 /* Abort due to constraint violation */
#define SQLITE_MISMATCH 20   /* Data type mismatch */
#define SQLITE_MISUSE 21     /* Library used incorrectly */
#define SQLITE_NOLFS 22      /* Uses OS features not supported on host */
#define SQLITE_AUTH 23       /* Authorization denied */
#define SQLITE_FORMAT 24     /* Not used */
#define SQLITE_RANGE 25      /* 2nd parameter to sqlite3_bind out of range */
#define SQLITE_NOTADB 26     /* File opened that is not a database file */

#define SQLITE_ROW 100  /* sqlite3_step() has another row ready */
#define SQLITE_DONE 101 /* sqlite3_step() has finished executing */

// Column types
#define SQLITE_INTEGER 1
#define SQLITE_FLOAT 2
#define SQLITE_TEXT 3
#define SQLITE_BLOB 4
#define SQLITE_NULL 5

// Open flags
#define SQLITE_OPEN_READONLY 0x00000001
#define SQLITE_OPEN_READWRITE 0x00000002
#define SQLITE_OPEN_CREATE 0x00000004
#define SQLITE_OPEN_URI 0x00000040
#define SQLITE_OPEN_MEMORY 0x00000080
#define SQLITE_OPEN_NOMUTEX 0x00008000
#define SQLITE_OPEN_FULLMUTEX 0x00010000
#define SQLITE_OPEN_SHAREDCACHE 0x00020000
#define SQLITE_OPEN_PRIVATECACHE 0x00040000

// Bind transient flag
#define SQLITE_TRANSIENT ((void (*)(void*)) - 1)
#define SQLITE_STATIC ((void (*)(void*))0)

// ============================================================================
// SQLite3 Types (opaque handles)
// ============================================================================

struct sqlite3;
struct sqlite3_stmt;

// ============================================================================
// Mock Configuration API
// ============================================================================

namespace storm::test {

    /**
     * @brief Configuration builder for mock SQLite behavior
     */
    class MockSqlite3Config {
      public:
        // Reset all mock configurations to default (success) behavior
        static auto reset() -> void;

        // Configure bind functions to return specific error codes
        static auto bind_int_returns(int return_code) -> MockSqlite3Config&;
        static auto bind_text_returns(int return_code) -> MockSqlite3Config&;
        static auto bind_int64_returns(int return_code) -> MockSqlite3Config&;
        static auto bind_double_returns(int return_code) -> MockSqlite3Config&;
        static auto bind_null_returns(int return_code) -> MockSqlite3Config&;
        static auto bind_blob_returns(int return_code) -> MockSqlite3Config&;

        // Configure step() to return specific codes
        static auto step_returns(int return_code) -> MockSqlite3Config&;
        static auto step_returns_sequence(std::vector<int> codes) -> MockSqlite3Config&;

        // Configure prepare to fail
        static auto prepare_returns(int return_code) -> MockSqlite3Config&;
        static auto prepare_error_message(const std::string& msg) -> MockSqlite3Config&;

        // Configure open to fail
        static auto open_returns(int return_code) -> MockSqlite3Config&;
        static auto open_error_message(const std::string& msg) -> MockSqlite3Config&;

        // Configure exec to fail
        static auto exec_returns(int return_code) -> MockSqlite3Config&;
        static auto exec_error_message(const std::string& msg) -> MockSqlite3Config&;

        // Configure for specific call counts (e.g., "fail on 3rd bind_int call")
        static auto bind_int_fails_on_call(int call_number, int return_code) -> MockSqlite3Config&;
        static auto step_fails_on_call(int call_number, int return_code) -> MockSqlite3Config&;

        // Get call counts for verification
        static auto get_bind_int_call_count() -> int;
        static auto get_bind_text_call_count() -> int;
        static auto get_step_call_count() -> int;
        static auto get_prepare_call_count() -> int;
        static auto get_exec_call_count() -> int;

      private:
        static MockSqlite3Config instance_;
    };

    /**
     * @brief RAII guard that resets mock configuration on destruction
     */
    class MockSqlite3Guard {
      public:
        MockSqlite3Guard() = default;
        ~MockSqlite3Guard() {
            MockSqlite3Config::reset();
        }

        MockSqlite3Guard(const MockSqlite3Guard&)                    = delete;
        auto operator=(const MockSqlite3Guard&) -> MockSqlite3Guard& = delete;
        MockSqlite3Guard(MockSqlite3Guard&&)                         = delete;
        auto operator=(MockSqlite3Guard&&) -> MockSqlite3Guard&      = delete;
    };

} // namespace storm::test

// ============================================================================
// SQLite3 API Functions (mock implementations)
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// Database connection management
auto sqlite3_open_v2(const char* filename, sqlite3** ppDb, int flags, const char* zVfs) -> int;

auto sqlite3_close_v2(sqlite3* db) -> int;

// Statement preparation
auto sqlite3_prepare_v2(sqlite3* db, const char* zSql, int nByte, sqlite3_stmt** ppStmt, const char** pzTail) -> int;

auto sqlite3_finalize(sqlite3_stmt* pStmt) -> int;
auto sqlite3_reset(sqlite3_stmt* pStmt) -> int;

// Statement execution
auto sqlite3_step(sqlite3_stmt* pStmt) -> int;
auto sqlite3_exec(sqlite3* db, const char* sql, int (*callback)(void*, int, char**, char**), void* arg, char** errmsg)
        -> int;

// Binding values
auto sqlite3_bind_int(sqlite3_stmt* pStmt, int idx, int value) -> int;
auto sqlite3_bind_int64(sqlite3_stmt* pStmt, int idx, int64_t value) -> int;
auto sqlite3_bind_double(sqlite3_stmt* pStmt, int idx, double value) -> int;
auto sqlite3_bind_text(sqlite3_stmt* pStmt, int idx, const char* value, int nBytes, void (*destructor)(void*)) -> int;
auto sqlite3_bind_null(sqlite3_stmt* pStmt, int idx) -> int;
auto sqlite3_bind_blob(sqlite3_stmt* pStmt, int idx, const void* value, int nBytes, void (*destructor)(void*)) -> int;

// Extracting column values
auto sqlite3_column_int(sqlite3_stmt* pStmt, int iCol) -> int;
auto sqlite3_column_int64(sqlite3_stmt* pStmt, int iCol) -> int64_t;
auto sqlite3_column_double(sqlite3_stmt* pStmt, int iCol) -> double;
auto sqlite3_column_text(sqlite3_stmt* pStmt, int iCol) -> const unsigned char*;
auto sqlite3_column_blob(sqlite3_stmt* pStmt, int iCol) -> const void*;
auto sqlite3_column_bytes(sqlite3_stmt* pStmt, int iCol) -> int;
auto sqlite3_column_type(sqlite3_stmt* pStmt, int iCol) -> int;

// Error handling
auto sqlite3_errmsg(sqlite3* db) -> const char*;
auto sqlite3_db_handle(sqlite3_stmt* pStmt) -> sqlite3*;
auto sqlite3_free(void* ptr) -> void;

// Last insert rowid
auto sqlite3_last_insert_rowid(sqlite3* db) -> int64_t;

#ifdef __cplusplus
}
#endif

#endif // STORM_MOCK_SQLITE3_H
