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
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// ============================================================================
// SQLite3 Constants (from sqlite3.h)
// ============================================================================
//
// These macros MUST remain as #define preprocessor macros (not constexpr/enum)
// because they replicate the real SQLite3 C API (<sqlite3.h>) which defines
// them as macros. Production code uses `#include <sqlite3.h>` while tests
// include this mock header instead; the two must be token-compatible so that
// SQLITE_OK, SQLITE_ROW, SQLITE_TRANSIENT, etc. behave identically in both
// the real and mock builds.

#define SQLITE_OK 0 /* Successful result */                                 // NOSONAR(cpp:S5028)
#define SQLITE_ERROR 1 /* Generic error */                                  // NOSONAR(cpp:S5028)
#define SQLITE_INTERNAL 2 /* Internal logic error in SQLite */              // NOSONAR(cpp:S5028)
#define SQLITE_PERM 3 /* Access permission denied */                        // NOSONAR(cpp:S5028)
#define SQLITE_ABORT 4 /* Callback routine requested an abort */            // NOSONAR(cpp:S5028)
#define SQLITE_BUSY 5 /* The database file is locked */                     // NOSONAR(cpp:S5028)
#define SQLITE_LOCKED 6 /* A table in the database is locked */             // NOSONAR(cpp:S5028)
#define SQLITE_NOMEM 7 /* A malloc() failed */                              // NOSONAR(cpp:S5028)
#define SQLITE_READONLY 8 /* Attempt to write a readonly database */        // NOSONAR(cpp:S5028)
#define SQLITE_INTERRUPT 9 /* Operation terminated by sqlite3_interrupt()*/ // NOSONAR(cpp:S5028)
#define SQLITE_IOERR 10 /* Some kind of disk I/O error occurred */          // NOSONAR(cpp:S5028)
#define SQLITE_CORRUPT 11 /* The database disk image is malformed */        // NOSONAR(cpp:S5028)
#define SQLITE_NOTFOUND 12 /* Unknown opcode in sqlite3_file_control() */   // NOSONAR(cpp:S5028)
#define SQLITE_FULL 13 /* Insertion failed because database is full */      // NOSONAR(cpp:S5028)
#define SQLITE_CANTOPEN 14 /* Unable to open the database file */           // NOSONAR(cpp:S5028)
#define SQLITE_PROTOCOL 15 /* Database lock protocol error */               // NOSONAR(cpp:S5028)
#define SQLITE_EMPTY 16 /* Internal use only */                             // NOSONAR(cpp:S5028)
#define SQLITE_SCHEMA 17 /* The database schema changed */                  // NOSONAR(cpp:S5028)
#define SQLITE_TOOBIG 18 /* String or BLOB exceeds size limit */            // NOSONAR(cpp:S5028)
#define SQLITE_CONSTRAINT 19 /* Abort due to constraint violation */        // NOSONAR(cpp:S5028)
#define SQLITE_MISMATCH 20 /* Data type mismatch */                         // NOSONAR(cpp:S5028)
#define SQLITE_MISUSE 21 /* Library used incorrectly */                     // NOSONAR(cpp:S5028)
#define SQLITE_NOLFS 22 /* Uses OS features not supported on host */        // NOSONAR(cpp:S5028)
#define SQLITE_AUTH 23 /* Authorization denied */                           // NOSONAR(cpp:S5028)
#define SQLITE_FORMAT 24 /* Not used */                                     // NOSONAR(cpp:S5028)
#define SQLITE_RANGE 25 /* 2nd parameter to sqlite3_bind out of range */    // NOSONAR(cpp:S5028)
#define SQLITE_NOTADB 26 /* File opened that is not a database file */      // NOSONAR(cpp:S5028)

#define SQLITE_ROW 100 /* sqlite3_step() has another row ready */   // NOSONAR(cpp:S5028)
#define SQLITE_DONE 101 /* sqlite3_step() has finished executing */ // NOSONAR(cpp:S5028)

// Column types
#define SQLITE_INTEGER 1 // NOSONAR(cpp:S5028)
#define SQLITE_FLOAT 2   // NOSONAR(cpp:S5028)
#define SQLITE_TEXT 3    // NOSONAR(cpp:S5028)
#define SQLITE_BLOB 4    // NOSONAR(cpp:S5028)
#define SQLITE_NULL 5    // NOSONAR(cpp:S5028)

// Open flags
#define SQLITE_OPEN_READONLY 0x00000001     // NOSONAR(cpp:S5028)
#define SQLITE_OPEN_READWRITE 0x00000002    // NOSONAR(cpp:S5028)
#define SQLITE_OPEN_CREATE 0x00000004       // NOSONAR(cpp:S5028)
#define SQLITE_OPEN_URI 0x00000040          // NOSONAR(cpp:S5028)
#define SQLITE_OPEN_MEMORY 0x00000080       // NOSONAR(cpp:S5028)
#define SQLITE_OPEN_NOMUTEX 0x00008000      // NOSONAR(cpp:S5028)
#define SQLITE_OPEN_FULLMUTEX 0x00010000    // NOSONAR(cpp:S5028)
#define SQLITE_OPEN_SHAREDCACHE 0x00020000  // NOSONAR(cpp:S5028)
#define SQLITE_OPEN_PRIVATECACHE 0x00040000 // NOSONAR(cpp:S5028)

