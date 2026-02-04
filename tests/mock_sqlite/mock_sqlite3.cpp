/**
 * @file mock_sqlite3.cpp
 * @brief Mock SQLite3 library implementation for testing error paths
 */

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,readability-implicit-bool-conversion) // NOSONAR(cpp:S125)
// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-owning-memory) // NOSONAR(cpp:S125)
// NOLINTBEGIN(cppcoreguidelines-no-malloc,readability-braces-around-statements) // NOSONAR(cpp:S125)

#include "mock_sqlite3.h"
#include <cstring>
#include <mutex>

// ============================================================================
// Internal Mock State
// ============================================================================

namespace {

    // Fake handles - just use pointers to distinguish between null and valid
    struct FakeSqlite3 {
        std::string error_message     = "not an error";
        int64_t     last_insert_rowid = 0;
    };

    struct FakeSqlite3Stmt {
        FakeSqlite3* parent_db       = nullptr;
        int          step_call_count = 0;
        // Store column values for extraction
        std::vector<int>         int_columns{0, 0, 0, 0, 0, 0, 0, 0};
        std::vector<int64_t>     int64_columns{0, 0, 0, 0, 0, 0, 0, 0};
        std::vector<double>      double_columns{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        std::vector<std::string> text_columns{"", "", "", "", "", "", "", ""};
        std::vector<int>         column_types{
                SQLITE_NULL, SQLITE_NULL, SQLITE_NULL, SQLITE_NULL, SQLITE_NULL, SQLITE_NULL, SQLITE_NULL, SQLITE_NULL
        };
    };

    // Global mock configuration
    struct MockConfig { // NOSONAR(cpp:S1820) - mock config needs all fields for full SQLite API coverage
        // Return codes for various functions
        int bind_int_return    = SQLITE_OK;
        int bind_text_return   = SQLITE_OK;
        int bind_int64_return  = SQLITE_OK;
        int bind_double_return = SQLITE_OK;
        int bind_null_return   = SQLITE_OK;
        int bind_blob_return   = SQLITE_OK;
        int step_return        = SQLITE_DONE;
        int prepare_return     = SQLITE_OK;
        int open_return        = SQLITE_OK;
        int exec_return        = SQLITE_OK;

        // Error messages
        std::string prepare_error_message = "mock error";
        std::string open_error_message    = "mock open error";
        std::string exec_error_message    = "mock exec error";

        // Sequence support for step()
        std::vector<int> step_sequence;
        size_t           step_sequence_index = 0;

        // Fail-on-call-N support
        int bind_int_fail_on_call   = -1;
        int bind_int_fail_code      = SQLITE_OK;
        int bind_int64_fail_on_call = -1;
        int bind_int64_fail_code    = SQLITE_OK;
        int bind_text_fail_on_call  = -1;
        int bind_text_fail_code     = SQLITE_OK;
        int step_fail_on_call       = -1;
        int step_fail_code          = SQLITE_OK;
        int prepare_fail_on_call    = -1;
        int prepare_fail_code       = SQLITE_OK;

        // Call counters
        int bind_int_calls    = 0;
        int bind_text_calls   = 0;
        int bind_int64_calls  = 0;
        int bind_double_calls = 0;
        int bind_null_calls   = 0;
        int bind_blob_calls   = 0;
        int step_calls        = 0;
        int prepare_calls     = 0;
        int exec_calls        = 0;

        auto reset() -> void {
            bind_int_return    = SQLITE_OK;
            bind_text_return   = SQLITE_OK;
            bind_int64_return  = SQLITE_OK;
            bind_double_return = SQLITE_OK;
            bind_null_return   = SQLITE_OK;
            bind_blob_return   = SQLITE_OK;
            step_return        = SQLITE_DONE;
            prepare_return     = SQLITE_OK;
            open_return        = SQLITE_OK;
            exec_return        = SQLITE_OK;

            prepare_error_message = "mock error";
            open_error_message    = "mock open error";
            exec_error_message    = "mock exec error";

            step_sequence.clear();
            step_sequence_index = 0;

            bind_int_fail_on_call   = -1;
            bind_int_fail_code      = SQLITE_OK;
            bind_int64_fail_on_call = -1;
            bind_int64_fail_code    = SQLITE_OK;
            bind_text_fail_on_call  = -1;
            bind_text_fail_code     = SQLITE_OK;
            step_fail_on_call       = -1;
            step_fail_code          = SQLITE_OK;
            prepare_fail_on_call    = -1;
            prepare_fail_code       = SQLITE_OK;

            bind_int_calls    = 0;
            bind_text_calls   = 0;
            bind_int64_calls  = 0;
            bind_double_calls = 0;
            bind_null_calls   = 0;
            bind_blob_calls   = 0;
            step_calls        = 0;
            prepare_calls     = 0;
            exec_calls        = 0;
        }
    };

    // Thread-local mock config for thread-safe testing
    thread_local MockConfig g_mock_config; // NOSONAR(cpp:S5421) - mutable mock state, cannot be const

    // Simple memory management for fake handles
    thread_local std::vector<std::unique_ptr<FakeSqlite3>>     g_fake_dbs;   // NOSONAR(cpp:S5421) - mutable mock state
    thread_local std::vector<std::unique_ptr<FakeSqlite3Stmt>> g_fake_stmts; // NOSONAR(cpp:S5421) - mutable mock state

} // anonymous namespace

// ============================================================================
// Mock Configuration API Implementation
// ============================================================================

namespace storm::test {

    MockSqlite3Config MockSqlite3Config::instance_;

    auto MockSqlite3Config::reset() -> void {
        g_mock_config.reset();
    }

    auto MockSqlite3Config::bind_int_returns(int return_code) -> MockSqlite3Config& {
        g_mock_config.bind_int_return = return_code;
        return instance_;
    }

    auto MockSqlite3Config::bind_text_returns(int return_code) -> MockSqlite3Config& {
        g_mock_config.bind_text_return = return_code;
        return instance_;
    }

    auto MockSqlite3Config::bind_int64_returns(int return_code) -> MockSqlite3Config& {
        g_mock_config.bind_int64_return = return_code;
        return instance_;
    }

    auto MockSqlite3Config::bind_double_returns(int return_code) -> MockSqlite3Config& {
        g_mock_config.bind_double_return = return_code;
        return instance_;
    }

    auto MockSqlite3Config::bind_null_returns(int return_code) -> MockSqlite3Config& {
        g_mock_config.bind_null_return = return_code;
        return instance_;
    }

    auto MockSqlite3Config::bind_blob_returns(int return_code) -> MockSqlite3Config& {
        g_mock_config.bind_blob_return = return_code;
        return instance_;
    }

    auto MockSqlite3Config::step_returns(int return_code) -> MockSqlite3Config& {
        g_mock_config.step_return = return_code;
        g_mock_config.step_sequence.clear();
        return instance_;
    }

    auto MockSqlite3Config::step_returns_sequence(std::vector<int> codes) -> MockSqlite3Config& {
        g_mock_config.step_sequence       = std::move(codes);
        g_mock_config.step_sequence_index = 0;
        return instance_;
    }

    auto MockSqlite3Config::prepare_returns(int return_code) -> MockSqlite3Config& {
        g_mock_config.prepare_return = return_code;
        return instance_;
    }

    auto MockSqlite3Config::prepare_error_message(std::string_view msg) -> MockSqlite3Config& {
        g_mock_config.prepare_error_message = msg;
        return instance_;
    }

    auto MockSqlite3Config::open_returns(int return_code) -> MockSqlite3Config& {
        g_mock_config.open_return = return_code;
        return instance_;
    }

    auto MockSqlite3Config::open_error_message(std::string_view msg) -> MockSqlite3Config& {
        g_mock_config.open_error_message = msg;
        return instance_;
    }

    auto MockSqlite3Config::exec_returns(int return_code) -> MockSqlite3Config& {
        g_mock_config.exec_return = return_code;
        return instance_;
    }

    auto MockSqlite3Config::exec_error_message(std::string_view msg) -> MockSqlite3Config& {
        g_mock_config.exec_error_message = msg;
        return instance_;
    }

    auto MockSqlite3Config::bind_int_fails_on_call(int call_number, int return_code) -> MockSqlite3Config& {
        g_mock_config.bind_int_fail_on_call = call_number;
        g_mock_config.bind_int_fail_code    = return_code;
        return instance_;
    }