// Bind transient flag
#define SQLITE_TRANSIENT ((void (*)(void *)) - 1) // NOSONAR(cpp:S5028)
#define SQLITE_STATIC ((void (*)(void *))0)       // NOSONAR(cpp:S5028)

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
    static auto bind_int_returns(int return_code) -> MockSqlite3Config &;
    static auto bind_text_returns(int return_code) -> MockSqlite3Config &;
    static auto bind_int64_returns(int return_code) -> MockSqlite3Config &;
    static auto bind_double_returns(int return_code) -> MockSqlite3Config &;
    static auto bind_null_returns(int return_code) -> MockSqlite3Config &;
    static auto bind_blob_returns(int return_code) -> MockSqlite3Config &;

    // Configure step() to return specific codes
    static auto step_returns(int return_code) -> MockSqlite3Config &;
    static auto step_returns_sequence(std::vector<int> codes) -> MockSqlite3Config &;

    // Configure prepare to fail
    static auto prepare_returns(int return_code) -> MockSqlite3Config &;
    static auto prepare_error_message(std::string_view msg) -> MockSqlite3Config &;

    // Configure open to fail
    static auto open_returns(int return_code) -> MockSqlite3Config &;
    static auto open_error_message(std::string_view msg) -> MockSqlite3Config &;

    // Configure exec to fail
    static auto exec_returns(int return_code) -> MockSqlite3Config &;
    static auto exec_error_message(std::string_view msg) -> MockSqlite3Config &;

    // Configure for specific call counts (e.g., "fail on 3rd bind_int call")
    static auto bind_int_fails_on_call(int call_number, int return_code) -> MockSqlite3Config &;
    static auto bind_int64_fails_on_call(int call_number, int return_code) -> MockSqlite3Config &;
    static auto bind_text_fails_on_call(int call_number, int return_code) -> MockSqlite3Config &;
    static auto step_fails_on_call(int call_number, int return_code) -> MockSqlite3Config &;
    static auto prepare_fails_on_call(int call_number, int return_code) -> MockSqlite3Config &;

    // Get call counts for verification
    static auto get_bind_int_call_count() -> int;
    static auto get_bind_text_call_count() -> int;
    static auto get_step_call_count() -> int;
    static auto get_prepare_call_count() -> int;
    static auto get_exec_call_count() -> int;

    // Configure expanded_sql to return null
    static auto expanded_sql_returns_null() -> MockSqlite3Config &;

  private:
    static MockSqlite3Config instance_;
};

/**
 * @brief RAII guard that resets mock configuration on destruction
 */
class MockSqlite3Guard {
  public:
    MockSqlite3Guard() = default;
    ~MockSqlite3Guard() noexcept {
        try {
            MockSqlite3Config::reset();
        } catch (...) { // NOSONAR(cpp:S2486) - intentionally suppress all exceptions in noexcept destructor
        }
    }

    MockSqlite3Guard(const MockSqlite3Guard &) = delete;
    auto operator=(const MockSqlite3Guard &) -> MockSqlite3Guard & = delete;
    MockSqlite3Guard(MockSqlite3Guard &&) = delete;
    auto operator=(MockSqlite3Guard &&) -> MockSqlite3Guard & = delete;
};

} // namespace storm::test

// ============================================================================
// SQLite3 API Functions (mock implementations)
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// Database connection management
auto sqlite3_open_v2(const char *filename, sqlite3 **ppDb, int flags, const char *zVfs) -> int;

auto sqlite3_close_v2(const sqlite3 *db) -> int;

// Statement preparation
auto sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail) -> int;

auto sqlite3_finalize(const sqlite3_stmt *pStmt) -> int;
auto sqlite3_reset(sqlite3_stmt *pStmt) -> int;

// Statement execution
auto sqlite3_step(const sqlite3_stmt *pStmt) -> int;
auto sqlite3_exec(sqlite3 *db, const char *sql, int (*callback)(void *, int, char **, char **),
                  void *arg, // NOSONAR(cpp:S5205)
                  char **errmsg) -> int;

// Binding values
auto sqlite3_bind_int(const sqlite3_stmt *pStmt, int idx, int value) -> int;
auto sqlite3_bind_int64(const sqlite3_stmt *pStmt, int idx, int64_t value) -> int;
auto sqlite3_bind_double(const sqlite3_stmt *pStmt, int idx, double value) -> int;
auto sqlite3_bind_text(const sqlite3_stmt *pStmt, int idx, const char *value, int nBytes,
                       void (*destructor)(void *) // NOSONAR(cpp:S5205) - C callback API
                       ) -> int;
auto sqlite3_bind_null(const sqlite3_stmt *pStmt, int idx) -> int;
auto sqlite3_bind_blob(const sqlite3_stmt *pStmt, int idx, const void *value, int nBytes,
                       void (*destructor)(void *) // NOSONAR(cpp:S5205) - C callback API
                       ) -> int;

// Extracting column values
auto sqlite3_column_int(sqlite3_stmt *pStmt, int iCol) -> int;
auto sqlite3_column_int64(sqlite3_stmt *pStmt, int iCol) -> int64_t;
auto sqlite3_column_double(sqlite3_stmt *pStmt, int iCol) -> double;
auto sqlite3_column_text(sqlite3_stmt *pStmt, int iCol) -> const unsigned char *;
auto sqlite3_column_blob(const sqlite3_stmt *pStmt, int iCol) -> const void *;
auto sqlite3_column_bytes(sqlite3_stmt *pStmt, int iCol) -> int;
auto sqlite3_column_type(sqlite3_stmt *pStmt, int iCol) -> int;

// Error handling
auto sqlite3_errmsg(const sqlite3 *db) -> const char *;
auto sqlite3_db_handle(sqlite3_stmt *pStmt) -> sqlite3 *;
auto sqlite3_free(void *ptr) -> void;

// Expanded SQL (parameter substitution for debugging)
auto sqlite3_expanded_sql(sqlite3_stmt *pStmt) -> char *;

// Last insert rowid
auto sqlite3_last_insert_rowid(sqlite3 *db) -> int64_t;

#ifdef __cplusplus
}
#endif

#endif // STORM_MOCK_SQLITE3_H