    auto MockSqlite3Config::bind_int64_fails_on_call(int call_number, int return_code) -> MockSqlite3Config& {
        g_mock_config.bind_int64_fail_on_call = call_number;
        g_mock_config.bind_int64_fail_code    = return_code;
        return instance_;
    }

    auto MockSqlite3Config::bind_text_fails_on_call(int call_number, int return_code) -> MockSqlite3Config& {
        g_mock_config.bind_text_fail_on_call = call_number;
        g_mock_config.bind_text_fail_code    = return_code;
        return instance_;
    }

    auto MockSqlite3Config::step_fails_on_call(int call_number, int return_code) -> MockSqlite3Config& {
        g_mock_config.step_fail_on_call = call_number;
        g_mock_config.step_fail_code    = return_code;
        return instance_;
    }

    auto MockSqlite3Config::prepare_fails_on_call(int call_number, int return_code) -> MockSqlite3Config& {
        g_mock_config.prepare_fail_on_call = call_number;
        g_mock_config.prepare_fail_code    = return_code;
        return instance_;
    }

    auto MockSqlite3Config::get_bind_int_call_count() -> int {
        return g_mock_config.bind_int_calls;
    }

    auto MockSqlite3Config::get_bind_text_call_count() -> int {
        return g_mock_config.bind_text_calls;
    }

    auto MockSqlite3Config::get_step_call_count() -> int {
        return g_mock_config.step_calls;
    }

    auto MockSqlite3Config::get_prepare_call_count() -> int {
        return g_mock_config.prepare_calls;
    }

    auto MockSqlite3Config::get_exec_call_count() -> int {
        return g_mock_config.exec_calls;
    }

} // namespace storm::test

// ============================================================================
// SQLite3 API Mock Implementations
// ============================================================================

extern "C" {

auto sqlite3_open_v2(const char* filename, sqlite3** ppDb, int flags, const char* zVfs) -> int {
    (void)filename;
    (void)flags;
    (void)zVfs;

    if (g_mock_config.open_return != SQLITE_OK) {
        // Create a db handle anyway so error message can be retrieved
        auto fake_db           = std::make_unique<FakeSqlite3>();
        fake_db->error_message = g_mock_config.open_error_message;
        *ppDb                  = reinterpret_cast<sqlite3*>(fake_db.get()); // NOSONAR(cpp:S3630)
        g_fake_dbs.push_back(std::move(fake_db));
        return g_mock_config.open_return;
    }

    auto fake_db = std::make_unique<FakeSqlite3>();
    *ppDb        = reinterpret_cast<sqlite3*>(fake_db.get()); // NOSONAR(cpp:S3630)
    g_fake_dbs.push_back(std::move(fake_db));
    return SQLITE_OK;
}

auto sqlite3_close_v2(sqlite3* db) -> int {
    (void)db;
    // In mock, we don't actually clean up - memory is managed by g_fake_dbs
    return SQLITE_OK;
}

auto sqlite3_prepare_v2(sqlite3* db, const char* zSql, int nByte, sqlite3_stmt** ppStmt, const char** pzTail) -> int {
    (void)zSql;
    (void)nByte;
    if (pzTail)
        *pzTail = nullptr;

    g_mock_config.prepare_calls++;

    auto* fake_db = reinterpret_cast<FakeSqlite3*>(db); // NOSONAR(cpp:S3630)

    // Check for fail-on-call-N
    if (g_mock_config.prepare_fail_on_call == g_mock_config.prepare_calls) {
        if (fake_db) {
            fake_db->error_message = g_mock_config.prepare_error_message;
        }
        *ppStmt = nullptr;
        return g_mock_config.prepare_fail_code;
    }

    if (g_mock_config.prepare_return != SQLITE_OK) {
        if (fake_db) {
            fake_db->error_message = g_mock_config.prepare_error_message;
        }
        *ppStmt = nullptr;
        return g_mock_config.prepare_return;
    }

    auto fake_stmt       = std::make_unique<FakeSqlite3Stmt>();
    fake_stmt->parent_db = fake_db;
    *ppStmt              = reinterpret_cast<sqlite3_stmt*>(fake_stmt.get()); // NOSONAR(cpp:S3630)
    g_fake_stmts.push_back(std::move(fake_stmt));
    return SQLITE_OK;
}

auto sqlite3_finalize(sqlite3_stmt* pStmt) -> int {
    (void)pStmt;
    // Memory managed by g_fake_stmts
    return SQLITE_OK;
}

auto sqlite3_reset(sqlite3_stmt* pStmt) -> int {
    if (pStmt) {
        auto* fake_stmt            = reinterpret_cast<FakeSqlite3Stmt*>(pStmt); // NOSONAR(cpp:S3630)
        fake_stmt->step_call_count = 0;
    }
    return SQLITE_OK;
}

auto sqlite3_step(sqlite3_stmt* pStmt) -> int {
    (void)pStmt;
    g_mock_config.step_calls++;

    // Check for fail-on-call-N
    if (g_mock_config.step_fail_on_call == g_mock_config.step_calls) {
        return g_mock_config.step_fail_code;
    }

    // Check for sequence
    if (!g_mock_config.step_sequence.empty()) {
        if (g_mock_config.step_sequence_index < g_mock_config.step_sequence.size()) {
            const auto idx = g_mock_config.step_sequence_index++;
            return g_mock_config.step_sequence[idx];
        }
        // After sequence exhausted, return DONE
        return SQLITE_DONE;
    }

    return g_mock_config.step_return;
}

auto sqlite3_exec(
        sqlite3* db, const char* sql, int (*callback)(void*, int, char**, char**), void* arg, char** errmsg
) // NOSONAR(cpp:S5205)
        -> int {
    (void)sql;
    (void)callback;
    (void)arg;

    g_mock_config.exec_calls++;

    if (g_mock_config.exec_return != SQLITE_OK) {
        if (errmsg) {
            // Allocate error message string that can be freed with sqlite3_free
            const size_t len = g_mock_config.exec_error_message.size() + 1;
            *errmsg          = static_cast<char*>(malloc(len)); // NOSONAR(cpp:S1231) - SQLite API requires malloc
            if (*errmsg) {
                std::memcpy(*errmsg, g_mock_config.exec_error_message.c_str(), len);
            }
        }
        auto* fake_db = reinterpret_cast<FakeSqlite3*>(db); // NOSONAR(cpp:S3630)
        if (fake_db) {
            fake_db->error_message = g_mock_config.exec_error_message;
        }
        return g_mock_config.exec_return;
    }

    return SQLITE_OK;
}

auto sqlite3_bind_int(sqlite3_stmt* pStmt, int idx, int value) -> int {
    (void)pStmt;
    (void)idx;
    (void)value;
    g_mock_config.bind_int_calls++;

    // Check for fail-on-call-N
    if (g_mock_config.bind_int_fail_on_call == g_mock_config.bind_int_calls) {
        return g_mock_config.bind_int_fail_code;
    }

    return g_mock_config.bind_int_return;
}

auto sqlite3_bind_int64(sqlite3_stmt* pStmt, int idx, int64_t value) -> int {
    (void)pStmt;
    (void)idx;
    (void)value;
    g_mock_config.bind_int64_calls++;

    // Check for fail-on-call-N
    if (g_mock_config.bind_int64_fail_on_call == g_mock_config.bind_int64_calls) {
        return g_mock_config.bind_int64_fail_code;
    }

    return g_mock_config.bind_int64_return;
}

auto sqlite3_bind_double(sqlite3_stmt* pStmt, int idx, double value) -> int {
    (void)pStmt;
    (void)idx;
    (void)value;
    g_mock_config.bind_double_calls++;
    return g_mock_config.bind_double_return;
}

auto sqlite3_bind_text(sqlite3_stmt* pStmt, int idx, const char* value, int nBytes, void (*destructor)(void*))
        -> int { // NOSONAR(cpp:S5205)
    (void)pStmt;
    (void)idx;
    (void)value;
    (void)nBytes;
    (void)destructor;
    g_mock_config.bind_text_calls++;

    // Check for fail-on-call-N
    if (g_mock_config.bind_text_fail_on_call == g_mock_config.bind_text_calls) {
        return g_mock_config.bind_text_fail_code;
    }

    return g_mock_config.bind_text_return;
}

auto sqlite3_bind_null(sqlite3_stmt* pStmt, int idx) -> int {
    (void)pStmt;
    (void)idx;
    g_mock_config.bind_null_calls++;
    return g_mock_config.bind_null_return;
}

auto sqlite3_bind_blob(sqlite3_stmt* pStmt, int idx, const void* value, int nBytes, void (*destructor)(void*))
        -> int { // NOSONAR(cpp:S5205)
    (void)pStmt;
    (void)idx;
    (void)value;
    (void)nBytes;
    (void)destructor;
    g_mock_config.bind_blob_calls++;
    return g_mock_config.bind_blob_return;
}

auto sqlite3_column_int(sqlite3_stmt* pStmt, int iCol) -> int {
    auto* fake_stmt = reinterpret_cast<FakeSqlite3Stmt*>(pStmt); // NOSONAR(cpp:S3630)
    if (fake_stmt && iCol >= 0 && static_cast<size_t>(iCol) < fake_stmt->int_columns.size()) {
        return fake_stmt->int_columns[iCol];
    }
    return 0;
}

auto sqlite3_column_int64(sqlite3_stmt* pStmt, int iCol) -> int64_t {
    auto* fake_stmt = reinterpret_cast<FakeSqlite3Stmt*>(pStmt);
    if (fake_stmt && iCol >= 0 && static_cast<size_t>(iCol) < fake_stmt->int64_columns.size()) {
        return fake_stmt->int64_columns[iCol];
    }
    return 0;
}

auto sqlite3_column_double(sqlite3_stmt* pStmt, int iCol) -> double {
    auto* fake_stmt = reinterpret_cast<FakeSqlite3Stmt*>(pStmt);
    if (fake_stmt && iCol >= 0 && static_cast<size_t>(iCol) < fake_stmt->double_columns.size()) {
        return fake_stmt->double_columns[iCol];
    }
    return 0.0;
}

auto sqlite3_column_text(sqlite3_stmt* pStmt, int iCol) -> const unsigned char* {
    auto* fake_stmt = reinterpret_cast<FakeSqlite3Stmt*>(pStmt);
    if (fake_stmt && iCol >= 0 && static_cast<size_t>(iCol) < fake_stmt->text_columns.size()) {
        return reinterpret_cast<const unsigned char*>(fake_stmt->text_columns[iCol].c_str());
    }
    return nullptr;
}

auto sqlite3_column_blob(sqlite3_stmt* pStmt, int iCol) -> const void* {
    (void)pStmt;
    (void)iCol;
    return nullptr; // Mock returns null for blobs
}

auto sqlite3_column_bytes(sqlite3_stmt* pStmt, int iCol) -> int {
    auto* fake_stmt = reinterpret_cast<FakeSqlite3Stmt*>(pStmt);
    if (fake_stmt && iCol >= 0 && static_cast<size_t>(iCol) < fake_stmt->text_columns.size()) {
        return static_cast<int>(fake_stmt->text_columns[iCol].size());
    }
    return 0;
}

auto sqlite3_column_type(sqlite3_stmt* pStmt, int iCol) -> int {
    auto* fake_stmt = reinterpret_cast<FakeSqlite3Stmt*>(pStmt);
    if (fake_stmt && iCol >= 0 && static_cast<size_t>(iCol) < fake_stmt->column_types.size()) {
        return fake_stmt->column_types[iCol];
    }
    return SQLITE_NULL;
}

auto sqlite3_errmsg(sqlite3* db) -> const char* {
    auto* fake_db = reinterpret_cast<FakeSqlite3*>(db);
    if (fake_db) {
        return fake_db->error_message.c_str();
    }
    return "unknown error";
}

auto sqlite3_db_handle(sqlite3_stmt* pStmt) -> sqlite3* {
    auto* fake_stmt = reinterpret_cast<FakeSqlite3Stmt*>(pStmt);
    if (fake_stmt && fake_stmt->parent_db) {
        return reinterpret_cast<sqlite3*>(fake_stmt->parent_db);
    }
    return nullptr;
}

auto sqlite3_free(void* ptr) -> void {
    free(ptr);
}

auto sqlite3_last_insert_rowid(sqlite3* db) -> int64_t {
    auto* fake_db = reinterpret_cast<FakeSqlite3*>(db);
    if (fake_db) {
        return fake_db->last_insert_rowid++;
    }
    return 0;
}

} // extern "C"

// NOLINTEND(cppcoreguidelines-no-malloc,readability-braces-around-statements) // NOSONAR(cpp:S125)
// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-owning-memory) // NOSONAR(cpp:S125)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,readability-implicit-bool-conversion) // NOSONAR(cpp:S125)
